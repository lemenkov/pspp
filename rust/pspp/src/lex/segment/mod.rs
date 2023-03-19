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

//! Syntax segmentation.
//!
//! PSPP divides traditional "lexical analysis" or "tokenization" into two
//! phases: a lower-level phase called "segmentation" and a higher-level phase
//! called "scanning".  This module implements the segmentation phase.
//! [`super::scan`] contains declarations for the scanning phase.
//!
//! Segmentation accepts a stream of UTF-8 bytes as input.  It outputs a label
//! (a segment type) for each byte or contiguous sequence of bytes in the input.
//! It also, in a few corner cases, outputs zero-width segments that label the
//! boundary between a pair of bytes in the input.
//!
//! Some segment types correspond directly to tokens; for example,
//! [Segment::Identifier] becomes [Token::Id] later in lexical analysis.  Other
//! segments contribute to tokens but do not correspond directly; for example,
//! multiple quoted string [Segment::QuotedString] separated by
//! [Segment::Spaces] and "+" punctuators [Segment::Punct] may be combined to
//! form a single string token [Token::String].  Still other segments are
//! ignored (e.g. [Segment::Spaces]) or trigger special behavior such as error
//! messages later in tokenization (e.g. [Segment::ExpectedQuote]).

use std::cmp::Ordering;

#[cfg(doc)]
use crate::lex::token::Token;

use crate::{
    identifier::{id_match, id_match_n, IdentifierChar},
    prompt::PromptStyle,
};
use bitflags::bitflags;

use super::command_name::{command_match, COMMAND_NAMES};

/// Syntax variant.
///
/// PSPP syntax is written in one of two syntax variant which are broadly
/// defined as follows:
///
/// - In interactive syntax, commands end with a period at the end of the line
///   or with a blank line.
///
/// - In batch syntax, the second and subsequent lines of a command are indented
///   from the left margin.
///
/// The segmenter can also try to automatically detect the kind of syntax in
/// use, using a heuristic that is usually correct.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Default)]
pub enum Syntax {
    /// Try to interpret input correctly regardless of whether it is written
    /// for interactive or batch syntax.
    #[default]
    Auto,

    /// Interactive syntax.
    Interactive,

    /// Batch syntax.
    Batch,
}

/// The type of a segment.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Segment {
    Number,
    QuotedString,
    HexString,
    UnicodeString,
    UnquotedString,
    Identifier,
    Punct,
    Shbang,
    Spaces,
    Comment,
    Newline,
    CommentCommand,
    DoRepeatCommand,
    DoRepeatOverflow,
    InlineData,
    MacroName,
    MacroBody,
    StartDocument,
    Document,
    StartCommand,
    SeparateCommands,
    EndCommand,
    ExpectedQuote,
    ExpectedExponent,
    UnexpectedChar,
}

bitflags! {
    #[derive(Copy, Clone, Debug)]
    pub struct Substate: u8 {
        const START_OF_LINE = 1;
        const START_OF_COMMAND = 2;
    }
}

#[derive(Copy, Clone)]
pub struct Segmenter {
    state: (State, Substate),
    nest: u8,
    syntax: Syntax,
}

#[derive(Copy, Clone, Debug)]
pub struct Incomplete;

impl Segmenter {
    /// Returns a segmenter with the given `syntax`.
    ///
    /// If `is_snippet` is false, then the segmenter will parse as if it's being
    /// given a whole file.  This means, for example, that it will interpret `-`
    /// or `+` at the beginning of the syntax as a separator between commands
    /// (since `-` or `+` at the beginning of a line has this meaning).
    ///
    /// If `is_snippet` is true, then the segmenter will parse as if it's being
    /// given an isolated piece of syntax.  This means that, for example, that
    /// it will interpret `-` or `+` at the beginning of the syntax as an
    /// operator token or (if followed by a digit) as part of a number.
    pub fn new(syntax: Syntax, is_snippet: bool) -> Self {
        Self {
            state: if is_snippet {
                (State::General, Substate::empty())
            } else {
                (State::Shbang, Substate::empty())
            },
            syntax,
            nest: 0,
        }
    }

    pub fn syntax(&self) -> Syntax {
        self.syntax
    }

    fn start_of_line(&self) -> bool {
        self.state.1.contains(Substate::START_OF_LINE)
    }

    fn start_of_command(&self) -> bool {
        self.state.1.contains(Substate::START_OF_COMMAND)
    }

    /// Returns the style of command prompt to display to an interactive user
    /// for input in the current state..  The return value is most accurate in
    /// with [Syntax::Interactive] syntax and at the beginning of a line (that
    /// is, if [`Segmenter::push`] consumed as much as possible of the input up
    /// to a new-line).
    pub fn prompt(&self) -> PromptStyle {
        match self.state.0 {
            State::Shbang => PromptStyle::First,
            State::General => {
                if self.start_of_command() {
                    PromptStyle::First
                } else {
                    PromptStyle::Later
                }
            }
            State::Comment1 | State::Comment2 => PromptStyle::Comment,
            State::Document1 | State::Document2 => PromptStyle::Document,
            State::Document3 => PromptStyle::First,
            State::FileLabel1 => PromptStyle::Later,
            State::FileLabel2 | State::FileLabel3 => PromptStyle::First,
            State::DoRepeat1 | State::DoRepeat2 => {
                if self.start_of_command() {
                    PromptStyle::First
                } else {
                    PromptStyle::Later
                }
            }
            State::DoRepeat3 => PromptStyle::DoRepeat,
            State::DoRepeat4 => PromptStyle::DoRepeat,
            State::Define1 | State::Define2 | State::Define3 => {
                if self.start_of_command() {
                    PromptStyle::First
                } else {
                    PromptStyle::Later
                }
            }
            State::Define4 | State::Define5 | State::Define6 => PromptStyle::Define,
            State::BeginData1 => PromptStyle::First,
            State::BeginData2 => PromptStyle::Later,
            State::BeginData3 | State::BeginData4 => PromptStyle::Data,
        }
    }

