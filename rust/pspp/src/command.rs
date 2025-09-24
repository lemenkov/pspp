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
use std::{
    fmt::{Debug, Write},
    ops::RangeFrom,
    sync::OnceLock,
};

use crosstabs::crosstabs_command;
use ctables::ctables_command;
use data_list::data_list_command;
use descriptives::descriptives_command;
use either::Either;
use flagset::{FlagSet, flags};
use pspp_derive::FromTokens;

use crate::{
    format::AbstractFormat,
    identifier::Identifier,
    integer::ToInteger,
    lex::{
        Punct, Token,
        command_name::CommandMatcher,
        lexer::{LexToken, TokenSlice},
    },
    message::{Diagnostic, Diagnostics},
};

pub mod crosstabs;
pub mod ctables;
pub mod data_list;
pub mod descriptives;

flags! {
    enum State: u8 {
        /// No active dataset yet defined.
        Initial,

        /// Active dataset has been defined.
        Data,

        /// Inside `INPUT PROGRAM`.
        InputProgram,

        /// Inside `FILE TYPE`.
        FileType,

        /// State nested inside `LOOP` or `DO IF`, inside [State::Data].
        NestedData,

        /// State nested inside `LOOP` or `DO IF`, inside [State::InputProgram].
        NestedInputProgram,
    }
}

struct Command {
    allowed_states: FlagSet<State>,
    enhanced_only: bool,
    testing_only: bool,
    no_abbrev: bool,
    name: &'static str,
    run: Box<dyn Fn(&mut Context) + Send + Sync>, //-> Box<dyn ParsedCommand> + Send + Sync>,
}

#[derive(Debug)]
enum ParseError {
    Error(Diagnostics),
    Mismatch(Diagnostics),
}

#[derive(Debug)]
struct Parsed<T> {
    value: T,
    rest: TokenSlice,
    diagnostics: Diagnostics,
}

impl<T> Parsed<T> {
    pub fn new(value: T, rest: TokenSlice, warnings: Diagnostics) -> Self {
        Self {
            value,
            rest,
            diagnostics: warnings,
        }
    }
    pub fn ok(value: T, rest: TokenSlice) -> Self {
        Self {
            value,
            rest,
            diagnostics: Diagnostics::default(),
        }
    }
    pub fn into_tuple(self) -> (T, TokenSlice, Diagnostics) {
        (self.value, self.rest, self.diagnostics)
    }
    pub fn take_diagnostics(self, d: &mut Diagnostics) -> (T, TokenSlice) {
        let (value, rest, mut diagnostics) = self.into_tuple();
        d.0.append(&mut diagnostics.0);
        (value, rest)
    }
    pub fn map<F, R>(self, f: F) -> Parsed<R>
    where
        F: FnOnce(T) -> R,
    {
        Parsed {
            value: f(self.value),
            rest: self.rest,
            diagnostics: self.diagnostics,
        }
    }
    pub fn warn(self, mut warnings: Diagnostics) -> Self {
        Self {
            value: self.value,
            rest: self.rest,
            diagnostics: {
                let mut vec = self.diagnostics.0;
                vec.append(&mut warnings.0);
                Diagnostics(vec)
            },
        }
    }
}

type ParseResult<T> = Result<Parsed<T>, ParseError>;

trait MismatchToError {
    fn mismatch_to_error(self) -> Self;
}

impl<T> MismatchToError for ParseResult<T> {
    fn mismatch_to_error(self) -> Self {
        match self {
            Err(ParseError::Mismatch(diagnostic)) => Err(ParseError::Error(diagnostic)),
            rest => rest,
        }
    }
}

trait FromTokens {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized;
}

impl<T> FromTokens for Option<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        match T::from_tokens(input) {
            Ok(p) => Ok(p.map(Some)),
            Err(ParseError::Mismatch(_)) => Ok(Parsed::ok(None, input.clone())),
            Err(ParseError::Error(error)) => Err(ParseError::Error(error)),
        }
    }
}

impl<L, R> FromTokens for Either<L, R>
where
    L: FromTokens,
    R: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        match L::from_tokens(input) {
            Ok(p) => Ok(p.map(Either::Left)),
            Err(ParseError::Mismatch(_)) => Ok(R::from_tokens(input)?.map(Either::Right)),
            Err(ParseError::Error(error)) => Err(ParseError::Error(error)),
        }
    }
}

