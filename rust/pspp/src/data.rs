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

//! Individual pieces of data.
//!
//! [Datum] is the value of one [Variable].  String data in a [Datum] is
//! represented as [RawString], whose character encoding is determined by the
//! associated [Variable].  (All the variables in a [Dictionary] have the same
//! character encoding.)
//!
//! [Variable]: crate::dictionary::Variable
//! [Dictionary]: crate::dictionary::Dictionary

// Warn about missing docs, but not for items declared with `#[cfg(test)]`.
#![cfg_attr(not(test), warn(missing_docs))]

use std::{
    borrow::{Borrow, Cow},
    cmp::Ordering,
    fmt::{Debug, Display, Formatter},
    hash::Hash,
    ops::Deref,
    str::from_utf8,
};

use encoding_rs::{mem::decode_latin1, Encoding, UTF_8};
use ordered_float::OrderedFloat;

use crate::dictionary::{VarType, VarWidth};

/// An owned string in an unspecified character encoding.
///
/// A [RawString] is usually associated with a [Variable] and uses the
/// variable's character encoding.  We assume that the encoding is one supported
/// by [encoding_rs] with byte units (that is, not a `UTF-16` encoding).  All of
/// these encodings have some basic ASCII compatibility.
///
/// A [RawString] owns its contents and can grow and shrink, like a [Vec] or
/// [String].  For a borrowed raw string, see [RawStr].
///
/// [Variable]: crate::dictionary::Variable
#[derive(Clone, PartialEq, Default, Eq, PartialOrd, Ord, Hash)]
pub struct RawString(pub Vec<u8>);

impl RawString {
    /// Creates a new [RawString] that consists of `n` ASCII spaces.
    pub fn spaces(n: usize) -> Self {
        Self(std::iter::repeat_n(b' ', n).collect())
    }

    /// Creates an [EncodedStr] with `encoding` that borrows this string's
    /// contents.
    pub fn as_encoded(&self, encoding: &'static Encoding) -> EncodedStr<'_> {
        EncodedStr::new(&self.0, encoding)
    }

    /// Extends or shortens this [RawString] to exactly `len` bytes.  If the
    /// string needs to be extended, does so by appending spaces.
    ///
    /// If this shortens the string, it can cut off a multibyte character in the
    /// middle.
    pub fn resize(&mut self, len: usize) {
        self.0.resize(len, b' ');
    }

    /// Removes any trailing ASCII spaces.
    pub fn trim_end(&mut self) {
        while self.0.pop_if(|c| *c == b' ').is_some() {}
    }
}

impl Borrow<RawStr> for RawString {
    fn borrow(&self) -> &RawStr {
        RawStr::from_bytes(&self.0)
    }
}

impl Deref for RawString {
    type Target = RawStr;

    fn deref(&self) -> &Self::Target {
        self.borrow()
    }
}

impl From<Cow<'_, [u8]>> for RawString {
    fn from(value: Cow<'_, [u8]>) -> Self {
        Self(value.into_owned())
    }
}

impl From<Vec<u8>> for RawString {
    fn from(source: Vec<u8>) -> Self {
        Self(source)
    }
}

impl From<&[u8]> for RawString {
    fn from(source: &[u8]) -> Self {
        Self(source.into())
    }
}

impl Debug for RawString {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        write!(f, "{:?}", *self)
    }
}

/// A borrowed string in an unspecified encoding.
///
/// A [RawString] is usually associated with a [Variable] and uses the
/// variable's character encoding.  We assume that the encoding is one supported
/// by [encoding_rs] with byte units (that is, not a `UTF-16` encoding).  All of
/// these encodings have some basic ASCII compatibility.
///
/// For an owned raw string, see [RawString].
///
/// [Variable]: crate::dictionary::Variable
#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RawStr(pub [u8]);

impl RawStr {
    /// Creates a new [RawStr] that contains `bytes`.
    pub fn from_bytes(bytes: &[u8]) -> &Self {
        // SAFETY: `RawStr` is a transparent wrapper around `[u8]`, so we can
        // turn a reference to the wrapped type into a reference to the wrapper
        // type.
        unsafe { &*(bytes as *const [u8] as *const Self) }
    }

    /// Returns the raw string's contents as a borrowed byte slice.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }

    /// Returns an object that implements [Display] for printing this [RawStr],
    /// given that it is encoded in `encoding`.
    pub fn display(&self, encoding: &'static Encoding) -> DisplayRawString {
        DisplayRawString(encoding.decode_without_bom_handling(&self.0).0)
    }

    /// Interprets the raw string's contents as the specified `encoding` and
    /// returns it decoded into UTF-8, replacing any malformed sequences by
    /// [REPLACEMENT_CHARACTER].
    ///
    /// [REPLACEMENT_CHARACTER]: std::char::REPLACEMENT_CHARACTER
    pub fn decode(&self, encoding: &'static Encoding) -> Cow<'_, str> {
        encoding.decode_without_bom_handling(&self.0).0
    }

    /// Compares this string and `other` for equality, ignoring trailing ASCII
    /// spaces in either string for the purpose of comparison.  (This is
    /// acceptable because we assume that the encoding is ASCII-compatible.)
    pub fn eq_ignore_trailing_spaces(&self, other: &RawStr) -> bool {
        let mut this = self.0.iter();
        let mut other = other.0.iter();
        loop {
            match (this.next(), other.next()) {
                (Some(a), Some(b)) if a == b => (),
                (Some(_), Some(_)) => return false,
                (None, None) => return true,
                (Some(b' '), None) => return this.all(|c| *c == b' '),
                (None, Some(b' ')) => return other.all(|c| *c == b' '),
                (Some(_), None) | (None, Some(_)) => return false,
            }
        }
    }

    /// Returns the string's length in bytes.
    pub fn len(&self) -> usize {
        self.0.len()
    }
}

/// Helper struct for printing [RawStr] with [format!].
///
/// Created by [RawStr::display].
pub struct DisplayRawString<'a>(Cow<'a, str>);

impl<'a> Display for DisplayRawString<'a> {
    // If `s` is valid UTF-8, displays it as UTF-8, otherwise as Latin-1
    // (actually bytes interpreted as Unicode code points).
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.0)
    }
}

impl Debug for RawStr {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        let s = from_utf8(&self.0).map_or_else(|_| decode_latin1(&self.0), Cow::from);
        write!(f, "{s:?}")
    }
}

/// The value of a [Variable](crate::dictionary::Variable).
#[derive(Clone)]
pub enum Datum {
    /// A numeric value.
    Number(
        /// A number, or `None` for the system-missing value.
        Option<f64>,
    ),
    /// A string value.
    String(
        /// The value, in the variable's encoding.
        RawString,
    ),
}

impl Debug for Datum {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        match self {
            Datum::Number(Some(number)) => write!(f, "{number:?}"),
            Datum::Number(None) => write!(f, "SYSMIS"),
            Datum::String(s) => write!(f, "{:?}", s),
        }
    }
}

impl PartialEq for Datum {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Number(Some(l0)), Self::Number(Some(r0))) => {
                OrderedFloat(*l0) == OrderedFloat(*r0)
            }
            (Self::Number(None), Self::Number(None)) => true,
            (Self::String(l0), Self::String(r0)) => l0 == r0,
            _ => false,
        }
    }
}

impl Eq for Datum {}

impl PartialOrd for Datum {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Datum {
    fn cmp(&self, other: &Self) -> Ordering {
        match (self, other) {
            (Datum::Number(a), Datum::Number(b)) => match (a, b) {
                (None, None) => Ordering::Equal,
                (None, Some(_)) => Ordering::Less,
                (Some(_), None) => Ordering::Greater,
                (Some(a), Some(b)) => a.total_cmp(b),
            },
            (Datum::Number(_), Datum::String(_)) => Ordering::Less,
            (Datum::String(_), Datum::Number(_)) => Ordering::Greater,
            (Datum::String(a), Datum::String(b)) => a.cmp(b),
        }
    }
}

impl Hash for Datum {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        match self {
            Datum::Number(number) => number.map(OrderedFloat).hash(state),
            Datum::String(string) => string.hash(state),
        }
    }
}

impl Datum {
    /// Constructs a new numerical [Datum] for the system-missing value.
    pub const fn sysmis() -> Self {
        Self::Number(None)
    }

    /// Returns the number inside this datum, or `None` if this is a string
    /// datum.
    pub fn as_number(&self) -> Option<Option<f64>> {
        match self {
            Datum::Number(number) => Some(*number),
            Datum::String(_) => None,
        }
    }

