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

use std::cmp::{max, min};
use std::collections::HashMap;
use std::iter::once;
use std::ops::Range;
use std::sync::Arc;

use enum_map::{enum_map, Enum, EnumMap};
use itertools::interleave;
use num::Integer;
use smallvec::SmallVec;

use crate::output::pivot::VertAlign;
use crate::output::table::DrawCell;

use super::pivot::{Axis2, BorderStyle, Coord2, Look, PivotTable, Rect2, Stroke};
use super::table::{Content, Table};

/// Parameters for rendering a table_item to a device.
///
///
/// # Coordinate system
///
/// The rendering code assumes that larger `x` is to the right and larger `y`
/// toward the bottom of the page.
///
/// The rendering code assumes that the table being rendered has its upper left
/// corner at (0,0) in device coordinates.  This is usually not the case from
/// the driver's perspective, so the driver should expect to apply its own
/// offset to coordinates passed to callback functions.
pub struct Params {
    /// Page size to try to fit the rendering into.  Some tables will, of
    /// course, overflow this size.
    pub size: Coord2,

    /// Nominal size of a character in the most common font:
    /// `font_size[Axis2::X]` is the em width.
    /// `font_size[Axis2::Y]` is the line leading.
    pub font_size: EnumMap<Axis2, usize>,

    /// Width of different kinds of lines.
    pub line_widths: EnumMap<Stroke, usize>,

    /// 1/96" of an inch (1px) in the rendering unit.  Currently used only for
    /// column width ranges, as in `width_ranges` in
    /// [crate::output::pivot::Look].  Set to `None` to disable this feature.
    pub px_size: Option<usize>,

    /// Minimum cell width or height before allowing the cell to be broken
    /// across two pages.  (Joined cells may always be broken at join
    /// points.)
    pub min_break: EnumMap<Axis2, usize>,

    /// True if the driver supports cell margins.  (If false, the rendering
    /// engine will insert a small space betweeen adjacent cells that don't have
    /// an intervening rule.)
    pub supports_margins: bool,

    /// True if the local language has a right-to-left direction, otherwise
    /// false.
    pub rtl: bool,

    /// True if the table is being rendered for printing (as opposed to
    /// on-screen display).
    pub printing: bool,

    /// Whether [RenderOps::adjust_break] is implemented.
    pub can_adjust_break: bool,

    /// Whether [RenderOps::scale] is implemented.
    pub can_scale: bool,
}

impl Params {
    /// Returns a small but visible width.
    fn em(&self) -> usize {
        self.font_size[Axis2::X]
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Enum)]
pub enum Extreme {
    Min,
    Max,
}

pub trait Device {
    fn params(&self) -> &Params;

    /// Measures `cell`'s width.  Returns `map`, where:
    ///
    /// - `map[Extreme::Min]` is the minimum width required to avoid splitting a
    ///   single word across multiple lines.  This is usually the width of the
    ///   longest word in the cell.
    ///
    /// - `map[Extreme::Max]` is the minimum width required to avoid line breaks
    ///   other than at new-lines.
    fn measure_cell_width(&self, cell: &DrawCell) -> EnumMap<Extreme, usize>;

    /// Returns the height required to render `cell` given a width of `width`.
    fn measure_cell_height(&self, cell: &DrawCell, width: usize) -> usize;

    /// Given that there is space measuring `size` to render `cell`, where
    /// `size.y()` is insufficient to render the entire height of the cell,
    /// returns the largest height less than `size.y()` at which it is
    /// appropriate to break the cell.  For example, if breaking at the
    /// specified `size.y()` would break in the middle of a line of text, the
    /// return value would be just sufficiently less that the breakpoint would
    /// be between lines of text.
    ///
    /// Optional.  If [RenderParams::can_adjust_break] is false, the rendering
    /// engine assumes that all breakpoints are acceptable.
    fn adjust_break(&self, cell: &Content, size: Coord2) -> usize;

    /// Draws a generalized intersection of lines in `bb`.
    ///
    /// `styles` is interpreted this way:
    ///
    /// `styles[Axis2::X][0]`: style of line from top of `bb` to its center.
    /// `styles[Axis2::X][1]`: style of line from bottom of `bb` to its center.
    /// `styles[Axis2::Y][0]`: style of line from left of `bb` to its center.
    /// `styles[Axis2::Y][1]`: style of line from right of `bb` to its center.
    fn draw_line(&mut self, bb: Rect2, styles: EnumMap<Axis2, [BorderStyle; 2]>);

    /// Draws `cell` within bounding box `bb`.  `clip` is the same as `bb` (the
    /// common case) or a subregion enclosed by `bb`.  In the latter case only
    /// the part of the cell that lies within `clip` should actually be drawn,
    /// although `bb` should used to determine the layout of the cell.
    ///
    /// The text in the cell needs to be vertically offset `valign_offset` units
    /// from the top of the bounding box.  This handles vertical alignment with
    /// the cell.  (The caller doesn't just reduce the bounding box size because
    /// that would prevent the implementation from filling the entire cell with
    /// the background color.)  The implementation must handle horizontal
    /// alignment itself.
    fn draw_cell(
        &mut self,
        draw_cell: &DrawCell,
        alternate_row: bool,
        bb: Rect2,
        valign_offset: usize,
        spill: EnumMap<Axis2, [usize; 2]>,
        clip: &Rect2,
    );

    /// Scales all output by `factor`, e.g. a `factor` of 0.5 would cause
    /// everything subsequent to be drawn half-size.  `factor` will be greater
    /// than 0 and less than or equal to 1.
    ///
    /// Optional.  If [RenderParams::can_scale] is false, the rendering engine
    /// won't try to scale output.
    fn scale(&mut self, factor: f64);
}

/// A layout for rendering a specific table on a specific device.
///
/// May represent the layout of an entire table presented to [Pager::new], or a
/// rectangular subregion of a table broken out using [Break::next] to allow a
/// table to be broken across multiple pages.
///
/// A page's size is not limited to the size passed in as part of [Params].
/// [Pager] breaks a [Page] into smaller [page]s that will fit in the available
/// space.
///
/// # Rendered cells
///
/// The horizontal cells rendered are the leftmost `h[X]`, then `r[X]`.
/// The vertical cells rendered are the topmost `h[Y]`, then `r[Y]`.
/// `n[i]` is the sum of `h[i]` and `r[i].len()`.
#[derive(Debug)]
struct Page {
    table: Arc<Table>,