impl<A, B> FromTokens for (A, B)
where
    A: FromTokens,
    B: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let (a, input, mut diagnostics) = A::from_tokens(input)?.into_tuple();
        let (b, rest, mut diagnostics2) = B::from_tokens(&input)?.into_tuple();
        diagnostics.0.append(&mut diagnostics2.0);
        Ok(Parsed::new((a, b), rest, diagnostics))
    }
}

impl<A, B, C> FromTokens for (A, B, C)
where
    A: FromTokens,
    B: FromTokens,
    C: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let (a, input, mut diagnostics) = A::from_tokens(input)?.into_tuple();
        let (b, input, mut diagnostics2) = B::from_tokens(&input)?.into_tuple();
        let (c, rest, mut diagnostics3) = C::from_tokens(&input)?.into_tuple();
        diagnostics.0.append(&mut diagnostics2.0);
        diagnostics.0.append(&mut diagnostics3.0);
        Ok(Parsed::new((a, b, c), rest, diagnostics))
    }
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "/")]
pub struct Slash;

#[derive(Debug)]
pub struct Comma;

impl FromTokens for Comma {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        _parse_token(input, &Token::Punct(Punct::Comma)).map(|p| p.map(|_| Comma))
    }
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "=")]
pub struct Equals;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "&")]
pub struct And;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = ">")]
pub struct Gt;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "+")]
pub struct Plus;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "-")]
pub struct Dash;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "*")]
pub struct Asterisk;

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(syntax = "**")]
pub struct Exp;

#[derive(Debug, pspp_derive::FromTokens)]
struct By;

pub struct Punctuated<T, P = Option<Comma>> {
    head: Vec<(T, P)>,
    tail: Option<T>,
}

impl<T, P> Debug for Punctuated<T, P>
where
    T: Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[")?;
        for (index, item) in self
            .head
            .iter()
            .map(|(t, _p)| t)
            .chain(self.tail.iter())
            .enumerate()
        {
            if index > 0 {
                write!(f, ", ")?;
            }
            write!(f, "{item:?}")?;
        }
        write!(f, "]")
    }
}

impl<T, P> FromTokens for Punctuated<T, P>
where
    T: FromTokens,
    P: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let mut head = Vec::new();
        let mut warnings_vec = Vec::new();
        let mut input = input.clone();
        let tail = loop {
            let t = match T::from_tokens(&input) {
                Ok(Parsed {
                    value,
                    rest,
                    diagnostics: mut warnings,
                }) => {
                    warnings_vec.append(&mut warnings.0);
                    input = rest;
                    value
                }
                Err(ParseError::Mismatch(_)) => break None,
                Err(ParseError::Error(e)) => return Err(ParseError::Error(e)),
            };
            let p = match P::from_tokens(&input) {
                Ok(Parsed {
                    value,
                    rest,
                    diagnostics: mut warnings,
                }) => {
                    warnings_vec.append(&mut warnings.0);
                    input = rest;
                    value
                }
                Err(ParseError::Mismatch(_)) => break Some(t),
                Err(ParseError::Error(e)) => return Err(ParseError::Error(e)),
            };
            head.push((t, p));
        };
        Ok(Parsed {
            value: Punctuated { head, tail },
            rest: input,
            diagnostics: Diagnostics(warnings_vec),
        })
    }
}

impl<T> FromTokens for Box<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        T::from_tokens(input).map(|p| p.map(|value| Box::new(value)))
    }
}

pub struct Subcommands<T>(Vec<T>);

impl<T> Debug for Subcommands<T>
where
    T: Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Subcommands[")?;
        for (index, item) in self.0.iter().enumerate() {
            if index > 0 {
                writeln!(f, ",")?;
            }
            write!(f, "{item:?}")?;
        }
        write!(f, "]")
    }
}

impl<T> FromTokens for Subcommands<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let mut items = Vec::new();
        let mut diagnostics = Vec::new();
        let mut input = input.clone();
        loop {
            let start = input.skip_until(|token| token != &Token::Punct(Punct::Slash));
            if start.is_empty() {
                break;
            }
            let end = start.skip_to(&Token::Punct(Punct::Slash));
            let subcommand = start.subslice(0..start.len() - end.len());
            match T::from_tokens(&subcommand) {
                Ok(p) => {
                    let (value, rest, mut d) = p.into_tuple();
                    items.push(value);
                    diagnostics.append(&mut d.0);
                    if !rest.is_empty() {
                        diagnostics.push(rest.warning("Syntax error expecting end of subcommand."));
                    }
                }
                Err(ParseError::Error(mut d) | ParseError::Mismatch(mut d)) => {
                    diagnostics.append(&mut d.0);
                }
            }
            input = end;
        }
        println!("{diagnostics:?}");
        Ok(Parsed {
            value: Subcommands(items),
            rest: input,
            diagnostics: Diagnostics(diagnostics),
        })
    }
}

