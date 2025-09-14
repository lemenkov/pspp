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

use std::{
    borrow::Cow,
    fmt::{Display, Error as FmtError, Result as FmtResult, Write as FmtWrite},
    fs::File,
    io::{BufWriter, Write as IoWrite},
    ops::{Index, Range},
    path::PathBuf,
    sync::{Arc, LazyLock},
};

use enum_map::{enum_map, Enum, EnumMap};
use serde::{Deserialize, Serialize};
use unicode_linebreak::{linebreaks, BreakOpportunity};
use unicode_width::UnicodeWidthStr;

use crate::output::{render::Extreme, table::DrawCell, text_line::Emphasis};

use super::{
    driver::Driver,
    pivot::{Axis2, BorderStyle, Coord2, HorzAlign, PivotTable, Rect2, Stroke},
    render::{Device, Pager, Params},
    table::Content,
    text_line::{clip_text, TextLine},
    Details, Item,
};

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Boxes {
    Ascii,
    #[default]
    Unicode,
}

impl Boxes {
    fn box_chars(&self) -> &'static BoxChars {
        match self {
            Boxes::Ascii => &ASCII_BOX,
            Boxes::Unicode => &UNICODE_BOX,
        }
    }
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct TextConfig {
    /// Output file name.
    file: Option<PathBuf>,

    /// Renderer config.
    #[serde(flatten)]
    options: TextRendererOptions,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(default)]
pub struct TextRendererOptions {
    /// Enable bold and underline in output?
    pub emphasis: bool,

    /// Page width.
    pub width: Option<usize>,

    /// ASCII or Unicode
    pub boxes: Boxes,
}

pub struct TextRenderer {
    /// Enable bold and underline in output?
    emphasis: bool,

    /// Page width.
    width: usize,

    /// Minimum cell size to break across pages.
    min_hbreak: usize,

    box_chars: &'static BoxChars,

    params: Params,
    n_objects: usize,
    lines: Vec<TextLine>,
}

impl Default for TextRenderer {
    fn default() -> Self {
        Self::new(&TextRendererOptions::default())
    }
}

impl TextRenderer {
    pub fn new(config: &TextRendererOptions) -> Self {
        let width = config.width.unwrap_or(usize::MAX);
        Self {
            emphasis: config.emphasis,
            width,
            min_hbreak: 20,
            box_chars: config.boxes.box_chars(),
            n_objects: 0,
            params: Params {
                size: Coord2::new(width, usize::MAX),
                font_size: EnumMap::from_fn(|_| 1),
                line_widths: EnumMap::from_fn(|stroke| if stroke == Stroke::None { 0 } else { 1 }),
                px_size: None,
                min_break: EnumMap::default(),
                supports_margins: false,
                rtl: false,
                printing: true,
                can_adjust_break: false,
                can_scale: false,
            },
            lines: Vec::new(),
        }
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Enum)]
enum Line {
    None,
    Dashed,
    Single,
    Double,
}

impl From<Stroke> for Line {
    fn from(stroke: Stroke) -> Self {
        match stroke {
            Stroke::None => Self::None,
            Stroke::Solid | Stroke::Thick | Stroke::Thin => Self::Single,
            Stroke::Dashed => Self::Dashed,
            Stroke::Double => Self::Double,
        }
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Enum)]
struct Lines {
    r: Line,
    b: Line,
    l: Line,
    t: Line,
}

#[derive(Default)]
struct BoxChars(EnumMap<Lines, char>);

impl BoxChars {
    fn put(&mut self, r: Line, b: Line, l: Line, chars: [char; 4]) {
        use Line::*;
        for (t, c) in [None, Dashed, Single, Double]
            .into_iter()
            .zip(chars.into_iter())
        {
            self.0[Lines { r, b, l, t }] = c;
        }
    }
}

impl Index<Lines> for BoxChars {
    type Output = char;

    fn index(&self, lines: Lines) -> &Self::Output {
        &self.0[lines]
    }
}

