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

use crate::{
    calendar::{calendar_gregorian_to_offset, DateError},
    dictionary::Datum,
    endian::{Endian, Parse},
    format::{DateTemplate, Decimals, Settings, TemplateItem, Type},
    settings::{EndianSettings, Settings as PsppSettings},
    sys::raw::{EncodedStr, EncodedString},
};
use encoding_rs::Encoding;
use smallstr::SmallString;
use std::{
    fmt::{Display, Write},
    str::FromStr,
};
use thiserror::Error as ThisError;

#[derive(Clone, Debug)]
pub struct ParseError {
    type_: Type,
    input: EncodedString,
    kind: ParseErrorKind,
}

impl std::error::Error for ParseError {}

impl Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{} cannot be parsed as {}: {}",
            self.input.borrowed().quoted(),
            &self.type_,
            &self.kind
        )
    }
}

#[derive(ThisError, Clone, Debug, PartialEq, Eq)]
enum ParseErrorKind {
    /// Input is not numeric.
    #[error("Input is not numeric.")]
    NotNumeric,

    /// Invalid numeric systax.
    #[error("Invalid numeric systax.")]
    InvalidNumericSyntax,

    /// Field contains unexpected non-digit.
    #[error("Field contains unexpected non-digit {0:?}.")]
    Nondigit(char),

    /// Field contains unexpected non-hex digit.
    #[error("Field contains unexpected non-hex digit {0:?}.")]
    NonHexDigit(char),

    /// Field contains odd number of hex digits.
    #[error("Field contains {0:?} hex digits but only an even number is allowed.")]
    OddLength(usize),

    /// Field contains invalid BCD digit.
    #[error("Field contains invalid BCD digit ({0:?}).")]
    NonBDCDigit(u8),

    /// Invalid BCD sign.
    #[error("Invalid BCD sign 0x{0:x}.")]
    InvalidBCDSign(u8),

    /// Day must be between 1 and 31.
    #[error("Day ({0}) must be between 1 and 31.")]
    InvalidDay(i32),

    /// Syntax error in date field.
    #[error("Syntax error in date field.")]
    DateSyntax,

    /// Julian day must have exactly three digits.
    #[error("Julian day must have exactly three digits.")]
    InvalidYDayLen,

    /// Julian day must be between 1 and 366, inclusive.
    #[error("Julian day ({0}) must be between 1 and 366, inclusive.")]
    InvalidYDay(i32),

    /// Quarter must be between 1 and 4, inclusive.
    #[error("Quarter ({0}) must be between 1 and 4, inclusive.")]
    InvalidQuarter(i32),

    /// Week must be between 1 and 53, inclusive.
    #[error("Week ({0}) must be between 1 and 53, inclusive.")]
    InvalidWeek(i32),

    /// Unrecognized month format.
    #[error("Unrecognized month format.  Months may be specified as Arabic or Roman numerals or as at least 3 letters of their English names.")]
    InvalidMonth,

    /// Delimiter expected between fields in time.
    #[error("Delimiter expected between fields in time.")]
    ExpectedTimeDelimiter,

    /// Delimiter expected between fields in date.
    #[error("Delimiter expected between fields in date.")]
    ExpectedDateDelimiter,

    /// Minute must be between 0 and 59, inclusive.
    #[error("Minute ({0}) must be between 0 and 59, inclusive.")]
    InvalidMinute(i32),

    /// Invalid weekday name.
    #[error("Unrecognized weekday name.  At least the first two letters of an English weekday name must be specified.")]
    InvalidWeekdayName,

    /// Expected character.
    #[error("{0:?} expected in date field.")]
    ExpectedChar(char),

    /// Trailing garbage.
    #[error("Trailing garbage {0:?} follows date.")]
    TrailingGarbage(String),

    /// Invalid date.
    #[error("{0}")]
    InvalidDate(#[from] DateError),

    /// Invalid zoned decimal (Z) syntax.
    #[error("Invalid zoned decimal (Z) syntax.")]
    InvalidZ,
}

pub struct ParseValue<'a> {
    type_: Type,
    settings: &'a Settings,
    endian: EndianSettings,
    implied_decimals: Option<Decimals>,
    output_encoding: &'static Encoding,
}

impl Type {
    pub fn parser(&self, output_encoding: &'static Encoding) -> ParseValue<'static> {
        ParseValue::new(*self, output_encoding)
    }
}

impl ParseValue<'static> {
    pub fn new(type_: Type, output_encoding: &'static Encoding) -> Self {
        let settings = PsppSettings::global();
        Self {
            type_,
            settings: &settings.formats,
            endian: settings.endian,
            implied_decimals: None,
            output_encoding,
        }
    }
}

impl<'a> ParseValue<'a> {
    pub fn with_settings(self, settings: &'a Settings) -> Self {
        Self { settings, ..self }
    }
    pub fn with_endian(self, endian: EndianSettings) -> Self {
        Self { endian, ..self }
    }
    pub fn with_implied_decimals(self, d: Decimals) -> Self {
        Self {
            implied_decimals: if d > 0 { Some(d) } else { None },
            ..self
        }
    }

    /// Parses `input`.
    ///
    /// # Input encoding
    ///
    /// Be careful about the encoding of `input`.  It's tempting to recode all
    /// input into UTF-8, but this will screw up parsing of binary formats,
    /// because recoding bytes from (e.g.) windows-1252 into UTF-8, and then
    /// interpreting them as a binary number yields nonsense.
    pub fn parse<'b, T>(&self, input: T) -> Result<Datum, ParseError>
    where
        T: Into<EncodedStr<'b>>,
    {
        let input: EncodedStr = input.into();
        if input.is_empty() {
            return Ok(self.type_.default_value());
        }
        match self.type_ {
            Type::F | Type::Comma | Type::Dot | Type::Dollar | Type::Pct | Type::E => {
                self.parse_number(&input.as_str(), self.type_)
            }
            Type::CC(_) => self.parse_number(&input.as_str(), Type::F),
            Type::N => self.parse_n(&input.as_str()),
            Type::Z => self.parse_z(&input.as_str()),
            Type::PIBHex => self.parse_pibhex(&input.as_str()),
            Type::RBHex => self.parse_rbhex(&input.as_str()),
            Type::Date
            | Type::ADate
            | Type::EDate
            | Type::JDate
            | Type::SDate
            | Type::QYr
            | Type::MoYr
            | Type::WkYr
            | Type::DateTime
            | Type::YmdHms
            | Type::MTime
            | Type::Time
            | Type::DTime => self.parse_date(&input.as_str()),
            Type::WkDay => self.parse_wkday(&input.as_str()),
            Type::Month => self.parse_month(&input.as_str()),
            Type::P => self.parse_p(input.as_bytes()),
            Type::PK => self.parse_pk(input.as_bytes()),
            Type::IB => self.parse_ib(input.as_bytes()),
            Type::PIB => self.parse_pib(input.as_bytes()),
            Type::RB => self.parse_rb(input.as_bytes()),
            Type::A => Ok(Datum::String(
                input.to_encoding(self.output_encoding).into(),
            )),
            Type::AHex => self.parse_ahex(&input.as_str()),
        }
        .map_err(|kind| ParseError {
            type_: self.type_,
            input: input.into(),
            kind,
        })
    }