#[derive(Debug)]
pub struct Seq0<T>(Vec<T>);

impl<T> FromTokens for Seq0<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let mut values_vec = Vec::new();
        let mut warnings_vec = Vec::new();
        let mut input = input.clone();
        while !input.is_empty() {
            match T::from_tokens(&input) {
                Ok(Parsed {
                    value,
                    rest,
                    diagnostics: mut warnings,
                }) => {
                    warnings_vec.append(&mut warnings.0);
                    if input.len() == rest.len() {
                        break;
                    }
                    values_vec.push(value);
                    input = rest;
                }
                Err(ParseError::Mismatch(_)) => break,
                Err(ParseError::Error(e)) => return Err(ParseError::Error(e)),
            }
        }
        Ok(Parsed {
            value: Seq0(values_vec),
            rest: input,
            diagnostics: Diagnostics(warnings_vec),
        })
    }
}

#[derive(Debug)]
pub struct Seq1<T>(Vec<T>);

impl<T> FromTokens for Seq1<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let mut values_vec = Vec::new();
        let mut warnings_vec = Vec::new();
        let mut input = input.clone();
        while !input.is_empty() {
            match T::from_tokens(&input) {
                Ok(Parsed {
                    value,
                    rest,
                    diagnostics: mut warnings,
                }) => {
                    warnings_vec.append(&mut warnings.0);
                    if input.len() == rest.len() {
                        break;
                    }
                    values_vec.push(value);
                    input = rest;
                }
                Err(ParseError::Mismatch(_)) => break,
                Err(ParseError::Error(e)) => return Err(ParseError::Error(e)),
            }
        }
        if values_vec.is_empty() {
            return Err(ParseError::Mismatch(input.error("Syntax error.").into()));
        }
        Ok(Parsed {
            value: Seq1(values_vec),
            rest: input,
            diagnostics: Diagnostics(warnings_vec),
        })
    }
}

/*
impl<T> FromTokens for Vec<T>
where
    T: FromTokens,
{
    fn from_tokens(mut input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let mut values_vec = Vec::new();
        let mut warnings_vec = Vec::new();
        while !input.is_empty() {
            match T::from_tokens(input) {
                Ok(Parsed {
                    value,
                    rest,
                    diagnostics: mut warnings,
                }) => {
                    values_vec.push(value);
                    warnings_vec.append(&mut warnings.0);
                    input = rest;
                }
                Err(ParseError::Mismatch(_)) => break,
                Err(ParseError::Error(e)) => return Err(ParseError::Error(e)),
            }
        }
        Ok(Parsed {
            value: values_vec,
            rest: input,
            diagnostics: Diagnostics(warnings_vec),
        })
    }
}*/

impl FromTokens for TokenSlice {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        Ok(Parsed::ok(input.clone(), input.end()))
    }
}

#[derive(Debug)]
struct Subcommand<T>(pub T);

impl<T> FromTokens for Subcommand<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let start = input.skip_until(|token| token != &Token::Punct(Punct::Slash));
        if start.is_empty() {
            return Err(ParseError::Error(
                input.error("Syntax error at end of input.").into(),
            ));
        }
        let end = start.skip_to(&Token::Punct(Punct::Slash));
        let subcommand = start.subslice(0..start.len() - end.len());
        let (value, rest, mut warnings) = T::from_tokens(&subcommand)?.into_tuple();
        if !rest.is_empty() {
            warnings
                .0
                .push(rest.warning("Syntax error expecting end of subcommand."));
        }
        Ok(Parsed::new(Self(value), end, warnings))
    }
}

#[derive(Debug)]
struct InParens<T>(pub T);

impl<T> FromTokens for InParens<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let ((), rest, _) = parse_token(input, &Token::Punct(Punct::LParen))?.into_tuple();
        let (value, rest, warnings) = T::from_tokens(&rest)?.into_tuple();
        let ((), rest, _) = parse_token(&rest, &Token::Punct(Punct::RParen))?.into_tuple();
        Ok(Parsed {
            value: Self(value),
            rest,
            diagnostics: warnings,
        })
    }
}

#[derive(Debug)]
struct InSquares<T>(pub T);

