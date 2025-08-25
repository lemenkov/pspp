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
    fmt::{Debug, Display, Formatter, Result as FmtResult, Write},
    ops::{Not, RangeInclusive},
    str::{Chars, FromStr},
    sync::LazyLock,
};

use chrono::{Datelike, Local};
use enum_iterator::{all, Sequence};
use enum_map::{Enum, EnumMap};
use serde::{Deserialize, Serialize};
use thiserror::Error as ThisError;
use unicode_width::UnicodeWidthStr;

use crate::{
    data::{ByteString, Datum},
    sys::raw,
    util::ToSmallString,
    variable::{VarType, VarWidth},
};

mod display;
mod parse;
pub use display::{DisplayDatum, DisplayPlain, DisplayPlainF64};

#[derive(Clone, ThisError, Debug, PartialEq, Eq)]
pub enum Error {
    #[error("Unknown format type {value}.")]
    UnknownFormat { value: u16 },

    #[error("Output format {0} specifies width {}, but {} requires an even width.", .0.w, .0.type_)]
    OddWidthNotAllowed(UncheckedFormat),

    #[error("Output format {0} specifies width {}, but {} requires a width between {} and {}.", .0.w, .0.type_, .0.type_.min_width(), .0.type_.max_width())]
    BadWidth(UncheckedFormat),

    #[error("Output format {0} specifies decimal places, but {} format does not allow any decimals.", .0.type_)]
    DecimalsNotAllowedForFormat(UncheckedFormat),

    #[error("Output format {0} specifies {} decimal places, but with a width of {}, {} does not allow any decimal places.", .0.d, .0.w, .0.type_)]
    DecimalsNotAllowedForWidth(UncheckedFormat),

    #[error("Output format {spec} specifies {} decimal places but, with a width of {}, {} allows at most {max_d} decimal places.", .spec.d, .spec.w, .spec.type_)]
    TooManyDecimalsForWidth {
        spec: UncheckedFormat,
        max_d: Decimals,
    },

    #[error("String variable is not compatible with numeric format {0}.")]
    UnnamedVariableNotCompatibleWithNumericFormat(Type),

    #[error("Numeric variable is not compatible with string format {0}.")]
    UnnamedVariableNotCompatibleWithStringFormat(Type),

    #[error("String variable {variable} with width {width} is not compatible with format {bad_spec}.  Use format {good_spec} instead.")]
    NamedStringVariableBadSpecWidth {
        variable: String,
        width: Width,
        bad_spec: Format,
        good_spec: Format,
    },

    #[error("String variable with width {width} is not compatible with format {bad_spec}.  Use format {good_spec} instead.")]
    UnnamedStringVariableBadSpecWidth {
        width: Width,
        bad_spec: Format,
        good_spec: Format,
    },
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub enum Category {
    // Numeric formats.
    Basic,
    Custom,
    Legacy,
    Binary,
    Hex,
    Date,
    Time,
    DateComponent,

    // String formats.
    String,
}

impl From<Type> for Category {
    fn from(source: Type) -> Self {
        match source {
            Type::F | Type::Comma | Type::Dot | Type::Dollar | Type::Pct | Type::E => Self::Basic,
            Type::CC(_) => Self::Custom,
            Type::N | Type::Z => Self::Legacy,
            Type::P | Type::PK | Type::IB | Type::PIB | Type::RB => Self::Binary,
            Type::PIBHex | Type::RBHex => Self::Hex,
            Type::Date
            | Type::ADate
            | Type::EDate
            | Type::JDate
            | Type::SDate
            | Type::QYr
            | Type::MoYr
            | Type::WkYr
            | Type::DateTime
            | Type::YmdHms => Self::Date,
            Type::MTime | Type::Time | Type::DTime => Self::Time,
            Type::WkDay | Type::Month => Self::DateComponent,
            Type::A | Type::AHex => Self::String,
        }
    }
}

#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq, Hash, Sequence, Serialize)]
pub enum CC {
    A,
    B,
    C,
    D,
    E,
}

impl CC {
    pub fn as_string(&self) -> &'static str {
        match self {
            CC::A => "A",
            CC::B => "B",
            CC::C => "C",
            CC::D => "D",
            CC::E => "E",
        }
    }
}

impl Display for CC {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{}", self.as_string())
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash, Sequence, Serialize)]
pub enum Type {
    // Basic numeric formats.
    F,
    Comma,
    Dot,
    Dollar,
    Pct,
    E,

    // Custom currency formats.
    CC(CC),

    // Legacy numeric formats.
    N,
    Z,

    // Binary and hexadecimal formats.
    P,
    PK,
    IB,
    PIB,
    PIBHex,
    RB,
    RBHex,