    /// Attempts to label a prefix of the remaining input with a segment type.
    /// The caller supplies a prefix of the remaining input as `input`.  If
    /// `eof` is true, then `input` is the entire (remainder) of the input; if
    /// `eof` is false, then further input is potentially available.
    ///
    /// The input may contain '\n' or '\r\n' line ends in any combination.
    ///
    /// If successful, returns `Ok((n, type))`, where `n` is the number of bytes
    /// in the segment at the beginning of `input` (a number in
    /// `0..=input.len()`) and the type of that segment.  The next call should
    /// not include those bytes in `input`, because they have (figuratively)
    /// been consumed by the segmenter.
    ///
    /// Segments can have zero length, including segment types `Type::End`,
    /// `Type::SeparateCommands`, `Type::StartDocument`, `Type::InlineData`, and
    /// `Type::Spaces`.
    ///
    /// Failure occurs only if the segment type of the bytes in `input` cannot
    /// yet be determined.  In this case, this function returns `Err(Incomplete)`.  If
    /// more input is available, the caller should obtain some more, then call
    /// again with a longer `input`.  If this is not enough, the process might
    /// need to repeat again and again.  If input is exhausted, then the caller
    /// may call again setting `eof` to true.  This function will never return
    /// `Err(Incomplete)` when `eof` is true.
    ///
    /// The caller must not, in a sequence of calls, supply contradictory input.
    /// That is, bytes provided as part of `input` in one call, but not
    /// consumed, must not be provided with *different* values on subsequent
    /// calls.  This is because the function must often make decisions based on
    /// looking ahead beyond the bytes that it consumes.
    fn push_rest<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        if input.is_empty() {
            if eof {
                return Ok(None);
            } else {
                return Err(Incomplete);
            };
        }