static ASCII_BOX: LazyLock<BoxChars> = LazyLock::new(|| {
    let mut ascii_box = BoxChars::default();
    let n = Line::None;
    let d = Line::Dashed;
    use Line::{Double as D, Single as S};
    ascii_box.put(n, n, n, [' ', '|', '|', '#']);
    ascii_box.put(n, n, d, ['-', '+', '+', '#']);
    ascii_box.put(n, n, S, ['-', '+', '+', '#']);
    ascii_box.put(n, n, D, ['=', '#', '#', '#']);
    ascii_box.put(n, d, n, ['|', '|', '|', '#']);
    ascii_box.put(n, d, d, ['+', '+', '+', '#']);
    ascii_box.put(n, d, S, ['+', '+', '+', '#']);
    ascii_box.put(n, d, D, ['#', '#', '#', '#']);
    ascii_box.put(n, S, n, ['|', '|', '|', '#']);
    ascii_box.put(n, S, d, ['+', '+', '+', '#']);
    ascii_box.put(n, S, S, ['+', '+', '+', '#']);
    ascii_box.put(n, S, D, ['#', '#', '#', '#']);
    ascii_box.put(n, D, n, ['#', '#', '#', '#']);
    ascii_box.put(n, D, d, ['#', '#', '#', '#']);
    ascii_box.put(n, D, S, ['#', '#', '#', '#']);
    ascii_box.put(n, D, D, ['#', '#', '#', '#']);
    ascii_box.put(d, n, n, ['-', '+', '+', '#']);
    ascii_box.put(d, n, d, ['-', '+', '+', '#']);
    ascii_box.put(d, n, S, ['-', '+', '+', '#']);
    ascii_box.put(d, n, D, ['#', '#', '#', '#']);
    ascii_box.put(d, d, n, ['+', '+', '+', '#']);
    ascii_box.put(d, d, d, ['+', '+', '+', '#']);
    ascii_box.put(d, d, S, ['+', '+', '+', '#']);
    ascii_box.put(d, d, D, ['#', '#', '#', '#']);
    ascii_box.put(d, S, n, ['+', '+', '+', '#']);
    ascii_box.put(d, S, d, ['+', '+', '+', '#']);
    ascii_box.put(d, S, S, ['+', '+', '+', '#']);
    ascii_box.put(d, S, D, ['#', '#', '#', '#']);
    ascii_box.put(d, D, n, ['#', '#', '#', '#']);
    ascii_box.put(d, D, d, ['#', '#', '#', '#']);
    ascii_box.put(d, D, S, ['#', '#', '#', '#']);
    ascii_box.put(d, D, D, ['#', '#', '#', '#']);
    ascii_box.put(S, n, n, ['-', '+', '+', '#']);
    ascii_box.put(S, n, d, ['-', '+', '+', '#']);
    ascii_box.put(S, n, S, ['-', '+', '+', '#']);
    ascii_box.put(S, n, D, ['#', '#', '#', '#']);
    ascii_box.put(S, d, n, ['+', '+', '+', '#']);
    ascii_box.put(S, d, d, ['+', '+', '+', '#']);
    ascii_box.put(S, d, S, ['+', '+', '+', '#']);
    ascii_box.put(S, d, D, ['#', '#', '#', '#']);
    ascii_box.put(S, S, n, ['+', '+', '+', '#']);
    ascii_box.put(S, S, d, ['+', '+', '+', '#']);
    ascii_box.put(S, S, S, ['+', '+', '+', '#']);
    ascii_box.put(S, S, D, ['#', '#', '#', '#']);
    ascii_box.put(S, D, n, ['#', '#', '#', '#']);
    ascii_box.put(S, D, d, ['#', '#', '#', '#']);
    ascii_box.put(S, D, S, ['#', '#', '#', '#']);
    ascii_box.put(S, D, D, ['#', '#', '#', '#']);
    ascii_box.put(D, n, n, ['=', '#', '#', '#']);
    ascii_box.put(D, n, d, ['#', '#', '#', '#']);
    ascii_box.put(D, n, S, ['#', '#', '#', '#']);
    ascii_box.put(D, n, D, ['=', '#', '#', '#']);
    ascii_box.put(D, d, n, ['#', '#', '#', '#']);
    ascii_box.put(D, d, d, ['#', '#', '#', '#']);
    ascii_box.put(D, d, S, ['#', '#', '#', '#']);
    ascii_box.put(D, d, D, ['#', '#', '#', '#']);
    ascii_box.put(D, S, n, ['#', '#', '#', '#']);
    ascii_box.put(D, S, d, ['#', '#', '#', '#']);
    ascii_box.put(D, S, S, ['#', '#', '#', '#']);
    ascii_box.put(D, S, D, ['#', '#', '#', '#']);
    ascii_box.put(D, D, n, ['#', '#', '#', '#']);
    ascii_box.put(D, D, d, ['#', '#', '#', '#']);
    ascii_box.put(D, D, S, ['#', '#', '#', '#']);
    ascii_box.put(D, D, D, ['#', '#', '#', '#']);
    ascii_box
});