    // Time and date formats.
    Date,
    ADate,
    EDate,
    JDate,
    SDate,
    QYr,
    MoYr,
    WkYr,
    DateTime,
    YmdHms,
    MTime,
    Time,
    DTime,

    // Date component formats.
    WkDay,
    Month,

    // String formats.
    A,
    AHex,
}

pub type Width = u16;
pub type SignedWidth = i16;

pub type Decimals = u8;

impl Type {
    pub fn max_width(self) -> Width {
        match self {
            Self::P | Self::PK | Self::PIBHex | Self::RBHex => 16,
            Self::IB | Self::PIB | Self::RB => 8,
            Self::A => 32767,
            Self::AHex => 32767 * 2,
            _ => 40,
        }
    }

    pub fn min_width(self) -> Width {
        match self {
            // Basic numeric formats.
            Self::F => 1,
            Self::Comma => 1,
            Self::Dot => 1,
            Self::Dollar => 2,
            Self::Pct => 2,
            Self::E => 6,

            // Custom currency formats.
            Self::CC(_) => 2,

            // Legacy numeric formats.
            Self::N => 1,
            Self::Z => 1,

            // Binary and hexadecimal formats.
            Self::P => 1,
            Self::PK => 1,
            Self::IB => 1,
            Self::PIB => 1,
            Self::PIBHex => 2,
            Self::RB => 2,
            Self::RBHex => 4,

            // Time and date formats.
            Self::Date => 9,
            Self::ADate => 8,
            Self::EDate => 8,
            Self::JDate => 5,
            Self::SDate => 8,
            Self::QYr => 6,
            Self::MoYr => 6,
            Self::WkYr => 8,
            Self::DateTime => 17,
            Self::YmdHms => 16,
            Self::MTime => 5,
            Self::Time => 5,
            Self::DTime => 8,

            // Date component formats.
            Self::WkDay => 2,
            Self::Month => 3,

            // String formats.
            Self::A => 1,
            Self::AHex => 2,
        }
    }

    pub fn width_range(self) -> RangeInclusive<Width> {
        self.min_width()..=self.max_width()
    }

    pub fn max_decimals(self, width: Width) -> Decimals {
        let width = width.clamp(1, 40) as SignedWidth;
        let max = match self {
            Self::F | Self::Comma | Self::Dot | Self::CC(_) => width - 1,
            Self::Dollar | Self::Pct => width - 2,
            Self::E => width - 7,
            Self::N | Self::Z => width,
            Self::P => width * 2 - 1,
            Self::PK => width * 2,
            Self::IB | Self::PIB => max_digits_for_bytes(width as usize) as SignedWidth,
            Self::PIBHex => 0,
            Self::RB | Self::RBHex => 16,
            Self::Date
            | Self::ADate
            | Self::EDate
            | Self::JDate
            | Self::SDate
            | Self::QYr
            | Self::MoYr
            | Self::WkYr => 0,
            Self::DateTime => width - 21,
            Self::YmdHms => width - 20,
            Self::MTime => width - 6,
            Self::Time => width - 9,
            Self::DTime => width - 12,
            Self::WkDay | Self::Month | Self::A | Self::AHex => 0,
        };
        max.clamp(0, 16) as Decimals
    }

    pub fn takes_decimals(self) -> bool {
        self.max_decimals(Width::MAX) > 0
    }

    pub fn category(self) -> Category {
        self.into()
    }

    pub fn width_step(self) -> Width {
        if self.category() == Category::Hex || self == Self::AHex {
            2
        } else {
            1
        }
    }

    pub fn clamp_width(self, width: Width) -> Width {
        let (min, max) = self.width_range().into_inner();
        let width = width.clamp(min, max);
        if self.width_step() == 2 {
            width / 2 * 2
        } else {
            width
        }
    }

    pub fn var_type(self) -> VarType {
        match self {
            Self::A | Self::AHex => VarType::String,
            _ => VarType::Numeric,
        }
    }

