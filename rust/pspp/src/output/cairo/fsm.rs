// PSPP - a program for statistical analysis.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.

use std::{cmp::min, f64::consts::PI, fmt::Write, ops::DerefMut, sync::Arc};

use cairo::Context;
use enum_map::{EnumMap, enum_map};
use itertools::Itertools;
use pango::{
    AttrFloat, AttrFontDesc, AttrInt, AttrList, Attribute, FontDescription, FontMask, Layout,
    SCALE, SCALE_SMALL, Underline, Weight, parse_markup,
};
use pangocairo::functions::show_layout;
use smallvec::{SmallVec, smallvec};

use crate::output::cairo::{horz_align_to_pango, px_to_xr, xr_to_pt};
use crate::output::pivot::{Axis2, BorderStyle, Coord2, FontStyle, HorzAlign, Rect2, Stroke};
use crate::output::render::{Device, Extreme, Pager, Params};
use crate::output::table::DrawCell;
use crate::output::{Details, Item};
use crate::output::{pivot::Color, table::Content};

/// Width of an ordinary line.
const LINE_WIDTH: usize = LINE_SPACE / 2;

/// Space between double lines.
const LINE_SPACE: usize = SCALE as usize;

/// Conversion from 1/96" units ("pixels") to Cairo/Pango units.
fn pxf_to_xr(x: f64) -> usize {
    (x * (SCALE as f64 * 72.0 / 96.0)).round() as usize
}

#[derive(Clone, Debug)]
pub struct CairoFsmStyle {
    /// Page size.
    pub size: Coord2,

    /// Minimum cell size to allow breaking.
    pub min_break: EnumMap<Axis2, usize>,

    /// The basic font.
    pub font: FontDescription,

    /// Foreground color.
    pub fg: Color,

    /// Use system colors?
    pub use_system_colors: bool,

    /// Vertical space between different output items.
    pub object_spacing: usize,

    /// Resolution, in units per inch, used for measuring font "points":
    ///
    /// - 72.0 if 1 pt is one device unit, e.g. for rendering to a surface
    ///   created by [cairo::PsSurface::new] with its default transformation
    ///   matrix of 72 units/inch.p
    ///
    /// - 96.0 is traditional for a screen-based surface.
    pub font_resolution: f64,
}

impl CairoFsmStyle {
    fn new_layout(&self, context: &Context) -> Layout {
        let pangocairo_context = pangocairo::functions::create_context(context);
        pangocairo::functions::context_set_resolution(&pangocairo_context, self.font_resolution);
        Layout::new(&pangocairo_context)
    }
}

pub struct CairoFsm {
    style: Arc<CairoFsmStyle>,
    params: Params,
    item: Arc<Item>,
    layer_iterator: Option<Box<dyn Iterator<Item = SmallVec<[usize; 4]>>>>,
    pager: Option<Pager>,
}

impl CairoFsm {
    pub fn new(
        style: Arc<CairoFsmStyle>,
        printing: bool,
        context: &Context,
        item: Arc<Item>,
    ) -> Self {
        let params = Params {
            size: style.size,
            font_size: {
                let layout = style.new_layout(context);
                layout.set_font_description(Some(&style.font));
                layout.set_text("0");
                let char_size = layout.size();
                enum_map! {
                    Axis2::X => char_size.0.max(0) as usize,
                    Axis2::Y => char_size.1.max(0) as usize
                }
            },
            line_widths: enum_map! {
                Stroke::None => 0,
                Stroke::Solid | Stroke::Dashed => LINE_WIDTH,
                Stroke::Thick => LINE_WIDTH * 2,
                Stroke::Thin => LINE_WIDTH / 2,
                Stroke::Double => LINE_WIDTH * 2 + LINE_SPACE,
            },
            px_size: Some(px_to_xr(1)),
            min_break: style.min_break,
            supports_margins: true,
            rtl: false,
            printing,
            can_adjust_break: false, // XXX
            can_scale: true,
        };
        let device = CairoDevice {
            style: &style,
            params: &params,
            context,
        };
        let (layer_iterator, pager) = match &item.details {
            Details::Table(pivot_table) => {
                let mut layer_iterator = pivot_table.layers(printing);
                let layer_indexes = layer_iterator.next();
                (
                    Some(layer_iterator),
                    Some(Pager::new(
                        &device,
                        pivot_table,
                        layer_indexes.as_ref().map(|indexes| indexes.as_slice()),
                    )),
                )
            }
            _ => (None, None),
        };
        Self {
            style,
            params,
            item,
            layer_iterator,
            pager,
        }
    }