static UNICODE_BOX: LazyLock<BoxChars> = LazyLock::new(|| {
    let mut unicode_box = BoxChars::default();
    let n = Line::None;
    let d = Line::Dashed;
    use Line::{Double as D, Single as S};
    unicode_box.put(n, n, n, [' ', '╵', '╵', '║']);
    unicode_box.put(n, n, d, ['╌', '╯', '╯', '╜']);
    unicode_box.put(n, n, S, ['╴', '╯', '╯', '╜']);
    unicode_box.put(n, n, D, ['═', '╛', '╛', '╝']);
    unicode_box.put(n, S, n, ['╷', '│', '│', '║']);
    unicode_box.put(n, S, d, ['╮', '┤', '┤', '╢']);
    unicode_box.put(n, S, S, ['╮', '┤', '┤', '╢']);
    unicode_box.put(n, S, D, ['╕', '╡', '╡', '╣']);
    unicode_box.put(n, d, n, ['╷', '┊', '│', '║']);
    unicode_box.put(n, d, d, ['╮', '┤', '┤', '╢']);
    unicode_box.put(n, d, S, ['╮', '┤', '┤', '╢']);
    unicode_box.put(n, d, D, ['╕', '╡', '╡', '╣']);
    unicode_box.put(n, D, n, ['║', '║', '║', '║']);
    unicode_box.put(n, D, d, ['╖', '╢', '╢', '╢']);
    unicode_box.put(n, D, S, ['╖', '╢', '╢', '╢']);
    unicode_box.put(n, D, D, ['╗', '╣', '╣', '╣']);
    unicode_box.put(d, n, n, ['╌', '╰', '╰', '╙']);
    unicode_box.put(d, n, d, ['╌', '┴', '┴', '╨']);
    unicode_box.put(d, n, S, ['─', '┴', '┴', '╨']);
    unicode_box.put(d, n, D, ['═', '╧', '╧', '╩']);
    unicode_box.put(d, d, n, ['╭', '├', '├', '╟']);
    unicode_box.put(d, d, d, ['┬', '+', '┼', '╪']);
    unicode_box.put(d, d, S, ['┬', '┼', '┼', '╪']);
    unicode_box.put(d, d, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(d, S, n, ['╭', '├', '├', '╟']);
    unicode_box.put(d, S, d, ['┬', '┼', '┼', '╪']);
    unicode_box.put(d, S, S, ['┬', '┼', '┼', '╪']);
    unicode_box.put(d, S, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(d, D, n, ['╓', '╟', '╟', '╟']);
    unicode_box.put(d, D, d, ['╥', '╫', '╫', '╫']);
    unicode_box.put(d, D, S, ['╥', '╫', '╫', '╫']);
    unicode_box.put(d, D, D, ['╦', '╬', '╬', '╬']);
    unicode_box.put(S, n, n, ['╶', '╰', '╰', '╙']);
    unicode_box.put(S, n, d, ['─', '┴', '┴', '╨']);
    unicode_box.put(S, n, S, ['─', '┴', '┴', '╨']);
    unicode_box.put(S, n, D, ['═', '╧', '╧', '╩']);
    unicode_box.put(S, d, n, ['╭', '├', '├', '╟']);
    unicode_box.put(S, d, d, ['┬', '┼', '┼', '╪']);
    unicode_box.put(S, d, S, ['┬', '┼', '┼', '╪']);
    unicode_box.put(S, d, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(S, S, n, ['╭', '├', '├', '╟']);
    unicode_box.put(S, S, d, ['┬', '┼', '┼', '╪']);
    unicode_box.put(S, S, S, ['┬', '┼', '┼', '╪']);
    unicode_box.put(S, S, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(S, D, n, ['╓', '╟', '╟', '╟']);
    unicode_box.put(S, D, d, ['╥', '╫', '╫', '╫']);
    unicode_box.put(S, D, S, ['╥', '╫', '╫', '╫']);
    unicode_box.put(S, D, D, ['╦', '╬', '╬', '╬']);
    unicode_box.put(D, n, n, ['═', '╘', '╘', '╚']);
    unicode_box.put(D, n, d, ['═', '╧', '╧', '╩']);
    unicode_box.put(D, n, S, ['═', '╧', '╧', '╩']);
    unicode_box.put(D, n, D, ['═', '╧', '╧', '╩']);
    unicode_box.put(D, d, n, ['╒', '╞', '╞', '╠']);
    unicode_box.put(D, d, d, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, d, S, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, d, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, S, n, ['╒', '╞', '╞', '╠']);
    unicode_box.put(D, S, d, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, S, S, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, S, D, ['╤', '╪', '╪', '╬']);
    unicode_box.put(D, D, n, ['╔', '╠', '╠', '╠']);
    unicode_box.put(D, D, d, ['╠', '╬', '╬', '╬']);
    unicode_box.put(D, D, S, ['╠', '╬', '╬', '╬']);
    unicode_box.put(D, D, D, ['╦', '╬', '╬', '╬']);
    unicode_box
});

impl PivotTable {
    pub fn display(&self) -> DisplayPivotTable<'_> {
        DisplayPivotTable::new(self)
    }
}

impl Display for PivotTable {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.display())
    }
}

pub struct DisplayPivotTable<'a> {
    pt: &'a PivotTable,
}

