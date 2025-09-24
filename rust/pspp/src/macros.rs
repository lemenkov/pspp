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

#![allow(dead_code)]
use num::Integer;
use std::{
    cell::RefCell,
    cmp::Ordering,
    collections::{BTreeMap, HashMap, HashSet},
    mem::take,
    num::NonZeroUsize,
    ops::RangeInclusive,
    sync::LazyLock,
};
use thiserror::Error as ThisError;
use unicase::UniCase;

use crate::{
    identifier::Identifier,
    lex::{
        Punct, Token,
        scan::{ScanError, StringScanner, StringSegmenter},
        segment::Syntax,
    },
    message::Location,
    settings::Settings,
};

#[derive(Clone, Debug, ThisError)]
pub enum MacroError {
    /// Expected more tokens.
    #[error(
        "Reached end of command expecting {n} more tokens in argument {arg} to macro {macro_}."
    )]
    ExpectedMoreTokens {
        n: usize,
        arg: Identifier,
        macro_: Identifier,
    },

    /// Expected a particular token at end of command.
    #[error("Reached end of command expecting {token:?} in argument {arg} to macro {macro_}.")]
    ExpectedToken {
        token: String,
        arg: Identifier,
        macro_: Identifier,
    },

    /// Expected a particular token, got a different one.
    #[error(
        "Found `{actual}` while expecting `{expected}` reading argument {arg} to macro {macro_}."
    )]
    UnexpectedToken {
        actual: String,
        expected: String,
        arg: Identifier,
        macro_: Identifier,
    },

    /// Argument specified multiple times,
    #[error("Argument {arg} specified multiple times in call to macro {macro_}.")]
    DuplicateArg { arg: Identifier, macro_: Identifier },

    /// Maximum nesting limit exceeded.
    #[error("Maximum nesting level {limit} exceeded. (Use `SET MNEST` to change the limit.)")]
    TooDeep { limit: usize },

    /// Invalid `!*`.
    #[error("`!*` may only be used within the expansion of a macro.")]
    InvalidBangAsterisk,

    /// Error tokenizing during expansion.
    #[error(transparent)]
    ScanError(ScanError),

    /// Expecting `)` in macro expression.
    #[error("Expecting `)` in macro expression.")]
    ExpectingRParen,

    /// Expecting literal.
    #[error("Expecting literal or function invocation in macro expression.")]
    ExpectingLiteral,

    /// Expecting `!THEN`.
    #[error("`!THEN` expected in macro `!IF` construct.")]
    ExpectingThen,

    /// Expecting `!ELSE` or `!THEN`.
    #[error("`!ELSE` or `!THEN` expected in macro `!IF` construct.")]
    ExpectingElseOrIfEnd,

    /// Expecting `!IFEND`.
    #[error("`!IFEND` expected in macro `!IF` construct.")]
    ExpectingIfEnd,

    /// Expecting macro variable name.
    #[error("Expecting macro variable name following `{0}`.")]
    ExpectingMacroVarName(&'static str),

    /// Invalid macro variable name.
    #[error("Cannot use argument name or macro keyword {name} as `{construct}` variable name.")]
    BadMacroVarName {
        name: Identifier,
        construct: &'static str,
    },

    /// Expecting `=` following `!LET`.
    #[error("Expecting `=` following `!LET`.")]
    ExpectingEquals,

    /// Expecting `=` or `!IN` in `!DO` loop.
    #[error("Expecting `=` or `!IN` in `!DO` loop.")]
    ExpectingEqualsOrIn,

    /// Missing `!DOEND`.
    #[error("Missing `!DOEND`.")]
    MissingDoEnd,

    /// Bad numberic macro expression.
    #[error("Macro expression must evaluate to a number (not {0:?})")]
    BadNumericMacroExpression(String),

    /// Too many iteration for list-based loop.
    #[error(
        "`!DO` loop over list exceeded maximum number of iterations {0}.  (Use `SET MITERATE` to change the limit.)"
    )]
    MiterateList(usize),

    /// Too many iteration for numerical loop.
    #[error(
        "Numerical `!DO` loop  exceeded maximum number of iterations {0}.  (Use `SET MITERATE` to change the limit.)"
    )]
    MiterateNumeric(usize),

    /// Expecting `!TO`  in numerical `!DO` loop.
    #[error("Expecting `!TO`  in numerical `!DO` loop.")]
    ExpectingTo,

    /// `!BY` value cannot be zero.
    #[error("`!BY` value cannot be zero.")]
    ZeroBy,

    /// `!BREAK` outside `!DO`.
    #[error("`!BREAK` outside `!DO`.")]
    BreakOutsideDo,

    /// `,` or `)` expected in call to macro function.
    #[error("`,` or `)` expected in call to macro function `{0}`.")]
    ExpectingCommaOrRParen(Identifier),

    /// Macro function takes one argument.
    #[error("Macro function `{name}` takes one argument (not {n_args}).")]
    ExpectingOneArg { name: Identifier, n_args: usize },

    /// Macro function takes two arguments.
    #[error("Macro function `{name}` takes two arguments (not {n_args}).")]
    ExpectingTwoArgs { name: Identifier, n_args: usize },

    /// Macro function takes two or three arguments.
    #[error("Macro function `{name}` takes two or three arguments (not {n_args}).")]
    ExpectingTwoOrThreeArgs { name: Identifier, n_args: usize },

    /// Macro function needs at least one argument).
    #[error("Macro function `{name}` needs at least one argument).")]
    ExpectingOneOrMoreArgs { name: Identifier },

    /// Argument to `!BLANKS` must be non-negative integer (not `{0}`).
    #[error("Argument to `!BLANKS` must be non-negative integer (not `{0}`).")]
    InvalidBlanks(String),

    /// Second argument of `!SUBSTR` must be positive integer (not `{0}`).
    #[error("Second argument of `!SUBSTR` must be positive integer (not `{0}`).")]
    InvalidSubstr2(String),

    /// Third argument of `!SUBSTR` must be non-negative integer (not `{0}`).
    #[error("Third argument of `!SUBSTR` must be non-negative integer (not `{0}`).")]
    InvalidSubstr3(String),
}

