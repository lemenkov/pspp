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

use enum_iterator::Sequence;
use std::{
    borrow::Cow,
    cmp::Ordering,
    fmt::{Debug, Display},
    ops::Range,
};

use unicode_width::UnicodeWidthChar;

use crate::output::pivot::FontStyle;

/// A line of text, encoded in UTF-8, with support functions that properly
/// handle double-width characters and backspaces.
///
/// Designed to make appending text fast, and access and modification of other
/// column positions possible.
#[derive(Clone, Default)]
pub struct TextLine {
    /// Content.
    string: String,

    /// Display width, in character positions.
    width: usize,
}

impl TextLine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn clear(&mut self) {
        self.string.clear();
        self.width = 0;
    }

    /// Changes the width of this line to `x` columns.  If `x` is longer than
    /// the current width, extends the line with spaces.  If `x` is shorter than
    /// the current width, removes trailing characters.
    pub fn resize(&mut self, x: usize) {
        match x.cmp(&self.width) {
            Ordering::Greater => self.string.extend((self.width..x).map(|_| ' ')),
            Ordering::Less => {
                let pos = self.find_pos(x);
                self.string.truncate(pos.offsets.start);
                if x > pos.offsets.start {
                    self.string.extend((pos.offsets.start..x).map(|_| '?'));
                }
            }
            Ordering::Equal => return,
        }
        self.width = x;
    }

    fn put_closure<F>(&mut self, x0: usize, w: usize, push_str: F)
    where
        F: FnOnce(&mut String),
    {
        let x1 = x0 + w;
        if w == 0 {
            // Nothing to do.
        } else if x0 >= self.width {
            // The common case: adding new characters at the end of a line.
            self.string.extend((self.width..x0).map(|_| ' '));
            push_str(&mut self.string);
            self.width = x1;
        } else if x1 >= self.width {
            let p0 = self.find_pos(x0);

            // If a double-width character occupies both `x0 - 1` and `x0`, then
            // replace its first character width by `?`.
            self.string.truncate(p0.offsets.start);
            self.string.extend((p0.columns.start..x0).map(|_| '?'));
            push_str(&mut self.string);
            self.width = x1;
        } else {
            let span = self.find_span(x0, x1);
            let tail = self.string.split_off(span.offsets.end);
            self.string.truncate(span.offsets.start);
            self.string.extend((span.columns.start..x0).map(|_| '?'));
            push_str(&mut self.string);
            self.string.extend((x1..span.columns.end).map(|_| '?'));
            self.string.push_str(&tail);
        }
    }

    pub fn put(&mut self, x0: usize, s: &str) {
        self.string.reserve(s.len());
        self.put_closure(x0, Widths::new(s).sum(), |dst| dst.push_str(s));
    }

    pub fn put_multiple(&mut self, x0: usize, c: char, n: usize) {
        self.string.reserve(c.len_utf8() * n);
        self.put_closure(x0, c.width().unwrap() * n, |dst| {
            (0..n).for_each(|_| dst.push(c))
        });
    }

    fn find_span(&self, x0: usize, x1: usize) -> Position {
        debug_assert!(x1 > x0);
        let p0 = self.find_pos(x0);
        let p1 = self.find_pos(x1 - 1);
        Position {
            columns: p0.columns.start..p1.columns.end,
            offsets: p0.offsets.start..p1.offsets.end,
        }
    }

    // Returns the [Position] that contains column `target_x`.
    fn find_pos(&self, target_x: usize) -> Position {
        let mut x = 0;
        let mut ofs = 0;
        let mut widths = Widths::new(&self.string);
        while let Some(w) = widths.next() {
            if x + w > target_x {
                return Position {
                    columns: x..x + w,
                    offsets: ofs..widths.offset(),
                };
            }
            ofs = widths.offset();
            x += w;
        }

        // This can happen if there are non-printable characters in a line.
        Position {
            columns: x..x,
            offsets: ofs..ofs,
        }
    }

    pub fn str(&self) -> &str {
        &self.string
    }
}

impl Display for TextLine {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.string)
    }
}

/// Position of one or more characters within a [TextLine].
#[derive(Debug)]
struct Position {
    /// 0-based display columns.
    columns: Range<usize>,

    /// Byte offests.
    offsets: Range<usize>,
}

/// Iterates through the column widths in a string.
struct Widths<'a> {
    s: &'a str,
    base: &'a str,
}

impl<'a> Widths<'a> {
    fn new(s: &'a str) -> Self {
        Self { s, base: s }
    }

    /// Returns the amount of the string remaining to be visited.
    fn as_str(&self) -> &str {
        self.s
    }

    // Returns the offset into the original string of the characters remaining
    // to be visited.
    fn offset(&self) -> usize {
        self.base.len() - self.s.len()
    }
}