    /// Size of the table in cells.
    ///
    n: Coord2,

    /// Header size.  Cells `0..h[X]` are rendered horizontally, and `0..h[Y]` vertically.
    h: Coord2,

    /// Main region of cells to render.
    r: Rect2,

    /// Mappings from [Page] positions to those in the underlying [Table].
    maps: EnumMap<Axis2, [Map; 2]>,

    /// "Cell positions".
    ///
    /// cp[X] represents x positions within the table.
    /// cp[X][0] = 0.
    /// cp[X][1] = the width of the leftmost vertical rule.
    /// cp[X][2] = cp[X][1] + the width of the leftmost column.
    /// cp[X][3] = cp[X][2] + the width of the second-from-left vertical rule.
    /// and so on:
    /// cp[X][2 * n[X]] = x position of the rightmost vertical rule.
    /// cp[X][2 * n[X] + 1] = total table width including all rules.
    ///
    /// Similarly, cp[Y] represents y positions within the table.
    /// cp[Y][0] = 0.
    /// cp[Y][1] = the height of the topmost horizontal rule.
    /// cp[Y][2] = cp[Y][1] + the height of the topmost row.
    /// cp[Y][3] = cp[Y][2] + the height of the second-from-top horizontal rule.
    /// and so on:
    /// cp[Y][2 * n[Y]] = y position of the bottommost horizontal rule.
    /// cp[Y][2 * n[Y] + 1] = total table height including all rules.
    ///
    /// Rules and columns can have width or height 0, in which case consecutive
    /// values in this array are equal.
    cp: EnumMap<Axis2, Vec<usize>>,

    /// [Break::next] can break a table such that some cells are not fully
    /// contained within a render_page.  This will happen if a cell is too wide
    /// or two tall to fit on a single page, or if a cell spans multiple rows
    /// or columns and the page only includes some of those rows or columns.
    ///
    /// This hash table contains represents each such cell that doesn't
    /// completely fit on this page.
    ///
    /// Each overflow cell borders at least one header edge of the table and may
    /// border more.  (A single table cell that is so large that it fills the
    /// entire page can overflow on all four sides!)
    ///
    /// # Interpretation
    ///
    /// overflow[X][0]: space trimmed off its left side.
    /// overflow[X][1]: space trimmed off its right side.
    /// overflow[Y][0]: space trimmed off its top.
    /// overflow[Y][1]: space trimmed off its bottom.
    ///
    /// During rendering, this information is used to position the rendered
    /// portion of the cell within the available space.
    ///
    /// When a cell is rendered, sometimes it is permitted to spill over into
    /// space that is ordinarily reserved for rules.  Either way, this space is
    /// still included in overflow values.
    ///
    /// Suppose, for example, that a cell that joins 2 columns has a width of 60
    /// pixels and content "abcdef", that the 2 columns that it joins have
    /// widths of 20 and 30 pixels, respectively, and that therefore the rule
    /// between the two joined columns has a width of 10 (20 + 10 + 30 = 60).
    /// It might render like this, if each character is 10x10, and showing a few
    /// extra table cells for context:
    ///
    /// ```text
    /// +------+
    /// |abcdef|
    /// +--+---+
    /// |gh|ijk|
    /// +--+---+
    /// ```
    ///
    /// If this render_page is broken at the rule that separates "gh" from
    /// "ijk", then the page that contains the left side of the "abcdef" cell
    /// will have overflow[X][1] of 10 + 30 = 40 for its portion of the cell,
    /// and the page that contains the right side of the cell will have
    /// overflow[X][0] of 20 + 10 = 30.  The two resulting pages would look like
    /// this:
    ///
    /// ```text
    /// +---
    /// |abc
    /// +--+
    /// |gh|
    /// +--+
    /// ```
    ///
    /// and:
    ///
    /// ```text
    /// ----+
    /// cdef|
    /// +---+
    /// |ijk|
    /// +---+
    /// ```
    /// Each entry maps from a cell that overflows to the space that has been
    /// trimmed off the cell:
    overflows: HashMap<Coord2, EnumMap<Axis2, [usize; 2]>>,

    /// If a single column (or row) is too wide (or tall) to fit on a page
    /// reasonably, then render_break_next() will split a single row or column
    /// across multiple render_pages.  This member indicates when this has
    /// happened:
    ///
    /// is_edge_cutoff[X][0] is true if pixels have been cut off the left side
    /// of the leftmost column in this page, and false otherwise.
    ///
    /// is_edge_cutoff[X][1] is true if pixels have been cut off the right side
    /// of the rightmost column in this page, and false otherwise.
    ///
    /// is_edge_cutoff[Y][0] and is_edge_cutoff[Y][1] are similar for the top
    /// and bottom of the table.
    ///
    /// The effect of is_edge_cutoff is to prevent rules along the edge in
    /// question from being rendered.
    ///
    /// When is_edge_cutoff is true for a given edge, the 'overflows' hmap will
    /// contain a node for each cell along that edge.
    is_edge_cutoff: EnumMap<Axis2, [bool; 2]>,
}

/// Returns the width of `extent` along `axis`.
fn axis_width(cp: &[usize], extent: Range<usize>) -> usize {
    cp[extent.end] - cp[extent.start]
}

/// Returns the width of cells within `extent` along `axis`.
fn joined_width(cp: &[usize], extent: Range<usize>) -> usize {
    axis_width(cp, cell_ofs(extent.start)..cell_ofs(extent.end) - 1)
}
/// Returns the offset in [Self::cp] of the cell with index `cell_index`.
/// That is, if `cell_index` is 0, then the offset is 1, that of the leftmost
/// or topmost cell; if `cell_index` is 1, then the offset is 3, that of the
/// next cell to the right (or below); and so on. */
fn cell_ofs(cell_index: usize) -> usize {
    cell_index * 2 + 1
}

/// Returns the offset in [Self::cp] of the rule with index `rule_index`.
/// That is, if `rule_index` is 0, then the offset is that of the leftmost
/// or topmost rule; if `rule_index` is 1, then the offset is that of the
/// next rule to the right (or below); and so on.
fn rule_ofs(rule_index: usize) -> usize {
    rule_index * 2
}