    fn parse_number(&self, input: &str, type_: Type) -> Result<Datum, ParseErrorKind> {
        let style = self.settings.number_style(type_);

        let input = input.trim();
        if input.is_empty() || input == "." {
            return Ok(Datum::sysmis());
        }
        let mut p = StrParser::new(input.trim());
        fn strip_integer(mut input: &str, grouping: Option<char>) -> &str {
            while let Some(rest) = input.strip_prefix(|c: char| c.is_ascii_digit()) {
                let rest = if let Some(grouping) = grouping {
                    rest.strip_prefix(grouping).unwrap_or(rest)
                } else {
                    rest
                };
                input = rest;
            }
            input
        }

        if p.strip_prefix(&style.prefix.s) {
            p.strip_ws();
        }
        let sign = p.strip_one_of(&['-', '+']).inspect(|_| p.strip_ws());
        if sign.is_some() && p.strip_prefix(&style.prefix.s) {
            p.strip_ws();
        }
        let integer = p.advance(strip_integer(p.0, style.grouping.map(char::from)));
        let decimals = if p.strip_prefix(style.decimal.as_str()) {
            p.strip_matches(|c| c.is_ascii_digit())
        } else {
            ""
        };
        let (exp_sign, exponent) = if p.0.starts_with(['e', 'E', 'd', 'D', '+', '-']) {
            let _e = p
                .strip_one_of(&['e', 'E', 'd', 'D'])
                .inspect(|_| p.strip_ws());
            let exp_sign = p.strip_one_of(&['-', '+']).inspect(|_| p.strip_ws());
            let exponent = p.strip_matches(|c| c.is_ascii_digit());
            (exp_sign, exponent)
        } else {
            (None, "")
        };
        if p.strip_prefix(&style.suffix.s) {
            p.strip_ws();
        }

        if !p.0.is_empty() {
            return Err(ParseErrorKind::NotNumeric);
        }

        let mut number = SmallString::<[u8; 64]>::new();
        if let Some(sign) = sign {
            number.push(sign);
        }
        number.extend(integer.chars().filter(|c| c.is_ascii_digit()));
        if !decimals.is_empty() {
            write!(&mut number, ".{decimals}").unwrap();
        }
        if !exponent.is_empty() {
            number.push('e');
            if let Some(exp_sign) = exp_sign {
                number.push(exp_sign);
            }
            write!(&mut number, "{exponent}").unwrap();
        }

        match f64::from_str(&number) {
            Ok(value) => Ok(Datum::Number(Some(value))),
            Err(_) => Err(ParseErrorKind::InvalidNumericSyntax),
        }
    }

    fn parse_n(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        match input.chars().find(|c| !c.is_ascii_digit()) {
            None => Ok(Datum::Number(Some(input.parse().unwrap()))),
            Some(nondigit) => Err(ParseErrorKind::Nondigit(nondigit)),
        }
    }

    fn parse_z(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        let input = input.trim();
        if input.is_empty() || input == "." {
            return Ok(Datum::sysmis());
        }

        enum ZChar {
            Digit(u32),
            SignedDigit(u32, Sign),
            Dot,
            Invalid,
        }

        impl From<char> for ZChar {
            fn from(c: char) -> Self {
                match c {
                    '0'..='9' => ZChar::Digit(c as u32 - '0' as u32),
                    '{' => ZChar::SignedDigit(0, Sign::Positive),
                    'A'..='I' => ZChar::SignedDigit(c as u32 - 'A' as u32 + 1, Sign::Positive),
                    '}' => ZChar::SignedDigit(0, Sign::Negative),
                    'J'..='R' => ZChar::SignedDigit(c as u32 - 'J' as u32 + 1, Sign::Negative),
                    '.' => ZChar::Dot,
                    _ => ZChar::Invalid,
                }
            }
        }

        let mut number = SmallString::<[u8; 40]>::new();
        let mut sign = None;
        let mut dot = false;
        for c in input.chars().map(ZChar::from) {
            match c {
                ZChar::Digit(digit) if sign.is_none() => {
                    number.push(char::from_digit(digit, 10).unwrap());
                }
                ZChar::SignedDigit(digit, s) if sign.is_none() => {
                    assert!(digit < 10, "{digit}");
                    number.push(char::from_digit(digit, 10).unwrap());
                    sign = Some(s);
                }
                ZChar::Dot if !dot => {
                    number.push('.');
                    dot = true;
                }
                _ => return Err(ParseErrorKind::InvalidZ),
            }
        }
        match self.implied_decimals {
            Some(d) if !dot && d > 0 => write!(&mut number, "e-{d}").unwrap(),
            _ => (),
        }
        let number = number.parse::<f64>().unwrap();
        let number = if sign == Some(Sign::Negative) {
            -number
        } else {
            number
        };
        Ok(Datum::Number(Some(number)))
    }

    fn parse_bcd(input: &[u8]) -> Result<u128, ParseErrorKind> {
        let mut value = 0;
        for byte in input.iter().copied() {
            let hi = nibble(byte >> 4)?;
            let lo = nibble(byte & 0x0f)?;
            value = value * 100 + hi * 10 + lo;
        }
        Ok(value)
    }

    fn apply_decimals(&self, number: f64) -> f64 {
        match self.implied_decimals {
            Some(d) if d > 0 => number / 10.0f64.powi(d as i32),
            _ => number,
        }
    }

    fn parse_pk(&self, input: &[u8]) -> Result<Datum, ParseErrorKind> {
        let number = Self::parse_bcd(input)?;
        Ok(Datum::Number(Some(self.apply_decimals(number as f64))))
    }

    fn parse_p(&self, input: &[u8]) -> Result<Datum, ParseErrorKind> {
        if input.is_empty() {
            return Ok(Datum::Number(None));
        };
        let (head, tail) = input.split_at(input.len() - 1);
        let number = Self::parse_bcd(head)?;
        let number = number * 10 + nibble(tail[0] >> 4)?;
        let number = match tail[0] & 0x0f {
            0xf => number as f64,
            0xd => -(number as f64),
            other => return Err(ParseErrorKind::InvalidBCDSign(other)),
        };
        Ok(Datum::Number(Some(self.apply_decimals(number))))
    }

