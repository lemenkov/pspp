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
    cmp::min,
    fmt::{Display, Error as FmtError, Formatter, Result as FmtResult, Write as _},
    io::{Error as IoError, Write as IoWrite},
    str::from_utf8_unchecked,
};

use chrono::{Datelike, NaiveDate};
use encoding_rs::{Encoding, UTF_8};
use libm::frexp;
use smallstr::SmallString;
use smallvec::{Array, SmallVec};

use crate::{
    calendar::{calendar_offset_to_gregorian, day_of_year, month_name, short_month_name},
    dictionary::Datum,
    endian::ToBytes,
    format::{Category, DateTemplate, Decimal, Format, NumberStyle, Settings, TemplateItem, Type},
    settings::{EndianSettings, Settings as PsppSettings},
};

pub struct DisplayDatum<'a, 'b> {
    format: Format,
    settings: &'b Settings,
    endian: EndianSettings,
    datum: &'a Datum,
    encoding: &'static Encoding,

    /// If true, the output will remove leading and trailing spaces from numeric
    /// values, and trailing spaces from string values.  (This might make the
    /// output narrower than the requested width.)
    trim_spaces: bool,

    /// If true, the output will include a double quote before and after string
    /// values.
    quote_strings: bool,
}

#[cfg(test)]
mod test;

pub trait DisplayPlain {
    fn display_plain(&self) -> impl Display;
}

impl DisplayPlain for f64 {
    fn display_plain(&self) -> impl Display {
        DisplayPlainF64(*self)
    }
}

pub struct DisplayPlainF64(pub f64);

impl Display for DisplayPlainF64 {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if self.0.abs() < 0.0005 || self.0.abs() > 1e15 {
            // Print self.0s that would otherwise have lots of leading or
            // trailing zeros in scientific notation with full precision.
            write!(f, "{:.e}", self.0)
        } else if self.0 == self.0.trunc() {
            // Print integers without decimal places.
            write!(f, "{:.0}", self.0)
        } else {
            // Print other numbers with full precision.
            write!(f, "{:.}", self.0)
        }
    }
}

impl Datum {
    /// Returns an object that implements [Display] for printing this [Datum] as
    /// `format`.  `encoding` specifies this `Datum`'s encoding (therefore, it
    /// is used only if this is a `Datum::String`).
    ///
    /// [Display]: std::fmt::Display
    pub fn display(&self, format: Format, encoding: &'static Encoding) -> DisplayDatum {
        DisplayDatum::new(format, self, encoding)
    }

    pub fn display_plain(&self, encoding: &'static Encoding) -> DisplayDatumPlain {
        DisplayDatumPlain {
            datum: self,
            encoding,
            quote_strings: true,
        }
    }
}

pub struct DisplayDatumPlain<'a> {
    datum: &'a Datum,
    encoding: &'static Encoding,
    quote_strings: bool,
}

impl DisplayDatumPlain<'_> {
    pub fn without_quotes(self) -> Self {
        Self {
            quote_strings: false,
            ..self
        }
    }
}

impl Display for DisplayDatumPlain<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self.datum {
            Datum::Number(None) => write!(f, "SYSMIS"),
            Datum::Number(Some(number)) => number.display_plain().fmt(f),
            Datum::String(string) => {
                if self.quote_strings {
                    write!(f, "\"{}\"", string.display(self.encoding))
                } else {
                    string.display(self.encoding).fmt(f)
                }
            }
        }
    }
}

impl Display for DisplayDatum<'_, '_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        let number = match self.datum {
            Datum::Number(number) => *number,
            Datum::String(string) => {
                if self.format.type_() == Type::AHex {
                    for byte in &string.0 {
                        write!(f, "{byte:02x}")?;
                    }
                } else {
                    let quote = if self.quote_strings { "\"" } else { "" };
                    let s = self.encoding.decode_without_bom_handling(&string.0).0;
                    let s = if self.trim_spaces {
                        s.trim_end_matches(' ')
                    } else {
                        &s
                    };
                    write!(f, "{quote}{s}{quote}")?;
                }
                return Ok(());
            }
        };

        let Some(number) = number else {
            return self.missing(f);
        };

        match self.format.type_() {
            Type::F
            | Type::Comma
            | Type::Dot
            | Type::Dollar
            | Type::Pct
            | Type::E
            | Type::CC(_) => self.number(f, number),
            Type::N => self.n(f, number),
            Type::Z => self.z(f, number),

            Type::P | Type::PK | Type::IB | Type::PIB | Type::RB => self.fmt_binary(f),

            Type::PIBHex => self.pibhex(f, number),
            Type::RBHex => self.rbhex(f, number),
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
            | Type::DTime
            | Type::WkDay => self.date(f, number),
            Type::Month => self.month(f, number),
            Type::A | Type::AHex => unreachable!(),
        }
    }
}

