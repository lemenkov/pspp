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
    fmt::Display,
    fs::File,
    io::{Error, Write},
    sync::Arc,
};

use crate::output::pivot::Coord2;

use super::{driver::Driver, pivot::PivotTable, table::Table, Details, Item, TextType};

struct CsvDriver {
    file: File,
    options: CsvOptions,

    /// Number of items written so far.
    n_items: usize,
}

#[derive(Copy, Clone, Debug)]
struct CsvOptions {
    quote: u8,
    delimiter: u8,
}

impl Default for CsvOptions {
    fn default() -> Self {
        Self {
            quote: b'"',
            delimiter: b',',
        }
    }
}

impl CsvOptions {
    fn byte_needs_quoting(&self, b: u8) -> bool {
        b == b'\r' || b == b'\n' || b == self.quote || b == self.delimiter
    }

    fn string_needs_quoting(&self, s: &str) -> bool {
        s.bytes().any(|b| self.byte_needs_quoting(b))
    }
}

struct CsvField<'a> {
    text: &'a str,
    options: CsvOptions,
}

impl<'a> CsvField<'a> {
    fn new(text: &'a str, options: CsvOptions) -> Self {
        Self { text, options }
    }
}

impl Display for CsvField<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.options.string_needs_quoting(self.text) {
            let quote = self.options.quote as char;
            write!(f, "{quote}")?;
            for c in self.text.chars() {
                if c == quote {
                    write!(f, "{c}")?;
                }
                write!(f, "{c}")?;
            }
            write!(f, "{quote}")
        } else {
            write!(f, "{}", self.text)
        }
    }
}

impl CsvDriver {
    pub fn new(file: File) -> Self {
        Self {
            file,
            options: CsvOptions::default(),
            n_items: 0,
        }
    }

    fn start_item(&mut self) {
        if self.n_items > 0 {
            writeln!(&mut self.file).unwrap();
        }
        self.n_items += 1;
    }

    fn output_table_layer(&mut self, pt: &PivotTable, layer: &[usize]) -> Result<(), Error> {
        let output = pt.output(layer, true);
        self.start_item();

        self.output_table(pt, output.title.as_ref(), Some("Table"))?;
        self.output_table(pt, output.layers.as_ref(), Some("Layer"))?;
        self.output_table(pt, Some(&output.body), None)?;
        self.output_table(pt, output.caption.as_ref(), Some("Caption"))?;
        self.output_table(pt, output.footnotes.as_ref(), Some("Footnote"))?;
        Ok(())
    }

    fn output_table(
        &mut self,
        pivot_table: &PivotTable,
        table: Option<&Table>,
        leader: Option<&str>,
    ) -> Result<(), Error> {
        let Some(table) = table else {
            return Ok(());
        };

        for y in 0..table.n.y() {
            for x in 0..table.n.x() {
                if x > 0 {
                    write!(&mut self.file, "{}", self.options.delimiter as char)?;
                }

                let coord = Coord2::new(x, y);
                let content = table.get(coord);
                if content.is_top_left() {
                    let display = content.inner().value.display(pivot_table);
                    let s = match leader {
                        Some(leader) if x == 0 && y == 0 => format!("{leader}: {display}"),
                        _ => display.to_string(),
                    };
                    write!(&mut self.file, "{}", CsvField::new(&s, self.options))?;
                }
            }
            writeln!(&mut self.file)?;
        }

        Ok(())
    }
}

impl Driver for CsvDriver {
    fn name(&self) -> Cow<'static, str> {
        Cow::from("csv")
    }

    fn write(&mut self, item: &Arc<Item>) {
        // todo: error handling (should not unwrap)
        match &item.details {
            Details::Chart | Details::Image | Details::Group(_) => (),
            Details::Message(diagnostic) => {
                self.start_item();
                let text = diagnostic.to_string();
                writeln!(&self.file, "{}", CsvField::new(&text, self.options)).unwrap();
            }
            Details::Table(pivot_table) => {
                for layer in pivot_table.layers(true) {
                    self.output_table_layer(pivot_table, &layer).unwrap();
                }
            }
            Details::PageBreak => {
                self.start_item();
                writeln!(&mut self.file).unwrap();
            }
            Details::Text(text) => match text.type_ {
                TextType::Syntax | TextType::PageTitle => (),
                TextType::Title | TextType::Log => {
                    self.start_item();
                    for line in text.content.display(()).to_string().lines() {
                        writeln!(&self.file, "{}", CsvField::new(line, self.options)).unwrap();
                    }
                }
            },
        }
    }

    fn flush(&mut self) {
        let _ = self.file.flush();
    }
}