        match self.state.0 {
            State::Shbang => self.parse_shbang(input, eof),
            State::General => {
                if self.start_of_line() {
                    self.parse_start_of_line(input, eof)
                } else {
                    self.parse_mid_line(input, eof)
                }
            }
            State::Comment1 => self.parse_comment_1(input, eof),
            State::Comment2 => self.parse_comment_2(input, eof),
            State::Document1 => self.parse_document_1(input, eof),
            State::Document2 => self.parse_document_2(input, eof),
            State::Document3 => self.parse_document_3(input, eof),
            State::FileLabel1 => self.parse_file_label_1(input, eof),
            State::FileLabel2 => self.parse_file_label_2(input, eof),
            State::FileLabel3 => self.parse_file_label_3(input, eof),
            State::DoRepeat1 => self.parse_do_repeat_1(input, eof),
            State::DoRepeat2 => self.parse_do_repeat_2(input, eof),
            State::DoRepeat3 => self.parse_do_repeat_3(input, eof),
            State::DoRepeat4 => self.parse_do_repeat_4(input),
            State::Define1 => self.parse_define_1_2(input, eof),
            State::Define2 => self.parse_define_1_2(input, eof),
            State::Define3 => self.parse_define_3(input, eof),
            State::Define4 => self.parse_define_4_5(input, eof),
            State::Define5 => self.parse_define_4_5(input, eof),
            State::Define6 => self.parse_define_6(input, eof),
            State::BeginData1 => self.parse_begin_data_1(input, eof),
            State::BeginData2 => self.parse_begin_data_2(input, eof),
            State::BeginData3 => self.parse_begin_data_3(input, eof),
            State::BeginData4 => self.parse_begin_data_4(input, eof),
        }
    }

    pub fn push(&mut self, input: &str, eof: bool) -> Result<Option<(usize, Segment)>, Incomplete> {
        Ok(self
            .push_rest(input, eof)?
            .map(|(rest, seg_type)| (input.len() - rest.len(), seg_type)))
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum State {
    Shbang,
    General,
    Comment1,
    Comment2,
    Document1,
    Document2,
    Document3,
    FileLabel1,
    FileLabel2,
    FileLabel3,
    DoRepeat1,
    DoRepeat2,
    DoRepeat3,
    DoRepeat4,
    Define1,
    Define2,
    Define3,
    Define4,
    Define5,
    Define6,
    BeginData1,
    BeginData2,
    BeginData3,
    BeginData4,
}

fn take(input: &str, eof: bool) -> Result<(Option<char>, &str), Incomplete> {
    let mut iter = input.chars();
    match iter.next() {
        None if !eof => Err(Incomplete),
        c => Ok((c, iter.as_str())),
    }
}

fn skip_comment(mut input: &str, eof: bool) -> Result<&str, Incomplete> {
    loop {
        let (Some(c), rest) = take(input, eof)? else {
            return Ok(input);
        };
        match c {
            '\n' | '\r' if is_end_of_line(input, eof)? => return Ok(input),
            '*' => {
                if let (Some('/'), rest) = take(rest, eof)? {
                    return Ok(rest);
                }
            }
            _ => (),
        };
        input = rest;
    }
}

fn skip_matching<F>(f: F, input: &str, eof: bool) -> Result<&str, Incomplete>
where
    F: Fn(char) -> bool,
{
    let input = input.trim_start_matches(f);
    if input.is_empty() && !eof {
        Err(Incomplete)
    } else {
        Ok(input)
    }
}

fn match_char<F>(f: F, input: &str, eof: bool) -> Result<Option<&str>, Incomplete>
where
    F: Fn(char) -> bool,
{
    if let (Some(c), rest) = take(input, eof)? {
        if f(c) {
            return Ok(Some(rest));
        }
    }
    Ok(None)
}

fn skip_spaces(mut input: &str, eof: bool) -> Result<&str, Incomplete> {
    loop {
        let (Some(c), rest) = take(input, eof)? else {
            return Ok(input);
        };
        match c {
            '\r' | '\n' if is_end_of_line(input, eof)? => return Ok(input),
            c if c.is_whitespace() => (),
            _ => return Ok(input),
        }
        input = rest;
    }
}

fn skip_digits(input: &str, eof: bool) -> Result<&str, Incomplete> {
    skip_matching(|c| c.is_ascii_digit(), input, eof)
}

fn skip_spaces_and_comments(mut input: &str, eof: bool) -> Result<&str, Incomplete> {
    loop {
        let (Some(c), rest) = take(input, eof)? else {
            return Ok(input);
        };
        match c {
            '/' => {
                let (c, rest2) = take(rest, eof)?;
                match c {
                    Some('*') => input = skip_comment(rest2, eof)?,
                    Some(_) | None => return Ok(rest),
                }
            }
            '\r' | '\n' if is_end_of_line(input, eof)? => return Ok(input),
            c if c.is_whitespace() => input = rest,
            _ => return Ok(input),
        };
    }
}

fn is_start_of_string(input: &str, eof: bool) -> Result<bool, Incomplete> {
    let (Some(c), rest) = take(input, eof)? else {
        return Ok(false);
    };
    match c {
        'x' | 'X' | 'u' | 'U' => {
            let (c, _rest) = take(rest, eof)?;
            Ok(c == Some('\'') || c == Some('"'))
        }
        '\'' | '"' => Ok(true),
        '\n' | '\r' if is_end_of_line(input, eof)? => Ok(true),
        _ => Ok(false),
    }
}

fn is_end_of_line(input: &str, eof: bool) -> Result<bool, Incomplete> {
    let (Some(c), rest) = take(input, eof)? else {
        return Ok(true);
    };
    Ok(match c {
        '\n' => true,
        '\r' => take(rest, eof)?.0 == Some('\n'),
        _ => false,
    })
}

fn at_end_of_line(input: &str, eof: bool) -> Result<bool, Incomplete> {
    is_end_of_line(skip_spaces_and_comments(input, eof)?, eof)
}

fn first(s: &str) -> char {
    s.chars().next().unwrap()
}
fn get_command_name_candidates(target: &str) -> &[&'static str] {
    if target.is_empty() {
        return &[];
    }
    let target_first = first(target).to_ascii_uppercase();
    let low = COMMAND_NAMES.partition_point(|s| first(s) < target_first);
    let high = COMMAND_NAMES.partition_point(|s| first(s) <= target_first);
    &COMMAND_NAMES[low..high]
}

fn detect_command_name(input: &str, eof: bool) -> Result<bool, Incomplete> {
    let command_name = input
        .split(|c: char| {
            !((c.is_whitespace() && c != '\n') || (c.may_continue_id() && c != '.') || c == '-')
        })
        .next()
        .unwrap();
    if !eof && command_name.len() == input.len() {
        return Err(Incomplete);
    }
    let command_name = command_name.trim_end_matches(|c: char| c.is_whitespace() || c == '.');
    for command in get_command_name_candidates(command_name) {
        if let Some(m) = command_match(command, command_name) {
            if m.missing_words <= 0 {
                return Ok(true);
            }
        }
    }
    Ok(false)
}

impl Segmenter {
    fn parse_shbang<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        if let (Some('#'), rest) = take(input, eof)? {
            if let (Some('!'), rest) = take(rest, eof)? {
                let rest = self.parse_full_line(rest, eof)?;
                self.state = (State::General, Substate::START_OF_COMMAND);
                return Ok(Some((rest, Segment::Shbang)));
            }
        }

        self.state = (
            State::General,
            Substate::START_OF_COMMAND | Substate::START_OF_LINE,
        );
        self.push_rest(input, eof)
    }
    fn at_command_start(&self, input: &str, eof: bool) -> Result<bool, Incomplete> {
        match self.syntax {
            Syntax::Auto => detect_command_name(input, eof),
            Syntax::Interactive => Ok(false),
            Syntax::Batch => Ok(true),
        }
    }
    fn parse_start_of_line<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        debug_assert_eq!(self.state.0, State::General);
        debug_assert!(self.start_of_line());
        debug_assert!(!input.is_empty());

        let (Some(c), rest) = take(input, eof).unwrap() else {
            unreachable!()
        };
        match c {
            '+' if is_start_of_string(skip_spaces_and_comments(rest, eof)?, eof)? => {
                // This  `+` is punctuation that may separate pieces of a string.
                self.state = (State::General, Substate::empty());
                return Ok(Some((rest, Segment::Punct)));
            }
            '+' | '-' | '.' => {
                self.state = (State::General, Substate::START_OF_COMMAND);
                return Ok(Some((rest, Segment::StartCommand)));
            }
            _ if c.is_whitespace() => {
                if at_end_of_line(input, eof)? {
                    self.state = (State::General, Substate::START_OF_COMMAND);
                    return Ok(Some((input, Segment::SeparateCommands)));
                }
            }
            _ => {
                if self.at_command_start(input, eof)?
                    && !self.state.1.contains(Substate::START_OF_COMMAND)
                {
                    self.state = (State::General, Substate::START_OF_COMMAND);
                    return Ok(Some((input, Segment::StartCommand)));
                }
            }
        }
        self.state.1 = Substate::START_OF_COMMAND;
        self.parse_mid_line(input, eof)
    }
    fn parse_mid_line<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        debug_assert!(self.state.0 == State::General);
        debug_assert!(!self.state.1.contains(Substate::START_OF_LINE));
        let (Some(c), rest) = take(input, eof)? else {
            unreachable!()
        };
        match c {
            '\r' | '\n' if is_end_of_line(input, eof)? => {
                self.state.1 |= Substate::START_OF_LINE;
                Ok(Some((
                    self.parse_newline(input, eof).unwrap().unwrap(),
                    Segment::Newline,
                )))
            }
            '/' => {
                if let (Some('*'), rest) = take(rest, eof)? {
                    let rest = skip_comment(rest, eof)?;
                    Ok(Some((rest, Segment::Comment)))
                } else {
                    self.state.1 = Substate::empty();
                    Ok(Some((rest, Segment::Punct)))
                }
            }
            '-' => {
                let (c, rest2) = take(skip_spaces(rest, eof)?, eof)?;
                match c {
                    Some(c) if c.is_ascii_digit() => {
                        return self.parse_number(rest, eof);
                    }
                    Some('.') => {
                        if let (Some(c), _rest) = take(rest2, eof)? {
                            if c.is_ascii_digit() {
                                return self.parse_number(rest, eof);
                            }
                        }
                    }
                    None | Some(_) => (),
                }
                self.state.1 = Substate::empty();
                Ok(Some((rest, Segment::Punct)))
            }
            '(' | ')' | '[' | ']' | '{' | '}' | ',' | '=' | ';' | ':' | '&' | '|' | '+' => {
                self.state.1 = Substate::empty();
                Ok(Some((rest, Segment::Punct)))
            }
            '*' => {
                if self.state.1.contains(Substate::START_OF_COMMAND) {
                    self.state = (State::Comment1, Substate::empty());
                    self.parse_comment_1(input, eof)
                } else {
                    self.parse_digraph(&['*'], rest, eof)
                }
            }
            '<' => self.parse_digraph(&['=', '>'], rest, eof),
            '>' => self.parse_digraph(&['='], rest, eof),
            '~' => self.parse_digraph(&['='], rest, eof),
            '.' if at_end_of_line(rest, eof)? => {
                self.state.1 = Substate::START_OF_COMMAND;
                Ok(Some((rest, Segment::EndCommand)))
            }
            '.' => match take(rest, eof)? {
                (Some(c), _) if c.is_ascii_digit() => self.parse_number(input, eof),
                _ => Ok(Some((rest, Segment::Punct))),
            },
            '0'..='9' => self.parse_number(input, eof),
            'u' | 'U' => self.maybe_parse_string(Segment::UnicodeString, (input, rest), eof),
            'x' | 'X' => self.maybe_parse_string(Segment::HexString, (input, rest), eof),
            '\'' | '"' => self.parse_string(Segment::QuotedString, c, rest, eof),
            '!' => {
                let (c, rest2) = take(rest, eof)?;
                match c {
                    Some('*') => Ok(Some((rest2, Segment::Punct))),
                    Some(_) => self.parse_id(input, eof),
                    None => Ok(Some((rest, Segment::Punct))),
                }
            }
            c if c.is_whitespace() => Ok(Some((skip_spaces(rest, eof)?, Segment::Spaces))),
            c if c.may_start_id() => self.parse_id(input, eof),
            '#'..='~' if c != '\\' && c != '^' => {
                self.state.1 = Substate::empty();
                Ok(Some((rest, Segment::Punct)))
            }
            _ => {
                self.state.1 = Substate::empty();
                Ok(Some((rest, Segment::UnexpectedChar)))
            }
        }
    }
    fn parse_string<'a>(
        &mut self,
        segment: Segment,
        quote: char,
        mut input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        while let (Some(c), rest) = take(input, eof)? {
            match c {
                _ if c == quote => {
                    let (c, rest2) = take(rest, eof)?;
                    if c != Some(quote) {
                        self.state.1 = Substate::empty();
                        return Ok(Some((rest, segment)));
                    }
                    input = rest2;
                }
                '\r' | '\n' if is_end_of_line(input, eof)? => break,
                _ => input = rest,
            }
        }
        self.state.1 = Substate::empty();
        Ok(Some((input, Segment::ExpectedQuote)))
    }
    fn maybe_parse_string<'a>(
        &mut self,
        segment: Segment,
        input: (&'a str, &'a str),
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        match take(input.1, eof)? {
            (Some(c), rest) if c == '\'' || c == '"' => self.parse_string(segment, c, rest, eof),
            _ => self.parse_id(input.0, eof),
        }
    }
    fn next_id_in_command<'a>(
        &self,
        mut input: &'a str,
        eof: bool,
    ) -> Result<(&'a str, &'a str), Incomplete> {
        let mut sub = Segmenter::new(self.syntax, true);
        loop {
            let Some((seg_len, seg_type)) = sub.push(input, eof)? else {
                return Ok((input, input));
            };
            let (segment, rest) = input.split_at(seg_len);
            match seg_type {
                Segment::Shbang | Segment::Spaces | Segment::Comment | Segment::Newline => (),

                Segment::Identifier => return Ok((segment, rest)),

                Segment::Number
                | Segment::QuotedString
                | Segment::HexString
                | Segment::UnicodeString
                | Segment::UnquotedString
                | Segment::Punct
                | Segment::CommentCommand
                | Segment::DoRepeatCommand
                | Segment::DoRepeatOverflow
                | Segment::InlineData
                | Segment::MacroName
                | Segment::MacroBody
                | Segment::StartDocument
                | Segment::Document
                | Segment::StartCommand
                | Segment::SeparateCommands
                | Segment::EndCommand
                | Segment::ExpectedQuote
                | Segment::ExpectedExponent
                | Segment::UnexpectedChar => return Ok(("", rest)),
            }
            input = rest;
        }
    }
    fn parse_id<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (Some(_), mut end) = take(input, eof).unwrap() else {
            unreachable!()
        };
        while let (Some(c), rest) = take(end, eof)? {
            if !c.may_continue_id() {
                break;
            };
            end = rest;
        }
        let identifier = &input[..input.len() - end.len()];
        let identifier = match identifier.strip_suffix('.') {
            Some(without_dot) if at_end_of_line(end, eof)? => without_dot,
            _ => identifier,
        };
        let rest = &input[identifier.len()..];

        if self.state.1.contains(Substate::START_OF_COMMAND) {
            if id_match_n("COMMENT", identifier, 4) {
                self.state = (State::Comment1, Substate::empty());
                return self.parse_comment_1(input, eof);
            } else if id_match("DOCUMENT", identifier) {
                self.state = (State::Document1, Substate::empty());
                return Ok(Some((input, Segment::StartDocument)));
            } else if id_match_n("DEFINE", identifier, 6) {
                self.state = (State::Define1, Substate::empty());
            } else if id_match("FILE", identifier) {
                if id_match("LABEL", self.next_id_in_command(rest, eof)?.0) {
                    self.state = (State::FileLabel1, Substate::empty());
                    return Ok(Some((rest, Segment::Identifier)));
                }
            } else if id_match("DO", identifier) {
                if id_match("REPEAT", self.next_id_in_command(rest, eof)?.0) {
                    self.state = (State::DoRepeat1, Substate::empty());
                    return Ok(Some((rest, Segment::Identifier)));
                }
            } else if id_match("BEGIN", identifier) {
                let (next_id, rest2) = self.next_id_in_command(rest, eof)?;
                if id_match("DATA", next_id) {
                    let rest2 = skip_spaces_and_comments(rest2, eof)?;
                    let rest2 = if let Some(s) = rest2.strip_prefix('.') {
                        skip_spaces_and_comments(s, eof)?
                    } else {
                        rest2
                    };
                    if is_end_of_line(rest2, eof)? {
                        let s = &input[..input.len() - rest2.len()];
                        self.state = (
                            if s.contains('\n') {
                                State::BeginData1
                            } else {
                                State::BeginData2
                            },
                            Substate::empty(),
                        );
                        return Ok(Some((rest, Segment::Identifier)));
                    }
                }
            }
        }

        self.state.1 = Substate::empty();
        Ok(Some((
            rest,
            if identifier != "!" {
                Segment::Identifier
            } else {
                Segment::Punct
            },
        )))
    }
    fn parse_digraph<'a>(
        &mut self,
        seconds: &[char],
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (c, rest) = take(input, eof)?;
        self.state.1 = Substate::empty();
        Ok(Some((
            match c {
                Some(c) if seconds.contains(&c) => rest,
                _ => input,
            },
            Segment::Punct,
        )))
    }
    fn parse_number<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let mut input = skip_digits(input, eof)?;
        if let Some(rest) = match_char(|c| c == '.', input, eof)? {
            let rest2 = skip_digits(rest, eof)?;
            if rest2.len() < rest.len() || !at_end_of_line(rest2, eof)? {
                input = rest2;
            }
        };
        if let Some(rest) = match_char(|c| c == 'e' || c == 'E', input, eof)? {
            let rest = match_char(|c| c == '+' || c == '-', rest, eof)?.unwrap_or(rest);
            let rest2 = skip_digits(rest, eof)?;
            if rest2.len() == rest.len() {
                self.state.1 = Substate::empty();
                return Ok(Some((rest, Segment::ExpectedExponent)));
            }
            input = rest2;
        }
        self.state.1 = Substate::empty();
        Ok(Some((input, Segment::Number)))
    }
    fn parse_comment_1<'a>(
        &mut self,
        mut input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        enum CommentState<'a> {
            Blank,
            NotBlank,
            Period(&'a str),
        }
        let mut state = CommentState::Blank;
        loop {
            let (Some(c), rest) = take(input, eof)? else {
                // End of file.
                self.state = (State::General, Substate::START_OF_COMMAND);
                return Ok(Some((input, Segment::SeparateCommands)));
            };
            match c {
                '.' => state = CommentState::Period(input),
                '\n' | '\r' if is_end_of_line(input, eof)? => {
                    match state {
                        CommentState::Blank => {
                            // Blank line ends comment command.
                            self.state = (State::General, Substate::START_OF_COMMAND);
                            return Ok(Some((input, Segment::SeparateCommands)));
                        }
                        CommentState::Period(period) => {
                            // '.' at end of line ends comment command.
                            self.state = (State::General, Substate::empty());
                            return Ok(Some((period, Segment::CommentCommand)));
                        }
                        CommentState::NotBlank => {
                            // Comment continues onto next line.
                            self.state = (State::Comment2, Substate::empty());
                            return Ok(Some((input, Segment::CommentCommand)));
                        }
                    }
                }
                c if c.is_whitespace() => (),
                _ => state = CommentState::NotBlank,
            }
            input = rest;
        }
    }
    fn parse_comment_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_newline(input, eof)?.unwrap();

        let new_command = match take(rest, eof)?.0 {
            Some('+') | Some('-') | Some('.') => true,
            Some(c) if !c.is_whitespace() => self.at_command_start(rest, eof)?,
            None | Some(_) => false,
        };
        if new_command {
            self.state = (
                State::General,
                Substate::START_OF_LINE | Substate::START_OF_COMMAND,
            );
        } else {
            self.state = (State::Comment1, Substate::empty());
        }
        Ok(Some((rest, Segment::Newline)))
    }
    fn parse_document_1<'a>(
        &mut self,
        mut input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let mut end_cmd = false;
        loop {
            let (Some(c), rest) = take(input, eof)? else {
                self.state = (State::Document3, Substate::empty());
                return Ok(Some((input, Segment::Document)));
            };
            match c {
                '.' => end_cmd = true,
                '\n' | '\r' if is_end_of_line(input, eof)? => {
                    self.state.0 = if end_cmd {
                        State::Document3
                    } else {
                        State::Document2
                    };
                    return Ok(Some((input, Segment::Document)));
                }
                c if !c.is_whitespace() => end_cmd = false,
                _ => (),
            }
            input = rest;
        }
    }
    fn parse_document_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_newline(input, eof)?.unwrap();
        self.state = (State::Document1, Substate::empty());
        Ok(Some((rest, Segment::Newline)))
    }
    fn parse_document_3<'a>(
        &mut self,
        input: &'a str,
        _eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        self.state = (
            State::General,
            Substate::START_OF_COMMAND | Substate::START_OF_LINE,
        );
        Ok(Some((input, Segment::EndCommand)))
    }
    fn quoted_file_label(input: &str, eof: bool) -> Result<bool, Incomplete> {
        let input = skip_spaces_and_comments(input, eof)?;
        match take(input, eof)?.0 {
            Some('\'') | Some('"') | Some('\n') => Ok(true),
            _ => Ok(false),
        }
    }
    fn parse_file_label_1<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let mut sub = Segmenter {
            state: (State::General, self.state.1),
            ..*self
        };
        let (rest, segment) = sub.push_rest(input, eof)?.unwrap();
        if segment == Segment::Identifier {
            let id = &input[..input.len() - rest.len()];
            debug_assert!(id_match("LABEL", id), "{id} should be LABEL");
            if Self::quoted_file_label(rest, eof)? {
                *self = sub;
            } else {
                self.state.0 = State::FileLabel2;
            }
        } else {
            self.state.1 = sub.state.1;
        }
        Ok(Some((rest, segment)))
    }
    fn parse_file_label_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let input = skip_spaces(input, eof)?;
        self.state = (State::FileLabel3, Substate::empty());
        Ok(Some((input, Segment::Spaces)))
    }
    fn parse_file_label_3<'a>(
        &mut self,
        mut input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let mut end_cmd = None;
        loop {
            let (c, rest) = take(input, eof)?;
            match c {
                None | Some('\n') | Some('\r') if is_end_of_line(input, eof)? => {
                    self.state = (State::General, Substate::empty());
                    return Ok(Some((end_cmd.unwrap_or(input), Segment::UnquotedString)));
                }
                None => unreachable!(),
                Some('.') => end_cmd = Some(input),
                Some(c) if !c.is_whitespace() => end_cmd = None,
                Some(_) => (),
            }
            input = rest;
        }
    }
    fn subparse<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let mut sub = Segmenter {
            syntax: self.syntax,
            state: (State::General, self.state.1),
            nest: 0,
        };
        let result = sub.push_rest(input, eof)?;
        self.state.1 = sub.state.1;
        Ok(result)
    }
    /// We are segmenting a `DO REPEAT` command, currently reading the syntax
    /// that defines the stand-in variables (the head) before the lines of
    /// syntax to be repeated (the body).
    fn parse_do_repeat_1<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        if segment == Segment::SeparateCommands {
            // We reached a blank line that separates the head from the body.
            self.state.0 = State::DoRepeat2;
        } else if segment == Segment::EndCommand || segment == Segment::StartCommand {
            // We reached the body.
            self.state.0 = State::DoRepeat3;
            self.nest = 1;
        }
        Ok(Some((rest, segment)))
    }
    /// We are segmenting a `DO REPEAT` command, currently reading a blank line
    /// that separates the head from the body.
    fn parse_do_repeat_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        if segment == Segment::Newline {
            // We reached the body.
            self.state.0 = State::DoRepeat3;
            self.nest = 1;
        }
        Ok(Some((rest, segment)))
    }
    fn parse_newline<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<&'a str>, Incomplete> {
        let (Some(c), rest) = take(input, eof)? else {
            return Ok(None);
        };
        match c {
            '\n' => Ok(Some(rest)),
            '\r' => {
                if let (Some('\n'), rest) = take(rest, eof)? {
                    Ok(Some(rest))
                } else {
                    Ok(None)
                }
            }
            _ => Ok(None),
        }
    }

    fn parse_full_line<'a>(
        &mut self,
        mut input: &'a str,
        eof: bool,
    ) -> Result<&'a str, Incomplete> {
        loop {
            if is_end_of_line(input, eof)? {
                return Ok(input);
            }
            input = take(input, eof).unwrap().1;
        }
    }
    fn check_repeat_command(&mut self, input: &str, eof: bool) -> Result<isize, Incomplete> {
        let input = input.strip_prefix(['-', '+']).unwrap_or(input);
        let (id1, input) = self.next_id_in_command(input, eof)?;
        if id_match("DO", id1) && id_match("REPEAT", self.next_id_in_command(input, eof)?.0) {
            Ok(1)
        } else if id_match("END", id1) && id_match("REPEAT", self.next_id_in_command(input, eof)?.0)
        {
            Ok(-1)
        } else {
            Ok(0)
        }
    }
    /// We are in the body of `DO REPEAT`, segmenting the lines of syntax that
    /// are to be repeated.  Report each line of syntax as a single
    /// [`Type::DoRepeatCommand`].
    ///
    /// `DO REPEAT` can be nested, so we look for `DO REPEAT...END REPEAT`
    /// blocks inside the lines we're segmenting.  `self.nest` counts the
    /// nesting level, starting at 1.
    fn parse_do_repeat_3<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        if let Some(rest) = self.parse_newline(input, eof)? {
            return Ok(Some((rest, Segment::Newline)));
        }
        let rest = self.parse_full_line(input, eof)?;
        match self.check_repeat_command(input, eof)?.cmp(&0) {
            Ordering::Greater => {
                if let Some(nest) = self.nest.checked_add(1) {
                    self.nest = nest;
                } else {
                    self.state.0 = State::DoRepeat4;
                }
            }
            Ordering::Less => {
                self.nest -= 1;
                if self.nest == 0 {
                    // Nesting level dropped to 0, so we've finished reading the `DO
                    // REPEAT` body.
                    self.state = (
                        State::General,
                        Substate::START_OF_COMMAND | Substate::START_OF_LINE,
                    );
                    return self.push_rest(input, eof);
                }
            }
            Ordering::Equal => (),
        }
        Ok(Some((rest, Segment::DoRepeatCommand)))
    }
    fn parse_do_repeat_4<'a>(
        &mut self,
        input: &'a str,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        self.state.0 = State::DoRepeat3;
        Ok(Some((input, Segment::DoRepeatOverflow)))
    }
    /// We are segmenting a `DEFINE` command, which consists of:
    ///
    ///   - The `DEFINE` keyword.
    ///
    ///   - An identifier.  We transform this into `Type::MacroName` instead of
    ///     `Type::Identifier` because this identifier must never  be macro-expanded.
    ///
    ///   - Anything but `(`.
    ///
    ///   - `(` followed by a sequence of tokens possibly including balanced
    ///     parentheses up to a final `)`.
    ///
    ///   - A sequence of any number of lines, one string per line, ending with
    ///     `!ENDDEFINE`.  The first line is usually blank (that is, a newline
    ///     follows the `(`).  The last line usually just has `!ENDDEFINE.` on
    ///     it, but it can start with other tokens.  The whole
    ///     DEFINE...!ENDDEFINE can be on a single line, even.
    fn parse_define_1_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        match segment {
            Segment::Identifier if self.state.0 == State::Define1 => {
                self.state.0 = State::Define2;
                return Ok(Some((rest, Segment::MacroName)));
            }
            Segment::SeparateCommands | Segment::EndCommand | Segment::StartCommand => {
                // The DEFINE command is malformed because we reached its end
                // without ever hitting a `(` token.  Transition back to general
                // parsing.
                self.state.0 = State::General;
            }
            Segment::Punct if input.starts_with('(') => {
                self.state.0 = State::Define3;
                self.nest = 1;
            }
            _ => (),
        }
        Ok(Some((rest, segment)))
    }
    fn parse_define_3<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        match segment {
            Segment::SeparateCommands | Segment::EndCommand | Segment::StartCommand => {
                // The DEFINE command is malformed because we reached its end
                // without ever hitting a `(` token.  Transition back to general
                // parsing.
                self.state.0 = State::General;
            }
            Segment::Punct if input.starts_with('(') => {
                self.nest += 1;
            }
            Segment::Punct if input.starts_with(')') => {
                self.nest -= 1;
                if self.nest == 0 {
                    self.state = (State::Define4, Substate::empty());
                }
            }
            _ => (),
        }
        Ok(Some((rest, segment)))
    }
    fn find_enddefine(mut input: &str) -> Option<&str> {
        loop {
            input = skip_spaces_and_comments(input, true).unwrap();
            let (Some(c), rest) = take(input, true).unwrap() else {
                return None;
            };
            match c {
                '!' if strip_prefix_ignore_ascii_case(input, "!ENDDEFINE").is_some() => {
                    return Some(input)
                }
                '\'' | '"' => {
                    let index = rest.find(c)?;
                    input = &rest[index + 1..];
                }
                _ => input = rest,
            }
        }
    }

    /// We are in the body of a macro definition, looking for additional lines
    /// of the body or `!ENDDEFINE`.
    ///
    /// In `State::Define4`, we're parsing the first line of the macro body (the
    /// same line as the closing parenthesis in the argument definition).  In
    /// `State::Define5`, we're on a later line.
    fn parse_define_4_5<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_full_line(input, eof)?;
        let line = &input[..input.len() - rest.len()];
        if let Some(end) = Self::find_enddefine(line) {
            // Macro ends at the !ENDDEFINE on this line.
            self.state = (State::General, Substate::empty());
            let (prefix, rest) = input.split_at(line.len() - end.len());
            if prefix.is_empty() {
                // Line starts with `!ENDDEFINE`.
                self.push_rest(input, eof)
            } else if prefix.trim_start().is_empty() {
                // Line starts with spaces followed by `!ENDDEFINE`.
                Ok(Some((rest, Segment::Spaces)))
            } else {
                // Line starts with some content followed by `!ENDDEFINE`.
                Ok(Some((rest, Segment::MacroBody)))
            }
        } else {
            // No `!ENDDEFINE`.  We have a full line of macro body.
            //
            // If the first line of the macro body is blank, we just report it
            // as spaces, or not at all if there are no spaces, because it's not
            // significant.
            //
            // However, if it's a later line, we need to report it because blank
            // lines can have significance.
            let segment = if self.state.0 == State::Define4 && line.trim_start().is_empty() {
                if line.is_empty() {
                    return self.parse_define_6(input, eof);
                }
                Segment::Spaces
            } else {
                Segment::MacroBody
            };
            self.state.0 = State::Define6;
            Ok(Some((rest, segment)))
        }
    }
    fn parse_define_6<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_newline(input, eof)?.unwrap();
        self.state.0 = State::Define5;
        Ok(Some((rest, Segment::Newline)))
    }
    fn parse_begin_data_1<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        if segment == Segment::Newline {
            self.state.0 = State::BeginData2;
        }
        Ok(Some((rest, segment)))
    }
    fn parse_begin_data_2<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let (rest, segment) = self.subparse(input, eof)?.unwrap();
        if segment == Segment::Newline {
            self.state.0 = State::BeginData3;
        }
        Ok(Some((rest, segment)))
    }
    fn is_end_data(line: &str) -> bool {
        let Some(rest) = strip_prefix_ignore_ascii_case(line, "END") else {
            return false;
        };
        let (Some(c), rest) = take(rest, true).unwrap() else {
            return false;
        };
        if !c.is_whitespace() {
            return false;
        };
        let Some(rest) = strip_prefix_ignore_ascii_case(rest, "DATA") else {
            return false;
        };

        let mut endcmd = false;
        for c in rest.chars() {
            match c {
                '.' if endcmd => return false,
                '.' => endcmd = true,
                c if c.is_whitespace() => (),
                _ => return false,
            }
        }
        true
    }
    fn parse_begin_data_3<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_full_line(input, eof)?;
        let line = &input[..input.len() - rest.len()];
        if Self::is_end_data(line) {
            self.state = (
                State::General,
                Substate::START_OF_COMMAND | Substate::START_OF_LINE,
            );
            self.push_rest(input, eof)
        } else {
            self.state.0 = State::BeginData4;
            Ok(Some((rest, Segment::InlineData)))
        }
    }
    fn parse_begin_data_4<'a>(
        &mut self,
        input: &'a str,
        eof: bool,
    ) -> Result<Option<(&'a str, Segment)>, Incomplete> {
        let rest = self.parse_newline(input, eof)?.unwrap();
        self.state.0 = State::BeginData3;
        Ok(Some((rest, Segment::Newline)))
    }
}

fn strip_prefix_ignore_ascii_case<'a>(line: &'a str, pattern: &str) -> Option<&'a str> {
    line.get(..pattern.len()).and_then(|prefix| {
        prefix
            .eq_ignore_ascii_case(pattern)
            .then(|| &line[pattern.len()..])
    })
}

#[cfg(test)]
mod test;