    /// Returns the string inside this datum, or `None` if this is a numeric
    /// datum.
    pub fn as_string(&self) -> Option<&RawString> {
        match self {
            Datum::Number(_) => None,
            Datum::String(s) => Some(s),
        }
    }

    /// Returns the string inside this datum as a mutable borrow, or `None` if
    /// this is a numeric datum.
    pub fn as_string_mut(&mut self) -> Option<&mut RawString> {
        match self {
            Datum::Number(_) => None,
            Datum::String(s) => Some(s),
        }
    }

    /// Returns true if this datum can be resized to the given `width` without
    /// loss, which is true only if this datum and `width` are both string or
    /// both numeric and, for string widths, if resizing would not drop any
    /// non-space characters.
    pub fn is_resizable(&self, width: VarWidth) -> bool {
        match (self, width) {
            (Datum::Number(_), VarWidth::Numeric) => true,
            (Datum::String(s), VarWidth::String(new_width)) => {
                let new_len = new_width as usize;
                new_len >= s.len() || s.0[new_len..].iter().all(|c| *c == b' ')
            }
            _ => false,
        }
    }

    /// Resizes this datum to the given `width`.
    ///
    /// # Panic
    ///
    /// Panics if resizing would change the datum from numeric to string or vice
    /// versa.
    pub fn resize(&mut self, width: VarWidth) {
        match (self, width) {
            (Datum::Number(_), VarWidth::Numeric) => (),
            (Datum::String(s), VarWidth::String(new_width)) => s.resize(new_width as usize),
            _ => unreachable!(),
        }
    }

    /// Returns the [VarType] corresponding to this datum.
    pub fn var_type(&self) -> VarType {
        match self {
            Self::Number(_) => VarType::Numeric,
            Self::String(_) => VarType::String,
        }
    }

    /// Returns the [VarWidth] corresponding to this datum.
    pub fn width(&self) -> VarWidth {
        match self {
            Datum::Number(_) => VarWidth::Numeric,
            Datum::String(s) => VarWidth::String(s.len().try_into().unwrap()),
        }
    }

    /// Compares this datum and `other` for equality, ignoring trailing ASCII
    /// spaces in either, if they are both strings, for the purpose of
    /// comparison.
    pub fn eq_ignore_trailing_spaces(&self, other: &Datum) -> bool {
        match (self, other) {
            (Self::String(a), Self::String(b)) => a.eq_ignore_trailing_spaces(b),
            _ => self == other,
        }
    }

    /// Removes trailing ASCII spaces from this datum, if it is a string.
    pub fn trim_end(&mut self) {
        match self {
            Self::Number(_) => (),
            Self::String(s) => s.trim_end(),
        }
    }
}

impl From<f64> for Datum {
    fn from(number: f64) -> Self {
        Some(number).into()
    }
}

impl From<Option<f64>> for Datum {
    fn from(value: Option<f64>) -> Self {
        Self::Number(value)
    }
}

impl From<&str> for Datum {
    fn from(value: &str) -> Self {
        value.as_bytes().into()
    }
}

impl From<&[u8]> for Datum {
    fn from(value: &[u8]) -> Self {
        Self::String(value.into())
    }
}

/// A case in a data set.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Case(
    /// One [Datum] per variable in the corresponding [Dictionary], in the same
    /// order.
    ///
    /// [Dictionary]: crate::dictionary::Dictionary
    pub Vec<Datum>,
);

/// An owned string and its [Encoding].
///
/// The string is not guaranteed to be valid in the encoding.
///
/// The borrowed form of such a string is [EncodedStr].
#[derive(Clone, Debug)]
pub enum EncodedString {
    /// A string in arbitrary encoding.
    Encoded {
        /// The bytes of the string.
        bytes: Vec<u8>,

        /// The string's encoding.
        ///
        /// This can be [UTF_8].
        encoding: &'static Encoding,
    },

    /// A string that is in UTF-8 and known to be valid.
    Utf8 {
        /// The string.
        s: String,
    },
}