    pub fn draw_slice(&mut self, context: &Context, space: usize) -> usize {
        debug_assert!(self.params.printing);

        context.save().unwrap();
        let used = match &self.item.details {
            Details::Table(_) => self.draw_table(context, space),
            _ => todo!(),
        };
        context.restore().unwrap();

        used
    }

    fn draw_table(&mut self, context: &Context, space: usize) -> usize {
        let Details::Table(pivot_table) = &self.item.details else {
            unreachable!()
        };
        let Some(pager) = &mut self.pager else {
            return 0;
        };
        let mut device = CairoDevice {
            style: &self.style,
            params: &self.params,
            context,
        };
        let mut used = pager.draw_next(&mut device, space);
        if !pager.has_next(&device) {
            match self.layer_iterator.as_mut().unwrap().next() {
                Some(layer_indexes) => {
                    self.pager = Some(Pager::new(
                        &device,
                        pivot_table,
                        Some(layer_indexes.as_slice()),
                    ));
                    if pivot_table.look.paginate_layers {
                        used = space;
                    } else {
                        used += self.style.object_spacing;
                    }
                }
                _ => {
                    self.pager = None;
                }
            }
        }
        used.min(space)
    }

    pub fn is_done(&self) -> bool {
        match &self.item.details {
            Details::Table(_) => self.pager.is_none(),
            _ => todo!(),
        }
    }
}

fn xr_clip(context: &Context, clip: &Rect2) {
    if clip[Axis2::X].end != usize::MAX || clip[Axis2::Y].end != usize::MAX {
        let x0 = xr_to_pt(clip[Axis2::X].start);
        let y0 = xr_to_pt(clip[Axis2::Y].start);
        let x1 = xr_to_pt(clip[Axis2::X].end);
        let y1 = xr_to_pt(clip[Axis2::Y].end);
        context.rectangle(x0, y0, x1 - x0, y1 - y0);
        context.clip();
    }
}

fn xr_set_color(context: &Context, color: &Color) {
    fn as_frac(x: u8) -> f64 {
        x as f64 / 255.0
    }

    context.set_source_rgba(
        as_frac(color.r),
        as_frac(color.g),
        as_frac(color.b),
        as_frac(color.alpha),
    );
}

fn xr_fill_rectangle(context: &Context, rectangle: Rect2) {
    context.new_path();
    context.set_line_width(xr_to_pt(LINE_WIDTH));

    let x0 = xr_to_pt(rectangle[Axis2::X].start);
    let y0 = xr_to_pt(rectangle[Axis2::Y].start);
    let width = xr_to_pt(rectangle[Axis2::X].len());
    let height = xr_to_pt(rectangle[Axis2::Y].len());
    context.rectangle(x0, y0, width, height);
    context.fill().unwrap();
}

fn margin(cell: &DrawCell, axis: Axis2) -> usize {
    px_to_xr(
        cell.style.cell_style.margins[axis]
            .iter()
            .sum::<i32>()
            .max(0) as usize,
    )
}

pub fn parse_font_style(font_style: &FontStyle) -> FontDescription {
    let font = font_style.font.as_str();
    let font = if font.eq_ignore_ascii_case("Monospaced") {
        "Monospace"
    } else {
        font
    };
    let mut font_desc = FontDescription::from_string(font);
    if !font_desc.set_fields().contains(FontMask::SIZE) {
        let default_size = if font_style.size != 0 {
            font_style.size * 1000
        } else {
            10_000
        };
        font_desc.set_size(((default_size as f64 / 1000.0) * (SCALE as f64)) as i32);
    }
    font_desc.set_weight(if font_style.bold {
        Weight::Bold
    } else {
        Weight::Normal
    });
    font_desc.set_style(if font_style.italic {
        pango::Style::Italic
    } else {
        pango::Style::Normal
    });
    font_desc
}