    fn parse_binary(&self, input: &[u8]) -> u128 {
        match self.endian.input {
            Endian::Big => input.iter().fold(0, |acc, b| (acc << 8) + *b as u128),
            Endian::Little => input.iter().rev().fold(0, |acc, b| (acc << 8) + *b as u128),
        }
    }

    fn parse_ib(&self, input: &[u8]) -> Result<Datum, ParseErrorKind> {
        let number = self.parse_binary(input);
        let sign_bit = 1 << (input.len() * 8 - 1);
        let number = if (number & sign_bit) == 0 {
            number as i128
        } else {
            -(number.wrapping_sub(sign_bit << 1) as i128)
        };
        Ok(Datum::Number(Some(self.apply_decimals(number as f64))))
    }

    fn parse_pib(&self, input: &[u8]) -> Result<Datum, ParseErrorKind> {
        let number = self.parse_binary(input);
        Ok(Datum::Number(Some(self.apply_decimals(number as f64))))
    }

    fn parse_rb(&self, input: &[u8]) -> Result<Datum, ParseErrorKind> {
        let mut bytes = [0; 8];
        let len = input.len().min(8);
        bytes[..len].copy_from_slice(&input[..len]);
        let bits: u64 = self.endian.input.parse(bytes);

        const SYSMIS: f64 = -f64::MAX;
        let number = match f64::from_bits(bits) {
            SYSMIS => None,
            other => Some(other),
        };
        Ok(Datum::Number(number))
    }

    fn parse_ahex(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        let mut result = Vec::with_capacity(input.len() / 2);
        let mut iter = input.chars();
        while let Some(hi) = iter.next() {
            let Some(lo) = iter.next() else {
                return Err(ParseErrorKind::OddLength(input.len()));
            };
            let Some(hi) = hi.to_digit(16) else {
                return Err(ParseErrorKind::NonHexDigit(hi));
            };
            let Some(lo) = lo.to_digit(16) else {
                return Err(ParseErrorKind::NonHexDigit(lo));
            };
            result.push((hi * 16 + lo) as u8);
        }
        Ok(Datum::String(result.into()))
    }

    fn parse_hex(&self, input: &str) -> Result<Option<u64>, ParseErrorKind> {
        let input = input.trim();
        if input.is_empty() || input == "." {
            Ok(None)
        } else if let Ok(value) = u64::from_str_radix(input, 16) {
            Ok(Some(value))
        } else {
            let c = input.chars().find(|c| !c.is_ascii_hexdigit()).unwrap();
            Err(ParseErrorKind::NonHexDigit(c))
        }
    }

    fn parse_pibhex(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        self.parse_hex(input)
            .map(|value| Datum::Number(value.map(|number| number as f64)))
    }

    fn parse_rbhex(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        self.parse_hex(input)
            .map(|value| Datum::Number(value.map(f64::from_bits)))
    }

    fn parse_date(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        let mut p = StrParser(input.trim());
        if p.0.is_empty() || p.0 == "." {
            return Ok(Datum::sysmis());
        }

        let mut day = 1;
        let mut yday = 1;
        let mut month = 1;
        let mut year = None;
        let mut time_sign = None;
        let mut time = 0.0;

        let mut iter = DateTemplate::new(self.type_, 0).unwrap();
        let template_width = iter.len();
        while let Some(TemplateItem { c, n }) = iter.next() {
            match c {
                'd' if n < 3 => {
                    day = parse_day(&mut p)?;
                }
                'd' => {
                    yday = parse_yday(&mut p)?;
                }
                'm' => {
                    month = parse_month(&mut p)?;
                }
                'y' => {
                    let max_digits = if !iter
                        .clone()
                        .next()
                        .is_some_and(|item| item.c.is_ascii_alphabetic())
                    {
                        usize::MAX
                    } else if p.0.len() >= template_width + 2 {
                        4
                    } else {
                        2
                    };
                    year = Some(parse_year(&mut p, self.settings, max_digits)?);
                }
                'q' => month = parse_quarter(&mut p)?,
                'w' => yday = parse_week(&mut p)?,
                'D' => {
                    time_sign = Some(parse_sign(&mut p, time_sign));
                    time += parse_time(&mut p)? * 60.0 * 60.0 * 24.0;
                }
                'H' => {
                    time_sign = Some(parse_sign(&mut p, time_sign));
                    time += parse_time(&mut p)? * 60.0 * 60.0;
                }
                'M' => {
                    if self.type_ == Type::MTime {
                        time_sign = Some(parse_sign(&mut p, time_sign));
                    }
                    time += self.parse_minute_second(&mut p)?;
                }
                '-' | '/' | '.' => parse_date_delimiter(&mut p)?,
                ':' => parse_time_delimiter(&mut p)?,
                ' ' => {
                    if self.type_ != Type::MoYr {
                        p.strip_ws();
                    } else {
                        parse_date_delimiter(&mut p)?
                    }
                }
                c => {
                    debug_assert_eq!(n, 1);
                    if p.strip_one_of(&[c.to_ascii_uppercase(), c.to_ascii_lowercase()])
                        .is_none()
                    {
                        return Err(ParseErrorKind::ExpectedChar(c));
                    }
                }
            }
        }
        parse_trailer(&mut p)?;

        let date = if let Some(year) = year {
            let date = calendar_gregorian_to_offset(year, month, day, self.settings)? + yday - 1;
            date as f64 * 60.0 * 60.0 * 24.0
        } else {
            0.0
        };
        let time_date = if time_sign == Some(Sign::Negative) {
            date - time
        } else {
            date + time
        };
        Ok(Datum::Number(Some(time_date)))
    }

    fn parse_minute_second(&self, p: &mut StrParser<'_>) -> Result<f64, ParseErrorKind> {
        let minute = parse_int::<i32>(p)?;
        if self.type_ != Type::MTime && !(0..=59).contains(&minute) {
            return Err(ParseErrorKind::InvalidMinute(minute));
        }
        let time = minute as f64 * 60.0;

        if parse_time_delimiter(p).is_err() || !p.0.starts_with(|c: char| c.is_ascii_digit()) {
            return Ok(time);
        }
        let integer = p.strip_matches(|c| c.is_ascii_digit());
        let fraction = if p.strip_prefix(self.settings.decimal.as_str()) {
            p.strip_matches(|c| c.is_ascii_digit())
        } else {
            ""
        };

        let mut number = SmallString::<[u8; 40]>::new();
        number.push_str(integer);
        number.push('.');
        number.push_str(fraction);
        let seconds = number.parse::<f64>().unwrap();
        Ok(time + seconds)
    }