impl<'a, 'b> DisplayDatum<'a, 'b> {
    pub fn new(format: Format, value: &'a Datum, encoding: &'static Encoding) -> Self {
        let settings = PsppSettings::global();
        Self {
            format,
            datum: value,
            encoding,
            settings: &settings.formats,
            endian: settings.endian,
            trim_spaces: false,
            quote_strings: false,
        }
    }
    pub fn with_settings(self, settings: &'b Settings) -> Self {
        Self { settings, ..self }
    }
    pub fn with_endian(self, endian: EndianSettings) -> Self {
        Self { endian, ..self }
    }
    pub fn with_trimming(self) -> Self {
        Self {
            trim_spaces: true,
            ..self
        }
    }
    pub fn with_quoted_string(self) -> Self {
        Self {
            quote_strings: true,
            ..self
        }
    }
    fn fmt_binary(&self, f: &mut Formatter) -> FmtResult {
        let output = self.to_binary().unwrap();
        for b in output {
            f.write_char(b as char)?;
        }
        Ok(())
    }
    fn number(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        if number.is_finite() {
            let style = self.settings.number_style(self.format.type_);
            if self.format.type_ != Type::E && number.abs() < 1.5 * power10(self.format.w()) {
                let rounder = Rounder::new(style, number, self.format.d);
                if self.decimal(f, &rounder, style, true)?
                    || self.scientific(f, number, style, true)?
                    || self.decimal(f, &rounder, style, false)?
                {
                    return Ok(());
                }
            }

            if !self.scientific(f, number, style, false)? {
                self.overflow(f)?;
            }
            Ok(())
        } else {
            self.infinite(f, number)
        }
    }

    fn infinite(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        if self.format.w >= 3 {
            let s = if number.is_nan() {
                "NaN"
            } else if number.is_infinite() {
                if number.is_sign_positive() {
                    "+Infinity"
                } else {
                    "-Infinity"
                }
            } else {
                "Unknown"
            };
            let w = if self.trim_spaces { 0 } else { self.format.w() };
            write!(f, "{s:>0$.*}", w)
        } else {
            self.overflow(f)
        }
    }

    fn missing(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self.format.type_ {
            Type::P | Type::PK | Type::IB | Type::PIB | Type::RB => return self.fmt_binary(f),
            Type::RBHex => return self.rbhex(f, -f64::MAX),
            _ => (),
        }

        if self.trim_spaces {
            return write!(f, ".");
        }

        let w = self.format.w() as isize;
        let d = self.format.d() as isize;
        let dot_position = match self.format.type_ {
            Type::N => w - 1,
            Type::Pct => w - d - 2,
            Type::E => w - d - 5,
            _ => w - d - 1,
        };
        let dot_position = dot_position.max(0) as u16;

        for i in 0..self.format.w {
            if i == dot_position {
                write!(f, ".")?;
            } else {
                write!(f, " ")?;
            }
        }
        Ok(())
    }

    fn overflow(&self, f: &mut Formatter<'_>) -> FmtResult {
        if self.trim_spaces {
            write!(f, "*")?;
        } else {
            for _ in 0..self.format.w {
                write!(f, "*")?;
            }
        }
        Ok(())
    }