/// Returns the width of cell `z` along `axis`.
fn cell_width(cp: &[usize], z: usize) -> usize {
    let ofs = cell_ofs(z);
    axis_width(cp, ofs..ofs + 1)
}

/// Is `ofs` the offset of a rule in `cp`?
fn is_rule(z: usize) -> bool {
    z.is_even()
}

#[derive(Clone)]
pub struct RenderCell<'a> {
    rect: Rect2,
    content: &'a Content,
}

impl Page {
    /// Creates and returns a new [Page] for rendering `table` with the given
    /// `look` on `device`.
    ///
    /// The new [Page] will be suitable for rendering on a device whose page
    /// size is `params.size`, but the caller is responsible for actually
    /// breaking it up to fit on such a device, using the [Break] abstraction.
    fn new(table: Arc<Table>, device: &dyn Device, min_width: usize, look: &Look) -> Self {
        use Axis2::*;
        use Extreme::*;

        let n = table.n;

        // Figure out rule widths.
        //
        // `rules[X]` is vertical rules.
        // `rules[Y]` is horizontal rules.
        let rules = EnumMap::from_fn(|axis| {
            (0..=n[axis])
                .map(|z| measure_rule(device, &table, axis, z))
                .collect::<Vec<_>>()
        });

        let px_size = device.params().px_size.unwrap_or_default();
        let heading_widths = look.heading_widths.clone().map(|_region, range| {
            enum_map![
                Min => *range.start() * px_size,
                Max => *range.end() * px_size
            ]
        });

        // Calculate minimum and maximum widths of cells that do not span
        // multiple columns.
        let mut unspanned_columns = EnumMap::from_fn(|_| vec![0; n.x()]);
        for cell in table.cells().filter(|cell| cell.col_span() == 1) {
            let mut w = device.measure_cell_width(&DrawCell::new(cell.inner(), &table));
            if device.params().px_size.is_some() {
                if let Some(region) = table.heading_region(cell.coord) {
                    let wr = &heading_widths[region];
                    if w[Min] < wr[Min] {
                        w[Min] = wr[Min];
                        if w[Min] > w[Max] {
                            w[Max] = w[Min];
                        }
                    } else if w[Max] > wr[Max] {
                        w[Max] = wr[Max];
                        if w[Max] < w[Min] {
                            w[Min] = w[Max];
                        }
                    }
                }
            }

            let x = cell.coord[X];
            for ext in [Min, Max] {
                if unspanned_columns[ext][x] < w[ext] {
                    unspanned_columns[ext][x] = w[ext];
                }
            }
        }

        // Distribute widths of spanned columns.
        let mut columns = unspanned_columns.clone();
        for cell in table.cells().filter(|cell| cell.col_span() > 1) {
            let rect = cell.rect();

            let w = device.measure_cell_width(&DrawCell::new(cell.inner(), &table));
            for ext in [Min, Max] {
                distribute_spanned_width(
                    w[ext],
                    &unspanned_columns[ext][rect[X].clone()],
                    &mut columns[ext][rect[X].clone()],
                    &rules[X][rect[X].start..rect[X].end + 1],
                );
            }
        }
        if min_width > 0 {
            for ext in [Min, Max] {
                distribute_spanned_width(
                    min_width,
                    &unspanned_columns[ext],
                    &mut columns[ext],
                    &rules[X],
                );
            }
        }

        // In pathological cases, spans can cause the minimum width of a column
        // to exceed the maximum width.  This bollixes our interpolation
        // algorithm later, so fix it up.
        for i in 0..n.x() {
            if columns[Min][i] > columns[Max][i] {
                columns[Max][i] = columns[Min][i];
            }
        }

        // Decide final column widths.
        let rule_widths = rules[X].iter().copied().sum::<usize>();
        let table_widths = EnumMap::from_fn(|ext| columns[ext].iter().sum::<usize>() + rule_widths);

        let cp_x = if table_widths[Max] <= device.params().size[X] {
            // Fits even with maximum widths.  Use them.
            Self::use_row_widths(&columns[Max], &rules[X])
        } else if table_widths[Min] <= device.params().size[X] {
            // Fits with minimum widths, so distribute the leftover space.
            //Self::new_with_interpolated_widths()
            Self::interpolate_row_widths(
                device.params(),
                &columns[Min],
                &columns[Max],
                table_widths[Min],
                table_widths[Max],
                &rules[X],
            )
        } else {
            // Doesn't fit even with minimum widths.  Assign minimums for now, and
            // later we can break it horizontally into multiple pages.
            Self::use_row_widths(&columns[Min], &rules[X])
        };

        // Calculate heights of cells that do not span multiple rows.
        let mut unspanned_rows = vec![0; n[Y]];
        for cell in table.cells().filter(|cell| cell.row_span() == 1) {
            let rect = cell.rect();

            let w = joined_width(&cp_x, rect[X].clone());
            let h = device.measure_cell_height(&DrawCell::new(cell.inner(), &table), w);

            let row = &mut unspanned_rows[cell.coord.y()];
            if h > *row {
                *row = h;
            }
        }

        // Distribute heights of spanned rows.
        let mut rows = unspanned_rows.clone();
        for cell in table.cells().filter(|cell| cell.row_span() > 1) {
            let rect = cell.rect();
            let w = joined_width(&cp_x, rect[X].clone());
            let h = device.measure_cell_height(&DrawCell::new(cell.inner(), &table), w);
            distribute_spanned_width(
                h,
                &unspanned_rows[rect[Y].clone()],
                &mut rows[rect[Y].clone()],
                &rules[Y][rect[Y].start..rect[Y].end + 1],
            );
        }

        // Decide final row heights.
        let cp_y = Self::use_row_widths(&rows, &rules[Y]);

        // Measure headers.  If they are "too big", get rid of them.
        let mut h = table.h;
        for (axis, cp) in [(X, cp_x.as_slice()), (Y, cp_y.as_slice())] {
            let header_width = axis_width(cp, 0..table.h[axis]);
            let max_cell_width = (table.h[axis]..n[axis])
                .map(|z| cell_width(cp, z))
                .max()
                .unwrap_or(0);
            let threshold = device.params().size[axis];
            if header_width * 2 >= threshold || header_width + max_cell_width > threshold {
                h[axis] = 0;
            }
        }
        let r = Rect2::new(h[X]..n[X], h[Y]..n[Y]);
        let maps = Self::new_mappings(h, &r);
        Self {
            table,
            n,
            h,
            r,
            cp: Axis2::new_enum(cp_x, cp_y),
            overflows: HashMap::new(),
            is_edge_cutoff: EnumMap::default(),
            maps,
        }
    }