/// A PSPP macro as defined with `!DEFINE`.
pub struct Macro {
    /// The macro's name. This is an ordinary identifier except that it is
    /// allowed (but not required) to begin with `!`.
    pub name: Identifier,

    /// Source code location of macro definition, for error reporting.
    pub location: Location,

    /// Parameters.
    parameters: Vec<Parameter>,

    /// Body.
    body: Vec<MacroToken>,
}

impl Macro {
    fn initial_state(&self) -> ParserState {
        if self.parameters.is_empty() {
            ParserState::Finished
        } else if self.parameters[0].is_positional() {
            ParserState::Keyword
        } else if let ValueType::Enclose(_, _) = self.parameters[0].arg {
            ParserState::Enclose
        } else {
            ParserState::Arg
        }
    }

    fn find_parameter(&self, name: &Identifier) -> Option<usize> {
        self.parameters.iter().position(|param| &param.name == name)
    }
}

struct Parameter {
    /// `!name` or `!1`.
    name: Identifier,

    /// Default value.
    ///
    /// The tokens don't include white space, etc. between them.
    default: Vec<MacroToken>,

    /// Macro-expand the value?
    expand_value: bool,

    /// How the argument is specified.
    arg: ValueType,
}

impl Parameter {
    /// Returns true if this is a positional parameter. Positional parameters
    /// are expanded by index (position) rather than by name.
    fn is_positional(&self) -> bool {
        self.name.0.as_bytes()[1].is_ascii_digit()
    }
}

enum ValueType {
    /// Argument consists of `.0` tokens.
    NTokens(usize),

    /// Argument runs until token `.0`.
    CharEnd(Token),

    /// Argument starts with token `.0` and ends with token `.1`.
    Enclose(Token, Token),

    /// Argument runs until the end of the command.
    CmdEnd,
}

/// A token and the syntax that was tokenized to produce it.  The syntax allows
/// the token to be turned back into syntax accurately.
#[derive(Clone)]
pub struct MacroToken {
    /// The token.
    pub token: Token,

    /// The syntax that produces `token`.
    pub syntax: String,
}

fn tokenize_string_into(
    s: &str,
    mode: Syntax,
    error: &(impl Fn(MacroError) + ?Sized),
    output: &mut Vec<MacroToken>,
) {
    for (syntax, token) in StringSegmenter::new(s, mode, true) {
        match token {
            Ok(token) => output.push(MacroToken {
                token,
                syntax: String::from(syntax),
            }),
            Err(scan_error) => error(MacroError::ScanError(scan_error)),
        }
    }
}

fn tokenize_string(
    s: &str,
    mode: Syntax,
    error: &(impl Fn(MacroError) + ?Sized),
) -> Vec<MacroToken> {
    let mut tokens = Vec::new();
    tokenize_string_into(s, mode, error, &mut tokens);
    tokens
}

fn try_unquote_string(input: &str, mode: Syntax) -> Option<String> {
    let mut scanner = StringScanner::new(input, mode, true);
    let Some(Ok(Token::String(unquoted))) = scanner.next() else {
        return None;
    };
    let None = scanner.next() else { return None };
    Some(unquoted)
}

fn unquote_string(input: String, mode: Syntax) -> String {
    try_unquote_string(&input, mode).unwrap_or(input)
}

#[derive(Clone)]
struct MacroTokens<'a>(&'a [MacroToken]);

impl MacroTokens<'_> {
    fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
    fn match_(&mut self, s: &str) -> bool {
        if let Some((first, rest)) = self.0.split_first() {
            if first.syntax.eq_ignore_ascii_case(s) {
                self.0 = rest;
                return true;
            }
        }
        false
    }
    fn take_relop(&mut self) -> Option<RelOp> {
        if let Some((first, rest)) = self.0.split_first() {
            if let Ok(relop) = first.syntax.as_str().try_into() {
                self.0 = rest;
                return Some(relop);
            }
        }
        None
    }
    fn macro_id(&self) -> Option<&Identifier> {
        self.0.first().and_then(|mt| mt.token.macro_id())
    }
    fn take_macro_id(&mut self) -> Option<&Identifier> {
        let result = self.0.first().and_then(|mt| mt.token.macro_id());
        if result.is_some() {
            self.advance();
        }
        result
    }
    fn take(&mut self) -> Option<&MacroToken> {
        match self.0.split_first() {
            Some((first, rest)) => {
                self.0 = rest;
                Some(first)
            }
            None => None,
        }
    }
    fn advance(&mut self) -> &MacroToken {
        let (first, rest) = self.0.split_first().unwrap();
        self.0 = rest;
        first
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum TokenClass {
    /// No space before or after (new-line after).
    EndCommand,

    /// Space on both sides.
    BinaryOperator,

    /// Space afterward.
    Comma,

    /// Don't need spaces except sequentially.
    Id,

    /// Don't need spaces except sequentially.
    Punct,
}

impl TokenClass {
    fn separator(prev: Self, next: Self) -> &'static str {
        match (prev, next) {
            // Don't need a separator before the end of a command, but we
            // need a new-line afterward.
            (_, Self::EndCommand) => "",
            (Self::EndCommand, _) => "\n",

            // Binary operators always have a space on both sides, and a comma always has a space afterward.
            (Self::BinaryOperator, _) | (_, Self::BinaryOperator) | (Self::Comma, _) => " ",

            // Otherwise, `prev` is `Self::Punct`, which only need a space if
            // there are two or them in a row.
            (Self::Punct, Self::Punct) => " ",
            _ => "",
        }
    }
}

impl From<&Token> for TokenClass {
    fn from(source: &Token) -> Self {
        match source {
            Token::Id(_) | Token::Number(_) | Token::String(_) => Self::Id,
            Token::End => Self::EndCommand,
            Token::Punct(punct) => match punct {
                Punct::LParen
                | Punct::RParen
                | Punct::LSquare
                | Punct::RSquare
                | Punct::LCurly
                | Punct::RCurly => Self::Punct,

                Punct::Plus
                | Punct::Dash
                | Punct::Asterisk
                | Punct::Slash
                | Punct::Equals
                | Punct::Colon
                | Punct::And
                | Punct::Or
                | Punct::Not
                | Punct::Eq
                | Punct::Ge
                | Punct::Gt
                | Punct::Le
                | Punct::Lt
                | Punct::Ne
                | Punct::All
                | Punct::By
                | Punct::To
                | Punct::With
                | Punct::Exp
                | Punct::Bang
                | Punct::Percent
                | Punct::Question
                | Punct::Backtick
                | Punct::Dot
                | Punct::Underscore
                | Punct::BangAsterisk => Self::BinaryOperator,

                Punct::Comma | Punct::Semicolon => Self::Comma,
            },
        }
    }
}