    /// Checks whether this format is valid for a variable with the given
    /// `var_type`.
    pub fn check_type_compatibility(self, var_type: VarType) -> Result<(), Error> {
        let my_type = self.var_type();
        match (my_type, var_type) {
            (VarType::Numeric, VarType::String) => {
                Err(Error::UnnamedVariableNotCompatibleWithNumericFormat(self))
            }
            (VarType::String, VarType::Numeric) => {
                Err(Error::UnnamedVariableNotCompatibleWithStringFormat(self))
            }
            _ => Ok(()),
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::F => "F",
            Self::Comma => "COMMA",
            Self::Dot => "DOT",
            Self::Dollar => "DOLLAR",
            Self::Pct => "PCT",
            Self::E => "E",
            Self::CC(CC::A) => "CCA",
            Self::CC(CC::B) => "CCB",
            Self::CC(CC::C) => "CCC",
            Self::CC(CC::D) => "CCD",
            Self::CC(CC::E) => "CCE",
            Self::N => "N",
            Self::Z => "Z",
            Self::P => "P",
            Self::PK => "PK",
            Self::IB => "IB",
            Self::PIB => "PIB",
            Self::PIBHex => "PIBHEX",
            Self::RB => "RB",
            Self::RBHex => "RBHEX",
            Self::Date => "DATE",
            Self::ADate => "ADATE",
            Self::EDate => "EDATE",
            Self::JDate => "JDATE",
            Self::SDate => "SDATE",
            Self::QYr => "QYR",
            Self::MoYr => "MOYR",
            Self::WkYr => "WKYR",
            Self::DateTime => "DATETIME",
            Self::YmdHms => "YMDHMS",
            Self::MTime => "MTIME",
            Self::Time => "TIME",
            Self::DTime => "DTIME",
            Self::WkDay => "WKDAY",
            Self::Month => "MONTH",
            Self::A => "A",
            Self::AHex => "AHEX",
        }
    }

    pub fn default_value(&self) -> Datum<ByteString> {
        match self.var_type() {
            VarType::Numeric => Datum::sysmis(),
            VarType::String => Datum::String(ByteString::default()),
        }
    }
}

impl Display for Type {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{}", self.as_str())
    }
}

impl FromStr for Type {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        for type_ in all::<Type>() {
            if type_.as_str().eq_ignore_ascii_case(s) {
                return Ok(type_);
            }
        }
        Err(())
    }
}

fn max_digits_for_bytes(bytes: usize) -> usize {
    *[0, 3, 5, 8, 10, 13, 15, 17].get(bytes).unwrap_or(&20)
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub struct AbstractFormat {
    pub name: String,
    w: Width,
    d: Decimals,
}

fn split<F>(s: &str, predicate: F) -> (&str, &str)
where
    F: Fn(&char) -> bool,
{
    let rest = s.trim_start_matches(|c| predicate(&c));
    let start = &s[..s.len() - rest.len()];
    (start, rest)
}

impl FromStr for AbstractFormat {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let (name, s) = split(s, char::is_ascii_alphabetic);
        if name.is_empty() {
            return Err(());
        }

        let (w, s) = split(s, char::is_ascii_digit);
        let Ok(w) = w.parse() else {
            return Err(());
        };

        let (d, rest) = if let Some(s) = s.strip_prefix('.') {
            let (d, rest) = split(s, char::is_ascii_digit);
            let Ok(d) = d.parse() else {
                return Err(());
            };
            (d, rest)
        } else {
            (0, s)
        };

        if !rest.is_empty() {
            return Err(());
        }
        Ok(Self {
            name: name.into(),
            w,
            d,
        })
    }
}

impl TryFrom<AbstractFormat> for UncheckedFormat {
    type Error = ();

    fn try_from(value: AbstractFormat) -> Result<Self, Self::Error> {
        Ok(UncheckedFormat::new(value.name.parse()?, value.w, value.d))
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Hash)]
pub struct Format {
    type_: Type,
    w: Width,
    d: Decimals,
}

impl Serialize for Format {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.to_small_string::<16>().serialize(serializer)
    }
}

impl Format {
    pub const F40: Format = Format {
        type_: Type::F,
        w: 40,
        d: 0,
    };

    pub const F40_1: Format = Format {
        type_: Type::F,
        w: 40,
        d: 1,
    };

    pub const F40_2: Format = Format {
        type_: Type::F,
        w: 40,
        d: 2,
    };

    pub const F40_3: Format = Format {
        type_: Type::F,
        w: 40,
        d: 3,
    };

    pub const PCT40_1: Format = Format {
        type_: Type::Pct,
        w: 40,
        d: 1,
    };

    pub const F8_2: Format = Format {
        type_: Type::F,
        w: 8,
        d: 2,
    };

    pub const DATETIME40_0: Format = Format {
        type_: Type::DateTime,
        w: 40,
        d: 0,
    };

    pub fn type_(self) -> Type {
        self.type_
    }
    pub fn w(self) -> usize {
        self.w as usize
    }
    pub fn d(self) -> usize {
        self.d as usize
    }

    pub fn new(type_: Type, w: Width, d: Decimals) -> Option<Self> {
        UncheckedFormat { type_, w, d }.try_into().ok()
    }