impl Iterator for Widths<'_> {
    type Item = usize;

    fn next(&mut self) -> Option<Self::Item> {
        let mut iter = self.s.char_indices();
        let (_, mut c) = iter.next()?;
        while iter.as_str().starts_with('\x08') {
            iter.next();
            c = match iter.next() {
                Some((_, c)) => c,
                _ => {
                    self.s = iter.as_str();
                    return Some(0);
                }
            };
        }

        let w = c.width().unwrap_or_default();
        if w == 0 {
            self.s = iter.as_str();
            return Some(0);
        }

        for (index, c) in iter {
            if c.width().is_some_and(|width| width > 0) {
                self.s = &self.s[index..];
                return Some(w);
            }
        }
        self.s = "";
        Some(w)
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Sequence)]
pub struct Emphasis {
    pub bold: bool,
    pub underline: bool,
}

impl From<&FontStyle> for Emphasis {
    fn from(style: &FontStyle) -> Self {
        Self {
            bold: style.bold,
            underline: style.underline,
        }
    }
}

impl Emphasis {
    const fn plain() -> Self {
        Self {
            bold: false,
            underline: false,
        }
    }
    pub fn is_plain(&self) -> bool {
        *self == Self::plain()
    }
    pub fn apply<'a>(&self, s: &'a str) -> Cow<'a, str> {
        if self.is_plain() {
            Cow::from(s)
        } else {
            let mut output = String::with_capacity(
                s.len() * (1 + self.bold as usize * 2 + self.underline as usize * 2),
            );
            for c in s.chars() {
                if self.bold {
                    output.push(c);
                    output.push('\x08');
                }
                if self.underline {
                    output.push('_');
                    output.push('\x08');
                }
                output.push(c);
            }
            Cow::from(output)
        }
    }
}

impl Debug for Emphasis {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}",
            match self {
                Self {
                    bold: false,
                    underline: false,
                } => "plain",
                Self {
                    bold: true,
                    underline: false,
                } => "bold",
                Self {
                    bold: false,
                    underline: true,
                } => "underline",
                Self {
                    bold: true,
                    underline: true,
                } => "bold+underline",
            }
        )
    }
}

pub fn clip_text<'a>(
    text: &'a str,
    bb: &Range<usize>,
    clip: &Range<usize>,
) -> Option<(usize, &'a str)> {
    let mut x = bb.start;
    let mut width = bb.len();

    let mut iter = text.chars();
    while x < clip.start {
        let c = iter.next()?;
        if let Some(w) = c.width() {
            x += w;
            width = width.checked_sub(w)?;
        }
    }
    if x + width > clip.end {
        if x >= clip.end {
            return None;
        }

        while x + width > clip.end {
            let c = iter.next_back()?;
            if let Some(w) = c.width() {
                width = width.checked_sub(w)?;
            }
        }
    }
    Some((x, iter.as_str()))
}

#[cfg(test)]
mod test {
    use super::{Emphasis, TextLine};
    use enum_iterator::all;

    #[test]
    fn overwrite_rest_of_line() {
        for lowercase in all::<Emphasis>() {
            for uppercase in all::<Emphasis>() {
                let mut line = TextLine::new();
                line.put(0, &lowercase.apply("abc"));
                line.put(1, &uppercase.apply("BCD"));
                assert_eq!(
                    line.str(),
                    &format!("{}{}", lowercase.apply("a"), uppercase.apply("BCD")),
                    "uppercase={uppercase:?} lowercase={lowercase:?}"
                );
            }
        }
    }

