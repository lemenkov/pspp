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

#[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
pub enum PromptStyle {
    /// First line of command.
    First,

    /// Second or later line of command.
    Later,

    /// Between `BEGIN DATA` and `END DATA`.
    Data,

    /// `COMMENT` or `*` command.
    Comment,

    /// DOCUMENT command.
    Document,

    /// `DO REPEAT` command.
    DoRepeat,

    /// `DEFINE` command.
    Define,
}

impl PromptStyle {
    pub fn to_string(&self) -> &'static str {
        match self {
            PromptStyle::First => "first",
            PromptStyle::Later => "later",
            PromptStyle::Data => "data",
            PromptStyle::Comment => "COMMENT",
            PromptStyle::Document => "DOCUMENT",
            PromptStyle::DoRepeat => "DO REPEAT",
            PromptStyle::Define => "DEFINE",
        }
    }
}