    pub fn default_for_width(var_width: VarWidth) -> Self {
        match var_width {
            VarWidth::Numeric => Format {
                type_: Type::F,
                w: 8,
                d: 2,
            },
            VarWidth::String(w) => Format {
                type_: Type::A,
                w,
                d: 0,
            },
        }
    }

    pub fn fixed_from(source: &UncheckedFormat) -> Self {
        let UncheckedFormat {
            type_: format,
            w,
            d,
        } = *source;
        let (min, max) = format.width_range().into_inner();
        let mut w = w.clamp(min, max);
        if d <= format.max_decimals(Width::MAX) {
            while d > format.max_decimals(w) {
                w += 1;
                assert!(w <= 40);
            }
        }
        let d = d.clamp(0, format.max_decimals(w));
        Self {
            type_: format,
            w,
            d,
        }
    }

    pub fn var_width(self) -> VarWidth {
        match self.type_ {
            Type::A => VarWidth::String(self.w),
            Type::AHex => VarWidth::String(self.w / 2),
            _ => VarWidth::Numeric,
        }
    }

    pub fn var_type(self) -> VarType {
        self.type_.var_type()
    }

    /// Checks whether this format specification is valid for a variable with
    /// width `var_width`.
    pub fn check_width_compatibility(self, var_width: VarWidth) -> Result<Self, Error> {
        // Verify that the format is right for the variable's type.
        self.type_.check_type_compatibility(var_width.into())?;

        if let VarWidth::String(w) = var_width {
            if var_width != self.var_width() {
                let bad_spec = self;
                let good_spec = if self.type_ == Type::A {
                    Format { w, ..self }
                } else {
                    Format { w: w * 2, ..self }
                };
                return Err(Error::UnnamedStringVariableBadSpecWidth {
                    width: w,
                    bad_spec,
                    good_spec,
                });
            }
        }

        Ok(self)
    }

    pub fn default_value(&self) -> Datum<ByteString> {
        match self.var_width() {
            VarWidth::Numeric => Datum::sysmis(),
            VarWidth::String(width) => Datum::String(ByteString::spaces(width as usize)),
        }
    }

    pub fn resize(&mut self, width: VarWidth) {
        match (self.var_width(), width) {
            (VarWidth::Numeric, VarWidth::Numeric) => {}
            (VarWidth::String(_), VarWidth::String(new_width)) => {
                self.w = if self.type_ == Type::AHex {
                    new_width * 2
                } else {
                    new_width
                };
            }
            _ => *self = Self::default_for_width(width),
        }
    }

    pub fn codepage_to_unicode(&mut self) {
        let mut width = self.var_width();
        width.codepage_to_unicode();
        if let Some(width) = width.as_string_width() {
            if self.type_ == Type::AHex {
                self.w = width as u16 * 2;
            } else {
                self.w = width as u16;
            }
        }
    }
}

impl Debug for Format {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{self}")
    }
}

impl Display for Format {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{}{}", self.type_, self.w)?;
        if self.type_.takes_decimals() || self.d > 0 {
            write!(f, ".{}", self.d)?;
        }
        Ok(())
    }
}

impl TryFrom<UncheckedFormat> for Format {
    type Error = Error;

    fn try_from(source: UncheckedFormat) -> Result<Self, Self::Error> {
        let UncheckedFormat {
            type_: format,
            w,
            d,
        } = source;
        let max_d = format.max_decimals(w);
        if w % format.width_step() != 0 {
            Err(Error::OddWidthNotAllowed(source))
        } else if !format.width_range().contains(&w) {
            Err(Error::BadWidth(source))
        } else if d > max_d {
            if format.takes_decimals() {
                Err(Error::DecimalsNotAllowedForFormat(source))
            } else if max_d > 0 {
                Err(Error::TooManyDecimalsForWidth {
                    spec: source,
                    max_d,
                })
            } else {
                Err(Error::DecimalsNotAllowedForWidth(source))
            }
        } else {
            Ok(Format {
                type_: format,
                w,
                d,
            })
        }
    }
}