    fn decimal(
        &self,
        f: &mut Formatter<'_>,
        rounder: &Rounder,
        style: &NumberStyle,
        require_affixes: bool,
    ) -> Result<bool, FmtError> {
        for decimals in (0..=self.format.d).rev() {
            // Make sure there's room for the number's magnitude, plus the
            // negative suffix, plus (if negative) the negative prefix.
            let RounderWidth {
                mut width,
                integer_digits,
                negative,
            } = rounder.width(decimals as usize);
            width += style.neg_suffix.width;
            if negative {
                width += style.neg_prefix.width;
            }
            if width > self.format.w() {
                continue;
            }

            // If there's room for the prefix and suffix, allocate
            // space.  If the affixes are required, but there's no
            // space, give up.
            let add_affixes = allocate_space(style.affix_width(), self.format.w(), &mut width);
            if !add_affixes && require_affixes {
                continue;
            }

            // Check whether we should include grouping characters.  We need
            // room for a complete set or we don't insert any at all.  We don't
            // include grouping characters if decimal places were requested but
            // they were all dropped.
            let grouping = style.grouping.filter(|_| {
                integer_digits > 3
                    && (self.format.d == 0 || decimals > 0)
                    && allocate_space((integer_digits - 1) / 3, self.format.w(), &mut width)
            });

            // Assemble number.
            let magnitude = rounder.format(decimals as usize);
            let mut output = SmallString::<[u8; 40]>::new();
            if !self.trim_spaces {
                for _ in width..self.format.w() {
                    output.push(' ');
                }
            }
            if negative {
                output.push_str(&style.neg_prefix.s);
            }
            if add_affixes {
                output.push_str(&style.prefix.s);
            }
            if let Some(grouping) = grouping {
                for (i, digit) in magnitude[..integer_digits].bytes().enumerate() {
                    if i > 0 && (integer_digits - i) % 3 == 0 {
                        output.push(grouping.into());
                    }
                    output.push(digit as char);
                }
            } else {
                output.push_str(&magnitude[..integer_digits]);
            }
            if decimals > 0 {
                output.push(style.decimal.into());
                let s = &magnitude[integer_digits + 1..];
                output.push_str(&s[..decimals as usize]);
            }
            if add_affixes {
                output.push_str(&style.suffix.s);
            }
            if negative {
                output.push_str(&style.neg_suffix.s);
            } else {
                for _ in 0..style.neg_suffix.width {
                    output.push(' ');
                }
            }

            debug_assert!(self.trim_spaces || output.len() >= self.format.w());
            debug_assert!(output.len() <= self.format.w() + style.extra_bytes);
            f.write_str(&output)?;
            return Ok(true);
        }
        Ok(false)
    }

    fn scientific(
        &self,
        f: &mut Formatter<'_>,
        number: f64,
        style: &NumberStyle,
        require_affixes: bool,
    ) -> Result<bool, FmtError> {
        // Allocate minimum required space.
        let mut width = 6 + style.neg_suffix.width;
        if number < 0.0 {
            width += style.neg_prefix.width;
        }
        if width > self.format.w() {
            return Ok(false);
        }

        // Check for room for prefix and suffix.
        let add_affixes = allocate_space(style.affix_width(), self.format.w(), &mut width);
        if require_affixes && !add_affixes {
            return Ok(false);
        }

        // Figure out number of characters we can use for the fraction, if any.
        // (If that turns out to be `1`, then we'll output a decimal point
        // without any digits following.)
        let mut fraction_width = min(self.format.d as usize + 1, self.format.w() - width).min(16);
        if self.format.type_ != Type::E && fraction_width == 1 {
            fraction_width = 0;
        }
        width += fraction_width;

        let mut output = SmallString::<[u8; 40]>::new();
        if !self.trim_spaces {
            for _ in width..self.format.w() {
                output.push(' ');
            }
        }
        if number < 0.0 {
            output.push_str(&style.neg_prefix.s);
        }
        if add_affixes {
            output.push_str(&style.prefix.s);
        }
        write!(
            &mut output,
            "{:.*E}",
            fraction_width.saturating_sub(1),
            number.abs()
        )
        .unwrap();
        if fraction_width == 1 {
            // Insert `.` before the `E`, to get a value like "1.E+000".
            output.insert(output.find('E').unwrap(), '.');
        }

        // Rust always uses `.` as the decimal point. Translate to `,` if
        // necessary.
        if style.decimal == Decimal::Comma {
            fix_decimal_point(&mut output);
        }

        // Make exponent have exactly three digits, plus sign.
        let e = output.as_bytes().iter().position(|c| *c == b'E').unwrap();
        let exponent: isize = output[e + 1..].parse().unwrap();
        if exponent.abs() > 999 {
            return Ok(false);
        }
        output.truncate(e + 1);
        write!(&mut output, "{exponent:+04}").unwrap();

        // Add suffixes.
        if add_affixes {
            output.push_str(&style.suffix.s);
        }
        if number.is_sign_negative() {
            output.push_str(&style.neg_suffix.s);
        } else {
            for _ in 0..style.neg_suffix.width {
                output.push(' ');
            }
        }

        println!(
            "{} for {number} width={width} fraction_width={fraction_width}: {output:?}",
            self.format
        );
        debug_assert!(self.trim_spaces || output.len() >= self.format.w());
        debug_assert!(output.len() <= self.format.w() + style.extra_bytes);
        f.write_str(&output)?;
        Ok(true)
    }