impl<'a> DisplayPivotTable<'a> {
    fn new(pt: &'a PivotTable) -> Self {
        Self { pt }
    }
}

impl Display for DisplayPivotTable<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        TextRenderer::default().render_table(self.pt, f)
    }
}

impl Display for Item {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        TextRenderer::default().render(self, f)
    }
}

pub struct TextDriver {
    file: BufWriter<File>,
    renderer: TextRenderer,
}

impl TextDriver {
    pub fn new(config: &TextConfig) -> std::io::Result<TextDriver> {
        Ok(Self {
            file: BufWriter::new(match &config.file {
                Some(file) => File::create(file)?,
                None => File::options().write(true).open("/dev/stdout")?,
            }),
            renderer: TextRenderer::new(&config.options),
        })
    }
}

impl TextRenderer {
    fn render<W>(&mut self, item: &Item, writer: &mut W) -> FmtResult
    where
        W: FmtWrite,
    {
        match &item.details {
            Details::Chart => todo!(),
            Details::Image => todo!(),
            Details::Group(children) => {
                for (index, child) in children.iter().enumerate() {
                    if index > 0 {
                        writeln!(writer)?;
                    }
                    self.render(child, writer)?;
                }
                Ok(())
            }
            Details::Message(_diagnostic) => todo!(),
            Details::PageBreak => Ok(()),
            Details::Table(pivot_table) => self.render_table(pivot_table, writer),
            Details::Text(text) => self.render_table(&PivotTable::from((**text).clone()), writer),
        }
    }

    fn render_table<W>(&mut self, table: &PivotTable, writer: &mut W) -> FmtResult
    where
        W: FmtWrite,
    {
        for (index, layer_indexes) in table.layers(true).enumerate() {
            if index > 0 {
                writeln!(writer)?;
            }

            let mut pager = Pager::new(self, table, Some(layer_indexes.as_slice()));
            while pager.has_next(self) {
                pager.draw_next(self, usize::MAX);
                for line in self.lines.drain(..) {
                    writeln!(writer, "{line}")?;
                }
            }
        }
        Ok(())
    }

    fn layout_cell(&self, text: &str, bb: Rect2) -> Coord2 {
        if text.is_empty() {
            return Coord2::default();
        }

        use Axis2::*;
        let breaks = new_line_breaks(text, bb[X].len());
        let mut size = Coord2::new(0, 0);
        for text in breaks.take(bb[Y].len()) {
            let width = text.width();
            if width > size[X] {
                size[X] = width;
            }
            size[Y] += 1;
        }
        size
    }

    fn get_line(&mut self, y: usize) -> &mut TextLine {
        if y >= self.lines.len() {
            self.lines.resize(y + 1, TextLine::new());
        }
        &mut self.lines[y]
    }
}