impl From<Type> for u16 {
    fn from(source: Type) -> Self {
        match source {
            Type::A => 1,
            Type::AHex => 2,
            Type::Comma => 3,
            Type::Dollar => 4,
            Type::F => 5,
            Type::IB => 6,
            Type::PIBHex => 7,
            Type::P => 8,
            Type::PIB => 9,
            Type::PK => 10,
            Type::RB => 11,
            Type::RBHex => 12,
            Type::Z => 15,
            Type::N => 16,
            Type::E => 17,
            Type::Date => 20,
            Type::Time => 21,
            Type::DateTime => 22,
            Type::ADate => 23,
            Type::JDate => 24,
            Type::DTime => 25,
            Type::WkDay => 26,
            Type::Month => 27,
            Type::MoYr => 28,
            Type::QYr => 29,
            Type::WkYr => 30,
            Type::Pct => 31,
            Type::Dot => 32,
            Type::CC(CC::A) => 33,
            Type::CC(CC::B) => 34,
            Type::CC(CC::C) => 35,
            Type::CC(CC::D) => 36,
            Type::CC(CC::E) => 37,
            Type::EDate => 38,
            Type::SDate => 39,
            Type::MTime => 40,
            Type::YmdHms => 41,
        }
    }
}

impl TryFrom<u16> for Type {
    type Error = Error;