    fn n(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        if number < 0.0 {
            return self.missing(f);
        }

        let legacy = LegacyFormat::new(number, self.format.d());
        let w = self.format.w();
        let len = legacy.len();
        if len > w {
            self.overflow(f)
        } else {
            write!(f, "{}{legacy}", Zeros(w.saturating_sub(len)))
        }
    }

    fn z(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        let legacy = LegacyFormat::new(number, self.format.d());
        let w = self.format.w();
        let len = legacy.len();
        if len > w {
            self.overflow(f)
        } else {
            let mut s = SmallString::<[u8; 40]>::new();
            write!(&mut s, "{legacy}")?;
            if number < 0.0 {
                if let Some(last) = s.pop() {
                    let last = last.to_digit(10).unwrap();
                    s.push(b"}JKLMNOPQR"[last as usize] as char);
                }
            }
            write!(f, "{}{s}", Zeros(w.saturating_sub(len)))
        }
    }

    fn pibhex(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        if number < 0.0 {
            self.overflow(f)
        } else {
            let number = number.round();
            if number >= power256(self.format.w / 2) {
                self.overflow(f)
            } else {
                let binary = integer_to_binary(number as u64, self.format.w / 2);
                output_hex(f, &binary)
            }
        }
    }

    fn rbhex(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        let rb = self.rb(Some(number), self.format.w() / 2);
        output_hex(f, &rb)
    }

    fn date(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        const MINUTE: f64 = 60.0;
        const HOUR: f64 = 60.0 * 60.0;
        const DAY: f64 = 60.0 * 60.0 * 24.0;

        let (date, mut time) = match self.format.type_.category() {
            Category::Date => {
                if number < 0.0 {
                    return self.missing(f);
                }
                let Some(date) = calendar_offset_to_gregorian(number / DAY) else {
                    return self.missing(f);
                };
                (date, number % DAY)
            }
            Category::Time => (NaiveDate::MIN, number),
            _ => unreachable!(),
        };

        let mut output = SmallString::<[u8; 40]>::new();
        for TemplateItem { c, n } in DateTemplate::for_format(self.format).unwrap() {
            match c {
                'd' if n < 3 => write!(&mut output, "{:02}", date.day()).unwrap(),
                'd' => write!(&mut output, "{:03}", day_of_year(date).unwrap_or(1)).unwrap(),
                'm' if n < 3 => write!(&mut output, "{:02}", date.month()).unwrap(),
                'm' => write!(&mut output, "{}", short_month_name(date.month()).unwrap()).unwrap(),
                'y' if n >= 4 => {
                    let year = date.year();
                    if year <= 9999 {
                        write!(&mut output, "{year:04}").unwrap();
                    } else if self.format.type_ == Type::DateTime
                        || self.format.type_ == Type::YmdHms
                    {
                        write!(&mut output, "****").unwrap();
                    } else {
                        return self.overflow(f);
                    }
                }
                'y' => {
                    let epoch = self.settings.epoch.0;
                    let offset = date.year() - epoch;
                    if !(0..=99).contains(&offset) {
                        return self.overflow(f);
                    }
                    write!(&mut output, "{:02}", date.year().abs() % 100).unwrap();
                }
                'q' => write!(&mut output, "{}", date.month0() / 3 + 1).unwrap(),
                'w' => write!(
                    &mut output,
                    "{:2}",
                    (day_of_year(date).unwrap_or(1) - 1) / 7 + 1
                )
                .unwrap(),
                'D' => {
                    if time < 0.0 {
                        output.push('-');
                    }
                    time = time.abs();
                    write!(&mut output, "{:1$.0}", (time / DAY).floor(), n).unwrap();
                    time %= DAY;
                }
                'H' => {
                    if time < 0.0 {
                        output.push('-');
                    }
                    time = time.abs();
                    write!(&mut output, "{:01$.0}", (time / HOUR).floor(), n).unwrap();
                    time %= HOUR;
                }
                'M' => {
                    if time < 0.0 {
                        output.push('-');
                    }
                    time = time.abs();
                    write!(&mut output, "{:02.0}", (time / MINUTE).floor()).unwrap();
                    time %= MINUTE;

                    let excess_width = self.format.w() as isize - output.len() as isize;
                    if excess_width < 0 || (self.format.type_ == Type::MTime && excess_width < 3) {
                        return self.overflow(f);
                    }
                    if excess_width == 3
                        || excess_width == 4
                        || (excess_width >= 5 && self.format.d == 0)
                    {
                        write!(&mut output, ":{:02.0}", time.floor()).unwrap();
                    } else if excess_width >= 5 {
                        let d = min(self.format.d(), excess_width as usize - 4);
                        let w = d + 3;
                        write!(&mut output, ":{:02$.*}", d, time, w).unwrap();
                        if self.settings.decimal == Decimal::Comma {
                            fix_decimal_point(&mut output);
                        }
                    }
                    break;
                }
                c if n == 1 => output.push(c),
                _ => unreachable!(),
            }
        }
        if !self.trim_spaces {
            write!(f, "{:>1$}", &output, self.format.w())
        } else {
            f.write_str(&output)
        }
    }