    fn use_row_widths(rows: &[usize], rules: &[usize]) -> Vec<usize> {
        let mut vec = once(0)
            .chain(interleave(rules, rows).copied())
            .collect::<Vec<_>>();
        for i in 1..vec.len() {
            vec[i] += vec[i - 1]
        }
        vec
    }

    fn interpolate_row_widths(
        params: &Params,
        rows_min: &[usize],
        rows_max: &[usize],
        w_min: usize,
        w_max: usize,
        rules: &[usize],
    ) -> Vec<usize> {
        let avail = params.size[Axis2::X] - w_min;
        let wanted = w_max - w_min;
        let mut w = wanted / 2;
        let rows_mid = rows_min
            .iter()
            .copied()
            .zip(rows_max.iter().copied())
            .map(|(min, max)| {
                w += avail * (max - min);
                let extra = w / wanted;
                w -= extra * wanted;
                min + extra
            })
            .collect::<Vec<_>>();
        Self::use_row_widths(&rows_mid, rules)
    }

    /// Returns the width of `extent` along `axis`.
    fn axis_width(&self, axis: Axis2, extent: Range<usize>) -> usize {
        axis_width(&self.cp[axis], extent)
    }

    /// Returns the width of cells within `extent` along `axis`.
    fn joined_width(&self, axis: Axis2, extent: Range<usize>) -> usize {
        joined_width(&self.cp[axis], extent)
    }

    /// Returns the width of the headers along `axis`.
    fn headers_width(&self, axis: Axis2) -> usize {
        self.axis_width(axis, rule_ofs(0)..cell_ofs(self.h[axis]))
    }

    /// Returns the width of rule `z` along `axis`.
    fn rule_width(&self, axis: Axis2, z: usize) -> usize {
        let ofs = rule_ofs(z);
        self.axis_width(axis, ofs..ofs + 1)
    }

    /// Returns the width of rule `z` along `axis`, counting in reverse order.
    fn rule_width_r(&self, axis: Axis2, z: usize) -> usize {
        let ofs = self.rule_ofs_r(axis, z);
        self.axis_width(axis, ofs..ofs + 1)
    }

    /// Returns the offset in [Self::cp] of the rule with
    /// index `rule_index_r`, which counts from the right side (or bottom) of the page
    /// left (or up), according to `axis`, respectively.  That is,
    /// if `rule_index_r` is 0, then the offset is that of the rightmost or bottommost
    /// rule; if `rule_index_r` is 1, then the offset is that of the next rule to the left
    /// (or above); and so on.
    fn rule_ofs_r(&self, axis: Axis2, rule_index_r: usize) -> usize {
        (self.n[axis] - rule_index_r) * 2
    }

    /// Returns the width of cell `z` along `axis`.
    fn cell_width(&self, axis: Axis2, z: usize) -> usize {
        let ofs = cell_ofs(z);
        self.axis_width(axis, ofs..ofs + 1)
    }

    /// Returns the width of the widest cell, excluding headers, along `axis`.
    fn max_cell_width(&self, axis: Axis2) -> usize {
        (self.h[axis]..self.n[axis])
            .map(|z| self.cell_width(axis, z))
            .max()
            .unwrap_or(0)
    }

    fn width(&self, axis: Axis2) -> usize {
        *self.cp[axis].last().unwrap()
    }

    fn new_mappings(h: Coord2, r: &Rect2) -> EnumMap<Axis2, [Map; 2]> {
        EnumMap::from_fn(|axis| {
            [
                Map {
                    p0: 0,
                    t0: 0,
                    ofs: 0,
                    n: h[axis],
                },
                Map {
                    p0: h[axis],
                    t0: r[axis].start,
                    ofs: r[axis].start - h[axis],
                    n: r[axis].len(),
                },
            ]
        })
    }

    fn get_map(&self, axis: Axis2, z: usize) -> &Map {
        if z < self.h[axis] {
            &self.maps[axis][0]
        } else {
            &self.maps[axis][1]
        }
    }

    fn map_z(&self, axis: Axis2, z: usize) -> usize {
        z + self.get_map(axis, z).ofs
    }

    fn map_coord(&self, coord: Coord2) -> Coord2 {
        Coord2::from_fn(|a| self.map_z(a, coord[a]))
    }