/// Deal with an oddity of the Unicode line-breaking algorithm (or perhaps in
/// Pango's implementation of it): it will break after a period or a comma that
/// precedes a digit, e.g. in `.000` it will break after the period.  This code
/// looks for such a situation and inserts a U+2060 WORD JOINER to prevent the
/// break.
///
/// This isn't necessary when the decimal point is between two digits
/// (e.g. `0.000` won't be broken) or when the display width is not limited so
/// that word wrapping won't happen.
///
/// It isn't necessary to look for more than one period or comma, as would
/// happen with grouping like `1,234,567.89` or `1.234.567,89` because if groups
/// are present then there will always be a digit on both sides of every period
/// and comma.
fn avoid_decimal_split(mut s: String) -> String {
    if let Some(position) = s.find(['.', ',']) {
        let followed_by_digit = s[position + 1..]
            .chars()
            .next()
            .is_some_and(|c| c.is_ascii_digit());
        let not_preceded_by_digit = s[..position]
            .chars()
            .next_back()
            .is_none_or(|c| !c.is_ascii_digit());
        if followed_by_digit && not_preceded_by_digit {
            s.insert(position + 1, '\u{2060}');
        }
    }
    s
}

struct CairoDevice<'a> {
    style: &'a CairoFsmStyle,
    params: &'a Params,
    context: &'a Context,
}