impl EncodedString {
    /// Returns the string's [Encoding].
    pub fn encoding(&self) -> &'static Encoding {
        match self {
            EncodedString::Encoded { encoding, .. } => encoding,
            EncodedString::Utf8 { .. } => UTF_8,
        }
    }

    /// Returns a borrowed form of this string.
    pub fn borrowed(&self) -> EncodedStr<'_> {
        match self {
            EncodedString::Encoded { bytes, encoding } => EncodedStr::Encoded { bytes, encoding },
            EncodedString::Utf8 { s } => EncodedStr::Utf8 { s },
        }
    }
}

impl<'a> From<EncodedStr<'a>> for EncodedString {
    fn from(value: EncodedStr<'a>) -> Self {
        match value {
            EncodedStr::Encoded { bytes, encoding } => Self::Encoded {
                bytes: bytes.into(),
                encoding,
            },
            EncodedStr::Utf8 { s } => Self::Utf8 { s: s.into() },
        }
    }
}

/// A borrowed string and its [Encoding].
///
/// The string is not guaranteed to be valid in the encoding.
///
/// The owned form of such a string is [EncodedString].
pub enum EncodedStr<'a> {
    /// A string in an arbitrary encoding
    Encoded {
        /// The bytes of the string.
        bytes: &'a [u8],

        /// The string's encoding.
        ///
        /// THis can be [UTF_8].
        encoding: &'static Encoding,
    },

    /// A string in UTF-8 that is known to be valid.
    Utf8 {
        /// The string.
        s: &'a str,
    },
}

impl<'a> EncodedStr<'a> {
    /// Construct a new string with an arbitrary encoding.
    pub fn new(bytes: &'a [u8], encoding: &'static Encoding) -> Self {
        Self::Encoded { bytes, encoding }
    }

    /// Returns this string recoded in UTF-8.  Invalid characters will be
    /// replaced by [REPLACEMENT_CHARACTER].
    ///
    /// [REPLACEMENT_CHARACTER]: std::char::REPLACEMENT_CHARACTER
    pub fn as_str(&self) -> Cow<'_, str> {
        match self {
            EncodedStr::Encoded { bytes, encoding } => {
                encoding.decode_without_bom_handling(bytes).0
            }
            EncodedStr::Utf8 { s } => Cow::from(*s),
        }
    }

    /// Returns the bytes in the string, in its encoding.
    pub fn as_bytes(&self) -> &[u8] {
        match self {
            EncodedStr::Encoded { bytes, .. } => bytes,
            EncodedStr::Utf8 { s } => s.as_bytes(),
        }
    }

    /// Returns this string recoded in `encoding`.  Invalid characters will be
    /// replaced by [REPLACEMENT_CHARACTER].
    ///
    /// [REPLACEMENT_CHARACTER]: std::char::REPLACEMENT_CHARACTER
    pub fn to_encoding(&self, encoding: &'static Encoding) -> Cow<[u8]> {
        match self {
            EncodedStr::Encoded { bytes, encoding } => {
                let utf8 = encoding.decode_without_bom_handling(bytes).0;
                match encoding.encode(&utf8).0 {
                    Cow::Borrowed(_) => {
                        // Recoding into UTF-8 and then back did not change anything.
                        Cow::from(*bytes)
                    }
                    Cow::Owned(owned) => Cow::Owned(owned),
                }
            }
            EncodedStr::Utf8 { s } => encoding.encode(s).0,
        }
    }

    /// Returns true if this string is empty.
    pub fn is_empty(&self) -> bool {
        match self {
            EncodedStr::Encoded { bytes, .. } => bytes.is_empty(),
            EncodedStr::Utf8 { s } => s.is_empty(),
        }
    }

    /// Returns a helper for displaying this string in double quotes.
    pub fn quoted(&self) -> QuotedEncodedStr {
        QuotedEncodedStr(self)
    }
}

impl<'a> From<&'a str> for EncodedStr<'a> {
    fn from(s: &'a str) -> Self {
        Self::Utf8 { s }
    }
}

impl<'a> From<&'a String> for EncodedStr<'a> {
    fn from(s: &'a String) -> Self {
        Self::Utf8 { s: s.as_str() }
    }
}

/// Helper struct for displaying a [QuotedEncodedStr] in double quotes.
pub struct QuotedEncodedStr<'a>(&'a EncodedStr<'a>);

impl Display for QuotedEncodedStr<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.0.as_str())
    }
}