    fn get_cell(&self, coord: Coord2) -> RenderCell<'_> {
        let maps = EnumMap::from_fn(|axis| self.get_map(axis, coord[axis]));
        let cell = self.table.get(self.map_coord(coord));
        RenderCell {
            rect: Rect2(cell.rect().0.map(|axis, Range { start, end }| {
                let m = maps[axis];
                max(m.p0, start - m.ofs)..min(m.p0 + m.n, end - m.ofs)
            })),
            content: cell.content,
        }
    }

    /// Creates and returns a new [Page] whose contents are a subregion of this
    /// page's contents.  The new page includes cells `extent` (exclusive) along
    /// `axis`, plus any headers on `axis`.
    ///
    /// If `pixel0` is nonzero, then it is a number of pixels to exclude from
    /// the left or top (according to `axis`) of cell `extent.start`.
    /// Similarly, `pixel1` is a number of pixels to exclude from the right or
    /// bottom of cell `extent.end - 1`.  (`pixel0` and `pixel1` are used to
    /// render cells that are too large to fit on a single page.)
    ///
    /// The whole of axis `!axis` is included.  (The caller may follow up with
    /// another call to select on `!axis`.)
    fn select(
        self: &Arc<Self>,
        a: Axis2,
        extent: Range<usize>,
        pixel0: usize,
        pixel1: usize,
    ) -> Arc<Self> {
        let b = !a;
        let z0 = extent.start;
        let z1 = extent.end;

        // If all of the page is selected, just make a copy.
        if z0 == self.h[a] && z1 == self.n[a] && pixel0 == 0 && pixel1 == 0 {
            return self.clone();
        }

        // Figure out `n`, `h`, `r` for the subpage.
        let trim = [z0 - self.h[a], self.n[a] - z1];
        let mut n = self.n;
        n[a] -= trim[0] + trim[1];
        let h = self.h;
        let mut r = self.r.clone();
        r[a].start += trim[0];
        r[a].end -= trim[1];

        // An edge is cut off if it was cut off in `self` or if we're trimming
        // pixels off that side of the page and there are no headers.
        let mut is_edge_cutoff = self.is_edge_cutoff;
        is_edge_cutoff[a][0] = h[a] == 0 && (pixel0 > 0 || (z0 == 0 && self.is_edge_cutoff[a][0]));
        is_edge_cutoff[a][1] = pixel1 > 0 || (z1 == self.n[a] && self.is_edge_cutoff[a][1]);

        // Select widths from `self` into subpage.
        let scp = self.cp[a].as_slice();
        let mut dcp = Vec::with_capacity(2 * n[a] + 1);
        dcp.push(0);
        let mut total = 0;
        for z in 0..=rule_ofs(h[a]) {
            total += if z == 0 && is_edge_cutoff[a][0] {
                0
            } else {
                scp[z + 1] - scp[z]
            };
            dcp.push(total);
        }
        for z in cell_ofs(z0)..=cell_ofs(z1 - 1) {
            total += scp[z + 1] - scp[z];
            if z == cell_ofs(z0) {
                total -= pixel0;
            }
            if z == cell_ofs(z1 - 1) {
                total -= pixel1;
            }
            dcp.push(total);
        }
        let z = self.rule_ofs_r(a, 0);
        if !is_edge_cutoff[a][1] {
            total += scp[z + 1] - scp[z];
        }
        dcp.push(total);
        debug_assert_eq!(dcp.len(), 1 + 2 * n[a] + 1);

        let mut cp = EnumMap::default();
        cp[a] = dcp;
        cp[!a] = self.cp[!a].clone();

        let mut overflows = HashMap::new();

        // Add new overflows.
        let s = Selection {
            a,
            b,
            h,
            z0,
            z1,
            p0: pixel0,
            p1: pixel1,
        };
        if self.h[a] == 0 || z0 > self.h[a] || pixel0 > 0 {
            let mut z = 0;
            while z < self.n[b] {
                let d = Coord2::for_axis((a, z0), z);
                let cell = self.get_cell(d);
                let overflow0 = pixel0 > 0 || cell.rect[a].start < z0;
                let overflow1 = cell.rect[a].end > z1 || (cell.rect[a].end == z1 && pixel1 > 0);
                if overflow0 || overflow1 {
                    let mut overflow = self.overflows.get(&d).cloned().unwrap_or_default();
                    if overflow0 {
                        overflow[a][0] +=
                            pixel0 + self.axis_width(a, cell_ofs(cell.rect[a].start)..cell_ofs(z0));
                    }
                    if overflow1 {
                        overflow[a][1] +=
                            pixel1 + self.axis_width(a, cell_ofs(z1)..cell_ofs(cell.rect[a].end));
                    }
                    assert!(overflows
                        .insert(s.coord_to_subpage(cell.rect.top_left()), overflow)
                        .is_none());
                }
                z += cell.rect[b].len();
            }
        }

        let mut z = 0;
        while z < self.n[b] {
            let d = Coord2::for_axis((a, z1 - 1), z);
            let cell = self.get_cell(d);
            if cell.rect[a].end > z1
                || (cell.rect[a].end == z1 && pixel1 > 0)
                    && overflows.contains_key(&s.coord_to_subpage(cell.rect.top_left()))
            {
                let mut overflow = self.overflows.get(&d).cloned().unwrap_or_default();
                overflow[a][1] +=
                    pixel1 + self.axis_width(a, cell_ofs(z1)..cell_ofs(cell.rect[a].end));
                assert!(overflows
                    .insert(s.coord_to_subpage(cell.rect.top_left()), overflow)
                    .is_none());
            }
            z += cell.rect[b].len();
        }

        // Copy overflows from `self` into the subpage.
        // XXX this could be done at the start, which would simplify the while loops above
        for (coord, overflow) in self.overflows.iter() {
            let cell = self.table.get(*coord);
            let rect = cell.rect();
            if rect[a].end > z0 && rect[a].start < z1 {
                overflows
                    .entry(s.coord_to_subpage(rect.top_left()))
                    .or_insert(*overflow);
            }
        }

        let maps = Self::new_mappings(h, &r);
        Arc::new(Self {
            table: self.table.clone(),
            n,
            h,
            r,
            maps,
            cp,
            overflows,
            is_edge_cutoff,
        })
    }

    fn total_size(&self, axis: Axis2) -> usize {
        self.cp[axis].last().copied().unwrap()
    }

    fn draw(&self, device: &mut dyn Device, ofs: Coord2) {
        use Axis2::*;
        self.draw_cells(
            device,
            ofs,
            Rect2::new(0..self.n[X] * 2 + 1, 0..self.n[Y] * 2 + 1),
        );
    }

    fn draw_cells(&self, device: &mut dyn Device, ofs: Coord2, cells: Rect2) {
        use Axis2::*;
        for y in cells[Y].clone() {
            let mut x = cells[X].start;
            while x < cells[X].end {
                if !is_rule(x) && !is_rule(y) {
                    let cell = self.get_cell(Coord2::new(x / 2, y / 2));
                    self.draw_cell(device, ofs, &cell);
                    x = rule_ofs(cell.rect[X].end);
                } else {
                    x += 1;
                }
            }
        }

        for y in cells[Y].clone() {
            for x in cells[X].clone() {
                if is_rule(x) || is_rule(y) {
                    self.draw_rule(device, ofs, Coord2::new(x, y));
                }
            }
        }
    }

    fn draw_rule(&self, device: &mut dyn Device, ofs: Coord2, coord: Coord2) {
        const NO_BORDER: BorderStyle = BorderStyle::none();
        let styles = EnumMap::from_fn(|a: Axis2| {
            let b = !a;
            if !is_rule(coord[a])
                || (self.is_edge_cutoff[a][0] && coord[a] == 0)
                || (self.is_edge_cutoff[a][1] && coord[a] == self.n[a] * 2)
            {
                [NO_BORDER, NO_BORDER]
            } else if is_rule(coord[b]) {
                let first = if coord[b] > 0 {
                    let mut e = coord;
                    e[b] -= 1;
                    self.get_rule(a, e)
                } else {
                    NO_BORDER
                };

                let second = if coord[b] / 2 < self.n[b] {
                    self.get_rule(a, coord)
                } else {
                    NO_BORDER
                };

                [first, second]
            } else {
                let rule = self.get_rule(a, coord);
                [rule, rule]
            }
        });

        if !styles
            .values()
            .all(|border| border.iter().all(BorderStyle::is_none))
        {
            let bb =
                Rect2::from_fn(|a| self.cp[a][coord[a]]..self.cp[a][coord[a] + 1]).translate(ofs);
            device.draw_line(bb, styles);
        }
    }

    fn get_rule(&self, a: Axis2, coord: Coord2) -> BorderStyle {
        let coord = Coord2::from_fn(|a| coord[a] / 2);
        let coord = self.map_coord(coord);

        let border = self.table.get_rule(a, coord);
        if self.h[a] > 0 && coord[a] == self.h[a] {
            let border2 = self
                .table
                .get_rule(a, Coord2::for_axis((a, self.h[a]), coord[!a]));
            border.combine(border2)
        } else {
            border
        }
    }

    fn extra_height(&self, device: &dyn Device, bb: &Rect2, cell: &DrawCell) -> usize {
        use Axis2::*;
        let height = device.measure_cell_height(cell, bb[X].len());
        usize::saturating_sub(bb[Y].len(), height)
    }
    fn draw_cell(&self, device: &mut dyn Device, ofs: Coord2, cell: &RenderCell) {
        use Axis2::*;

        let mut bb = Rect2::from_fn(|a| {
            self.cp[a][cell.rect[a].start * 2 + 1]..self.cp[a][cell.rect[a].end * 2]
        })
        .translate(ofs);
        /*
            let spill = EnumMap::from_fn(|a| {
                [
                    self.rule_width(a, cell.rect[a].start) / 2,
                    self.rule_width(a, cell.rect[a].end) / 2,
                ]
        });*/
        let spill = EnumMap::from_fn(|_| [0, 0]);

        let clip = if let Some(overflow) = self.overflows.get(&cell.rect.top_left()) {
            Rect2::from_fn(|a| {
                let mut clip = bb[a].clone();
                if overflow[a][0] > 0 {
                    bb[a].start -= overflow[a][0];
                    if cell.rect[a].start == 0 && !self.is_edge_cutoff[a][0] {
                        clip.start = ofs[a] + self.cp[a][cell.rect[a].start * 2];
                    }
                }

                if overflow[a][1] > 0 {
                    bb[a].end += overflow[a][1];
                    if cell.rect[a].end == self.n[a] && !self.is_edge_cutoff[a][1] {
                        clip.end = ofs[a] + self.cp[a][cell.rect[a].end * 2 + 1];
                    }
                }

                clip
            })
        } else {
            bb.clone()
        };

        // Header rows are never alternate rows.
        let alternate_row =
            usize::checked_sub(cell.rect[Y].start, self.h[Y]).is_some_and(|row| row % 2 == 1);

        let draw_cell = DrawCell::new(cell.content.inner(), &self.table);
        let valign_offset = match draw_cell.style.cell_style.vert_align {
            VertAlign::Top => 0,
            VertAlign::Middle => self.extra_height(device, &bb, &draw_cell) / 2,
            VertAlign::Bottom => self.extra_height(device, &bb, &draw_cell),
        };
        device.draw_cell(&draw_cell, alternate_row, bb, valign_offset, spill, &clip)
    }
}