pub fn macro_tokens_to_syntax(input: &[MacroToken]) -> impl Iterator<Item = [&str; 2]> {
    input
        .iter()
        .take(1)
        .map(|token| ["", token.syntax.as_str()])
        .chain(input.windows(2).map(|w| {
            let c0 = (&w[0].token).into();
            let c1 = (&w[1].token).into();
            [TokenClass::separator(c0, c1), w[1].syntax.as_str()]
        }))
}

trait MacroId {
    fn macro_id(&self) -> Option<&Identifier>;
}

impl MacroId for Token {
    fn macro_id(&self) -> Option<&Identifier> {
        let id = self.id()?;
        id.0.starts_with('!').then_some(id)
    }
}

enum RelOp {
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,
}

impl TryFrom<&str> for RelOp {
    type Error = ();

    fn try_from(source: &str) -> Result<Self, Self::Error> {
        match source {
            "=" => Ok(Self::Eq),
            "~=" | "<>" => Ok(Self::Ne),
            "<" => Ok(Self::Lt),
            ">" => Ok(Self::Gt),
            "<=" => Ok(Self::Le),
            ">=" => Ok(Self::Ge),
            _ if source.len() == 3 && source.as_bytes()[0] == b'!' => match (
                source.as_bytes()[0].to_ascii_uppercase(),
                source.as_bytes()[1].to_ascii_uppercase(),
            ) {
                (b'E', b'Q') => Ok(Self::Eq),
                (b'N', b'E') => Ok(Self::Ne),
                (b'L', b'T') => Ok(Self::Lt),
                (b'L', b'E') => Ok(Self::Le),
                (b'G', b'T') => Ok(Self::Gt),
                (b'G', b'E') => Ok(Self::Ge),
                _ => Err(()),
            },
            _ => Err(()),
        }
    }
}

impl RelOp {
    fn evaluate(&self, cmp: Ordering) -> bool {
        match self {
            RelOp::Eq => cmp == Ordering::Equal,
            RelOp::Ne => cmp != Ordering::Equal,
            RelOp::Lt => cmp == Ordering::Less,
            RelOp::Gt => cmp == Ordering::Greater,
            RelOp::Le => cmp != Ordering::Greater,
            RelOp::Ge => cmp != Ordering::Less,
        }
    }
}

pub type MacroSet = HashMap<UniCase<String>, Macro>;

enum ParserState {
    /// Accumulating tokens toward the end of any type of argument.
    Arg,

    /// Expecting the opening delimiter of an ARG_ENCLOSE argument.
    Enclose,

    /// Expecting a keyword for a keyword argument.
    Keyword,

    /// Expecting an equal sign for a keyword argument.
    Equals,

    /// Macro fully parsed and ready for expansion.
    Finished,
}

/// Macro call parser FSM.
pub struct Parser<'a> {
    macros: &'a MacroSet,
    macro_: &'a Macro,
    state: ParserState,
    args: Box<[Option<Vec<MacroToken>>]>,
    arg_index: usize,

    /// Length of macro call so far.
    n_tokens: usize,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ParseStatus {
    Complete,
    Incomplete,
}

impl<'a> Parser<'a> {
    pub fn new(macros: &'a MacroSet, token: &Token) -> Option<Self> {
        let macro_ = macros.get(&token.id()?.0)?;
        Some(Self {
            macros,
            macro_,
            state: macro_.initial_state(),
            args: (0..macro_.parameters.len()).map(|_| None).collect(),
            arg_index: 0,
            n_tokens: 1,
        })
    }

    fn finished(&mut self) {
        self.state = ParserState::Finished;
        for (i, arg) in self.args.iter_mut().enumerate() {
            if arg.is_none() {
                *arg = Some(self.macro_.parameters[i].default.clone());
            }
        }
        self.state = ParserState::Finished;
    }

    fn next_arg(&mut self) {
        if self.macro_.parameters.is_empty() {
            self.finished()
        } else {
            let param = &self.macro_.parameters[self.arg_index];
            if param.is_positional() {
                self.arg_index += 1;
                if self.arg_index >= self.args.len() {
                    self.finished()
                } else {
                    let param = &self.macro_.parameters[self.arg_index];
                    self.state = if !param.is_positional() {
                        ParserState::Keyword
                    } else if let ValueType::Enclose(_, _) = param.arg {
                        ParserState::Enclose
                    } else {
                        ParserState::Arg
                    };
                }
            } else if self.args.iter().any(|arg| arg.is_none()) {
                self.state = ParserState::Keyword;
            } else {
                self.finished();
            }
        }
    }

    fn push_arg(&mut self, token: &Token, syntax: &str, error: &impl Fn(MacroError)) {
        let param = &self.macro_.parameters[self.args.len() - 1];
        if let Token::End = token {
            if let Some(arg) = &self.args[self.arg_index] {
                let param = &self.macro_.parameters[self.args.len() - 1];

                match &param.arg {
                    ValueType::NTokens(n) => error(MacroError::ExpectedMoreTokens {
                        n: n - arg.len(),
                        arg: param.name.clone(),
                        macro_: self.macro_.name.clone(),
                    }),
                    ValueType::CharEnd(end) | ValueType::Enclose(_, end) => {
                        error(MacroError::ExpectedToken {
                            token: end.to_string(),
                            arg: param.name.clone(),
                            macro_: self.macro_.name.clone(),
                        })
                    }
                    ValueType::CmdEnd => {
                        // This is OK, it's the expected way to end the argument.
                    }
                }
            }
            self.finished();
        }

        self.n_tokens += 1;
        let arg = self.args[self.arg_index].get_or_insert(Vec::new());
        let (
            add_token, // Should we add `mt` to the current arg?
            next_arg,  // Should we advance to the next arg?
        ) = match &param.arg {
            ValueType::NTokens(n) => (arg.len() + 1 >= *n, true),
            ValueType::CharEnd(end) | ValueType::Enclose(_, end) => {
                let at_end = token == end;
                (at_end, !at_end)
            }
            ValueType::CmdEnd => (false, true),
        };
        if add_token {
            if true
            // !macro_expand_arg (&mt->token, mc->me, *argp)
            {
                arg.push(MacroToken {
                    token: token.clone(),
                    syntax: String::from(syntax),
                });
            }
        }
        if next_arg {
            self.next_arg()
        }
    }