    #[test]
    fn overwrite_partial_line() {
        for lowercase in all::<Emphasis>() {
            for uppercase in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `AbCDEf`.
                line.put(0, &lowercase.apply("abcdef"));
                line.put(0, &uppercase.apply("A"));
                line.put(2, &uppercase.apply("CDE"));
                assert_eq!(
                    line.str().replace('\x08', "#"),
                    format!(
                        "{}{}{}{}",
                        uppercase.apply("A"),
                        lowercase.apply("b"),
                        uppercase.apply("CDE"),
                        lowercase.apply("f")
                    )
                    .replace('\x08', "#"),
                    "uppercase={uppercase:?} lowercase={lowercase:?}"
                );
            }
        }
    }

    #[test]
    fn overwrite_rest_with_double_width() {
        for lowercase in all::<Emphasis>() {
            for hiragana in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `kaきくけ"`.
                line.put(0, &lowercase.apply("kakiku"));
                line.put(2, &hiragana.apply("きくけ"));
                assert_eq!(
                    line.str(),
                    &format!("{}{}", lowercase.apply("ka"), hiragana.apply("きくけ")),
                    "lowercase={lowercase:?} hiragana={hiragana:?}"
                );
            }
        }
    }

    #[test]
    fn overwrite_partial_with_double_width() {
        for lowercase in all::<Emphasis>() {
            for hiragana in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `かkiくけko".
                line.put(0, &lowercase.apply("kakikukeko"));
                line.put(0, &hiragana.apply("か"));
                line.put(4, &hiragana.apply("くけ"));
                assert_eq!(
                    line.str(),
                    &format!(
                        "{}{}{}{}",
                        hiragana.apply("か"),
                        lowercase.apply("ki"),
                        hiragana.apply("くけ"),
                        lowercase.apply("ko")
                    ),
                    "lowercase={lowercase:?} hiragana={hiragana:?}"
                );
            }
        }
    }

    /// Overwrite rest of line, aligned double-width over double-width
    #[test]
    fn aligned_double_width_rest_of_line() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `あきくけ`.
                line.put(0, &bottom.apply("あいう"));
                line.put(2, &top.apply("きくけ"));
                assert_eq!(
                    line.str(),
                    &format!("{}{}", bottom.apply("あ"), top.apply("きくけ")),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite rest of line, misaligned double-width over double-width
    #[test]
    fn misaligned_double_width_rest_of_line() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `あきくけ`.
                line.put(0, &bottom.apply("あいう"));
                line.put(3, &top.apply("きくけ"));
                assert_eq!(
                    line.str(),
                    &format!("{}?{}", bottom.apply("あ"), top.apply("きくけ")),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite partial line, aligned double-width over double-width
    #[test]
    fn aligned_double_width_partial() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `かいくけお`.
                line.put(0, &bottom.apply("あいうえお"));
                line.put(0, &top.apply("か"));
                line.put(4, &top.apply("くけ"));
                assert_eq!(
                    line.str(),
                    &format!(
                        "{}{}{}{}",
                        top.apply("か"),
                        bottom.apply("い"),
                        top.apply("くけ"),
                        bottom.apply("お")
                    ),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite partial line, misaligned double-width over double-width
    #[test]
    fn misaligned_double_width_partial() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `?か?うえおさ`.
                line.put(0, &bottom.apply("あいうえおさ"));
                line.put(1, &top.apply("か"));
                assert_eq!(
                    line.str(),
                    &format!("?{}?{}", top.apply("か"), bottom.apply("うえおさ"),),
                    "bottom={bottom:?} top={top:?}"
                );

                // Produces `?か??くけ?さ`.
                line.put(5, &top.apply("くけ"));
                assert_eq!(
                    line.str(),
                    &format!(
                        "?{}??{}?{}",
                        top.apply("か"),
                        top.apply("くけ"),
                        bottom.apply("さ")
                    ),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite rest of line, aligned single-width over double-width.
    #[test]
    fn aligned_rest_single_over_double() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `あkikuko`.
                line.put(0, &bottom.apply("あいう"));
                line.put(2, &top.apply("kikuko"));
                assert_eq!(
                    line.str(),
                    &format!("{}{}", bottom.apply("あ"), top.apply("kikuko"),),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite rest of line, misaligned single-width over double-width.
    #[test]
    fn misaligned_rest_single_over_double() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `あ?kikuko`.
                line.put(0, &bottom.apply("あいう"));
                line.put(3, &top.apply("kikuko"));
                assert_eq!(
                    line.str(),
                    &format!("{}?{}", bottom.apply("あ"), top.apply("kikuko"),),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite partial line, aligned single-width over double-width.
    #[test]
    fn aligned_partial_single_over_double() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `kaいうえお`.
                line.put(0, &bottom.apply("あいうえお"));
                line.put(0, &top.apply("ka"));
                assert_eq!(
                    line.str(),
                    &format!("{}{}", top.apply("ka"), bottom.apply("いうえお"),),
                    "bottom={bottom:?} top={top:?}"
                );

                // Produces `kaいkukeお`.
                line.put(4, &top.apply("kuke"));
                assert_eq!(
                    line.str(),
                    &format!(
                        "{}{}{}{}",
                        top.apply("ka"),
                        bottom.apply("い"),
                        top.apply("kuke"),
                        bottom.apply("お")
                    ),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }

    /// Overwrite partial line, misaligned single-width over double-width.
    #[test]
    fn misaligned_partial_single_over_double() {
        for bottom in all::<Emphasis>() {
            for top in all::<Emphasis>() {
                let mut line = TextLine::new();
                // Produces `?aいうえおさ`.
                line.put(0, &bottom.apply("あいうえおさ"));
                line.put(1, &top.apply("a"));
                assert_eq!(
                    line.str(),
                    &format!("?{}{}", top.apply("a"), bottom.apply("いうえおさ"),),
                    "bottom={bottom:?} top={top:?}"
                );

                // Produces `?aい?kuke?さ`.
                line.put(5, &top.apply("kuke"));
                assert_eq!(
                    line.str(),
                    &format!(
                        "?{}{}?{}?{}",
                        top.apply("a"),
                        bottom.apply("い"),
                        top.apply("kuke"),
                        bottom.apply("さ")
                    ),
                    "bottom={bottom:?} top={top:?}"
                );
            }
        }
    }
}