struct LineBreaks<'a, B>
where
    B: Iterator<Item = (usize, BreakOpportunity)> + Clone + 'a,
{
    text: &'a str,
    max_width: usize,
    indexes: Range<usize>,
    width: usize,
    saved: Option<(usize, BreakOpportunity)>,
    breaks: B,
    trailing_newlines: usize,
}

impl<'a, B> Iterator for LineBreaks<'a, B>
where
    B: Iterator<Item = (usize, BreakOpportunity)> + Clone + 'a,
{
    type Item = &'a str;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some((postindex, opportunity)) = self.saved.take().or_else(|| self.breaks.next())
        {
            let index = if postindex != self.text.len() {
                self.text[..postindex].char_indices().next_back().unwrap().0
            } else {
                postindex
            };
            if index <= self.indexes.end {
                continue;
            }

            let segment_width = self.text[self.indexes.end..index].width();
            if self.width == 0 || self.width + segment_width <= self.max_width {
                // Add this segment to the current line.
                self.width += segment_width;
                self.indexes.end = index;

                // If this was a new-line, we're done.
                if opportunity == BreakOpportunity::Mandatory {
                    let segment = self.text[self.indexes.clone()].trim_end_matches('\n');
                    self.indexes = postindex..postindex;
                    self.width = 0;
                    return Some(segment);
                }
            } else {
                // Won't fit. Return what we've got and save this segment for next time.
                //
                // We trim trailing spaces from the line we return, and leading
                // spaces from the position where we resume.
                let segment = self.text[self.indexes.clone()].trim_end();

                let start = self.text[self.indexes.end..].trim_start_matches([' ', '\t']);
                let start_index = self.text.len() - start.len();
                self.indexes = start_index..start_index;
                self.width = 0;
                self.saved = Some((postindex, opportunity));
                return Some(segment);
            }
        }
        if self.trailing_newlines > 1 {
            self.trailing_newlines -= 1;
            Some("")
        } else {
            None
        }
    }
}

fn new_line_breaks(
    text: &str,
    width: usize,
) -> LineBreaks<'_, impl Iterator<Item = (usize, BreakOpportunity)> + Clone + '_> {
    // Trim trailing new-lines from the text, because the linebreaking algorithm
    // treats them as if they have width.  That is, if you break `"a b c\na b
    // c\n"` with a 5-character width, then you end up with:
    //
    // ```text
    // a b c
    // a b
    // c
    // ```
    //
    // So, we trim trailing new-lines and then add in extra blank lines at the
    // end if necessary.
    //
    // (The linebreaking algorithm treats new-lines in the middle of the text in
    // a normal way, though.)
    let trimmed = text.trim_end_matches('\n');
    LineBreaks {
        text: trimmed,
        max_width: width,
        indexes: 0..0,
        width: 0,
        saved: None,
        breaks: linebreaks(trimmed),
        trailing_newlines: text.len() - trimmed.len(),
    }
}

impl Driver for TextDriver {
    fn name(&self) -> Cow<'static, str> {
        Cow::from("text")
    }

    fn write(&mut self, item: &Arc<Item>) {
        let _ = self.renderer.render(item, &mut FmtAdapter(&mut self.file));
    }
}

struct FmtAdapter<W>(W);

impl<W> FmtWrite for FmtAdapter<W>
where
    W: IoWrite,
{
    fn write_str(&mut self, s: &str) -> FmtResult {
        self.0.write_all(s.as_bytes()).map_err(|_| FmtError)
    }
}

impl Device for TextRenderer {
    fn params(&self) -> &Params {
        &self.params
    }

    fn measure_cell_width(&self, cell: &DrawCell) -> EnumMap<Extreme, usize> {
        let text = cell.display().to_string();
        enum_map![
            Extreme::Min => self.layout_cell(&text, Rect2::new(0..1, 0..usize::MAX)).x(),
            Extreme::Max => self.layout_cell(&text, Rect2::new(0..usize::MAX, 0..usize::MAX)).x(),
        ]
    }

    fn measure_cell_height(&self, cell: &DrawCell, width: usize) -> usize {
        let text = cell.display().to_string();
        self.layout_cell(&text, Rect2::new(0..width, 0..usize::MAX))
            .y()
    }