    fn push_enclose(&mut self, token: &Token, syntax: &str, error: &impl Fn(MacroError)) {
        let param = &self.macro_.parameters[self.arg_index];
        let ValueType::Enclose(start, _) = &param.arg else {
            unreachable!()
        };
        if token == start {
            self.n_tokens += 1;
            self.args[self.arg_index].get_or_insert(Vec::new());
            self.state = ParserState::Arg;
        } else if param.is_positional() && matches!(token, Token::End) {
            self.finished();
        } else {
            error(MacroError::UnexpectedToken {
                actual: String::from(syntax),
                expected: start.to_string(),
                arg: param.name.clone(),
                macro_: self.macro_.name.clone(),
            });
            self.finished();
        }
    }

    fn push_keyword(&mut self, token: &Token, _syntax: &str, error: &impl Fn(MacroError)) {
        let Some(id) = token.id() else {
            return self.finished();
        };
        let Some(arg_index) = self.macro_.find_parameter(id) else {
            return self.finished();
        };
        self.arg_index = arg_index;
        if self.args[arg_index].is_some() {
            error(MacroError::DuplicateArg {
                arg: id.clone(),
                macro_: self.macro_.name.clone(),
            });
        }
        self.args[arg_index] = Some(Vec::new());
    }

    fn push_equals(&mut self, token: &Token, syntax: &str, error: &impl Fn(MacroError)) {
        let param = &self.macro_.parameters[self.arg_index];
        if let Token::Punct(Punct::Eq) = token {
            self.n_tokens += 1;
            self.state = if let ValueType::Enclose(_, _) = param.arg {
                ParserState::Enclose
            } else {
                ParserState::Arg
            };
        } else {
            error(MacroError::UnexpectedToken {
                actual: syntax.into(),
                expected: String::from("="),
                arg: param.name.clone(),
                macro_: self.macro_.name.clone(),
            });
            self.finished()
        }
    }

    /// Adds `token`, which has the given `syntax`, to the collection of tokens
    /// in `self` that potentially need to be macro expanded.
    ///
    /// Returns [ParseStatus::Incomplete] if the macro expander needs more
    /// tokens, for macro arguments or to decide whether this is actually a
    /// macro invocation.  The caller should call `push` again with the next
    /// token.
    ///
    /// Returns [ParseStatus::Complete] if the macro invocation is now complete.
    /// The caller should call [`Self::finish()`] to obtain the expansion.
    pub fn push(
        &mut self,
        token: &Token,
        syntax: &str,
        error: &impl Fn(MacroError),
    ) -> ParseStatus {
        match self.state {
            ParserState::Arg => self.push_arg(token, syntax, error),
            ParserState::Enclose => self.push_enclose(token, syntax, error),
            ParserState::Keyword => self.push_keyword(token, syntax, error),
            ParserState::Equals => self.push_equals(token, syntax, error),
            ParserState::Finished => (),
        }
        if let ParserState::Finished = self.state {
            ParseStatus::Complete
        } else {
            ParseStatus::Incomplete
        }
    }

    pub fn finish(self) -> Call<'a> {
        let ParserState::Finished = self.state else {
            panic!()
        };
        Call(self)
    }
}

/// Expansion stack entry.
struct Frame {
    /// A macro name or `!IF`, `!DO`, etc.
    name: Option<Identifier>,

    /// Source location, if available.
    location: Option<Location>,
}

struct Expander<'a> {
    /// Macros to expand recursively.
    macros: &'a MacroSet,

    /// Error reporting callback.
    error: &'a (dyn Fn(MacroError) + 'a),

    /// Tokenization mode.
    mode: Syntax,

    /// Remaining nesting levels.
    nesting_countdown: usize,

    /// Stack for error reporting.
    stack: Vec<Frame>,

    // May macro calls be expanded?
    expand: &'a RefCell<bool>,

    /// Variables from `!DO` and `!LET`.
    vars: &'a RefCell<BTreeMap<Identifier, String>>,

    // Only set if inside a `!DO` loop. If true, break out of the loop.
    break_: Option<&'a mut bool>,

    /// Only set if expanding a macro (and not, say, a macro argument).
    macro_: Option<&'a Macro>,

    /// Only set if expanding a macro (and not, say, a macro argument).
    args: Option<&'a [Option<Vec<MacroToken>>]>,
}

fn bool_to_string(b: bool) -> String {
    if b {
        String::from("1")
    } else {
        String::from("0")
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum IfEndClause {
    Else,
    IfEnd,
}

fn macro_keywords() -> HashSet<Identifier> {
    let mut keywords = HashSet::new();
    for kw in [
        "!BREAK",
        "!CHAREND",
        "!CMDEND",
        "!DEFAULT",
        "!DO",
        "!DOEND",
        "!ELSE",
        "!ENCLOSE",
        "!ENDDEFINE",
        "!IF",
        "!IFEND",
        "!IN",
        "!LET",
        "!NOEXPAND",
        "!OFFEXPAND",
        "!ONEXPAND",
        "!POSITIONAL",
        "!THEN",
        "!TOKENS",
    ] {
        keywords.insert(Identifier::new(kw).unwrap());
    }
    keywords
}

fn is_macro_keyword(s: &Identifier) -> bool {
    static KEYWORDS: LazyLock<HashSet<Identifier>> = LazyLock::new(macro_keywords);
    KEYWORDS.contains(s)
}

enum DoInput {
    List(Vec<String>),
    Up { first: f64, last: f64, by: f64 },
    Down { first: f64, last: f64, by: f64 },
    Empty,
}

impl DoInput {
    fn from_list(items: Vec<MacroToken>) -> Self {
        Self::List(
            items
                .into_iter()
                .rev()
                .take(Settings::global().macros.max_iterations + 1)
                .map(|mt| mt.syntax)
                .collect(),
        )
    }

    fn from_by(first: f64, last: f64, by: f64) -> Self {
        if by > 0.0 && first <= last {
            Self::Up { first, last, by }
        } else if by < 0.0 && first <= last {
            Self::Down { first, last, by }
        } else {
            Self::Empty
        }
    }
}

impl Iterator for DoInput {
    type Item = String;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            DoInput::List(vec) => vec.pop(),
            DoInput::Up { first, last, by } => {
                if first <= last {
                    let value = *first;
                    *first += *by;
                    Some(format!("{value}"))
                } else {
                    None
                }
            }
            DoInput::Down { first, last, by } => {
                if first >= last {
                    let value = *first;
                    *first += *by;
                    Some(format!("{value}"))
                } else {
                    None
                }
            }
            DoInput::Empty => None,
        }
    }
}