    fn try_from(source: u16) -> Result<Self, Self::Error> {
        match source {
            1 => Ok(Self::A),
            2 => Ok(Self::AHex),
            3 => Ok(Self::Comma),
            4 => Ok(Self::Dollar),
            5 => Ok(Self::F),
            6 => Ok(Self::IB),
            7 => Ok(Self::PIBHex),
            8 => Ok(Self::P),
            9 => Ok(Self::PIB),
            10 => Ok(Self::PK),
            11 => Ok(Self::RB),
            12 => Ok(Self::RBHex),
            15 => Ok(Self::Z),
            16 => Ok(Self::N),
            17 => Ok(Self::E),
            20 => Ok(Self::Date),
            21 => Ok(Self::Time),
            22 => Ok(Self::DateTime),
            23 => Ok(Self::ADate),
            24 => Ok(Self::JDate),
            25 => Ok(Self::DTime),
            26 => Ok(Self::WkDay),
            27 => Ok(Self::Month),
            28 => Ok(Self::MoYr),
            29 => Ok(Self::QYr),
            30 => Ok(Self::WkYr),
            31 => Ok(Self::Pct),
            32 => Ok(Self::Dot),
            33 => Ok(Self::CC(CC::A)),
            34 => Ok(Self::CC(CC::B)),
            35 => Ok(Self::CC(CC::C)),
            36 => Ok(Self::CC(CC::D)),
            37 => Ok(Self::CC(CC::E)),
            38 => Ok(Self::EDate),
            39 => Ok(Self::SDate),
            40 => Ok(Self::MTime),
            41 => Ok(Self::YmdHms),
            _ => Err(Error::UnknownFormat { value: source }),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct UncheckedFormat {
    pub type_: Type,

    pub w: Width,

    pub d: Decimals,
}

impl UncheckedFormat {
    pub fn new(type_: Type, w: Width, d: Decimals) -> Self {
        Self { type_, w, d }
    }
    pub fn fix(&self) -> Format {
        Format::fixed_from(self)
    }
}

impl TryFrom<raw::records::RawFormat> for UncheckedFormat {
    type Error = Error;

    fn try_from(raw: raw::records::RawFormat) -> Result<Self, Self::Error> {
        let raw = raw.0;
        let raw_format = (raw >> 16) as u16;
        let format = raw_format.try_into()?;
        let w = ((raw >> 8) & 0xff) as Width;
        let d = (raw & 0xff) as Decimals;
        Ok(Self {
            type_: format,
            w,
            d,
        })
    }
}

impl Display for UncheckedFormat {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{}{}", self.type_, self.w)?;
        if self.type_.takes_decimals() || self.d > 0 {
            write!(f, ".{}", self.d)?;
        }
        Ok(())
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Enum, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Decimal {
    #[default]
    Dot,
    Comma,
}

impl Decimal {
    pub fn as_str(&self) -> &'static str {
        match self {
            Decimal::Dot => ".",
            Decimal::Comma => ",",
        }
    }
}

impl From<Decimal> for char {
    fn from(value: Decimal) -> Self {
        u8::from(value).into()
    }
}

impl From<Decimal> for u8 {
    fn from(value: Decimal) -> Self {
        match value {
            Decimal::Dot => b'.',
            Decimal::Comma => b',',
        }
    }
}

impl TryFrom<char> for Decimal {
    type Error = ();

    fn try_from(c: char) -> Result<Self, Self::Error> {
        match c {
            '.' => Ok(Self::Dot),
            ',' => Ok(Self::Comma),
            _ => Err(()),
        }
    }
}

impl Not for Decimal {
    type Output = Self;

    fn not(self) -> Self::Output {
        match self {
            Self::Dot => Self::Comma,
            Self::Comma => Self::Dot,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct Epoch(pub i32);

impl Epoch {
    /// Applies the epoch to `year`:
    ///
    /// - If `year` is 2 digits (between 0 and 99, inclusive), returns it
    ///   converted it to the correct year considering the epoch.
    ///
    /// - Otherwise, returns `year` unchanged.
    pub fn apply(&self, year: i32) -> i32 {
        match year {
            0..=99 => {
                let century = self.0 / 100 * 100;
                let offset = self.0 - century;
                if year >= offset {
                    year + century
                } else {
                    year + century + 100
                }
            }
            other => other,
        }
    }
}

impl Default for Epoch {
    fn default() -> Self {
        static DEFAULT: LazyLock<Epoch> = LazyLock::new(|| Epoch(Local::now().year() - 69));
        *DEFAULT
    }
}

impl Display for Epoch {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}", self.0)
    }
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct Settings {
    pub epoch: Epoch,

    /// Either `'.'` or `','`.
    pub decimal: Decimal,

    /// Format `F`, `E`, `COMMA`, and `DOT` with leading zero (e.g. `0.5`
    /// instead of `.5`)?
    pub leading_zero: bool,

    /// Custom currency styles.
    pub ccs: EnumMap<CC, Option<Box<NumberStyle>>>,
}

#[derive(Copy, Clone, Enum)]
struct StyleParams {
    decimal: Decimal,
    leading_zero: bool,
}
impl From<&Settings> for StyleParams {
    fn from(value: &Settings) -> Self {
        Self {
            decimal: value.decimal,
            leading_zero: value.leading_zero,
        }
    }
}

struct StyleSet(EnumMap<StyleParams, NumberStyle>);

impl StyleSet {
    fn new(f: impl Fn(StyleParams) -> NumberStyle) -> Self {
        Self(EnumMap::from_fn(f))
    }
    fn get(&self, settings: &Settings) -> &NumberStyle {
        &self.0[settings.into()]
    }
}

impl Settings {
    pub fn with_cc(mut self, cc: CC, style: NumberStyle) -> Self {
        self.ccs[cc] = Some(Box::new(style));
        self
    }
    pub fn with_leading_zero(self, leading_zero: bool) -> Self {
        Self {
            leading_zero,
            ..self
        }
    }
    pub fn with_epoch(self, epoch: Epoch) -> Self {
        Self { epoch, ..self }
    }
    pub fn number_style(&self, type_: Type) -> &NumberStyle {
        static DEFAULT: LazyLock<NumberStyle> =
            LazyLock::new(|| NumberStyle::new("", "", Decimal::Dot, None, false));

        match type_ {
            Type::F | Type::E => {
                static F: LazyLock<StyleSet> = LazyLock::new(|| {
                    StyleSet::new(|p| NumberStyle::new("", "", p.decimal, None, p.leading_zero))
                });
                F.get(self)
            }
            Type::Comma => {
                static COMMA: LazyLock<StyleSet> = LazyLock::new(|| {
                    StyleSet::new(|p| {
                        NumberStyle::new("", "", p.decimal, Some(!p.decimal), p.leading_zero)
                    })
                });
                COMMA.get(self)
            }
            Type::Dot => {
                static DOT: LazyLock<StyleSet> = LazyLock::new(|| {
                    StyleSet::new(|p| {
                        NumberStyle::new("", "", !p.decimal, Some(p.decimal), p.leading_zero)
                    })
                });
                DOT.get(self)
            }
            Type::Dollar => {
                static DOLLAR: LazyLock<StyleSet> = LazyLock::new(|| {
                    StyleSet::new(|p| NumberStyle::new("$", "", p.decimal, Some(!p.decimal), false))
                });
                DOLLAR.get(self)
            }
            Type::Pct => {
                static PCT: LazyLock<StyleSet> = LazyLock::new(|| {
                    StyleSet::new(|p| NumberStyle::new("", "%", p.decimal, None, false))
                });
                PCT.get(self)
            }
            Type::CC(cc) => self.ccs[cc].as_deref().unwrap_or(&DEFAULT),
            Type::N
            | Type::Z
            | Type::P
            | Type::PK
            | Type::IB
            | Type::PIB
            | Type::PIBHex
            | Type::RB
            | Type::RBHex
            | Type::Date
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
            | Type::WkDay
            | Type::Month
            | Type::A
            | Type::AHex => &DEFAULT,
        }
    }
}

/// A numeric output style.  This can express numeric formats in
/// [Category::Basic] and [Category::Custom].
#[derive(Clone, Debug, Serialize)]
pub struct NumberStyle {
    pub neg_prefix: Affix,
    pub prefix: Affix,
    pub suffix: Affix,
    pub neg_suffix: Affix,

    /// Decimal point.
    pub decimal: Decimal,

    /// Grouping character.
    pub grouping: Option<Decimal>,

    /// Format as `.5` or `0.5`?
    pub leading_zero: bool,

    /// An `Affix` may require more bytes than its display width; for example,
    /// U+00A5 (¥) is 2 bytes in UTF-8 but occupies only one display column.
    /// This member is the sum of the number of bytes required by all of the
    /// `Affix` members in this struct, minus their display widths.  Thus, it
    /// can be used to size memory allocations: for example, the formatted
    /// result of `CCA20.5` requires no more than `(20 + extra_bytes)` bytes in
    /// UTF-8.
    #[serde(skip)]
    pub extra_bytes: usize,
}

impl Display for NumberStyle {
    /// Display this number style in the format used for custom currency.
    ///
    /// This format can only accurately represent number styles that include a
    /// grouping character.  If this number style doesn't, it will pretend that
    /// the grouping character is the opposite of the decimal point character.
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        let grouping = char::from(!self.decimal);
        write!(
            f,
            "{}{}{}{}{}{}{}",
            self.neg_prefix.display(grouping),
            grouping,
            self.prefix.display(grouping),
            grouping,
            self.suffix.display(grouping),
            grouping,
            self.neg_suffix.display(grouping),
        )
    }
}

impl NumberStyle {
    fn new(
        prefix: &str,
        suffix: &str,
        decimal: Decimal,
        grouping: Option<Decimal>,
        leading_zero: bool,
    ) -> Self {
        // These assertions ensure that zero is correct for `extra_bytes`.
        debug_assert!(prefix.is_ascii());
        debug_assert!(suffix.is_ascii());

        Self {
            neg_prefix: Affix::new("-"),
            prefix: Affix::new(prefix),
            suffix: Affix::new(suffix),
            neg_suffix: Affix::new(""),
            decimal,
            grouping,
            leading_zero,
            extra_bytes: 0,
        }
    }

    fn affix_width(&self) -> usize {
        self.prefix.width + self.suffix.width
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Affix {
    /// String contents of affix.
    pub s: String,

    #[serde(skip)]
    /// Display width in columns (see [unicode_width])
    pub width: usize,
}

impl Affix {
    fn new(s: impl Into<String>) -> Self {
        let s = s.into();
        Self {
            width: s.width(),
            s,
        }
    }

    fn extra_bytes(&self) -> usize {
        self.s.len().checked_sub(self.width).unwrap()
    }

    fn display(&self, escape: char) -> DisplayAffix<'_> {
        DisplayAffix {
            affix: self.s.as_str(),
            escape,
        }
    }
}

pub struct DisplayAffix<'a> {
    affix: &'a str,
    escape: char,
}

impl Display for DisplayAffix<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        for c in self.affix.chars() {
            if c == self.escape {
                f.write_char('\'')?;
            }
            f.write_char(c)?;
        }
        Ok(())
    }
}

impl FromStr for NumberStyle {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        fn find_separator(s: &str) -> Option<char> {
            // Count commas and periods.  There must be exactly three of one or
            // the other, except that an apostrophe escapes a following comma or
            // period.
            let mut n_commas = 0;
            let mut n_periods = 0;
            let s = s.as_bytes();
            for i in 0..s.len() {
                if i > 0 && s[i - 1] == b'\'' {
                } else if s[i] == b',' {
                    n_commas += 1;
                } else if s[i] == b'.' {
                    n_periods += 1;
                }
            }

            if n_commas == 3 && n_periods != 3 {
                Some(',')
            } else if n_periods == 3 && n_commas != 3 {
                Some('.')
            } else {
                None
            }
        }

        fn take_cc_token(iter: &mut Chars<'_>, grouping: char) -> Affix {
            let mut s = String::new();
            let mut quote = false;
            for c in iter {
                if c == '\'' && !quote {
                    quote = true;
                } else if c == grouping && !quote {
                    break;
                } else {
                    s.push(c);
                    quote = false;
                }
            }
            Affix::new(s)
        }

        let Some(grouping) = find_separator(s) else {
            return Err(());
        };
        let mut iter = s.chars();
        let neg_prefix = take_cc_token(&mut iter, grouping);
        let prefix = take_cc_token(&mut iter, grouping);
        let suffix = take_cc_token(&mut iter, grouping);
        let neg_suffix = take_cc_token(&mut iter, grouping);
        let grouping: Decimal = grouping.try_into().unwrap();
        let decimal = !grouping;
        let extra_bytes = neg_prefix.extra_bytes()
            + prefix.extra_bytes()
            + suffix.extra_bytes()
            + neg_suffix.extra_bytes();
        Ok(Self {
            neg_prefix,
            prefix,
            suffix,
            neg_suffix,
            decimal,
            grouping: Some(grouping),
            leading_zero: false,
            extra_bytes,
        })
    }
}

/// An item within a [DateTemplate].
pub struct TemplateItem {
    /// Character in the template.
    pub c: char,