struct Selection {
    a: Axis2,
    b: Axis2,
    z0: usize,
    z1: usize,
    p0: usize,
    p1: usize,
    h: Coord2,
}

impl Selection {
    /// Returns the coordinates of `coord` as it will appear in this subpage.
    ///
    /// `coord` must be in the selected region or the results will not make
    /// sense.
    fn coord_to_subpage(&self, coord: Coord2) -> Coord2 {
        let a = self.a;
        let b = self.b;
        let ha0 = self.h[a];
        Coord2::for_axis((a, max(coord[a] + ha0 - self.z0, ha0)), coord[b])
    }
}

/// Maps a contiguous range of cells from a page to the underlying table along
/// the horizontal or vertical dimension.
#[derive(Copy, Clone, Debug)]
struct Map {
    /// First ordinate in the page.
    p0: usize,

    /// First ordinate in the table.
    t0: usize,

    /// `t0 - p0`.
    ofs: usize,

    /// Number of ordinates in page and table.
    n: usize,
}

/// Modifies the 'width' members of `rows` so that their sum, when added to rule
/// widths `rules[1..n - 1]`, where n is rows.len(), is at least `width`.
///
/// # Implementation
///
/// The algorithm used here is based on the following description from HTML 4:
///
/// > For cells that span multiple columns, a simple approach consists of
/// > apportioning the min/max widths evenly to each of the constituent
/// > columns.  A slightly more complex approach is to use the min/max
/// > widths of unspanned cells to weight how spanned widths are
/// > apportioned.  Experiments suggest that a blend of the two approaches
/// > gives good results for a wide range of tables.
///
/// We blend the two approaches half-and-half, except that we cannot use the
/// unspanned weights when 'total_unspanned' is 0 (because that would cause a
/// division by zero).
///
/// The calculation we want to do is this:
///
/// ```text
/// w0 = width / n
/// w1 = width * (column's unspanned width) / (total unspanned width)
/// (column's width) = (w0 + w1) / 2
/// ```
///
/// We implement it as a precise calculation in integers by multiplying `w0` and
/// `w1` by the common denominator of all three calculations (`d`), dividing
/// that out in the column width calculation, and then keeping the remainder for
/// the next iteration.
///
/// (We actually compute the unspanned width of a column as twice the unspanned
/// width, plus the width of the rule on the left, plus the width of the rule on
/// the right.  That way each rule contributes to both the cell on its left and
/// on its right.)
fn distribute_spanned_width(
    width: usize,
    unspanned: &[usize],
    spanned: &mut [usize],
    rules: &[usize],
) {
    let n = unspanned.len();
    if n == 0 {
        return;
    }

    debug_assert_eq!(spanned.len(), n);
    debug_assert_eq!(rules.len(), n + 1);

    let total_unspanned = unspanned.iter().sum::<usize>()
        + rules
            .get(1..n)
            .map_or(0, |rules| rules.iter().copied().sum::<usize>());
    if total_unspanned >= width {
        return;
    }

    let d0 = n;
    let d1 = 2 * total_unspanned.max(1);
    let d = if total_unspanned > 0 {
        d0 * d1 * 2
    } else {
        d0 * d1
    };
    let mut w = d / 2;
    for x in 0..n {
        w += width * d1;
        if total_unspanned > 0 {
            let mut unspanned = unspanned[x] * 2;
            if x + 1 < n {
                unspanned += rules[x + 1];
            }
            if x > 0 {
                unspanned += rules[x];
            }
            w += width * unspanned * d0;
        }
        spanned[x] = max(spanned[x], w / d);
        w = w.checked_sub(spanned[x] * d).unwrap();
    }
}