    fn parse_wkday(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        let mut p = StrParser(input.trim());
        if p.0.is_empty() || p.0 == "." {
            Ok(Datum::sysmis())
        } else {
            let weekday = parse_weekday(&mut p)?;
            parse_trailer(&mut p)?;
            Ok(Datum::Number(Some(weekday as f64)))
        }
    }

    fn parse_month(&self, input: &str) -> Result<Datum, ParseErrorKind> {
        let mut p = StrParser(input.trim());
        if p.0.is_empty() || p.0 == "." {
            Ok(Datum::sysmis())
        } else {
            let month = parse_month(&mut p)?;
            parse_trailer(&mut p)?;
            Ok(Datum::Number(Some(month as f64)))
        }
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
enum Sign {
    #[default]
    Positive,
    Negative,
}

fn parse_trailer(p: &mut StrParser<'_>) -> Result<(), ParseErrorKind> {
    p.strip_ws();
    if p.0.is_empty() {
        Ok(())
    } else {
        Err(ParseErrorKind::TrailingGarbage(p.0.into()))
    }
}

fn parse_sign(p: &mut StrParser<'_>, sign: Option<Sign>) -> Sign {
    if let Some(sign) = sign {
        sign
    } else if p.strip_one_of(&['-', '+']) == Some('-') {
        Sign::Negative
    } else {
        Sign::Positive
    }
}

fn parse_time(p: &mut StrParser<'_>) -> Result<f64, ParseErrorKind> {
    let number = parse_int::<i32>(p)?;
    if number < 0 {
        return Err(ParseErrorKind::DateSyntax);
    }
    Ok(number as f64)
}

fn parse_day(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    let day = parse_int::<i32>(p)?;
    if (1..=31).contains(&day) {
        Ok(day)
    } else {
        Err(ParseErrorKind::InvalidDay(day))
    }
}

fn parse_yday(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    let Some(s) = p.0.get(..3) else {
        return Err(ParseErrorKind::InvalidYDayLen);
    };
    if !s.chars().all(|c| c.is_ascii_digit()) {
        return Err(ParseErrorKind::InvalidYDayLen);
    }
    let yday = s.parse().unwrap();
    if !(1..=366).contains(&yday) {
        return Err(ParseErrorKind::InvalidYDay(yday));
    }
    p.0 = &p.0[3..];
    Ok(yday)
}

fn parse_month(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    if p.0.starts_with(|c: char| c.is_ascii_digit()) {
        let month = parse_int(p)?;
        if (1..=12).contains(&month) {
            return Ok(month);
        }
    } else {
        let name = p.strip_matches(|c| c.is_ascii_alphabetic());

        static ENGLISH_NAMES: [&str; 12] = [
            "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
        ];
        if let Some(month) = match_name(&name[..3.min(name.len())], &ENGLISH_NAMES) {
            return Ok(month);
        }

        static ROMAN_NAMES: [&str; 12] = [
            "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii",
        ];
        if let Some(month) = match_name(&name[..4.min(name.len())], &ROMAN_NAMES) {
            return Ok(month);
        }
    }
    Err(ParseErrorKind::InvalidMonth)
}

fn parse_weekday(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    static WEEKDAY_NAMES: [&str; 7] = ["su", "mo", "tu", "we", "th", "fr", "sa"];
    let name = p.strip_matches(|c| c.is_ascii_alphabetic());
    name.get(..2)
        .and_then(|name| match_name(name, &WEEKDAY_NAMES))
        .ok_or(ParseErrorKind::InvalidWeekdayName)
}

fn parse_quarter(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    match parse_int(p)? {
        quarter @ 1..=4 => Ok((quarter - 1) * 3 + 1),
        other => Err(ParseErrorKind::InvalidQuarter(other)),
    }
}

fn parse_week(p: &mut StrParser<'_>) -> Result<i32, ParseErrorKind> {
    match parse_int(p)? {
        week @ 1..=53 => Ok((week - 1) * 7 + 1),
        other => Err(ParseErrorKind::InvalidWeek(other)),
    }
}

fn parse_time_delimiter(p: &mut StrParser<'_>) -> Result<(), ParseErrorKind> {
    let delimiter = p.strip_matches(|c| c == ':' || c.is_ascii_whitespace());
    if !delimiter.is_empty() {
        Ok(())
    } else {
        Err(ParseErrorKind::ExpectedTimeDelimiter)
    }
}

fn parse_date_delimiter(p: &mut StrParser<'_>) -> Result<(), ParseErrorKind> {
    let delimiter = p
        .strip_matches(|c| c == '-' || c == '/' || c == '.' || c == ',' || c.is_ascii_whitespace());
    if !delimiter.is_empty() {
        Ok(())
    } else {
        Err(ParseErrorKind::ExpectedDateDelimiter)
    }
}

fn match_name(name: &str, candidates: &[&str]) -> Option<i32> {
    for (index, candidate) in candidates.iter().enumerate() {
        if candidate.eq_ignore_ascii_case(name) {
            return Some(index as i32 + 1);
        }
    }
    None
}

fn parse_year(
    p: &mut StrParser<'_>,
    settings: &Settings,
    max_digits: usize,
) -> Result<i32, ParseErrorKind> {
    let head = p.clone().strip_matches(|c| c.is_ascii_digit());
    let head = if head.len() > max_digits {
        head.get(..max_digits).ok_or(ParseErrorKind::DateSyntax)?
    } else {
        head
    };

    let year = head
        .parse::<i32>()
        .map_err(|_| ParseErrorKind::DateSyntax)?;
    p.0 = &p.0[head.len()..];
    Ok(settings.epoch.apply(year))
}

fn parse_int<T>(p: &mut StrParser<'_>) -> Result<T, ParseErrorKind>
where
    T: FromStr,
{
    let mut tmp = *p;
    tmp.strip_one_of(&['+', '-']).inspect(|_| tmp.strip_ws());
    tmp.strip_matches(|c| c.is_ascii_digit());
    let number = p
        .up_to(tmp.0)
        .parse::<T>()
        .map_err(|_| ParseErrorKind::DateSyntax)?;
    *p = tmp;
    Ok(number)
}

#[derive(Copy, Clone, Debug)]
pub struct StrParser<'a>(pub &'a str);

impl<'a> StrParser<'a> {
    pub fn new(s: &'a str) -> Self {
        Self(s)
    }

    pub fn strip_prefix(&mut self, prefix: &'a str) -> bool {
        if prefix.is_empty() {
            false
        } else if let Some(rest) = self.0.strip_prefix(prefix) {
            self.0 = rest;
            true
        } else {
            false
        }
    }

    fn strip_one_of(&mut self, chars: &[char]) -> Option<char> {
        let mut iter = self.0.chars();
        match iter.next() {
            Some(c) if chars.contains(&c) => {
                self.0 = iter.as_str();
                Some(c)
            }
            _ => None,
        }
    }

    fn strip_matches(&mut self, f: impl Fn(char) -> bool) -> &'a str {
        self.advance(self.0.trim_start_matches(f))
    }

    fn strip_ws(&mut self) {
        self.0 = self.0.trim_start();
    }

    fn advance(&mut self, rest: &'a str) -> &'a str {
        let head = self.up_to(rest);
        self.0 = rest;
        head
    }

    fn up_to(&self, rest: &'a str) -> &'a str {
        &self.0[..self.0.len() - rest.len()]
    }
}

/*
#[derive(Copy, Clone, Debug)]
pub struct ByteParser<'a>(pub &'a [u8]);

impl<'a> ByteParser<'a> {
    pub fn new(s: &'a [u8]) -> Self {
        Self(s)
    }

    pub fn strip_prefix(&mut self, prefix: &'a [u8]) -> bool {
        if prefix.is_empty() {
            false
        } else if let Some(rest) = self.0.strip_prefix(prefix) {
            self.0 = rest;
            true
        } else {
            false
        }
    }

    fn strip_one_of(&mut self, chars: &[char]) -> Option<char> {
        let mut iter = self.0.iter();
        match iter.next() {
            Some(c) if chars.contains(&c) => {
                self.0 = iter.as_str();
                Some(c)
            }
            _ => None,
        }
    }

    fn strip_matches(&mut self, f: impl Fn(char) -> bool) -> &'a [u8] {
        self.advance(self.0.trim_start_matches(f))
    }

    fn strip_ws(&mut self) {
        self.0 = self.0.trim_start();
    }

    fn advance(&mut self, rest: &'a [u8]) -> &'a [u8] {
        let head = self.up_to(rest);
        self.0 = rest;
        head
    }

    fn up_to(&self, rest: &'a [u8]) -> &'a [u8] {
        &self.0[..self.0.len() - rest.len()]
    }
}*/

fn nibble(b: u8) -> Result<u128, ParseErrorKind> {
    if b < 10 {
        Ok(b as u128)
    } else {
        Err(ParseErrorKind::NonBDCDigit(b))
    }
}

#[cfg(test)]
mod test {
    use std::{
        fs::File,
        io::{BufRead, BufReader},
        path::Path,
    };

    use encoding_rs::UTF_8;
    use rand::random;

    use crate::{
        calendar::{days_in_month, is_leap_year},
        dictionary::Datum,
        endian::Endian,
        format::{
            parse::{ParseError, ParseErrorKind, Sign},
            Epoch, Format, Settings as FormatSettings, Type,
        },
        settings::EndianSettings,
        sys::raw::EncodedStr,
    };

    fn test(name: &str, type_: Type) {
        let base = Path::new(env!("CARGO_MANIFEST_DIR")).join("src/format/testdata/parse");
        let input_stream = BufReader::new(File::open(base.join("num-in.txt")).unwrap());
        let expected_stream = BufReader::new(File::open(base.join(name)).unwrap());
        for ((input, expected), line_number) in input_stream
            .lines()
            .map(|result| result.unwrap())
            .zip(expected_stream.lines().map(|result| result.unwrap()))
            .zip(1..)
        {
            let result = type_.parser(UTF_8).parse(&input);
            let error = result.clone().err();
            let value = result
                .unwrap_or(type_.default_value())
                .display(Format::new(Type::F, 10, 4).unwrap(), UTF_8)
                .to_string();
            if value != expected {
                panic!(
                    "parsing {input:?} as {type_} failed ({name}:{line_number}):\n     got: {value:?}\nexpected: {expected:?}\ndecode error: {error:?}",
                );
            }
        }
    }

    #[test]
    fn f() {
        test("f.txt", Type::F);
    }

    #[test]
    fn e() {
        test("e.txt", Type::E);
    }

    #[test]
    fn comma() {
        test("comma.txt", Type::Comma);
    }

    #[test]
    fn dot() {
        test("dot.txt", Type::Dot);
    }

    #[test]
    fn dollar() {
        test("dollar.txt", Type::Dollar);
    }

    #[test]
    fn pct() {
        test("pct.txt", Type::Pct);
    }

    #[derive(Clone, Debug)]
    struct TestDate {
        year: i32,
        month: i32,
        day: i32,
        yday: i32,
        hour: i32,
        minute: i32,
        second: i32,
    }

    impl TestDate {
        const fn new(
            year: i32,
            month: i32,
            day: i32,
            yday: i32,
            hour: i32,
            minute: i32,
            second: i32,
        ) -> Self {
            Self {
                year,
                month,
                day,
                yday,
                hour,
                minute,
                second,
            }
        }
    }

    #[derive(Copy, Clone, Debug)]
    struct ExpectDate {
        year: i32,
        month: i32,
        day: i32,
        time: i32,
        sign: Sign,
    }

    impl Default for ExpectDate {
        fn default() -> Self {
            Self {
                year: 0,
                month: 0,
                day: 1,
                time: 0,
                sign: Sign::default(),
            }
        }
    }

    struct DateTester<'a> {
        date: TestDate,
        template: &'a str,
        formatted: String,
        type_: Type,
    }

    impl DateTester<'_> {
        fn visit(&self, extra: &str, mut expected: ExpectDate) {
            let formatted = format!("{}{extra}", self.formatted);
            if !self.template.is_empty() {
                fn years(y: i32) -> Vec<i32> {
                    match y {
                        1930..2030 => vec![y, y % 100],
                        _ => vec![y],
                    }
                }
                let mut iter = self.template.chars();
                let first = iter.next().unwrap();
                let next = DateTester {
                    date: self.date.clone(),
                    template: iter.as_str(),
                    formatted: formatted.clone(),
                    type_: self.type_,
                };
                match first {
                    'd' => {
                        expected.day = self.date.day;
                        next.visit(&format!("{}", self.date.day), expected);
                        next.visit(&format!("{:02}", self.date.day), expected);
                    }
                    'm' => {
                        let m = self.date.month as usize - 1;
                        static ROMAN: [&str; 12] = [
                            "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi",
                            "xii",
                        ];
                        static ENGLISH: [&str; 12] = [
                            "january",
                            "february",
                            "march",
                            "april",
                            "may",
                            "june",
                            "july",
                            "august",
                            "september",
                            "october",
                            "november",
                            "december",
                        ];
                        let roman = ROMAN[m];
                        let english = ENGLISH[m];

                        expected.month = self.date.month;
                        next.visit(&format!("{}", self.date.month), expected);
                        next.visit(&format!("{:02}", self.date.month), expected);
                        next.visit(roman, expected);
                        next.visit(&roman.to_ascii_uppercase(), expected);
                        next.visit(english, expected);
                        next.visit(&english[..3], expected);
                        next.visit(&english.to_ascii_uppercase(), expected);
                        next.visit(&english[..3].to_ascii_uppercase(), expected);
                    }
                    'y' => {
                        expected.year = self.date.year;
                        for year in years(self.date.year) {
                            next.visit(&format!("{year}"), expected);
                        }
                    }
                    'j' => {
                        expected.year = self.date.year;
                        expected.month = self.date.month;
                        expected.day = self.date.day;
                        for year in years(self.date.year) {
                            next.visit(&format!("{year}{:03}", self.date.yday), expected);
                        }
                    }
                    'q' => {
                        let quarter = (self.date.month - 1) / 3 + 1;
                        let month = (quarter - 1) * 3 + 1;
                        next.visit(&format!("{}", quarter), ExpectDate { month, ..expected });
                    }
                    'w' => {
                        let week = (self.date.yday - 1) / 7 + 1;
                        expected.month = self.date.month;
                        expected.day = self.date.day - (self.date.yday - 1) % 7;
                        if expected.day < 1 {
                            expected.month -= 1;
                            expected.day += days_in_month(self.date.year, expected.month + 1);
                        }
                        next.visit(&format!("{week}"), expected);
                    }
                    'H' => {
                        expected.time += self.date.hour * 3600;
                        next.visit(&format!("{}", self.date.hour), expected);
                        next.visit(&format!("{:02}", self.date.hour), expected);
                    }
                    'M' => {
                        expected.time += self.date.minute * 60;
                        next.visit(&format!("{}", self.date.minute), expected);
                        next.visit(&format!("{:02}", self.date.minute), expected);
                    }
                    'S' => {
                        expected.time += self.date.second;
                        next.visit(&format!("{}", self.date.second), expected);
                        next.visit(&format!("{:02}", self.date.second), expected);
                    }
                    '-' => {
                        for c in b" -.,/" {
                            next.visit(&format!("{}", *c as char), expected);
                        }
                    }
                    ':' => {
                        for c in b" :" {
                            next.visit(&format!("{}", *c as char), expected);
                        }
                    }
                    ' ' => {
                        next.visit(" ", expected);
                    }
                    'Q' => {
                        for s in ["q", " q", "q ", " q "] {
                            for s in [String::from(s), s.to_ascii_uppercase()] {
                                next.visit(&s, expected);
                            }
                        }
                    }
                    'W' => {
                        for s in ["wk", " wk", "wk ", " wk "] {
                            for s in [String::from(s), s.to_ascii_uppercase()] {
                                next.visit(&s, expected);
                            }
                        }
                    }
                    '+' => {
                        next.visit("", expected);
                        next.visit(
                            "+",
                            ExpectDate {
                                sign: Sign::Positive,
                                ..expected
                            },
                        );
                        next.visit(
                            "-",
                            ExpectDate {
                                sign: Sign::Negative,
                                ..expected
                            },
                        );
                    }
                    _ => unreachable!(),
                }
            } else {
                assert!((1582..=2100).contains(&expected.year));
                assert!((1..=12).contains(&expected.month));
                assert!((1..=31).contains(&expected.day));

                let ExpectDate {
                    year,
                    month,
                    day,
                    time,
                    sign,
                }: ExpectDate = expected;

                const EPOCH: i32 = -577734; // 14 Oct 1582
                let expected = (EPOCH - 1 + 365 * (year - 1) + (year - 1) / 4 - (year - 1) / 100
                    + (year - 1) / 400
                    + (367 * month - 362) / 12
                    + if month <= 2 {
                        0
                    } else if month >= 2 && is_leap_year(year) {
                        -1
                    } else {
                        -2
                    }
                    + day) as i64
                    * 86400;
                let expected = if sign == Sign::Negative && expected > 0 {
                    expected - time as i64
                } else {
                    expected + time as i64
                };
                let settings = FormatSettings::default().with_epoch(Epoch(1930));
                let parsed = self
                    .type_
                    .parser(UTF_8)
                    .with_settings(&settings)
                    .parse(&formatted)
                    .unwrap();
                assert_eq!(parsed, Datum::Number(Some(expected as f64)));
            }
        }

        fn test(template: &str, type_: Type) {
            static DATES: [TestDate; 20] = [
                TestDate::new(1648, 6, 10, 162, 0, 0, 0),
                TestDate::new(1680, 6, 30, 182, 4, 50, 38),
                TestDate::new(1716, 7, 24, 206, 12, 31, 35),
                TestDate::new(1768, 6, 19, 171, 12, 47, 53),
                TestDate::new(1819, 8, 2, 214, 1, 26, 0),
                TestDate::new(1839, 3, 27, 86, 20, 58, 11),
                TestDate::new(1903, 4, 19, 109, 7, 36, 5),
                TestDate::new(1929, 8, 25, 237, 15, 43, 49),
                TestDate::new(1941, 9, 29, 272, 4, 25, 9),
                TestDate::new(1943, 4, 19, 109, 6, 49, 27),
                TestDate::new(1943, 10, 7, 280, 2, 57, 52),
                TestDate::new(1992, 3, 17, 77, 16, 45, 44),
                TestDate::new(1996, 2, 25, 56, 21, 30, 57),
                TestDate::new(1941, 9, 29, 272, 4, 25, 9),
                TestDate::new(1943, 4, 19, 109, 6, 49, 27),
                TestDate::new(1943, 10, 7, 280, 2, 57, 52),
                TestDate::new(1992, 3, 17, 77, 16, 45, 44),
                TestDate::new(1996, 2, 25, 56, 21, 30, 57),
                TestDate::new(2038, 11, 10, 314, 22, 30, 4),
                TestDate::new(2094, 7, 18, 199, 1, 56, 51),
            ];
            for date in &DATES {
                let visitor = DateTester {
                    date: date.clone(),
                    template,
                    formatted: "".to_string(),
                    type_,
                };
                visitor.visit("", ExpectDate::default());
            }
        }
    }

    #[test]
    fn date() {
        DateTester::test("d-m-y", Type::Date);
    }

    #[test]
    fn adate() {
        DateTester::test("m-d-y", Type::ADate);
    }

    #[test]
    fn edate() {
        DateTester::test("d-m-y", Type::EDate);
    }

    #[test]
    fn jdate() {
        DateTester::test("j", Type::JDate);
    }

    #[test]
    fn sdate() {
        DateTester::test("y-m-d", Type::SDate);
    }

    #[test]
    fn qyr() {
        DateTester::test("qQy", Type::QYr);
    }

    #[test]
    fn moyr() {
        DateTester::test("m-y", Type::MoYr);
    }

    #[test]
    fn wkyr() {
        DateTester::test("wWy", Type::WkYr);
    }

    #[test]
    fn datetime_without_seconds() {
        // A more exhaustive template would be "d-m-y +H:M", but that takes much
        // longer to run. We get confidence about delimiters from our other
        // tests.
        DateTester::test("d m-y +H:M", Type::DateTime);
    }

    #[test]
    fn datetime_with_seconds() {
        // A more exhaustive template would be "d-m-y +H:M:S", but that takes
        // much longer to run. We get confidence about delimiters from our other
        // tests.
        DateTester::test("d-m y +H M:S", Type::DateTime);
    }

    #[test]
    fn ymdhms_without_seconds() {
        // A more exhaustive template would be "y-m-d +H:M", but that takes much
        // longer to run. We get confidence about delimiters from our other tests.
        DateTester::test("y m-d +H:M", Type::YmdHms);
    }

    #[test]
    fn ymdhms_with_seconds() {
        // A more exhaustive template would be "y-m-d +H:M:S", but that takes
        // much longer to run. We get confidence about delimiters from our other
        // tests.
        DateTester::test("y-m d +H M:S", Type::YmdHms);
    }

    #[derive(Clone, Debug)]
    struct TestTime {
        days: i32,
        hours: i32,
        minutes: i32,
        seconds: f64,
    }

    impl TestTime {
        const fn new(days: i32, hours: i32, minutes: i32, seconds: f64) -> Self {
            Self {
                days,
                hours,
                minutes,
                seconds,
            }
        }
    }

    struct TimeTester<'a> {
        time: TestTime,
        template: &'a str,
        formatted: String,
        type_: Type,
    }