    /// Number of repetitions of the character.
    pub n: usize,
}

/// A template for date and time formats.
#[derive(Clone)]
pub struct DateTemplate(&'static str);

impl DateTemplate {
    /// Returns a [DateTemplate] used for date and time input and output in a
    /// field of the given `type_` and `width`.
    ///
    /// `width` only affects whether a 2-digit year or a 4-digit year is used,
    /// that is, whether the returned string contains `yy` or `yyyy`, and
    /// whether seconds are included, that is, whether the returned string
    /// contains `:SS`.  A caller that doesn't care whether the returned string
    /// contains `yy` or `yyyy` or `:SS` can just specify 0 to omit them.
    pub fn new(type_: Type, width: usize) -> Option<Self> {
        let (short, long) = match type_ {
            Type::F
            | Type::Comma
            | Type::Dot
            | Type::Dollar
            | Type::Pct
            | Type::E
            | Type::CC(_)
            | Type::N
            | Type::Z
            | Type::P
            | Type::PK
            | Type::IB
            | Type::PIB
            | Type::PIBHex
            | Type::RB
            | Type::RBHex
            | Type::WkDay
            | Type::Month
            | Type::A
            | Type::AHex => return None,
            Type::Date => ("dd-mmm-yy", "dd-mmm-yyyy"),
            Type::ADate => ("mm/dd/yy", "mm/dd/yyyy"),
            Type::EDate => ("dd.mm.yy", "dd.mm.yyyy"),
            Type::JDate => ("yyddd", "yyyyddd"),
            Type::SDate => ("yy/mm/dd", "yyyy/mm/dd"),
            Type::QYr => ("q Q yy", "q Q yyyy"),
            Type::MoYr => ("mmm yy", "mmm yyyy"),
            Type::WkYr => ("ww WK yy", "ww WK yyyy"),
            Type::DateTime => ("dd-mmm-yyyy HH:MM", "dd-mmm-yyyy HH:MM:SS"),
            Type::YmdHms => ("yyyy-mm-dd HH:MM", "yyyy-mm-dd HH:MM:SS"),
            Type::MTime => ("MM", "MM:SS"),
            Type::Time => ("HH:MM", "HH:MM:SS"),
            Type::DTime => ("D HH:MM", "D HH:MM:SS"),
        };
        if width >= long.len() {
            Some(DateTemplate(long))
        } else {
            Some(DateTemplate(short))
        }
    }

    pub fn for_format(format: Format) -> Option<Self> {
        Self::new(format.type_(), format.w())
    }

    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        self.0.len()
    }
}

impl Iterator for DateTemplate {
    type Item = TemplateItem;