/// Returns the width of the rule in `table` that is at offset `z` along axis
/// `a`, if rendered on `device`.
fn measure_rule(device: &dyn Device, table: &Table, a: Axis2, z: usize) -> usize {
    let b = !a;

    // Determine the types of rules that are present.
    let mut rules = EnumMap::default();
    for w in 0..table.n[b] {
        let stroke = table.get_rule(a, Coord2::for_axis((a, z), w)).stroke;
        rules[stroke] = true;
    }

    // Turn off [Stroke::None] because it has width 0 and we needn't bother.
    // However, if the device doesn't support margins, make sure that there is
    // at least a small gap between cells (but we don't need any at the left or
    // right edge of the table).
    if rules[Stroke::None] {
        rules[Stroke::None] = false;
        if z > 0 && z < table.n[a] && !device.params().supports_margins && a == Axis2::X {
            rules[Stroke::Solid] = true;
        }
    }

    // Calculate maximum width of rules that are present.
    let line_widths = &device.params().line_widths;
    rules
        .into_iter()
        .map(
            |(rule, present)| {
                if present {
                    line_widths[rule]
                } else {
                    0
                }
            },
        )
        .max()
        .unwrap_or(0)
}

#[derive(Debug)]
struct Break {
    page: Arc<Page>,

    /// Axis along which `page` is being broken.
    axis: Axis2,

    /// Next cell along `axis`.
    z: usize,

    /// Pixel offset within cell `z` (usually 0).
    pixel: usize,

    /// Width of headers of `page` along `axis`.
    hw: usize,
}

impl Break {
    fn new(page: Arc<Page>, axis: Axis2) -> Self {
        let z = page.h[axis];
        let hw = page.headers_width(axis);
        Self {
            page,
            axis,
            z,
            pixel: 0,
            hw,
        }
    }

    fn has_next(&self) -> bool {
        self.z < self.page.n[self.axis]
    }

    /// Returns the width that would be required along this breaker's axis to
    /// render a page from the current position up to but not including `cell`.
    fn needed_size(&self, cell: usize) -> usize {
        // Width of header not including its rightmost rule.
        let mut size = self
            .page
            .axis_width(self.axis, 0..rule_ofs(self.page.h[self.axis]));

        // If we have a pixel offset and there is no header, then we omit
        // the leftmost rule of the body.  Otherwise the rendering is deceptive
        // because it looks like the whole cell is present instead of a partial
        // cell.
        //
        // Otherwise (if there is a header) we will be merging two rules: the
        // rightmost rule in the header and the leftmost rule in the body.  We
        // assume that the width of a merged rule is the larger of the widths of
        // either rule individually.
        if self.pixel == 0 || self.page.h[self.axis] > 0 {
            size += max(
                self.page.rule_width(self.axis, self.page.h[self.axis]),
                self.page.rule_width(self.axis, self.z),
            );
        }

        // Width of body, minus any pixel offset in the leftmost cell.
        size += self
            .page
            .joined_width(self.axis, self.z..cell)
            .checked_sub(self.pixel)
            .unwrap();

        // Width of rightmost rule in body merged with leftmost rule in headers.
        size += max(
            self.page.rule_width_r(self.axis, 0),
            self.page.rule_width(self.axis, cell),
        );

        size
    }

    /// Returns a new [Page] that is up to `size` pixels wide along the axis
    /// used for breaking.  Returns `None` if the page has already been
    /// completely broken up, or if `size` is too small to reasonably render any
    /// cells.  The latter will never happen if `size` is at least as large as
    /// the page size passed to [Page::new] along the axis using for breaking.
    fn next(&mut self, device: &dyn Device, size: usize) -> Option<Arc<Page>> {
        if !self.has_next() {
            return None;
        }

        self.find_breakpoint(device, size).map(|(z, pixel)| {
            let page = match pixel {
                0 => self.page.select(self.axis, self.z..z, self.pixel, 0),
                pixel => self.page.select(
                    self.axis,
                    self.z..z + 1,
                    pixel,
                    self.page.cell_width(self.axis, z) - pixel,
                ),
            };
            self.z = z;
            self.pixel = pixel;
            page
        })
    }

    fn break_cell(&self, device: &dyn Device, z: usize, overflow: usize) -> usize {
        if self.cell_is_breakable(device, z) {
            // If there is no right header and we render a partial cell
            // on the right side of the body, then we omit the rightmost
            // rule of the body.  Otherwise the rendering is deceptive
            // because it looks like the whole cell is present instead
            // of a partial cell.
            //
            // This is similar to code for the left side in
            // [Self::needed_size].
            let rule_allowance = self.page.rule_width(self.axis, z);

            // The amount that, if we added cell `z`, the rendering
            // would overfill the allocated `size`.
            let overhang = overflow - rule_allowance; // XXX could go negative

            // The width of cell `z`.
            let cell_size = self.page.cell_width(self.axis, z);

            // The amount trimmed off the left side of `z`, and the
            // amount left to render.
            let cell_ofs = if z == self.z { self.pixel } else { 0 };
            let cell_left = cell_size - cell_ofs;

            // If some of the cell remains to render, and there would
            // still be some of the cell left afterward, then partially
            // render that much of the cell.
            let mut pixel = if cell_left > 0 && cell_left > overhang {
                cell_left - overhang + cell_ofs
            } else {
                0
            };

            // If there would be only a tiny amount of the cell left
            // after rendering it partially, reduce the amount rendered
            // slightly to make the output look a little better.
            let em = device.params().em();
            if pixel + em > cell_size {
                pixel = pixel.saturating_sub(em);
            }

            // If we're breaking vertically, then consider whether the
            // cells being broken have a better internal breakpoint than
            // the exact number of pixels available, which might look
            // bad e.g. because it breaks in the middle of a line of
            // text.
            if self.axis == Axis2::Y && device.params().can_adjust_break {
                let mut x = 0;
                while x < self.page.n[Axis2::X] {
                    let cell = self.page.get_cell(Coord2::new(x, z));
                    let better_pixel = device.adjust_break(
                        cell.content,
                        Coord2::new(
                            self.page
                                .joined_width(Axis2::X, cell.rect[Axis2::X].clone()),
                            pixel,
                        ),
                    );
                    x += cell.rect[Axis2::X].len();

                    if better_pixel < pixel {
                        let start_pixel = if z > self.z { self.pixel } else { 0 };
                        if better_pixel > start_pixel {
                            pixel = better_pixel;
                            break;
                        } else if better_pixel == 0 && z != self.z {
                            pixel = 0;
                            break;
                        }
                    }
                }
            }

            pixel
        } else {
            0
        }
    }