    impl TimeTester<'_> {
        fn visit(&self, extra: &str, mut expected: f64, sign: Sign) {
            let formatted = format!("{}{extra}", self.formatted);
            if !self.template.is_empty() {
                let mut iter = self.template.chars();
                let first = iter.next().unwrap();
                let next = TimeTester {
                    time: self.time.clone(),
                    template: iter.as_str(),
                    formatted: formatted.clone(),
                    type_: self.type_,
                };
                match first {
                    '+' => {
                        next.visit("", expected, sign);
                        next.visit("+", expected, Sign::Positive);
                        next.visit("-", expected, Sign::Negative);
                    }
                    'D' => {
                        expected += (self.time.days * 86400) as f64;
                        next.visit(&format!("{}", self.time.days), expected, sign);
                        next.visit(&format!("{:02}", self.time.days), expected, sign);
                    }
                    'H' => {
                        expected += (self.time.hours * 3600) as f64;
                        next.visit(&format!("{}", self.time.hours), expected, sign);
                        next.visit(&format!("{:02}", self.time.hours), expected, sign);
                    }
                    'M' => {
                        expected += (self.time.minutes * 60) as f64;
                        next.visit(&format!("{}", self.time.minutes), expected, sign);
                        next.visit(&format!("{:02}", self.time.minutes), expected, sign);
                    }
                    'S' => {
                        expected += self.time.seconds;
                        next.visit(&format!("{}", self.time.seconds), expected, sign);
                        next.visit(&format!("{:02}", self.time.seconds), expected, sign);
                    }
                    ':' => {
                        for c in b" :" {
                            next.visit(&format!("{}", *c as char), expected, sign);
                        }
                    }
                    ' ' => {
                        next.visit(" ", expected, sign);
                    }
                    _ => unreachable!(),
                }
            } else {
                let expected = match sign {
                    Sign::Positive => expected,
                    Sign::Negative => -expected,
                };

                let parsed = self
                    .type_
                    .parser(UTF_8)
                    .parse(&formatted)
                    .unwrap()
                    .as_number()
                    .unwrap()
                    .unwrap();
                assert_eq!((parsed * 1000.0).round(), expected * 1000.0);
            }
        }