    fn month(&self, f: &mut Formatter<'_>, number: f64) -> FmtResult {
        if let Some(month) = month_name(number as u32) {
            if !self.trim_spaces {
                write!(f, "{month:.*}", self.format.w())
            } else {
                f.write_str(month)
            }
        } else {
            self.missing(f)
        }
    }

    /// Writes this object to `w`. Writes binary formats ([Type::P],
    /// [Type::PIB], and so on) as binary values, and writes other output
    /// formats in the given `encoding`.
    ///
    /// If `dv` is a [DisplayDatum], the difference between `write!(f, "{}",
    /// dv)` and `dv.write(f, encoding)` is:
    ///
    /// * `write!` always outputs UTF-8. Binary formats are encoded as the
    ///   Unicode characters corresponding to their bytes.
    ///
    /// * `dv.write` outputs the desired `encoding`. Binary formats are not
    ///   encoded in `encoding` (and thus they might be invalid for the
    ///   encoding).
    pub fn write<W>(&self, mut w: W, encoding: &'static Encoding) -> Result<(), IoError>
    where
        W: IoWrite,
    {
        match self.to_binary() {
            Some(binary) => w.write_all(&binary),
            None if encoding == UTF_8 => {
                write!(&mut w, "{}", self)
            }
            None => {
                let mut temp = SmallString::<[u8; 64]>::new();
                write!(&mut temp, "{}", self).unwrap();
                w.write_all(&encoding.encode(&temp).0)
            }
        }
    }

    fn to_binary(&self) -> Option<SmallVec<[u8; 16]>> {
        let number = self.datum.as_number()?;
        match self.format.type_() {
            Type::P => Some(self.p(number)),
            Type::PK => Some(self.pk(number)),
            Type::IB => Some(self.ib(number)),
            Type::PIB => Some(self.pib(number)),
            Type::RB => Some(self.rb(number, self.format.w())),
            _ => None,
        }
    }

    fn bcd(&self, number: Option<f64>, digits: usize) -> (bool, SmallVec<[u8; 16]>) {
        let legacy = LegacyFormat::new(number.unwrap_or_default(), self.format.d());
        let len = legacy.len();

        let mut output = SmallVec::new();
        if len > digits {
            output.resize(digits.div_ceil(2), 0);
            (false, output)
        } else {
            let mut decimal = SmallString::<[u8; 16]>::new();
            write!(
                &mut decimal,
                "{}{legacy}",
                Zeros(digits.saturating_sub(len))
            )
            .unwrap();

            let mut src = decimal.bytes();
            for _ in 0..digits / 2 {
                let d0 = src.next().unwrap() - b'0';
                let d1 = src.next().unwrap() - b'0';
                output.push((d0 << 4) + d1);
            }
            if digits % 2 != 0 {
                let d = src.next().unwrap() - b'0';
                output.push(d << 4);
            }
            (true, output)
        }
    }

    fn p(&self, number: Option<f64>) -> SmallVec<[u8; 16]> {
        let (valid, mut output) = self.bcd(number, self.format.w() * 2 - 1);
        if valid && number.is_some_and(|number| number < 0.0) {
            *output.last_mut().unwrap() |= 0xd;
        } else {
            *output.last_mut().unwrap() |= 0xf;
        }
        output
    }

    fn pk(&self, number: Option<f64>) -> SmallVec<[u8; 16]> {
        let number = match number {
            Some(number) if number < 0.0 => None,
            other => other,
        };
        let (_valid, output) = self.bcd(number, self.format.w() * 2);
        output
    }

    fn ib(&self, number: Option<f64>) -> SmallVec<[u8; 16]> {
        let number = number.map_or(0.0, |number| (number * power10(self.format.d())).round());
        let number = if number >= power256(self.format.w) / 2.0 - 1.0
            || number < -power256(self.format.w) / 2.0
        {
            0.0
        } else {
            number
        };
        let integer = number.abs() as u64;
        let integer = if number < 0.0 {
            (-(integer as i64)) as u64
        } else {
            integer
        };
        self.endian.output.to_smallvec(integer, self.format.w())
    }

    fn pib(&self, number: Option<f64>) -> SmallVec<[u8; 16]> {
        let number = number.map_or(0.0, |number| (number * power10(self.format.d())).round());
        let number = if number >= power256(self.format.w) || number < 0.0 {
            0.0
        } else {
            number
        };
        let integer = number.abs() as u64;
        self.endian.output.to_smallvec(integer, self.format.w())
    }

    fn rb(&self, number: Option<f64>, w: usize) -> SmallVec<[u8; 16]> {
        let number = number.unwrap_or(-f64::MAX);
        let bytes: [u8; 8] = self.endian.output.to_bytes(number);
        let mut vec = SmallVec::new();
        vec.extend_from_slice(&bytes);
        vec.resize(w, 0);
        vec
    }
}

struct LegacyFormat {
    s: SmallVec<[u8; 40]>,
    trailing_zeros: usize,
}

impl LegacyFormat {
    fn new(number: f64, d: usize) -> Self {
        let mut s = SmallVec::<[u8; 40]>::new();
        write!(&mut s, "{:E}", number.abs()).unwrap();
        debug_assert!(s.is_ascii());

        // Parse exponent.
        //
        // Add 1 because of the transformation we will do just below, and `d` so
        // that we just need to round to the nearest integer.
        let e_index = s.iter().position(|c| *c == b'E').unwrap();
        let mut exponent = unsafe { from_utf8_unchecked(&s[e_index + 1..]) }
            .parse::<i32>()
            .unwrap()
            + 1
            + d as i32;

        // Transform `1.234E56` into `1234`.
        if e_index == 1 {
            // No decimals, e.g. `1E4` or `0E0`.
            s.truncate(1)
        } else {
            s.remove(1);
            s.truncate(e_index - 1);
        };
        debug_assert!(s.iter().all(|c| c.is_ascii_digit()));

        if exponent >= 0 && exponent < s.len() as i32 {
            // The first `exponent` digits are before the decimal point.  We
            // need to round off there.
            let exp = exponent as usize;

            fn round_up(digits: &mut [u8], position: usize) -> bool {
                for index in (0..position).rev() {
                    match digits[index] {
                        b'0'..=b'8' => {
                            digits[index] += 1;
                            return true;
                        }
                        b'9' => {
                            digits[index] = b'0';
                        }
                        _ => unreachable!(),
                    }
                }
                false
            }

            if s[exp] >= b'5' && !round_up(&mut s, exp) {
                s.clear();
                s.push(b'1');
                exponent += 1;
            }
        }

        let exponent = exponent.max(0) as usize;
        s.truncate(exponent);
        s.resize(exponent, b'0');
        let trailing_zeros = exponent.saturating_sub(s.len());
        Self { s, trailing_zeros }
    }
    fn s(&self) -> &str {
        unsafe { from_utf8_unchecked(&self.s) }
    }
    fn len(&self) -> usize {
        self.s.len() + self.trailing_zeros
    }
}

impl Display for LegacyFormat {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}{}", self.s(), Zeros(self.trailing_zeros))
    }
}