impl Expander<'_> {
    fn may_expand(&self) -> bool {
        *self.expand.borrow()
    }

    fn should_break(&self) -> bool {
        self.break_.as_ref().map(|b| **b).unwrap_or(false)
    }

    fn expand(&mut self, input: &mut MacroTokens, output: &mut Vec<MacroToken>) {
        if self.nesting_countdown == 0 {
            (self.error)(MacroError::TooDeep {
                limit: Settings::global().macros.max_nest,
            });
            output.extend(take(&mut input.0).iter().cloned());
        } else {
            while !input.is_empty() && !self.should_break() {
                self.expand__(input, output);
            }
        }
    }

    fn expand_arg(&mut self, param_idx: usize, output: &mut Vec<MacroToken>) {
        let param = &self.macro_.unwrap().parameters[param_idx];
        let arg = &self.args.unwrap()[param_idx].as_ref().unwrap();
        if self.may_expand() && param.expand_value {
            let vars = RefCell::new(BTreeMap::new());
            let mut stack = take(&mut self.stack);
            stack.push(Frame {
                name: Some(param.name.clone()),
                location: None,
            });
            let mut subexpander = Expander {
                stack,
                vars: &vars,
                break_: None,
                macro_: None,
                args: None,
                ..*self
            };
            let mut arg_tokens = MacroTokens(arg);
            subexpander.expand(&mut arg_tokens, output);
            self.stack = subexpander.stack;
            self.stack.pop();
        } else {
            output.extend(arg.iter().cloned());
        }
    }
    fn parse_function_args(
        &mut self,
        function: &Identifier,
        input: &mut MacroTokens,
    ) -> Option<Vec<String>> {
        input.advance();
        input.advance();
        let mut args = Vec::new();
        if input.match_(")") {
            return Some(args);
        }
        loop {
            args.push(self.parse_function_arg(input)?);
            match input.take() {
                Some(MacroToken {
                    token: Token::Punct(Punct::Comma),
                    ..
                }) => (),
                Some(MacroToken {
                    token: Token::Punct(Punct::RParen),
                    ..
                }) => return Some(args),
                _ => {
                    (self.error)(MacroError::ExpectingCommaOrRParen(function.clone()));
                    return None;
                }
            }
        }
    }

    fn expand_blanks(e: &mut Expander, args: Vec<String>) -> Option<String> {
        let Ok(n) = args[0].trim().parse::<usize>() else {
            (e.error)(MacroError::InvalidBlanks(args[0].clone()));
            return None;
        };
        Some(" ".repeat(n))
    }

    fn expand_concat(e: &mut Expander, args: Vec<String>) -> Option<String> {
        Some(
            args.into_iter()
                .map(|arg| unquote_string(arg, e.mode))
                .collect(),
        )
    }

    fn expand_eval(e: &mut Expander, args: Vec<String>) -> Option<String> {
        let tokens = tokenize_string(&args[0], e.mode, e.error);
        let mut stack = take(&mut e.stack);
        stack.push(Frame {
            name: Some(Identifier::new("!EVAL").unwrap()),
            location: None,
        });
        let mut break_ = false;
        let mut subexpander = Expander {
            break_: Some(&mut break_),
            stack,
            vars: e.vars,
            ..*e
        };
        let mut output = Vec::new();
        subexpander.expand(&mut MacroTokens(tokens.as_slice()), &mut output);
        subexpander.stack.pop();
        e.stack = subexpander.stack;
        Some(macro_tokens_to_syntax(&output).flatten().collect())
    }

    fn expand_head(e: &mut Expander, mut args: Vec<String>) -> Option<String> {
        let arg = unquote_string(args.remove(0), e.mode);
        let mut output = tokenize_string(&arg, e.mode, e.error);
        if output.is_empty() {
            Some(String::new())
        } else {
            Some(output.swap_remove(0).syntax)
        }
    }

    fn expand_index(_e: &mut Expander, args: Vec<String>) -> Option<String> {
        let haystack = &args[0];
        let needle = &args[1];
        let position = haystack.find(needle);
        Some(format!(
            "{}",
            position.map_or(0, |position| &haystack[0..position].chars().count() + 1)
        ))
    }

    fn expand_length(_e: &mut Expander, args: Vec<String>) -> Option<String> {
        Some(format!("{}", args[0].chars().count()))
    }

    fn expand_quote(e: &mut Expander, mut args: Vec<String>) -> Option<String> {
        let arg = args.remove(0);
        if try_unquote_string(&arg, e.mode).is_some() {
            Some(arg)
        } else {
            let mut output = String::with_capacity(arg.len() + 2);
            output.push('\'');
            for c in arg.chars() {
                if c == '"' {
                    output.push('\'');
                }
                output.push(c);
            }
            output.push('\'');
            Some(output)
        }
    }

    fn expand_substr(e: &mut Expander, args: Vec<String>) -> Option<String> {
        let Ok(start) = args[1].trim().parse::<NonZeroUsize>() else {
            (e.error)(MacroError::InvalidSubstr3(args[0].clone()));
            return None;
        };
        let start = start.get();
        let Ok(count) = args[2].trim().parse::<usize>() else {
            (e.error)(MacroError::InvalidSubstr2(args[0].clone()));
            return None;
        };

        Some(args[0].chars().skip(start - 1).take(count).collect())
    }

    fn expand_tail(e: &mut Expander, mut args: Vec<String>) -> Option<String> {
        let arg = unquote_string(args.remove(0), e.mode);
        let mut output = tokenize_string(&arg, e.mode, e.error);
        Some(output.pop().map_or_else(String::new, |tail| tail.syntax))
    }

    fn expand_unquote(e: &mut Expander, mut args: Vec<String>) -> Option<String> {
        Some(unquote_string(args.remove(0), e.mode))
    }

    fn expand_upcase(e: &mut Expander, mut args: Vec<String>) -> Option<String> {
        Some(unquote_string(args.remove(0), e.mode).to_uppercase())
    }

    fn expand_macro_function(&mut self, orig_input: &mut MacroTokens) -> Option<String> {
        let mut input = orig_input.clone();
        let name = input.macro_id()?;
        if name == "!NULL" {
            return Some(String::new());
        }
        if input.0.len() < 2 || !matches!(input.0[1].token, Token::Punct(Punct::LParen)) {
            return None;
        }

        struct MacroFunction {
            name: Identifier,
            args: RangeInclusive<usize>,
            parser: fn(&mut Expander, Vec<String>) -> Option<String>,
        }
        impl MacroFunction {
            fn new(
                name: &str,
                args: RangeInclusive<usize>,
                parser: fn(&mut Expander, Vec<String>) -> Option<String>,
            ) -> Self {
                Self {
                    name: Identifier::new(name).unwrap(),
                    args,
                    parser,
                }
            }
        }
        static MACRO_FUNCTIONS: LazyLock<[MacroFunction; 11]> = LazyLock::new(|| {
            [
                MacroFunction::new("!BLANKS", 1..=1, Expander::expand_blanks),
                MacroFunction::new("!CONCAT", 1..=usize::MAX, Expander::expand_concat),
                MacroFunction::new("!HEAD", 1..=1, Expander::expand_head),
                MacroFunction::new("!INDEX", 2..=2, Expander::expand_index),
                MacroFunction::new("!LENGTH", 1..=1, Expander::expand_length),
                MacroFunction::new("!QUOTE", 1..=1, Expander::expand_quote),
                MacroFunction::new("!SUBSTR", 2..=3, Expander::expand_substr),
                MacroFunction::new("!TAIL", 1..=1, Expander::expand_tail),
                MacroFunction::new("!UNQUOTE", 1..=1, Expander::expand_unquote),
                MacroFunction::new("!UPCASE", 1..=1, Expander::expand_upcase),
                MacroFunction::new("!EVAL", 1..=1, Expander::expand_eval),
            ]
        });

        let function = MACRO_FUNCTIONS.iter().find(|mf| &mf.name == name)?;

        let args = self.parse_function_args(&function.name, &mut input)?;

        let n_args = args.len();
        if !function.args.contains(&n_args) {
            let name = function.name.clone();
            let error = match &function.args {
                x if x == &(1..=1) => MacroError::ExpectingOneArg { name, n_args },
                x if x == &(2..=2) => MacroError::ExpectingTwoArgs { name, n_args },
                x if x == &(2..=3) => MacroError::ExpectingTwoOrThreeArgs { name, n_args },
                x if x == &(1..=usize::MAX) => MacroError::ExpectingOneOrMoreArgs { name },
                _ => unreachable!(),
            };
            (self.error)(error);
            return None;
        }

        *orig_input = input;
        (function.parser)(self, args)
    }

    /// Parses one function argument from `input`.  Each argument to a macro
    /// function is one of:
    ///
    /// - A quoted string or other single literal token.
    ///
    /// - An argument to the macro being expanded, e.g. `!1` or a named
    ///   argument.
    ///
    /// - `!*`.
    ///
    /// - A function invocation.
    ///
    /// Each function invocation yields a character sequence to be turned into a
    /// sequence of tokens.  The case where that character sequence is a single
    /// quoted string is an important special case.
    fn parse_function_arg(&mut self, input: &mut MacroTokens) -> Option<String> {
        if let Some(macro_) = self.macro_ {
            match &input.0.first()?.token {
                Token::Id(id) if id.0.starts_with('!') => {
                    if let Some(param_idx) = macro_.find_parameter(id) {
                        input.advance();
                        return Some(
                            macro_tokens_to_syntax(self.args.unwrap()[param_idx].as_ref().unwrap())
                                .flatten()
                                .collect(),
                        );
                    }
                    if let Some(value) = self.vars.borrow().get(id) {
                        return Some(value.clone());
                    }

                    if let Some(output) = self.expand_macro_function(input) {
                        return Some(output);
                    }
                }
                Token::Punct(Punct::BangAsterisk) => {
                    let mut arg = String::new();
                    for i in 0..macro_.parameters.len() {
                        if !macro_.parameters[i].is_positional() {
                            break;
                        }
                        if i > 0 {
                            arg.push(' ')
                        }
                        arg.extend(
                            macro_tokens_to_syntax(self.args.unwrap()[i].as_ref().unwrap())
                                .flatten(),
                        );
                    }
                    input.advance();
                    return Some(arg);
                }
                _ => (),
            }
        }
        Some(input.advance().syntax.clone())
    }

    fn evaluate_literal(&mut self, input: &mut MacroTokens) -> Option<String> {
        if input.match_("(") {
            let value = self.evaluate_or(input)?;
            if input.match_(")") {
                Some(value)
            } else {
                (self.error)(MacroError::ExpectingRParen);
                None
            }
        } else if input.match_(")") {
            (self.error)(MacroError::ExpectingLiteral);
            None
        } else {
            Some(unquote_string(self.parse_function_arg(input)?, self.mode))
        }
    }

    fn evaluate_relational(&mut self, input: &mut MacroTokens) -> Option<String> {
        let lhs = self.evaluate_literal(input)?;
        let Some(relop) = input.take_relop() else {
            return Some(lhs);
        };
        let rhs = self.evaluate_literal(input)?;
        let cmp = unquote_string(lhs, self.mode).cmp(&unquote_string(rhs, self.mode));
        Some(bool_to_string(relop.evaluate(cmp)))
    }

    fn evaluate_not(&mut self, input: &mut MacroTokens) -> Option<String> {
        let mut negations = 0;
        while input.match_("!AND") || input.match_("&") {
            negations += 1;
        }

        let operand = self.evaluate_relational(input)?;
        if negations == 0 {
            return Some(operand);
        }

        let mut b = operand != "0";
        if negations.is_odd() {
            b = !b;
        }
        Some(bool_to_string(b))
    }

    fn evaluate_and(&mut self, input: &mut MacroTokens) -> Option<String> {
        let mut lhs = self.evaluate_not(input)?;
        while input.match_("!AND") || input.match_("&") {
            let rhs = self.evaluate_not(input)?;
            lhs = bool_to_string(lhs != "0" && rhs != "0");
        }
        Some(lhs)
    }
    fn evaluate_or(&mut self, input: &mut MacroTokens) -> Option<String> {
        let mut lhs = self.evaluate_and(input)?;
        while input.match_("!OR") || input.match_("|") {
            let rhs = self.evaluate_and(input)?;
            lhs = bool_to_string(lhs != "0" || rhs != "0");
        }
        Some(lhs)
    }

    fn evaluate_expression(&mut self, input: &mut MacroTokens) -> Option<String> {
        self.evaluate_or(input)
    }

    fn evaluate_number(&mut self, input: &mut MacroTokens) -> Option<f64> {
        let s = self.evaluate_expression(input)?;
        let tokens = tokenize_string(&s, self.mode, self.error);
        let (
            Some(MacroToken {
                token: Token::Number(number),
                ..
            }),
            1,
        ) = (tokens.first(), tokens.len())
        else {
            (self.error)(MacroError::BadNumericMacroExpression(s));
            return None;
        };

        Some(*number)
    }

    fn find_ifend_clause<'b>(
        input: &mut MacroTokens<'b>,
    ) -> Option<(MacroTokens<'b>, IfEndClause)> {
        let input_copy = input.clone();
        let mut nesting = 0;
        while !input.is_empty() {
            if input.match_("!IF") {
                nesting += 1;
            } else if input.match_("!IFEND") {
                if nesting == 0 {
                    return Some((
                        MacroTokens(&input_copy.0[..input_copy.0.len() - input.0.len() - 1]),
                        IfEndClause::IfEnd,
                    ));
                }
                nesting -= 1;
            } else if input.match_("!ELSE") && nesting == 0 {
                return Some((
                    MacroTokens(&input_copy.0[..input_copy.0.len() - input.0.len() - 1]),
                    IfEndClause::Else,
                ));
            } else {
                input.advance();
            }
        }
        None
    }
    fn expand_if(&mut self, orig_input: &mut MacroTokens, output: &mut Vec<MacroToken>) -> bool {
        let mut input = orig_input.clone();
        if !input.match_("!IF") {
            return false;
        }
        let Some(result) = self.evaluate_expression(&mut input) else {
            return false;
        };
        if !input.match_("!THEN") {
            (self.error)(MacroError::ExpectingThen);
            return false;
        }

        let Some((if_tokens, clause)) = Self::find_ifend_clause(&mut input) else {
            (self.error)(MacroError::ExpectingElseOrIfEnd);
            return false;
        };

        let else_tokens = match clause {
            IfEndClause::Else => {
                let Some((else_tokens, IfEndClause::IfEnd)) = Self::find_ifend_clause(&mut input)
                else {
                    (self.error)(MacroError::ExpectingIfEnd);
                    return false;
                };
                Some(else_tokens)
            }
            IfEndClause::IfEnd => None,
        };

        let subinput = match result.as_str() {
            "0" => else_tokens,
            _ => Some(if_tokens),
        };
        if let Some(mut subinput) = subinput {
            self.stack.push(Frame {
                name: Some(Identifier::new("!IF").unwrap()),
                location: None,
            });
            self.expand(&mut subinput, output);
            self.stack.pop();
        }
        *orig_input = input;
        true
    }

    fn take_macro_var_name(
        &mut self,
        input: &mut MacroTokens,
        construct: &'static str,
    ) -> Option<Identifier> {
        let Some(var_name) = input.take_macro_id() else {
            (self.error)(MacroError::ExpectingMacroVarName(construct));
            return None;
        };
        if is_macro_keyword(var_name)
            || self
                .macro_
                .and_then(|m| m.find_parameter(var_name))
                .is_some()
        {
            (self.error)(MacroError::BadMacroVarName {
                name: var_name.clone(),
                construct,
            });
            None
        } else {
            Some(var_name.clone())
        }
    }

    fn expand_let(&mut self, orig_input: &mut MacroTokens) -> bool {
        let mut input = orig_input.clone();
        if !input.match_("!LET") {
            return false;
        }

        let Some(var_name) = self.take_macro_var_name(&mut input, "!LET") else {
            return false;
        };
        input.advance();

        if !input.match_("=") {
            (self.error)(MacroError::ExpectingEquals);
            return false;
        }

        let Some(value) = self.evaluate_expression(&mut input) else {
            return false;
        };
        self.vars.borrow_mut().insert(var_name.clone(), value);
        *orig_input = input;
        true
    }

    fn find_doend<'b>(&mut self, input: &mut MacroTokens<'b>) -> Option<MacroTokens<'b>> {
        let input_copy = input.clone();
        let mut nesting = 0;
        while !input.is_empty() {
            if input.match_("!DO") {
                nesting += 1;
            } else if input.match_("!DOEND") {
                if nesting == 0 {
                    return Some(MacroTokens(
                        &input_copy.0[..input_copy.0.len() - input.0.len() - 1],
                    ));
                }
                nesting -= 1;
            } else {
                input.advance();
            }
        }
        (self.error)(MacroError::MissingDoEnd);
        None
    }

    fn expand_do(&mut self, orig_input: &mut MacroTokens, output: &mut Vec<MacroToken>) -> bool {
        let mut input = orig_input.clone();
        if !input.match_("!DO") {
            return false;
        }

        let Some(var_name) = self.take_macro_var_name(&mut input, "!DO") else {
            return false;
        };

        let (items, miterate_error) = if input.match_("!IN") {
            let Some(list) = self.evaluate_expression(&mut input) else {
                return false;
            };
            let items = tokenize_string(list.as_str(), self.mode, &self.error);
            (
                DoInput::from_list(items),
                MacroError::MiterateList(Settings::global().macros.max_iterations),
            )
        } else if input.match_("=") {
            let Some(first) = self.evaluate_number(&mut input) else {
                return false;
            };
            if !input.match_("!TO") {
                (self.error)(MacroError::ExpectingTo);
                return false;
            }
            let Some(last) = self.evaluate_number(&mut input) else {
                return false;
            };
            let by = if input.match_("!BY") {
                let Some(by) = self.evaluate_number(&mut input) else {
                    return false;
                };
                if by == 0.0 {
                    (self.error)(MacroError::ZeroBy);
                    return false;
                }
                by
            } else {
                1.0
            };
            (
                DoInput::from_by(first, last, by),
                MacroError::MiterateNumeric(Settings::global().macros.max_iterations),
            )
        } else {
            (self.error)(MacroError::ExpectingEqualsOrIn);
            return false;
        };

        let Some(body) = self.find_doend(&mut input) else {
            return false;
        };

        let mut stack = take(&mut self.stack);
        stack.push(Frame {
            name: Some(Identifier::new("!DO").unwrap()),
            location: None,
        });
        let mut break_ = false;
        let mut subexpander = Expander {
            break_: Some(&mut break_),
            stack,
            vars: self.vars,
            ..*self
        };

        for (i, item) in items.enumerate() {
            if subexpander.should_break() {
                break;
            }
            if i >= Settings::global().macros.max_iterations {
                (self.error)(miterate_error);
                break;
            }
            let mut vars = self.vars.borrow_mut();
            if let Some(value) = vars.get_mut(&var_name) {
                *value = item;
            } else {
                vars.insert(var_name.clone(), item);
            }
            subexpander.expand(&mut body.clone(), output);
        }
        *orig_input = input;
        true
    }

    fn expand__(&mut self, input: &mut MacroTokens, output: &mut Vec<MacroToken>) {
        // Recursive macro calls.
        if self.may_expand() {
            if let Some(call) = Call::for_tokens(self.macros, input.0, &self.error) {
                let vars = RefCell::new(BTreeMap::new());
                let mut stack = take(&mut self.stack);
                stack.push(Frame {
                    name: Some(call.0.macro_.name.clone()),
                    location: Some(call.0.macro_.location.clone()),
                });
                let mut subexpander = Expander {
                    break_: None,
                    vars: &vars,
                    nesting_countdown: self.nesting_countdown.saturating_sub(1),
                    stack,
                    ..*self
                };
                let mut body = MacroTokens(call.0.macro_.body.as_slice());
                subexpander.expand(&mut body, output);
                self.stack = subexpander.stack;
                self.stack.pop();
                input.0 = &input.0[call.len()..];
                return;
            }
        }

        // Only identifiers beginning with `!` receive further processing.
        let id = match &input.0[0].token {
            Token::Id(id) if id.0.starts_with('!') => id,
            Token::Punct(Punct::BangAsterisk) => {
                if let Some(macro_) = self.macro_ {
                    for i in 0..macro_.parameters.len() {
                        self.expand_arg(i, output);
                    }
                } else {
                    (self.error)(MacroError::InvalidBangAsterisk);
                }
                input.advance();
                return;
            }
            _ => {
                output.push(input.advance().clone());
                return;
            }
        };

        // Macro arguments.
        if let Some(macro_) = self.macro_ {
            if let Some(param_idx) = macro_.find_parameter(id) {
                self.expand_arg(param_idx, output);
                input.advance();
                return;
            }
        }

        // Variables set by `!DO` or `!LET`.
        if let Some(value) = self.vars.borrow().get(id) {
            tokenize_string_into(value.as_str(), self.mode, &self.error, output);
            input.advance();
            return;
        }

        // Macro functions.
        if self.expand_if(input, output) {
            return;
        }
        if self.expand_let(input) {
            return;
        }
        if self.expand_do(input, output) {
            return;
        }

        if input.match_("!BREAK") {
            if let Some(ref mut break_) = self.break_ {
                **break_ = true;
            } else {
                (self.error)(MacroError::BreakOutsideDo);
            }
            return;
        }

        if input.match_("!ONEXPAND") {
            *self.expand.borrow_mut() = true;
        } else if input.match_("!OFFEXPAND") {
            *self.expand.borrow_mut() = false;
        } else {
            output.push(input.advance().clone());
        }
    }
}

