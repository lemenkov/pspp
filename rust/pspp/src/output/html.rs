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
    fmt::{Display, Write as _},
    fs::File,
    io::Write,
    path::PathBuf,
    sync::Arc,
};

use serde::{Deserialize, Serialize};
use smallstr::SmallString;

use crate::output::{
    Details, Item,
    driver::Driver,
    pivot::{Axis2, BorderStyle, Color, Coord2, HorzAlign, PivotTable, Rect2, Stroke, VertAlign},
    table::{DrawCell, Table},
};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct HtmlConfig {
    file: PathBuf,
}

pub struct HtmlDriver<W> {
    writer: W,
    fg: Color,
    bg: Color,
}

impl Stroke {
    fn as_css(&self) -> Option<&'static str> {
        match self {
            Stroke::None => None,
            Stroke::Solid => Some("1pt solid"),
            Stroke::Dashed => Some("1pt dashed"),
            Stroke::Thick => Some("2pt solid"),
            Stroke::Thin => Some("0.5pt solid"),
            Stroke::Double => Some("double"),
        }
    }
}

impl HtmlDriver<File> {
    pub fn new(config: &HtmlConfig) -> std::io::Result<Self> {
        Ok(Self::for_writer(File::create(&config.file)?))
    }
}

impl<W> HtmlDriver<W>
where
    W: Write,
{
    pub fn for_writer(mut writer: W) -> Self {
        let _ = put_header(&mut writer);
        Self {
            fg: Color::BLACK,
            bg: Color::WHITE,
            writer,
        }
    }

    fn render(&mut self, pivot_table: &PivotTable) -> std::io::Result<()> {
        for layer_indexes in pivot_table.layers(true) {
            let output = pivot_table.output(&layer_indexes, false);
            write!(&mut self.writer, "<table")?;
            if let Some(notes) = &pivot_table.notes {
                write!(&mut self.writer, r#" title="{}""#, Escape::new(notes))?;
            }
            writeln!(&mut self.writer, ">")?;

            if let Some(title) = output.title {
                let cell = title.get(Coord2::new(0, 0));
                self.put_cell(
                    DrawCell::new(cell.inner(), &title),
                    Rect2::new(0..1, 0..1),
                    false,
                    "caption",
                    None,
                )?;
            }

            if let Some(layers) = output.layers {
                writeln!(&mut self.writer, "<thead>")?;
                for cell in layers.cells() {
                    writeln!(&mut self.writer, "<tr>")?;
                    self.put_cell(
                        DrawCell::new(cell.inner(), &layers),
                        Rect2::new(0..output.body.n[Axis2::X], 0..1),
                        false,
                        "td",
                        None,
                    )?;
                    writeln!(&mut self.writer, "</tr>")?;
                }
                writeln!(&mut self.writer, "</thead>")?;
            }

            writeln!(&mut self.writer, "<tbody>")?;
            for y in 0..output.body.n.y() {
                writeln!(&mut self.writer, "<tr>")?;
                for x in output.body.iter_x(y) {
                    let cell = output.body.get(Coord2::new(x, y));
                    if cell.is_top_left() {
                        let is_header = x < output.body.h[Axis2::X] || y < output.body.h[Axis2::Y];
                        let tag = if is_header { "th" } else { "td" };
                        let alternate_row = y
                            .checked_sub(output.body.h[Axis2::Y])
                            .is_some_and(|y| y % 2 == 1);
                        self.put_cell(
                            DrawCell::new(cell.inner(), &output.body),
                            cell.rect(),
                            alternate_row,
                            tag,
                            Some(&output.body),
                        )?;
                    }
                }
                writeln!(&mut self.writer, "</tr>")?;
            }
            writeln!(&mut self.writer, "</tbody>")?;

            if output.caption.is_some() || output.footnotes.is_some() {
                writeln!(&mut self.writer, "<tfoot>")?;
                writeln!(&mut self.writer, "<tr>")?;
                if let Some(caption) = output.caption {
                    self.put_cell(
                        DrawCell::new(caption.get(Coord2::new(0, 0)).inner(), &caption),
                        Rect2::new(0..output.body.n[Axis2::X], 0..1),
                        false,
                        "td",
                        None,
                    )?;
                }
                writeln!(&mut self.writer, "</tr>")?;

                if let Some(footnotes) = output.footnotes {
                    for cell in footnotes.cells() {
                        writeln!(&mut self.writer, "<tr>")?;
                        self.put_cell(
                            DrawCell::new(cell.inner(), &footnotes),
                            Rect2::new(0..output.body.n[Axis2::X], 0..1),
                            false,
                            "td",
                            None,
                        )?;
                        writeln!(&mut self.writer, "</tr>")?;
                    }
                }
                writeln!(&mut self.writer, "</tfoot>")?;
            }
        }
        Ok(())
    }

    fn put_cell(
        &mut self,
        cell: DrawCell<'_>,
        rect: Rect2,
        alternate_row: bool,
        tag: &str,
        table: Option<&Table>,
    ) -> std::io::Result<()> {
        write!(&mut self.writer, "<{tag}")?;
        let (body, suffixes) = cell.display().split_suffixes();

        let mut style = String::new();
        let horz_align = match cell.horz_align(&body) {
            HorzAlign::Right | HorzAlign::Decimal { .. } => Some("right"),
            HorzAlign::Center => Some("center"),
            HorzAlign::Left => None,
        };
        if let Some(horz_align) = horz_align {
            write!(&mut style, "text-align: {horz_align}; ").unwrap();
        }

        if cell.rotate {
            write!(&mut style, "writing-mode: sideways-lr; ").unwrap();
        }

        let vert_align = match cell.style.cell_style.vert_align {
            VertAlign::Top => None,
            VertAlign::Middle => Some("middle"),
            VertAlign::Bottom => Some("bottom"),
        };
        if let Some(vert_align) = vert_align {
            write!(&mut style, "vertical-align: {vert_align}; ").unwrap();
        }
        let bg = cell.style.font_style.bg[alternate_row as usize];
        if bg != Color::WHITE {
            write!(&mut style, "background: {}; ", bg.display_css()).unwrap();
        }

        let fg = cell.style.font_style.fg[alternate_row as usize];
        if fg != Color::BLACK {
            write!(&mut style, "color: {}; ", fg.display_css()).unwrap();
        }

        if !cell.style.font_style.font.is_empty() {
            write!(
                &mut style,
                r#"font-family: "{}"; "#,
                Escape::new(&cell.style.font_style.font)
            )
            .unwrap();
        }

        if cell.style.font_style.bold {
            write!(&mut style, "font-weight: bold; ").unwrap();
        }
        if cell.style.font_style.italic {
            write!(&mut style, "font-style: italic; ").unwrap();
        }
        if cell.style.font_style.underline {
            write!(&mut style, "text-decoration: underline; ").unwrap();
        }
        if cell.style.font_style.size != 0 {
            write!(&mut style, "font-size: {}pt; ", cell.style.font_style.size).unwrap();
        }

        if let Some(table) = table {
            Self::put_border(&mut style, table.get_rule(Axis2::Y, rect.top_left()), "top");
            Self::put_border(
                &mut style,
                table.get_rule(Axis2::X, rect.top_left()),
                "left",
            );
            if rect[Axis2::X].end == table.n[Axis2::X] {
                Self::put_border(
                    &mut style,
                    table.get_rule(
                        Axis2::X,
                        Coord2::new(rect[Axis2::X].end, rect[Axis2::Y].start),
                    ),
                    "right",
                );
            }
            if rect[Axis2::Y].end == table.n[Axis2::Y] {
                Self::put_border(
                    &mut style,
                    table.get_rule(
                        Axis2::Y,
                        Coord2::new(rect[Axis2::X].start, rect[Axis2::Y].end),
                    ),
                    "bottom",
                );
            }
        }

        if !style.is_empty() {
            write!(
                &mut self.writer,
                " style='{}'",
                Escape::new(style.trim_end_matches("; "))
                    .with_apos("&apos;")
                    .with_quote("\"")
            )?;
        }

        let col_span = rect[Axis2::X].len();
        if col_span > 1 {
            write!(&mut self.writer, r#" colspan="{col_span}""#)?;
        }

        let row_span = rect[Axis2::Y].len();
        if row_span > 1 {
            write!(&mut self.writer, r#" rowspan="{row_span}""#)?;
        }

        write!(&mut self.writer, ">")?;

        let mut text = SmallString::<[u8; 64]>::new();
        write!(&mut text, "{body}").unwrap();
        write!(
            &mut self.writer,
            "{}",
            Escape::new(&text).with_newline("<br>")
        )?;

        if suffixes.has_subscripts() {
            write!(&mut self.writer, "<sub>")?;
            for (index, subscript) in suffixes.subscripts().enumerate() {
                if index > 0 {
                    write!(&mut self.writer, ",")?;
                }
                write!(
                    &mut self.writer,
                    "{}",
                    Escape::new(subscript)
                        .with_space("&nbsp;")
                        .with_newline("<br>")
                )?;
            }
            write!(&mut self.writer, "</sub>")?;
        }

        if suffixes.has_footnotes() {
            write!(&mut self.writer, "<sup>")?;
            for (index, footnote) in suffixes.footnotes().enumerate() {
                if index > 0 {
                    write!(&mut self.writer, ",")?;
                }
                let mut marker = SmallString::<[u8; 8]>::new();
                write!(&mut marker, "{footnote}").unwrap();
                write!(
                    &mut self.writer,
                    "{}",
                    Escape::new(&marker)
                        .with_space("&nbsp;")
                        .with_newline("<br>")
                )?;
            }
            write!(&mut self.writer, "</sup>")?;
        }

        writeln!(&mut self.writer, "</{tag}>")
    }

    fn put_border(dst: &mut String, style: BorderStyle, border_name: &str) {
        if let Some(css_style) = style.stroke.as_css() {
            write!(dst, "border-{border_name}: {css_style}").unwrap();
            if style.color != Color::BLACK {
                write!(dst, " {}", style.color.display_css()).unwrap();
            }
            write!(dst, "; ").unwrap();
        }
    }
}

fn put_header<W>(mut writer: W) -> std::io::Result<()>
where
    W: Write,
{
    write!(
        &mut writer,
        r#"<!doctype html>
<html>
<head>
<title>PSPP Output</title>
<meta name="generator" content="PSPP {}"/>
<meta http-equiv="content-type" content="text/html; charset=utf8"/>
{}
"#,
        Escape::new(env!("CARGO_PKG_VERSION")),
        HEADER_CSS,
    )?;
    Ok(())
}

const HEADER_CSS: &str = r#"<style>
body {
  background: white;
  color: black;
  padding: 0em 12em 0em 3em;
  margin: 0
}
body>p {
  margin: 0pt 0pt 0pt 0em
}
body>p + p {
  text-indent: 1.5em;
}
h1 {
  font-size: 150%;
  margin-left: -1.33em
}
h2 {
  font-size: 125%;
  font-weight: bold;
  margin-left: -.8em
}
h3 {
  font-size: 100%;
  font-weight: bold;
  margin-left: -.5em }
h4 {
  font-size: 100%;
  margin-left: 0em
}
h1, h2, h3, h4, h5, h6 {
  font-family: sans-serif;
  color: blue
}
html {
  margin: 0
}
code {
  font-family: sans-serif
}
table {
  border-collapse: collapse;
  margin-bottom: 1em
}
caption {
  text-align: left;
  width: 100%
}
th { font-weight: normal }
a:link {
  color: #1f00ff;
}
a:visited {
  color: #9900dd;
}
a:active {
  color: red;
}
</style>
</head>
<body>
"#;

impl<W> Driver for HtmlDriver<W>
where
    W: Write,
{
    fn name(&self) -> Cow<'static, str> {
        Cow::from("html")
    }

    fn write(&mut self, item: &Arc<Item>) {
        match &item.details {
            Details::Chart => todo!(),
            Details::Image => todo!(),
            Details::Group(_) => todo!(),
            Details::Message(_diagnostic) => todo!(),
            Details::PageBreak => (),
            Details::Table(pivot_table) => {
                self.render(pivot_table).unwrap(); // XXX
            }
            Details::Text(_text) => todo!(),
        }
    }
}

struct Escape<'a> {
    string: &'a str,
    space: &'static str,
    newline: &'static str,
    quote: &'static str,
    apos: &'static str,
}

impl<'a> Escape<'a> {
    fn new(string: &'a str) -> Self {
        Self {
            string,
            space: " ",
            newline: "\n",
            quote: "&quot;",
            apos: "'",
        }
    }
    fn with_space(self, space: &'static str) -> Self {
        Self { space, ..self }
    }
    fn with_newline(self, newline: &'static str) -> Self {
        Self { newline, ..self }
    }
    fn with_quote(self, quote: &'static str) -> Self {
        Self { quote, ..self }
    }
    fn with_apos(self, apos: &'static str) -> Self {
        Self { apos, ..self }
    }
}

impl Display for Escape<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        for c in self.string.chars() {
            match c {
                '\n' => f.write_str(self.newline)?,
                ' ' => f.write_str(self.space)?,
                '&' => f.write_str("&amp;")?,
                '<' => f.write_str("&lt;")?,
                '>' => f.write_str("&gt;")?,
                '"' => f.write_str(self.quote)?,
                '\'' => f.write_str(self.apos)?,
                _ => f.write_char(c)?,
            }
        }
        Ok(())
    }
}