        fn test(template: &str, type_: Type) {
            static TIMES: [TestTime; 20] = [
                TestTime::new(0, 0, 0, 0.00),
                TestTime::new(1, 4, 50, 38.68),
                TestTime::new(5, 12, 31, 35.82),
                TestTime::new(0, 12, 47, 53.41),
                TestTime::new(3, 1, 26, 0.69),
                TestTime::new(1, 20, 58, 11.19),
                TestTime::new(12, 7, 36, 5.98),
                TestTime::new(52, 15, 43, 49.27),
                TestTime::new(7, 4, 25, 9.24),
                TestTime::new(0, 6, 49, 27.89),
                TestTime::new(20, 2, 57, 52.56),
                TestTime::new(555, 16, 45, 44.12),
                TestTime::new(120, 21, 30, 57.27),
                TestTime::new(0, 4, 25, 9.98),
                TestTime::new(3, 6, 49, 27.24),
                TestTime::new(5, 2, 57, 52.13),
                TestTime::new(0, 16, 45, 44.35),
                TestTime::new(1, 21, 30, 57.32),
                TestTime::new(10, 22, 30, 4.27),
                TestTime::new(22, 1, 56, 51.18),
            ];
            for time in &TIMES {
                let visitor = TimeTester {
                    time: time.clone(),
                    template,
                    formatted: "".to_string(),
                    type_,
                };
                visitor.visit("", 0.0, Sign::default());
            }
        }
    }

    #[test]
    fn mtime() {
        TimeTester::test("+M:S", Type::MTime);
    }

    #[test]
    fn time() {
        TimeTester::test("+H:M", Type::Time);
        TimeTester::test("+H:M:S", Type::Time);
    }

    #[test]
    fn dtime() {
        TimeTester::test("+D H:M", Type::DTime);
        TimeTester::test("+D H:M:S", Type::DTime);
    }

    #[test]
    fn wkday() {
        for (mut input, expected) in [
            ("", None),
            (".", None),
            ("sudnay", Some(1.0)),
            ("monady", Some(2.0)),
            ("tuseday", Some(3.0)),
            ("WEDENSDAY", Some(4.0)),
            ("Thrudsay", Some(5.0)),
            ("fRidya", Some(6.0)),
            ("SAturady", Some(7.0)),
            ("sturday", None),
        ] {
            loop {
                let parsed = Type::WkDay
                    .parser(UTF_8)
                    .parse(input)
                    .unwrap_or(Datum::Number(None))
                    .as_number()
                    .unwrap();
                assert_eq!(parsed, expected);

                if input.len() <= 2 {
                    break;
                }
                input = &input[..input.len() - 1];
            }
        }
    }

    #[test]
    fn month() {
        for (input, expected, with_shortening) in [
            ("", None, false),
            ("i", Some(1.0), false),
            ("ii", Some(2.0), false),
            ("iii", Some(3.0), false),
            ("iiii", None, false),
            ("iv", Some(4.0), false),
            ("v", Some(5.0), false),
            ("vi", Some(6.0), false),
            ("vii", Some(7.0), false),
            ("viii", Some(8.0), false),
            ("ix", Some(9.0), false),
            ("viiii", Some(8.0), false),
            ("x", Some(10.0), false),
            ("xi", Some(11.0), false),
            ("xii", Some(12.0), false),
            ("0", None, false),
            ("1", Some(1.0), false),
            ("2", Some(2.0), false),
            ("3", Some(3.0), false),
            ("4", Some(4.0), false),
            ("5", Some(5.0), false),
            ("6", Some(6.0), false),
            ("7", Some(7.0), false),
            ("8", Some(8.0), false),
            ("9", Some(9.0), false),
            ("10", Some(10.0), false),
            ("11", Some(11.0), false),
            ("12", Some(12.0), false),
            ("13", None, false),
            ("january", Some(1.0), true),
            ("JANAURY", Some(1.0), true),
            ("February", Some(2.0), true),
            ("fEbraury", Some(2.0), true),
            ("MArch", Some(3.0), true),
            ("marhc", Some(3.0), true),
            ("apRIL", Some(4.0), true),
            ("may", Some(5.0), true),
            ("june", Some(6.0), true),
            ("july", Some(7.0), true),
            ("august", Some(8.0), true),
            ("september", Some(9.0), true),
            ("october", Some(10.0), true),
            ("november", Some(11.0), true),
            ("decmeber", Some(12.0), true),
            ("december", Some(12.0), true),
        ] {
            let lengths = if with_shortening {
                (3..input.len()).rev().collect()
            } else {
                vec![input.len()]
            };

            for length in lengths {
                let input = &input[..length];
                let parsed = Type::Month
                    .parser(UTF_8)
                    .parse(input)
                    .unwrap_or(Datum::Number(None))
                    .as_number()
                    .unwrap();
                assert_eq!(parsed, expected, "parsing {input}");
            }
        }
    }

    #[test]
    fn pibhex() {
        fn hex_digits() -> impl Iterator<Item = (u8, char)> {
            ((0..=9).zip('0'..='9'))
                .chain((0xa..=0xf).zip('a'..='f'))
                .chain((0xa..=0xf).zip('A'..='F'))
                .chain(std::iter::once((0, 'x')))
        }
        let parser = Type::PIBHex.parser(UTF_8);
        for (a, ac) in hex_digits() {
            for (b, bc) in hex_digits() {
                let s = [ac, bc].into_iter().collect::<String>();
                let parsed = parser
                    .parse(&s)
                    .unwrap_or(Datum::Number(None))
                    .as_number()
                    .unwrap();
                let expected = if ac == 'x' || bc == 'x' {
                    None
                } else {
                    Some((a * 16 + b) as f64)
                };
                assert_eq!(parsed, expected);
            }
        }
        assert_eq!(parser.parse(".").unwrap(), Datum::Number(None));
        assert_eq!(parser.parse("",).unwrap(), Datum::Number(None));
    }

    #[test]
    fn rbhex() {
        for _ in 0..10000 {
            let number = random::<f64>();
            let formatted = format!("{:016x}", number.to_bits());
            let parsed = Type::RBHex
                .parser(UTF_8)
                .parse(&formatted)
                .unwrap()
                .as_number()
                .unwrap()
                .unwrap();
            assert_eq!(parsed, number, "formatted as {formatted:?}");
        }
    }

    #[test]
    fn rb() {
        for _ in 0..10000 {
            let number = random::<f64>();
            let raw = number.to_be_bytes();
            let parsed = Type::RB
                .parser(UTF_8)
                .with_endian(EndianSettings::new(Endian::Big))
                .parse(EncodedStr::new(&raw[..], UTF_8))
                .unwrap()
                .as_number()
                .unwrap()
                .unwrap();
            assert_eq!(parsed, number);
        }
    }

    #[test]
    fn n() {
        let parser = Type::N.parser(UTF_8);
        for number in 0..=99 {
            let formatted = format!("{:02}", number);
            let parsed = parser
                .parse(&formatted)
                .unwrap()
                .as_number()
                .unwrap()
                .unwrap();
            assert_eq!(parsed, number as f64, "formatted as {formatted:?}");
        }
        assert!(matches!(
            parser.parse(" 0"),
            Err(ParseError {
                kind: ParseErrorKind::Nondigit(' '),
                ..
            })
        ));
        assert!(matches!(
            parser.parse("."),
            Err(ParseError {
                kind: ParseErrorKind::Nondigit('.'),
                ..
            })
        ));
    }

    #[test]
    fn z() {
        let parser = Type::Z.parser(UTF_8);
        for number in -99i32..=99 {
            for mut formatted in [
                format!("{:02}", number.abs()),
                format!("{:2}", number.abs()),
            ] {
                let last = formatted.pop().unwrap();
                let digit = last.to_digit(10).unwrap() as usize;
                if number >= 0 {
                    formatted.push(b"{ABCDEFGHI"[digit] as char);
                } else {
                    formatted.push(b"}JKLMNOPQR"[digit] as char);
                }
                let parsed = parser
                    .parse(&formatted)
                    .unwrap()
                    .as_number()
                    .unwrap()
                    .unwrap();
                assert_eq!(parsed, number as f64, "formatted as {formatted:?}");
            }
        }
        assert_eq!(parser.parse(".").unwrap(), Datum::Number(None));

        let parser = Type::Z.parser(UTF_8).with_implied_decimals(1);
        for number in -999i32..=999 {
            let tenths = number as f64 / 10.0;
            for mut formatted in [format!("{}", number.abs()), format!("{:.1}", tenths.abs())] {
                let last = formatted.pop().unwrap();
                let digit = last.to_digit(10).unwrap() as usize;
                if number >= 0 {
                    formatted.push(b"{ABCDEFGHI"[digit] as char);
                } else {
                    formatted.push(b"}JKLMNOPQR"[digit] as char);
                }
                let parsed = parser
                    .parse(&formatted)
                    .unwrap()
                    .as_number()
                    .unwrap()
                    .unwrap();
                assert_eq!(parsed, tenths, "formatted as {formatted:?}");
            }
        }
    }

    #[test]
    fn ahex() {
        let parser = Type::AHex.parser(UTF_8);

        // Correct.
        assert_eq!(
            parser
                .parse("6162636465666768")
                .unwrap()
                .as_string()
                .unwrap()
                .as_encoded(UTF_8)
                .as_str(),
            "abcdefgh"
        );

        // Non-hex digit.
        assert_eq!(
            parser.parse("61626364656667xyzzy").unwrap_err().kind,
            ParseErrorKind::NonHexDigit('x')
        );

        // Odd number of hex digits.
        assert_eq!(
            parser.parse("616263646566676").unwrap_err().kind,
            ParseErrorKind::OddLength(15)
        );
    }
}