impl<T> FromTokens for InSquares<T>
where
    T: FromTokens,
{
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        let ((), rest, _) = parse_token(input, &Token::Punct(Punct::LSquare))?.into_tuple();
        let (value, rest, warnings) = T::from_tokens(&rest)?.into_tuple();
        let ((), rest, _) = parse_token(&rest, &Token::Punct(Punct::RSquare))?.into_tuple();
        Ok(Parsed {
            value: Self(value),
            rest,
            diagnostics: warnings,
        })
    }
}

fn parse_token_if<F, R>(input: &TokenSlice, parse: F) -> ParseResult<R>
where
    F: Fn(&Token) -> Option<R>,
{
    if let Some(token) = input.get_token(0) {
        if let Some(result) = parse(token) {
            return Ok(Parsed::ok(result, input.subslice(1..input.len())));
        }
    }
    Err(ParseError::Mismatch(Diagnostics::default()))
}

fn _parse_token(input: &TokenSlice, token: &Token) -> ParseResult<Token> {
    if let Some(rest) = input.skip(token) {
        Ok(Parsed::ok(input.first().token.clone(), rest))
    } else {
        Err(ParseError::Mismatch(
            input.error(format!("expecting {token}")).into(),
        ))
    }
}

fn parse_token(input: &TokenSlice, token: &Token) -> ParseResult<()> {
    if let Some(rest) = input.skip(token) {
        Ok(Parsed::ok((), rest))
    } else {
        Err(ParseError::Mismatch(
            input.error(format!("expecting {token}")).into(),
        ))
    }
}

fn parse_syntax(input: &TokenSlice, syntax: &str) -> ParseResult<()> {
    if let Some(rest) = input.skip_syntax(syntax) {
        Ok(Parsed::ok((), rest))
    } else {
        Err(ParseError::Mismatch(
            input.error(format!("expecting {syntax}")).into(),
        ))
    }
}

pub type VarList = Punctuated<VarRange>;

pub struct Number(f64);

impl Debug for Number {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.0)
    }
}

impl FromTokens for Number {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        parse_token_if(input, |token| token.as_number().map(Number))
            .map_err(|_| ParseError::Mismatch(input.error(String::from("expecting number")).into()))
    }
}

#[derive(Debug)]
pub struct Integer(i64);

impl FromTokens for Integer {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        parse_token_if(input, |token| token.as_integer().map(Integer)).map_err(|_| {
            ParseError::Mismatch(input.error(String::from("expecting integer")).into())
        })
    }
}

pub enum VarRange {
    Single(Identifier),
    Range(Identifier, Identifier),
    All,
}

impl Debug for VarRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Single(var) => write!(f, "{var:?}"),
            Self::Range(from, to) => write!(f, "{from:?} TO {to:?}"),
            Self::All => write!(f, "ALL"),
        }
    }
}

impl FromTokens for VarRange {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        if let Ok(Parsed { rest, .. }) = parse_token(input, &Token::Punct(Punct::All)) {
            Ok(Parsed::ok(Self::All, rest))
        } else {
            let (from, rest, _) = parse_id(input)?.into_tuple();
            if let Ok(Parsed { rest, .. }) = parse_token(&rest, &Token::Punct(Punct::To)) {
                if let Ok(p) = parse_id(&rest) {
                    return Ok(p.map(|to| Self::Range(from, to)));
                }
            }
            Ok(Parsed::ok(Self::Single(from), rest))
        }
    }
}

fn parse_id(input: &TokenSlice) -> ParseResult<Identifier> {
    let mut iter = input.iter();
    if let Some(LexToken {
        token: Token::Id(id),
        ..
    }) = iter.next()
    {
        Ok(Parsed::ok(id.clone(), iter.remainder()))
    } else {
        Err(ParseError::Mismatch(
            input.error("Syntax error expecting identifier.").into(),
        ))
    }
}

fn parse_format(input: &TokenSlice) -> ParseResult<AbstractFormat> {
    let mut iter = input.iter();
    if let Some(LexToken {
        token: Token::Id(id),
        ..
    }) = iter.next()
    {
        if let Ok(format) = id.0.as_ref().parse() {
            return Ok(Parsed::ok(format, iter.remainder()));
        }
    }
    Err(ParseError::Mismatch(
        input.error("Syntax error expecting identifier.").into(),
    ))
}

fn parse_string(input: &TokenSlice) -> ParseResult<String> {
    let mut iter = input.iter();
    if let Some(LexToken {
        token: Token::String(s),
        ..
    }) = iter.next()
    {
        Ok(Parsed::ok(s.clone(), iter.remainder()))
    } else {
        Err(ParseError::Mismatch(
            input.error("Syntax error expecting identifier.").into(),
        ))
    }
}