    fn find_breakpoint(&mut self, device: &dyn Device, size: usize) -> Option<(usize, usize)> {
        for z in self.z..self.page.n[self.axis] {
            let needed = self.needed_size(z + 1);
            if needed > size {
                let pixel = self.break_cell(device, z, needed - size);
                if z == self.z && pixel == 0 {
                    return None;
                } else {
                    return Some((z, pixel));
                }
            }
        }
        Some((self.page.n[self.axis], 0))
    }

    /// Returns true if `cell` along this breaker's axis may be broken across a
    /// page boundary.
    ///
    /// This is just a heuristic.  Breaking cells across page boundaries can
    /// save space, but it looks ugly.
    fn cell_is_breakable(&self, device: &dyn Device, cell: usize) -> bool {
        self.page.cell_width(self.axis, cell) >= device.params().min_break[self.axis]
    }
}

pub struct Pager {
    scale: f64,

    /// [Page]s to be rendered, in order, vertically.  There may be up to 5
    /// pages, for the pivot table's title, layers, body, captions, and
    /// footnotes.
    pages: SmallVec<[Arc<Page>; 5]>,

    x_break: Option<Break>,
    y_break: Option<Break>,
}

impl Pager {
    pub fn new(
        device: &dyn Device,
        pivot_table: &PivotTable,
        layer_indexes: Option<&[usize]>,
    ) -> Self {
        let output = pivot_table.output(
            layer_indexes.unwrap_or(&pivot_table.current_layer),
            device.params().printing,
        );

        // Figure out the width of the body of the table. Use this to determine
        // the base scale.
        let body_page = Page::new(Arc::new(output.body), device, 0, &pivot_table.look);
        let body_width = body_page.width(Axis2::X);
        let mut scale = if body_width > device.params().size[Axis2::X]
            && pivot_table.look.shrink_to_fit[Axis2::X]
            && device.params().can_scale
        {
            device.params().size[Axis2::X] as f64 / body_width as f64
        } else {
            1.0
        };

        let mut pages = SmallVec::new();
        for table in [output.title, output.layers].into_iter().flatten() {
            pages.push(Arc::new(Page::new(
                Arc::new(table),
                device,
                body_width,
                &pivot_table.look,
            )));
        }
        pages.push(Arc::new(body_page));
        for table in [output.caption, output.footnotes].into_iter().flatten() {
            pages.push(Arc::new(Page::new(
                Arc::new(table),
                device,
                0,
                &pivot_table.look,
            )));
        }
        pages.reverse();

        // If we're shrinking tables to fit the page length, then adjust the
        // scale factor.
        //
        // XXX This will sometimes shrink more than needed, because adjusting
        // the scale factor allows for cells to be "wider", which means that
        // sometimes they won't break across as much vertical space, thus
        // shrinking the table vertically more than the scale would imply.
        // Shrinking only as much as necessary would require an iterative
        // search.
        if pivot_table.look.shrink_to_fit[Axis2::Y] && device.params().can_scale {
            let total_height = pages
                .iter()
                .map(|page: &Arc<Page>| page.total_size(Axis2::Y))
                .sum::<usize>() as f64;
            let max_height = device.params().size[Axis2::Y] as f64;
            if total_height * scale >= max_height {
                scale *= max_height / total_height;
            }
        }

        Self {
            scale,
            pages,
            x_break: None,
            y_break: None,
        }
    }

    /// True if there's content left to render.
    pub fn has_next(&mut self, device: &dyn Device) -> bool {
        while self
            .y_break
            .as_mut()
            .is_none_or(|y_break| !y_break.has_next())
        {
            self.y_break = self
                .x_break
                .as_mut()
                .and_then(|x_break| {
                    x_break.next(
                        device,
                        (device.params().size[Axis2::X] as f64 / self.scale) as usize,
                    )
                })
                .map(|page| Break::new(page, Axis2::Y));
            if self.y_break.is_none() {
                match self.pages.pop() {
                    Some(page) => self.x_break = Some(Break::new(page, Axis2::X)),
                    _ => return false,
                }
            }
        }
        true
    }

    /// Draws a chunk of content to fit in a space that has vertical size
    /// `space` and the horizontal size specified in the device parameters.
    /// Returns the amount of vertical space actually used by the rendered
    /// chunk, which will be 0 if `space` is too small to render anything or if
    /// no content remains (use [Self::has_next] to distinguish these cases).
    pub fn draw_next(&mut self, device: &mut dyn Device, mut space: usize) -> usize {
        use Axis2::*;

        if self.scale != 1.0 {
            device.scale(self.scale);
            space = (space as f64 / self.scale) as usize;
        }

        let mut ofs = Coord2::new(0, 0);
        while self.has_next(device) {
            let Some(page) = self
                .y_break
                .as_mut()
                .and_then(|y_break| y_break.next(device, space - ofs[Y]))
            else {
                break;
            };
            page.draw(device, ofs);
            ofs[Y] += page.total_size(Y);
        }

        if self.scale != 1.0 {
            ofs[Y] = (ofs[Y] as f64 * self.scale) as usize;
        }
        ofs[Y]
    }
}
