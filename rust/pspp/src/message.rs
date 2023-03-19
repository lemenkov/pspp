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
    cmp::{max, min},
    fmt::{Debug, Display, Formatter, Result as FmtResult},
    ops::Range,
    sync::Arc,
};

use enum_map::Enum;
use unicode_width::UnicodeWidthStr;

/// A line number and optional column number within a source file.
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Point {
    /// 1-based line number.
    pub line: i32,

    /// 1-based column number.
    ///
    /// Column numbers are measured according to the width of characters as
    /// shown in a typical fixed-width font, in which CJK characters have width
    /// 2 and combining characters have width 0, as measured by the
    /// `unicode_width` crate.
    pub column: Option<i32>,
}

impl Point {
    /// Takes `point`, adds to it the syntax in `syntax`, incrementing the line
    /// number for each new-line in `syntax` and the column number for each
    /// column, and returns the result.
    pub fn advance(&self, syntax: &str) -> Self {
        let mut result = *self;
        for line in syntax.split_inclusive('\n') {
            if line.ends_with('\n') {
                result.line += 1;
                result.column = Some(1);
            } else {
                result.column = result.column.map(|column| column + line.width() as i32);
            }
        }
        result
    }

    pub fn without_column(&self) -> Self {
        Self {
            line: self.line,
            column: None,
        }
    }
}

/// Location relevant to an diagnostic message.
#[derive(Clone, Debug, Default)]
pub struct Location {
    /// File name, if any.
    pub file_name: Option<Arc<String>>,

    /// Starting and ending point, if any.
    pub span: Option<Range<Point>>,

    /// Normally, if `span` contains column information, then displaying the
    /// message will underline the location.  Setting this to true disables
    /// displaying underlines.
    pub omit_underlines: bool,
}

impl Display for Location {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if let Some(file_name) = &self.file_name {
            write!(f, "{}", file_name)?;
        }

        if let Some(span) = &self.span {
            if self.file_name.is_some() {
                write!(f, ":")?;
            }
            let l1 = span.start.line;
            let l2 = span.end.line;
            match (span.start.column.zip(span.end.column), l2 > l1) {
                (Some((c1, c2)), true) => write!(f, "{l1}.{c1}-{l2}.{}", c2 - 1)?,
                (Some((c1, c2)), false) => write!(f, "{l1}.{c1}-{}", c2 - 1)?,
                (None, true) => write!(f, "{l1}-{l2}")?,
                (None, false) => write!(f, "{l1}")?,
            }
        }
        Ok(())
    }
}

impl Location {
    pub fn without_columns(&self) -> Self {
        Self {
            file_name: self.file_name.clone(),
            span: self
                .span
                .as_ref()
                .map(|span| span.start.without_column()..span.end.without_column()),
            omit_underlines: self.omit_underlines,
        }
    }
    pub fn merge(a: Option<Self>, b: &Option<Self>) -> Option<Self> {
        let Some(a) = a else { return b.clone() };
        let Some(b) = b else { return Some(a) };
        if a.file_name != b.file_name {
            // Failure.
            return Some(a);
        }
        let span = match (&a.span, &b.span) {
            (None, None) => None,
            (Some(r), None) | (None, Some(r)) => Some(r.clone()),
            (Some(ar), Some(br)) => Some(min(ar.start, br.start)..max(ar.end, br.end)),
        };
        Some(Self {
            file_name: a.file_name,
            span,
            omit_underlines: a.omit_underlines || b.omit_underlines,
        })
    }
    pub fn is_empty(&self) -> bool {
        self.file_name.is_none() && self.span.is_none()
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Enum)]
pub enum Severity {
    Error,
    Warning,
    Note,
}

impl Severity {
    pub fn as_str(&self) -> &'static str {
        match self {
            Severity::Error => "error",
            Severity::Warning => "warning",
            Severity::Note => "note",
        }
    }

    pub fn as_title_str(&self) -> &'static str {
        match self {
            Severity::Error => "Error",
            Severity::Warning => "Warning",
            Severity::Note => "Note",
        }
    }
}

impl Display for Severity {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}", self.as_str())
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Category {
    General,
    Syntax,
    Data,
}

pub struct Stack {
    location: Location,
    description: String,
}

#[derive(Debug, Default)]
pub struct Diagnostics(pub Vec<Diagnostic>);

impl From<Diagnostic> for Diagnostics {
    fn from(value: Diagnostic) -> Self {
        Self(vec![value])
    }
}

pub struct Diagnostic {
    pub severity: Severity,
    pub category: Category,
    pub location: Location,
    pub source: Vec<(i32, String)>,
    pub stack: Vec<Stack>,
    pub command_name: Option<&'static str>,
    pub text: String,
}

impl Display for Diagnostic {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        for Stack {
            location,
            description,
        } in &self.stack
        {
            if !location.is_empty() {
                write!(f, "{location}: ")?;
            }
            writeln!(f, "{description}")?;
        }
        if self.category != Category::General && !self.location.is_empty() {
            write!(f, "{}: ", self.location)?;
        }

        write!(f, "{}: ", self.severity)?;

        match self.command_name {
            Some(command_name) if self.category == Category::Syntax => {
                write!(f, "{command_name}: ")?
            }
            _ => (),
        }

        write!(f, "{}", self.text)?;

        if let Some(Range {
            start: Point {
                line: l0,
                column: Some(c0),
            },
            end: Point {
                line: l1,
                column: Some(c1),
            },
        }) = self.location.span
        {
            let mut prev_line_number = None;
            for (line_number, line) in &self.source {
                if let Some(prev_line_number) = prev_line_number {
                    if *line_number != prev_line_number + 1 {
                        write!(f, "\n  ... |")?;
                    }
                }
                prev_line_number = Some(line_number);

                write!(f, "\n{line_number:5} | {line}")?;

                if !self.location.omit_underlines {
                    let c0 = if *line_number == l0 { c0 } else { 1 };
                    let c1 = if *line_number == l1 {
                        c1
                    } else {
                        line.width() as i32
                    };
                    write!(f, "\n      |")?;
                    for _ in 0..c0 {
                        f.write_str(" ")?;
                    }
                    if *line_number == l0 {
                        f.write_str("^")?;
                        for _ in c0..c1 {
                            f.write_str("~")?;
                        }
                    } else {
                        for _ in c0..=c1 {
                            f.write_str("~")?;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

impl Debug for Diagnostic {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        Display::fmt(&self, f)
    }
}