impl FromTokens for Identifier {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        parse_id(input)
    }
}

impl FromTokens for String {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        parse_string(input)
    }
}

impl FromTokens for AbstractFormat {
    fn from_tokens(input: &TokenSlice) -> ParseResult<Self>
    where
        Self: Sized,
    {
        parse_format(input)
    }
}

fn collect_subcommands(src: TokenSlice) -> Vec<TokenSlice> {
    src.split(|token| token.token == Token::Punct(Punct::Slash))
        .filter(|slice| !slice.is_empty())
        .collect()
}

fn commands() -> &'static [Command] {
    fn new_commands() -> Vec<Command> {
        vec![
            descriptives_command(),
            crosstabs_command(),
            ctables_command(),
            data_list_command(),
            Command {
                allowed_states: FlagSet::full(),
                enhanced_only: false,
                testing_only: false,
                no_abbrev: false,
                name: "ECHO",
                run: Box::new(|_context| todo!()),
            },
        ]
    }

    static COMMANDS: OnceLock<Vec<Command>> = OnceLock::new();
    COMMANDS.get_or_init(new_commands).as_slice()
}

fn parse_command_word(lexer: &mut TokenSlice, s: &mut String, n: usize) -> bool {
    let separator = match s.chars().next_back() {
        Some(c) if c != '-' => " ",
        _ => "",
    };

    match lexer.get_token(n) {
        Some(Token::Punct(Punct::Dash)) => {
            s.push('-');
            true
        }
        Some(Token::Id(id)) => {
            write!(s, "{separator}{id}").unwrap();
            true
        }
        Some(Token::Number(number)) if number.is_sign_positive() => {
            if let Some(integer) = number.to_exact_usize() {
                write!(s, "{separator}{integer}").unwrap();
                true
            } else {
                false
            }
        }
        _ => false,
    }
}

fn find_best_match(s: &str) -> (Option<&'static Command>, isize) {
    let mut cm = CommandMatcher::new(s);
    for command in commands() {
        cm.add(command.name, command);
    }
    cm.get_match()
}

fn parse_command_name(
    lexer: &mut TokenSlice,
    error: &dyn Fn(Diagnostic),
) -> Result<(&'static Command, usize), ()> {
    let mut s = String::new();
    let mut word = 0;
    let mut missing_words = 0;
    let mut command = None;
    while parse_command_word(lexer, &mut s, word) {
        (command, missing_words) = find_best_match(&s);
        if missing_words <= 0 {
            break;
        }
        word += 1;
    }
    if command.is_none() && missing_words > 0 {
        s.push_str(" .");
        (command, missing_words) = find_best_match(&s);
        s.truncate(s.len() - 2);
    }

    match command {
        Some(command) => Ok((command, ((word as isize + 1) + missing_words) as usize)),
        None => {
            if word == 0 {
                error(
                    lexer
                        .subslice(0..1)
                        .error("Syntax error expecting command name"),
                )
            } else {
                error(lexer.subslice(0..word + 1).error("Unknown command `{s}`."))
            };
            Err(())
        }
    }
}

pub enum Success {
    Success,
    Eof,
    Finish,
}

pub fn end_of_command(context: &Context, range: RangeFrom<usize>) -> Result<Success, ()> {
    match context.lexer.get_token(range.start) {
        None | Some(Token::End) => Ok(Success::Success),
        _ => {
            context.error(
                context
                    .lexer
                    .subslice(range.start..context.lexer.len())
                    .error("Syntax error expecting end of command."),
            );
            Err(())
        }
    }
}

fn parse_in_state(mut lexer: TokenSlice, error: &dyn Fn(Diagnostic), _state: State) {
    match lexer.get_token(0) {
        None | Some(Token::End) => (),
        _ => match parse_command_name(&mut lexer, error) {
            Ok((command, n_tokens)) => {
                let mut context = Context {
                    error,
                    lexer: lexer.subslice(n_tokens..lexer.len()),
                    command_name: Some(command.name),
                };
                (command.run)(&mut context);
            }
            Err(error) => println!("{error:?}"),
        },
    }
}

pub fn parse_command(lexer: TokenSlice, error: &dyn Fn(Diagnostic)) {
    parse_in_state(lexer, error, State::Initial)
}

pub struct Context<'a> {
    error: &'a dyn Fn(Diagnostic),
    lexer: TokenSlice,
    command_name: Option<&'static str>,
}

impl Context<'_> {
    pub fn error(&self, diagnostic: Diagnostic) {
        (self.error)(diagnostic);
    }
}