impl CairoDevice<'_> {
    fn layout_cell(&self, cell: &DrawCell, mut bb: Rect2, clip: &Rect2) -> Coord2 {
        // XXX rotation
        //let h = if cell.rotate { Axis2::Y } else { Axis2::X };

        let layout = self.style.new_layout(self.context);

        let cell_font = if !cell.style.font_style.font.is_empty() {
            Some(parse_font_style(&cell.style.font_style))
        } else {
            None
        };
        let font = cell_font.as_ref().unwrap_or(&self.style.font);
        layout.set_font_description(Some(font));

        let (body, suffixes) = cell.display().split_suffixes();
        let horz_align = cell.horz_align(&body);
        let body = body.to_string();

        match horz_align {
            HorzAlign::Decimal { offset, decimal } if !cell.rotate => {
                let decimal_position = if let Some(position) = body.rfind(char::from(decimal)) {
                    layout.set_text(&body[position..]);
                    layout.set_width(-1);
                    layout.size().0.max(0) as usize
                } else {
                    0
                };
                bb[Axis2::X].end -= pxf_to_xr(offset).saturating_sub(decimal_position);
            }
            _ => (),
        }

        let mut attrs = None;
        let mut body = if cell.style.font_style.markup {
            match parse_markup(&body, 0 as char) {
                Ok((markup_attrs, string, _accel)) => {
                    attrs = Some(markup_attrs);
                    string.into()
                }
                Err(_) => body,
            }
        } else {
            avoid_decimal_split(body)
        };

        if cell.style.font_style.underline {
            attrs
                .get_or_insert_default()
                .insert(AttrInt::new_underline(Underline::Single));
        }

        if !suffixes.is_empty() {
            let subscript_ofs = body.len();
            #[allow(unstable_name_collisions)]
            body.extend(suffixes.subscripts().intersperse(","));
            let has_subscripts = subscript_ofs != body.len();

            let footnote_ofs = body.len();
            for (index, footnote) in suffixes.footnotes().enumerate() {
                if index > 0 {
                    body.push(',');
                }
                write!(&mut body, "{footnote}").unwrap();
            }
            let has_footnotes = footnote_ofs != body.len();

            // Allow footnote markers to occupy the right margin.  That way,
            // numbers in the column are still aligned.
            if has_footnotes && horz_align == HorzAlign::Right {
                // Measure the width of the footnote marker, so we know how much we
                // need to make room for.
                layout.set_text(&body[footnote_ofs..]);

                let footnote_attrs = AttrList::new();
                footnote_attrs.insert(AttrFloat::new_scale(SCALE_SMALL));
                footnote_attrs.insert(AttrInt::new_rise(3000));
                layout.set_attributes(Some(&footnote_attrs));
                let footnote_width = layout.size().0.max(0) as usize;

                // Bound the adjustment by the width of the right margin.
                let right_margin =
                    px_to_xr(cell.style.cell_style.margins[Axis2::X][1].max(0) as usize);
                let footnote_adjustment = min(footnote_width, right_margin);

                // Adjust the bounding box.
                if cell.rotate {
                    bb[Axis2::X].end = bb[Axis2::X].end.saturating_sub(footnote_adjustment);
                } else {
                    bb[Axis2::X].end = bb[Axis2::X].end.saturating_add(footnote_adjustment);
                }

                // Clean up.
                layout.set_attributes(None);
            }

            fn with_start<T: DerefMut<Target = Attribute>>(index: usize, mut attr: T) -> T {
                attr.deref_mut().set_start_index(index.try_into().unwrap());
                attr
            }
            fn with_end<T: DerefMut<Target = Attribute>>(index: usize, mut attr: T) -> T {
                attr.deref_mut().set_end_index(index.try_into().unwrap());
                attr
            }

            // Set attributes.
            let attrs = attrs.get_or_insert_default();
            attrs.insert(with_start(subscript_ofs, AttrFontDesc::new(font)));
            attrs.insert(with_start(subscript_ofs, AttrFloat::new_scale(SCALE_SMALL)));
            if has_subscripts {
                attrs.insert(with_start(
                    subscript_ofs,
                    with_end(footnote_ofs, AttrInt::new_rise(-3000)),
                ));
            }
            if has_footnotes {
                let rise = 3000; // XXX check look for superscript vs subscript
                attrs.insert(with_start(footnote_ofs, AttrInt::new_rise(rise)));
            }
        }

        layout.set_attributes(attrs.as_ref());
        layout.set_text(&body);
        layout.set_alignment(horz_align_to_pango(horz_align));
        if bb[Axis2::X].end == usize::MAX {
            layout.set_width(-1);
        } else {
            layout.set_width(bb[Axis2::X].len() as i32);
        }

        let size = layout.size();

        if !clip.is_empty() {
            self.context.save().unwrap();
            if !cell.rotate {
                xr_clip(self.context, clip);
            }
            if cell.rotate {
                let extra = bb[Axis2::X].len().saturating_sub(size.1.max(0) as usize);
                let halign_offset = extra / 2;
                self.context.translate(
                    xr_to_pt(bb[Axis2::X].start + halign_offset),
                    xr_to_pt(bb[Axis2::Y].end),
                );
                self.context.rotate(-PI / 2.0);
            } else {
                self.context
                    .translate(xr_to_pt(bb[Axis2::X].start), xr_to_pt(bb[Axis2::Y].start));
            }
            show_layout(self.context, &layout);
            self.context.restore().unwrap();
        }

        layout.set_attributes(None);

        Coord2::new(size.0.max(0) as usize, size.1.max(0) as usize)
    }

    fn do_draw_line(
        &self,
        x0: usize,
        y0: usize,
        x1: usize,
        y1: usize,
        stroke: Stroke,
        color: Color,
    ) {
        self.context.new_path();
        self.context.set_line_width(xr_to_pt(match stroke {
            Stroke::Thick => LINE_WIDTH * 2,
            Stroke::Thin => LINE_WIDTH / 2,
            _ => LINE_WIDTH,
        }));
        self.context.move_to(xr_to_pt(x0), xr_to_pt(y0));
        self.context.line_to(xr_to_pt(x1), xr_to_pt(y1));
        if !self.style.use_system_colors {
            xr_set_color(self.context, &color);
        }
        if stroke == Stroke::Dashed {
            self.context.set_dash(&[2.0], 0.0);
            let _ = self.context.stroke();
            self.context.set_dash(&[], 0.0);
        } else {
            let _ = self.context.stroke();
        }
    }
}