struct Zeros(usize);

impl Display for Zeros {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        let mut n = self.0;
        while n > 0 {
            static ZEROS: &str = "0000000000000000000000000000000000000000";
            let chunk = n.min(ZEROS.len());
            f.write_str(&ZEROS[..chunk])?;
            n -= chunk;
        }
        Ok(())
    }
}

fn integer_to_binary(number: u64, width: u16) -> SmallVec<[u8; 8]> {
    let bytes = (number << ((8 - width) * 8)).to_be_bytes();
    SmallVec::from_slice(&bytes[..width as usize])
}

fn output_hex(f: &mut Formatter<'_>, bytes: &[u8]) -> FmtResult {
    for byte in bytes {
        write!(f, "{byte:02X}")?;
    }
    Ok(())
}

fn allocate_space(want: usize, capacity: usize, used: &mut usize) -> bool {
    if *used + want <= capacity {
        *used += want;
        true
    } else {
        false
    }
}

/// A representation of a number that can be quickly rounded to any desired
/// number of decimal places (up to a specified maximum).
#[derive(Debug)]
struct Rounder {
    /// Magnitude of number with excess precision.
    string: SmallString<[u8; 40]>,

    /// Number of digits before decimal point.
    integer_digits: usize,

    /// Number of `9`s or `.`s at start of string.
    leading_nines: usize,