    fn adjust_break(&self, _cell: &Content, _size: Coord2) -> usize {
        unreachable!()
    }

    fn draw_line(&mut self, bb: Rect2, styles: EnumMap<Axis2, [BorderStyle; 2]>) {
        use Axis2::*;
        let x = bb[X].start.max(0)..bb[X].end.min(self.width);
        let y = bb[Y].start.max(0)..bb[Y].end;
        if x.is_empty() || x.end >= self.width {
            return;
        }

        let lines = Lines {
            l: styles[Y][0].stroke.into(),
            r: styles[Y][1].stroke.into(),
            t: styles[X][0].stroke.into(),
            b: styles[X][1].stroke.into(),
        };
        let c = self.box_chars[lines];
        for y in y {
            self.get_line(y).put_multiple(x.start, c, x.len());
        }
    }

    fn draw_cell(
        &mut self,
        cell: &DrawCell,
        _alternate_row: bool,
        bb: Rect2,
        valign_offset: usize,
        _spill: EnumMap<Axis2, [usize; 2]>,
        clip: &Rect2,
    ) {
        let display = cell.display();
        let text = display.to_string();
        let horz_align = cell.horz_align(&display);

        use Axis2::*;
        let breaks = new_line_breaks(&text, bb[X].len());
        for (text, y) in breaks.zip(bb[Y].start + valign_offset..bb[Y].end) {
            let width = text.width();
            if !clip[Y].contains(&y) {
                continue;
            }

            let x = match horz_align {
                HorzAlign::Right | HorzAlign::Decimal { .. } => bb[X].end - width,
                HorzAlign::Left => bb[X].start,
                HorzAlign::Center => (bb[X].start + bb[X].end - width).div_ceil(2),
            };
            let Some((x, text)) = clip_text(text, &(x..x + width), &clip[X]) else {
                continue;
            };

            let text = if self.emphasis {
                Emphasis::from(&cell.style.font_style).apply(text)
            } else {
                Cow::from(text)
            };
            self.get_line(y).put(x, &text);
        }
    }

    fn scale(&mut self, _factor: f64) {
        unimplemented!()
    }
}

#[cfg(test)]
mod tests {
    use unicode_width::{UnicodeWidthChar, UnicodeWidthStr};

    use crate::output::text::new_line_breaks;

    #[test]
    fn unicode_width() {
        // `\n` is a control character, so [UnicodeWidthChar] considers it to
        // have no width.
        assert_eq!('\n'.width(), None);

        // But [UnicodeWidthStr] in unicode-width 0.1.14+ has a different idea.
        assert_eq!("\n".width(), 1);
        assert_eq!("\r\n".width(), 1);
    }

    #[track_caller]
    fn test_line_breaks(input: &str, width: usize, expected: Vec<&str>) {
        let actual = new_line_breaks(input, width).collect::<Vec<_>>();
        if expected != actual {
            panic!("filling {input:?} to {width} columns:\nexpected: {expected:?}\nactual:   {actual:?}");
        }
    }
    #[test]
    fn line_breaks() {
        test_line_breaks(
            "One line of text\nOne line of text\n",
            16,
            vec!["One line of text", "One line of text"],
        );
        test_line_breaks("a b c\na b c\na b c\n", 5, vec!["a b c", "a b c", "a b c"]);
        for width in 0..=6 {
            test_line_breaks("abc def ghi", width, vec!["abc", "def", "ghi"]);
        }
        for width in 7..=10 {
            test_line_breaks("abc def ghi", width, vec!["abc def", "ghi"]);
        }
        test_line_breaks("abc def ghi", 11, vec!["abc def ghi"]);

        for width in 0..=6 {
            test_line_breaks("abc  def ghi", width, vec!["abc", "def", "ghi"]);
        }
        test_line_breaks("abc  def ghi", 7, vec!["abc", "def ghi"]);
        for width in 8..=11 {
            test_line_breaks("abc  def ghi", width, vec!["abc  def", "ghi"]);
        }
        test_line_breaks("abc  def ghi", 12, vec!["abc  def ghi"]);

        test_line_breaks("abc\ndef\nghi", 2, vec!["abc", "def", "ghi"]);
    }
}