impl Device for CairoDevice<'_> {
    fn params(&self) -> &Params {
        self.params
    }

    fn measure_cell_width(&self, cell: &DrawCell) -> EnumMap<Extreme, usize> {
        fn add_margins(cell: &DrawCell, width: usize) -> usize {
            if width > 0 {
                width + margin(cell, Axis2::X)
            } else {
                0
            }
        }

        /// An empty clipping rectangle.
        fn clip() -> Rect2 {
            Rect2::default()
        }

        enum_map![
            Extreme::Min => {
                let bb = Rect2::new(0..1, 0..usize::MAX);
                add_margins(cell, self.layout_cell(cell, bb, &clip()).x())
            }
            Extreme::Max => {
                let bb = Rect2::new(0..usize::MAX, 0..usize::MAX);
                add_margins(cell, self.layout_cell(cell, bb, &clip()).x())
            },
        ]
    }

    fn measure_cell_height(&self, cell: &DrawCell, width: usize) -> usize {
        let margins = &cell.style.cell_style.margins;
        let bb = Rect2::new(
            0..width.saturating_sub(px_to_xr(margins[Axis2::X].len())),
            0..usize::MAX,
        );
        self.layout_cell(cell, bb, &Rect2::default()).y() + margin(cell, Axis2::Y)
    }

    fn adjust_break(&self, _cell: &Content, _size: Coord2) -> usize {
        todo!()
    }

    fn draw_line(&mut self, bb: Rect2, styles: EnumMap<Axis2, [BorderStyle; 2]>) {
        let x0 = bb[Axis2::X].start;
        let y0 = bb[Axis2::Y].start;
        let x3 = bb[Axis2::X].end;
        let y3 = bb[Axis2::Y].end;

        let top = styles[Axis2::X][0].stroke;
        let bottom = styles[Axis2::X][1].stroke;
        let left = styles[Axis2::Y][0].stroke;
        let right = styles[Axis2::Y][1].stroke;

        let top_color = styles[Axis2::X][0].color;
        let bottom_color = styles[Axis2::X][1].color;
        let left_color = styles[Axis2::Y][0].color;
        let right_color = styles[Axis2::Y][1].color;

        // The algorithm here is somewhat subtle, to allow it to handle
        // all the kinds of intersections that we need.
        //
        // Three additional ordinates are assigned along the X axis.  The first
        // is `xc`, midway between `x0` and `x3`.  The others are `x1` and `x2`;
        // for a single vertical line these are equal to `xc`, and for a double
        // vertical line they are the ordinates of the left and right half of
        // the double line.
        //
        // `yc`, `y1`, and `y2` are assigned similarly along the Y axis.
        //
        // The following diagram shows the coordinate system and output for
        // double top and bottom lines, single left line, and no right line:
        //
        // ```
        //             x0       x1 xc  x2      x3
        //           y0 ________________________
        //              |        #     #       |
        //              |        #     #       |
        //              |        #     #       |
        //              |        #     #       |
        //              |        #     #       |
        // y1 = y2 = yc |#########     #       |
        //              |        #     #       |
        //              |        #     #       |
        //              |        #     #       |
        //              |        #     #       |
        //           y3 |________#_____#_______|
        // ```

        // Offset from center of each line in a pair of double lines.
        let double_line_ofs = (LINE_SPACE + LINE_WIDTH) / 2;

        // Are the lines along each axis single or double?  (It doesn't make
        // sense to have different kinds of line on the same axis, so we don't
        // try to gracefully handle that case.)
        let double_vert = top == Stroke::Double || bottom == Stroke::Double;
        let double_horz = left == Stroke::Double || right == Stroke::Double;

        // When horizontal lines are doubled, the left-side line along `y1`
        // normally runs from `x0` to `x2`, and the right-side line along `y1`
        // from `x3` to `x1`.  If the top-side line is also doubled, we shorten
        // the `y1` lines, so that the left-side line runs only to `x1`, and the
        // right-side line only to `x2`.  Otherwise, the horizontal line at `y =
        // y1` below would cut off the intersection, which looks ugly:
        //
        // ```
        //           x0       x1     x2      x3
        //         y0 ________________________
        //            |        #     #       |
        //            |        #     #       |
        //            |        #     #       |
        //            |        #     #       |
        //         y1 |#########     ########|
        //            |                      |
        //            |                      |
        //         y2 |######################|
        //            |                      |
        //            |                      |
        //         y3 |______________________|
        // ```
        //
        // It is more of a judgment call when the horizontal line is single.  We
        // choose to cut off the line anyhow, as shown in the first diagram
        // above.
        let shorten_y1_lines = top == Stroke::Double;
        let shorten_y2_lines = bottom == Stroke::Double;
        let shorten_yc_line = shorten_y1_lines && shorten_y2_lines;
        let horz_line_ofs = if double_vert { double_line_ofs } else { 0 };
        let xc = (x0 + x3) / 2;
        let x1 = xc - horz_line_ofs;
        let x2 = xc + horz_line_ofs;

        let shorten_x1_lines = left == Stroke::Double;
        let shorten_x2_lines = right == Stroke::Double;
        let shorten_xc_line = shorten_x1_lines && shorten_x2_lines;
        let vert_line_ofs = if double_horz { double_line_ofs } else { 0 };
        let yc = (y0 + y3) / 2;
        let y1 = yc - vert_line_ofs;
        let y2 = yc + vert_line_ofs;

        let horz_lines: SmallVec<[_; 2]> = if double_horz {
            smallvec![(y1, shorten_y1_lines), (y2, shorten_y2_lines)]
        } else {
            smallvec![(yc, shorten_yc_line)]
        };
        for (y, shorten) in horz_lines {
            if left != Stroke::None
                && right != Stroke::None
                && !shorten
                && left_color == right_color
            {
                self.do_draw_line(x0, y, x3, y, left, left_color);
            } else {
                if left != Stroke::None {
                    self.do_draw_line(x0, y, if shorten { x1 } else { x2 }, y, left, left_color);
                }
                if right != Stroke::None {
                    self.do_draw_line(if shorten { x2 } else { x1 }, y, x3, y, right, right_color);
                }
            }
        }

        let vert_lines: SmallVec<[_; 2]> = if double_vert {
            smallvec![(x1, shorten_x1_lines), (x2, shorten_x2_lines)]
        } else {
            smallvec![(xc, shorten_xc_line)]
        };
        for (x, shorten) in vert_lines {
            if top != Stroke::None
                && bottom != Stroke::None
                && !shorten
                && top_color == bottom_color
            {
                self.do_draw_line(x, y0, x, y3, top, top_color);
            } else {
                if top != Stroke::None {
                    self.do_draw_line(x, y0, x, if shorten { y1 } else { y2 }, top, top_color);
                }
                if bottom != Stroke::None {
                    self.do_draw_line(
                        x,
                        if shorten { y2 } else { y1 },
                        x,
                        y3,
                        bottom,
                        bottom_color,
                    );
                }
            }
        }
    }

    fn draw_cell(
        &mut self,
        draw_cell: &DrawCell,
        alternate_row: bool,
        mut bb: Rect2,
        valign_offset: usize,
        spill: EnumMap<Axis2, [usize; 2]>,
        clip: &Rect2,
    ) {
        let fg = &draw_cell.style.font_style.fg[alternate_row as usize];
        let bg = &draw_cell.style.font_style.bg[alternate_row as usize];

        if (bg.r != 255 || bg.g != 255 || bg.b != 255) && bg.alpha != 0 {
            self.context.save().unwrap();
            let bg_clip = Rect2::from_fn(|axis| {
                let start = if bb[axis].start == clip[axis].start {
                    clip[axis].start.saturating_sub(spill[axis][0])
                } else {
                    clip[axis].start
                };
                let end = if bb[axis].end == clip[axis].end {
                    clip[axis].end + spill[axis][1]
                } else {
                    clip[axis].end
                };
                start..end
            });
            xr_clip(self.context, &bg_clip);
            xr_set_color(self.context, bg);
            let x0 = bb[Axis2::X].start.saturating_sub(spill[Axis2::X][0]);
            let y0 = bb[Axis2::Y].start.saturating_sub(spill[Axis2::X][1]);
            let x1 = bb[Axis2::X].end + spill[Axis2::X][1];
            let y1 = bb[Axis2::Y].end + spill[Axis2::Y][1];
            xr_fill_rectangle(self.context, Rect2::new(x0..x1, y0..y1));
            self.context.restore().unwrap();
        }

        if !self.style.use_system_colors {
            xr_set_color(self.context, fg);
        }

        self.context.save().unwrap();
        bb[Axis2::Y].start += valign_offset;
        for axis in [Axis2::X, Axis2::Y] {
            bb[axis].start += px_to_xr(draw_cell.style.cell_style.margins[axis][0].max(0) as usize);
            bb[axis].end = bb[axis]
                .end
                .saturating_sub(draw_cell.style.cell_style.margins[axis][0].max(0) as usize);
        }
        if bb[Axis2::X].start < bb[Axis2::X].end && bb[Axis2::Y].start < bb[Axis2::Y].end {
            self.layout_cell(draw_cell, bb, clip);
        }
        self.context.restore().unwrap();
    }

    fn scale(&mut self, factor: f64) {
        self.context.scale(factor, factor);
    }
}