    /// Number of `0`s or `.`s at start of string.
    leading_zeros: usize,

    /// Is the number negative?
    negative: bool,
}

impl Rounder {
    fn new(style: &NumberStyle, number: f64, max_decimals: u8) -> Self {
        debug_assert!(number.abs() < 1e41);
        debug_assert!((0..=16).contains(&max_decimals));

        let mut string = SmallString::new();
        if max_decimals == 0 {
            // Fast path.  No rounding needed.
            //
            // We append `.00` to the integer representation because
            // [Self::round_up] assumes that fractional digits are present.
            write!(&mut string, "{:.0}.00", number.round().abs()).unwrap()
        } else {
            // Slow path.
            //
            // This is more difficult than it really should be because we have
            // to make sure that numbers that are exactly halfway between two
            // representations are always rounded away from zero.  This is not
            // what format! normally does (usually it rounds to even), so we
            // have to fake it as best we can, by formatting with extra
            // precision and then doing the rounding ourselves.
            //
            // We take up to two rounds to format numbers.  In the first round,
            // we obtain 2 digits of precision beyond those requested by the
            // user.  If those digits are exactly "50", then in a second round
            // we format with as many digits as are significant in a "double".
            //
            // It might be better to directly implement our own floating-point
            // formatting routine instead of relying on the system's sprintf
            // implementation.  But the classic Steele and White paper on
            // printing floating-point numbers does not hint how to do what we
            // want, and it's not obvious how to change their algorithms to do
            // so.  It would also be a lot of work.
            write!(
                &mut string,
                "{:.*}",
                max_decimals as usize + 2,
                number.abs()
            )
            .unwrap();
            if string.ends_with("50") {
                let (_sig, binary_exponent) = frexp(number);
                let decimal_exponent = binary_exponent * 3 / 10;
                let format_decimals = (f64::DIGITS as i32 + 1) - decimal_exponent;
                if format_decimals > max_decimals as i32 + 2 {
                    string.clear();
                    write!(&mut string, "{:.*}", format_decimals as usize, number.abs()).unwrap();
                }
            }
        };

        if !style.leading_zero && string.starts_with("0") {
            string.remove(0);
        }
        let leading_zeros = string
            .bytes()
            .take_while(|c| *c == b'0' || *c == b'.')
            .count();
        let leading_nines = string
            .bytes()
            .take_while(|c| *c == b'9' || *c == b'.')
            .count();
        let integer_digits = string.bytes().take_while(u8::is_ascii_digit).count();
        let negative = number.is_sign_negative();
        Self {
            string,
            integer_digits,
            leading_nines,
            leading_zeros,
            negative,
        }
    }