pub struct Call<'a>(Parser<'a>);

impl<'a> Call<'a> {
    pub fn for_tokens<F>(macros: &'a MacroSet, tokens: &[MacroToken], error: &F) -> Option<Self>
    where
        F: Fn(MacroError),
    {
        let mut parser = Parser::new(macros, &tokens.first()?.token)?;
        for token in tokens[1..].iter().chain(&[MacroToken {
            token: Token::End,
            syntax: String::from(""),
        }]) {
            if parser.push(&token.token, &token.syntax, error) == ParseStatus::Complete {
                return Some(parser.finish());
            }
        }
        None
    }

    pub fn expand<F>(
        &self,
        mode: Syntax,
        call_loc: Location,
        output: &mut Vec<MacroToken>,
        error: F,
    ) where
        F: Fn(MacroError) + 'a,
    {
        let error: Box<dyn Fn(MacroError) + 'a> = Box::new(error);
        let vars = RefCell::new(BTreeMap::new());
        let expand = RefCell::new(true);
        let mut me = Expander {
            macros: self.0.macros,
            error: &error,
            macro_: Some(self.0.macro_),
            args: Some(&self.0.args),
            mode,
            nesting_countdown: Settings::global().macros.max_nest,
            stack: vec![
                Frame {
                    name: None,
                    location: Some(call_loc),
                },
                Frame {
                    name: Some(self.0.macro_.name.clone()),
                    location: Some(self.0.macro_.location.clone()),
                },
            ],
            vars: &vars,
            break_: None,
            expand: &expand,
        };
        let mut body = MacroTokens(&self.0.macro_.body);
        me.expand(&mut body, output);
    }

    /// Returns the number of tokens consumed from the input for the macro
    /// invocation. If the result is 0, then there was no macro invocation and
    /// the expansion will be empty.
    pub fn len(&self) -> usize {
        self.0.n_tokens
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}