    fn next(&mut self) -> Option<Self::Item> {
        let mut iter = self.0.chars();
        let c = iter.next()?;
        self.0 = iter.as_str();
        let mut n = 1;
        while iter.next() == Some(c) {
            self.0 = iter.as_str();
            n += 1;
        }
        Some(TemplateItem { c, n })
    }
}

#[cfg(test)]
mod tests {
    use crate::format::{Format, Type, Width};

    #[test]
    fn codepage_to_unicode() {
        fn check_format(input: Format, expected_width: Width) {
            let mut output = input;
            output.codepage_to_unicode();
            let expected = Format::new(input.type_, expected_width, input.d).unwrap();
            assert_eq!(output, expected);
        }
        check_format(Format::new(Type::A, 1, 0).unwrap(), 3);
        check_format(Format::new(Type::A, 2, 0).unwrap(), 6);
        check_format(Format::new(Type::A, 3, 0).unwrap(), 9);
        check_format(Format::new(Type::A, 1000, 0).unwrap(), 3000);
        check_format(Format::new(Type::A, 20000, 0).unwrap(), 32767);

        check_format(Format::new(Type::AHex, 2, 0).unwrap(), 6);
        check_format(Format::new(Type::AHex, 4, 0).unwrap(), 12);
        check_format(Format::new(Type::AHex, 6, 0).unwrap(), 18);
        check_format(Format::new(Type::AHex, 2000, 0).unwrap(), 6000);
        check_format(Format::new(Type::AHex, 20000, 0).unwrap(), 60000);
        check_format(Format::new(Type::AHex, 30000, 0).unwrap(), 65534);

        check_format(Format::new(Type::F, 40, 0).unwrap(), 40);
    }
}