    /// Returns a [RounderWdith] for formatting the magnitude to `decimals`
    /// decimal places. `decimals` must be in `0..=16`.
    fn width(&self, decimals: usize) -> RounderWidth {
        // Calculate base measures.
        let mut width = self.integer_digits;
        if decimals > 0 {
            width += decimals + 1;
        }
        let mut integer_digits = self.integer_digits;
        let mut negative = self.negative;

        // Rounding can cause adjustments.
        if self.should_round_up(decimals) {
            // Rounding up leading `9s` adds a new digit (a `1`).
            if self.leading_nines >= width {
                width += 1;
                integer_digits += 1;
            }
        } else {
            // Rounding down.
            if self.leading_zeros >= width {
                // All digits that remain after rounding are zeros.  Therefore
                // we drop the negative sign.
                negative = false;
                if self.integer_digits == 0 && decimals == 0 {
                    // No digits at all are left.  We need to display
                    // at least a single digit (a zero).
                    debug_assert_eq!(width, 0);
                    width += 1;
                    integer_digits = 1;
                }
            }
        }
        RounderWidth {
            width,
            integer_digits,
            negative,
        }
    }

    /// Returns true if the number should be rounded up when chopped off at
    /// `decimals` decimal places, false if it should be rounded down.
    fn should_round_up(&self, decimals: usize) -> bool {
        let digit = self.string.as_bytes()[self.integer_digits + decimals + 1];
        debug_assert!(digit.is_ascii_digit());
        digit >= b'5'
    }

    /// Formats the number, rounding to `decimals` decimal places.  Exactly as
    /// many characters as indicated by [Self::width(decimals)] are written.
    fn format(&self, decimals: usize) -> SmallString<[u8; 40]> {
        let mut output = SmallString::new();
        let mut base_width = self.integer_digits;
        if decimals > 0 {
            base_width += decimals + 1;
        }

        if self.should_round_up(decimals) {
            if self.leading_nines < base_width {
                // Rounding up.  This is the common case where rounding up
                // doesn't add an extra digit.
                output.push_str(&self.string[..base_width]);

                // SAFETY: This loop only changes ASCII characters to other
                // ASCII characters.
                unsafe {
                    for c in output.as_bytes_mut().iter_mut().rev() {
                        match *c {
                            b'9' => *c = b'0',
                            b'0'..=b'8' => {
                                *c += 1;
                                break;
                            }
                            b'.' => (),
                            _ => unreachable!(),
                        }
                    }
                }
            } else {
                // Rounding up leading 9s causes the result to be a 1 followed
                // by a number of 0s, plus a decimal point.
                output.push('1');
                for _ in 0..self.integer_digits {
                    output.push('0');
                }
                if decimals > 0 {
                    output.push('.');
                    for _ in 0..decimals {
                        output.push('0');
                    }
                }
                debug_assert_eq!(output.len(), base_width + 1);
            }
        } else {
            // Rounding down.
            if self.integer_digits != 0 || decimals != 0 {
                // Common case: just copy the digits.
                output.push_str(&self.string);
            } else {
                // No digits remain.  The output is just a zero.
                output.push('0');
            }
        }
        output
    }
}

struct RounderWidth {
    /// Number of characters required to format the number to a specified number
    /// of decimal places.  This includes integer digits and a decimal point and
    /// fractional digits, if any, but it does not include any negative prefix
    /// or suffix or other affixes.
    width: usize,

    /// Number of digits before the decimal point, between 0 and 40.
    integer_digits: usize,

    /// True if the number is negative and its rounded representation would
    /// include at least one nonzero digit.
    negative: bool,
}

/// Returns `10^x`.
fn power10(x: usize) -> f64 {
    const POWERS: [f64; 41] = [
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16,
        1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30, 1e31,
        1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40,
    ];
    POWERS
        .get(x)
        .copied()
        .unwrap_or_else(|| 10.0_f64.powi(x as i32))
}

/// Returns `256^x`.
fn power256(x: u16) -> f64 {
    const POWERS: [f64; 9] = [
        1.0,
        256.0,
        65536.0,
        16777216.0,
        4294967296.0,
        1099511627776.0,
        281474976710656.0,
        72057594037927936.0,
        18446744073709551616.0,
    ];
    POWERS
        .get(x as usize)
        .copied()
        .unwrap_or_else(|| 256.0_f64.powi(x as i32))
}

fn fix_decimal_point<A>(s: &mut SmallString<A>)
where
    A: Array<Item = u8>,
{
    // SAFETY: This only changes only one ASCII character (`.`) to
    // another ASCII character (`,`).
    unsafe {
        if let Some(dot) = s.as_bytes_mut().iter_mut().find(|c| **c == b'.') {
            *dot = b',';
        }
    }
}
