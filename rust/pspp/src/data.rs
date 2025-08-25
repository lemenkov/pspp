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
//! [Variable]: crate::variable::Variable
//! [Dictionary]: crate::dictionary::Dictionary

// Warn about missing docs, but not for items declared with `#[cfg(test)]`.
//#![cfg_attr(not(test), warn(missing_docs))]

use std::{
    borrow::{Borrow, BorrowMut, Cow},
    cmp::Ordering,
    fmt::{Debug, Display, Formatter},
    hash::Hash,
    str::from_utf8,
};

use encoding_rs::{mem::decode_latin1, Encoding, UTF_8};
use itertools::Itertools;
use ordered_float::OrderedFloat;
use serde::{
    ser::{SerializeSeq, SerializeTupleVariant},
    Serialize,
};

use crate::{
    format::DisplayPlain,
    variable::{VarType, VarWidth},
};

pub trait RawString: Debug + PartialEq + Eq + PartialOrd + Ord + Hash {
    fn raw_string_bytes(&self) -> &[u8];

    /// Compares this string and `other` for equality, ignoring trailing ASCII
    /// spaces in either string for the purpose of comparison.  (This is
    /// acceptable because we assume that the encoding is ASCII-compatible.)
    ///
    /// This compares the bytes of the strings, disregarding their encodings (if
    /// known).
    fn eq_ignore_trailing_spaces<R>(&self, other: &R) -> bool
    where
        R: RawString,
    {
        self.raw_string_bytes()
            .iter()
            .copied()
            .zip_longest(other.raw_string_bytes().iter().copied())
            .all(|elem| {
                let (left, right) = elem.or(b' ', b' ');
                left == right
            })
    }

    /// Returns true if this raw string can be resized to `len` bytes without
    /// dropping non-space characters.
    fn is_resizable(&self, new_len: usize) -> bool {
        new_len >= self.len()
            || self.raw_string_bytes()[new_len..]
                .iter()
                .copied()
                .all(|b| b == b' ')
    }

    fn is_empty(&self) -> bool {
        self.raw_string_bytes().is_empty()
    }

    fn len(&self) -> usize {
        self.raw_string_bytes().len()
    }

    fn as_ref(&self) -> &ByteStr {
        ByteStr::new(self.raw_string_bytes())
    }

    fn without_trailing_spaces(&self) -> &ByteStr {
        let mut raw = self.raw_string_bytes();
        while let Some(trimmed) = raw.strip_suffix(b" ") {
            raw = trimmed;
        }
        ByteStr::new(raw)
    }

    fn as_encoded(&self, encoding: &'static Encoding) -> WithEncoding<&ByteStr>
    where
        Self: Sized,
    {
        WithEncoding::new(self.as_ref(), encoding)
    }

    fn with_encoding(self, encoding: &'static Encoding) -> WithEncoding<Self>
    where
        Self: Sized,
    {
        WithEncoding::new(self, encoding)
    }
}

pub trait MutRawString: RawString {
    fn resize(&mut self, new_len: usize) -> Result<(), ResizeError>;
    fn trim_end(&mut self);
}

impl RawString for &'_ str {
    fn raw_string_bytes(&self) -> &[u8] {
        self.as_bytes()
    }
}

impl RawString for String {
    fn raw_string_bytes(&self) -> &[u8] {
        self.as_bytes()
    }
}

impl RawString for &'_ String {
    fn raw_string_bytes(&self) -> &[u8] {
        self.as_bytes()
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Hash)]
#[repr(transparent)]
pub struct ByteStr(pub [u8]);

impl PartialEq<ByteString> for &ByteStr {
    fn eq(&self, other: &ByteString) -> bool {
        self.raw_string_bytes() == other.raw_string_bytes()
    }
}

impl ByteStr {
    pub fn new(s: &[u8]) -> &ByteStr {
        // SAFETY: ByteStr is just a wrapper of [u8],
        // therefore converting &[u8] to &ByteStr is safe.
        unsafe { &*(s as *const [u8] as *const ByteStr) }
    }
}

impl<'a> RawString for &'a ByteStr {
    fn raw_string_bytes(&self) -> &[u8] {
        &self.0
    }
}

impl<'a> Serialize for &'a ByteStr {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        if let Ok(s) = str::from_utf8(&self.0) {
            let (variant_index, variant) = if self.0.iter().all(|b| b.is_ascii()) {
                (0, "Ascii")
            } else {
                (1, "Utf8")
            };
            let mut tuple =
                serializer.serialize_tuple_variant("RawString", variant_index, variant, 1)?;
            tuple.serialize_field(s)?;
            tuple.end()
        } else {
            let mut tuple = serializer.serialize_tuple_variant("RawString", 2, "Windows1252", 1)?;
            tuple.serialize_field(&decode_latin1(&self.0))?;
            tuple.end()
        }
    }
}

impl Debug for ByteStr {
    // If `s` is valid UTF-8, displays it as UTF-8, otherwise as Latin-1
    // (actually bytes interpreted as Unicode code points).
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        let s = from_utf8(&self.0).map_or_else(|_| decode_latin1(&self.0), Cow::from);
        write!(f, "{s:?}")
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ByteCow<'a>(pub Cow<'a, [u8]>);

impl ByteCow<'_> {
    pub fn into_owned(self) -> ByteString {
        ByteString(self.0.into_owned())
    }
}

impl Serialize for ByteCow<'_> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        ByteStr::new(&self.0).serialize(serializer)
    }
}

impl RawString for ByteCow<'_> {
    fn raw_string_bytes(&self) -> &[u8] {
        &self.0
    }
}

impl Debug for ByteCow<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        ByteStr::new(&self.0).fmt(f)
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ByteStrArray<const N: usize>(pub [u8; N]);

impl<const N: usize> Serialize for ByteStrArray<N> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        ByteStr::new(&self.0).serialize(serializer)
    }
}

impl<const N: usize> RawString for ByteStrArray<N> {
    fn raw_string_bytes(&self) -> &[u8] {
        &self.0
    }
}

impl<const N: usize> Debug for ByteStrArray<N> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        ByteStr::new(&self.0).fmt(f)
    }
}

#[derive(Clone, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct ByteString(pub Vec<u8>);

impl ByteString {
    /// Creates a new [ByteString] that consists of `n` ASCII spaces.
    pub fn spaces(n: usize) -> Self {
        Self(std::iter::repeat_n(b' ', n).collect())
    }
}

impl Borrow<ByteStr> for ByteString {
    fn borrow(&self) -> &ByteStr {
        ByteStr::new(&self.0)
    }
}

impl From<String> for ByteString {
    fn from(value: String) -> Self {
        value.into_bytes().into()
    }
}

impl From<&'_ str> for ByteString {
    fn from(value: &str) -> Self {
        value.as_bytes().into()
    }
}

impl From<Cow<'_, str>> for ByteString {
    fn from(value: Cow<'_, str>) -> Self {
        value.into_owned().into()
    }
}

impl From<Cow<'_, [u8]>> for ByteString {
    fn from(value: Cow<'_, [u8]>) -> Self {
        value.into_owned().into()
    }
}

impl From<Vec<u8>> for ByteString {
    fn from(value: Vec<u8>) -> Self {
        Self(value)
    }
}

impl From<&[u8]> for ByteString {
    fn from(value: &[u8]) -> Self {
        Self(value.into())
    }
}

impl From<&ByteString> for ByteString {
    fn from(value: &ByteString) -> Self {
        value.clone()
    }
}

impl<const N: usize> From<&ByteStrArray<N>> for ByteString {
    fn from(value: &ByteStrArray<N>) -> Self {
        Self::from(value.raw_string_bytes())
    }
}

impl<const N: usize> From<[u8; N]> for ByteString {
    fn from(value: [u8; N]) -> Self {
        value.as_slice().into()
    }
}

impl RawString for ByteString {
    fn raw_string_bytes(&self) -> &[u8] {
        self.0.as_slice()
    }
}

impl Serialize for ByteString {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        if let Ok(s) = str::from_utf8(&self.0) {
            let (variant_index, variant) = if self.0.iter().all(|b| b.is_ascii()) {
                (0, "Ascii")
            } else {
                (1, "Utf8")
            };
            let mut tuple =
                serializer.serialize_tuple_variant("RawString", variant_index, variant, 1)?;
            tuple.serialize_field(s)?;
            tuple.end()
        } else {
            let mut tuple = serializer.serialize_tuple_variant("RawString", 2, "Windows1252", 1)?;
            tuple.serialize_field(&decode_latin1(&self.0))?;
            tuple.end()
        }
    }
}

impl Debug for ByteString {
    // If `s` is valid UTF-8, displays it as UTF-8, otherwise as Latin-1
    // (actually bytes interpreted as Unicode code points).
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        let s =
            from_utf8(&self.0.borrow()).map_or_else(|_| decode_latin1(self.0.borrow()), Cow::from);
        write!(f, "{s:?}")
    }
}

impl MutRawString for ByteString {
    fn resize(&mut self, new_len: usize) -> Result<(), ResizeError> {
        match new_len.cmp(&self.0.len()) {
            Ordering::Less => {
                if !self.0[new_len..].iter().all(|b| *b == b' ') {
                    return Err(ResizeError::TooWide);
                }
                self.0.truncate(new_len);
            }
            Ordering::Equal => (),
            Ordering::Greater => self.0.resize(new_len, b' '),
        }
        Ok(())
    }

    /// Removes any trailing ASCII spaces.
    fn trim_end(&mut self) {
        while self.0.pop_if(|c| *c == b' ').is_some() {}
    }
}

mod encoded;
pub use encoded::{Encoded, EncodedString, WithEncoding};

/// A [Datum] that owns its string data (if any).
pub type OwnedDatum = Datum<WithEncoding<ByteString>>;

/// The value of a [Variable](crate::variable::Variable).
///
/// `T` is the type for a string `Datum`, typically [ByteString] or
/// `WithEncoding<ByteString>` or some borrowed type.
#[derive(Clone)]
pub enum Datum<T> {
    /// A numeric value.
    Number(
        /// A number, or `None` for the system-missing value.
        Option<f64>,
    ),
    /// A string value.
    String(
        /// The value, in the variable's encoding.
        T,
    ),
}

impl Datum<WithEncoding<ByteString>> {
    pub fn new_utf8(s: impl Into<String>) -> Self {
        let s: String = s.into();
        Datum::String(ByteString::from(s).with_encoding(UTF_8))
    }

    pub fn codepage_to_unicode(&mut self) {
        if let Some(s) = self.as_string_mut() {
            s.codepage_to_unicode();
        }
    }

    pub fn without_encoding(self) -> Datum<ByteString> {
        self.map_string(|s| s.into_inner())
    }
}

impl<'a> Datum<WithEncoding<ByteCow<'a>>> {
    pub fn into_owned(self) -> Datum<WithEncoding<ByteString>> {
        self.map_string(|s| s.into_owned())
    }
}

impl<T> Datum<T>
where
    T: EncodedString,
{
    pub fn as_borrowed(&self) -> Datum<WithEncoding<&ByteStr>> {
        self.as_ref().map_string(|s| s.as_encoded_byte_str())
    }
    pub fn cloned(&self) -> Datum<WithEncoding<ByteString>> {
        self.as_ref().map_string(|s| s.cloned())
    }
}

impl<B> Debug for Datum<B>
where
    B: Debug,
{
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        match self {
            Self::Number(Some(number)) => write!(f, "{number:?}"),
            Self::Number(None) => write!(f, "SYSMIS"),
            Self::String(s) => write!(f, "{:?}", s),
        }
    }
}

impl<T> Display for Datum<T>
where
    T: Display,
{
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Number(None) => write!(f, "SYSMIS"),
            Self::Number(Some(number)) => number.display_plain().fmt(f),
            Self::String(string) => string.fmt(f),
        }
    }
}

impl<B> Serialize for Datum<B>
where
    B: Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        match self {
            Self::Number(number) => number.serialize(serializer),
            Self::String(raw_string) => raw_string.serialize(serializer),
        }
    }
}

impl<T, R> PartialEq<Datum<R>> for Datum<T>
where
    T: PartialEq<R>,
{
    fn eq(&self, other: &Datum<R>) -> bool {
        match (self, other) {
            (Self::Number(Some(n1)), Datum::Number(Some(n2))) => {
                OrderedFloat(*n1) == OrderedFloat(*n2)
            }
            (Self::Number(None), Datum::Number(None)) => true,
            (Self::String(s1), Datum::String(s2)) => s1 == s2,
            _ => false,
        }
    }
}

impl<T> Eq for Datum<T> where T: Eq {}

impl<T, R> PartialOrd<Datum<R>> for Datum<T>
where
    T: PartialOrd<R>,
{
    fn partial_cmp(&self, other: &Datum<R>) -> Option<Ordering> {
        match (self, other) {
            (Self::Number(a), Datum::Number(b)) => {
                a.map(OrderedFloat).partial_cmp(&b.map(OrderedFloat))
            }
            (Self::Number(_), Datum::String(_)) => Some(Ordering::Less),
            (Self::String(_), Datum::Number(_)) => Some(Ordering::Greater),
            (Self::String(a), Datum::String(b)) => a.partial_cmp(b),
        }
    }
}

impl<T> Ord for Datum<T>
where
    T: Ord,
{
    fn cmp(&self, other: &Self) -> Ordering {
        self.partial_cmp(other).unwrap()
    }
}

impl<T> Hash for Datum<T>
where
    T: Hash,
{
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        match self {
            Self::Number(number) => number.map(OrderedFloat).hash(state),
            Self::String(string) => string.hash(state),
        }
    }
}

impl<B> Datum<B> {
    pub fn as_ref(&self) -> Datum<&B> {
        match self {
            Datum::Number(number) => Datum::Number(*number),
            Datum::String(string) => Datum::String(&string),
        }
    }

    pub fn map_string<F, R>(self, f: F) -> Datum<R>
    where
        F: Fn(B) -> R,
    {
        match self {
            Datum::Number(number) => Datum::Number(number),
            Datum::String(string) => Datum::String(f(string)),
        }
    }

    /// Constructs a new numerical [Datum] for the system-missing value.
    pub const fn sysmis() -> Self {
        Self::Number(None)
    }

    pub const fn is_sysmis(&self) -> bool {
        matches!(self, Self::Number(None))
    }

    pub const fn is_number(&self) -> bool {
        matches!(self, Self::Number(_))
    }

    pub const fn is_string(&self) -> bool {
        matches!(self, Self::String(_))
    }

    /// Returns the number inside this datum, or `None` if this is a string
    /// datum.
    pub fn as_number(&self) -> Option<Option<f64>> {
        match self {
            Self::Number(number) => Some(*number),
            Self::String(_) => None,
        }
    }

    /// Returns the string inside this datum, or `None` if this is a numeric
    /// datum.
    pub fn as_string(&self) -> Option<&B> {
        match self {
            Self::Number(_) => None,
            Self::String(s) => Some(s),
        }
    }

    /// Returns the string inside this datum, or `None` if this is a numeric
    /// datum.
    pub fn into_string(self) -> Option<B> {
        match self {
            Self::Number(_) => None,
            Self::String(s) => Some(s),
        }
    }

    /// Returns the [VarType] corresponding to this datum.
    pub fn var_type(&self) -> VarType {
        match self {
            Self::Number(_) => VarType::Numeric,
            Self::String(_) => VarType::String,
        }
    }
}

impl<T> Datum<T>
where
    T: RawString,
{
    /// Returns true if this datum can be resized to the given `width` without
    /// loss, which is true only if this datum and `width` are both string or
    /// both numeric and, for string widths, if resizing would not drop any
    /// non-space characters.
    pub fn is_resizable(&self, width: VarWidth) -> bool {
        match (self, width) {
            (Self::Number(_), VarWidth::Numeric) => true,
            (Self::String(s), VarWidth::String(new_width)) => s.is_resizable(new_width as usize),
            _ => false,
        }
    }

    /// Returns the [VarWidth] corresponding to this datum.
    pub fn width(&self) -> VarWidth {
        match self {
            Self::Number(_) => VarWidth::Numeric,
            Self::String(s) => VarWidth::String(s.len().try_into().unwrap()),
        }
    }

    /// Compares this datum and `other` for equality, ignoring trailing ASCII
    /// spaces in either, if they are both strings, for the purpose of
    /// comparison.
    pub fn eq_ignore_trailing_spaces<R>(&self, other: &Datum<R>) -> bool
    where
        R: RawString,
    {
        match (self, other) {
            (Self::String(a), Datum::String(b)) => a.eq_ignore_trailing_spaces(b),
            (Self::Number(a), Datum::Number(b)) => a == b,
            _ => false,
        }
    }

    pub fn as_raw(&self) -> Datum<&ByteStr> {
        self.as_ref().map_string(|s| s.as_ref())
    }

    pub fn as_encoded(&self, encoding: &'static Encoding) -> Datum<WithEncoding<&ByteStr>> {
        self.as_ref().map_string(|s| s.as_encoded(encoding))
    }

    pub fn with_encoding(self, encoding: &'static Encoding) -> Datum<WithEncoding<T>> {
        self.map_string(|s| s.with_encoding(encoding))
    }
}

impl<B> Datum<B>
where
    B: EncodedString,
{
    pub fn quoted<'a>(&'a self) -> QuotedDatum<'a, B> {
        QuotedDatum(self)
    }
}

pub struct QuotedDatum<'a, B>(&'a Datum<B>);

impl<'a, B> Display for QuotedDatum<'a, B>
where
    B: Display,
{
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match &self.0 {
            Datum::Number(None) => write!(f, "SYSMIS"),
            Datum::Number(Some(number)) => number.display_plain().fmt(f),
            Datum::String(string) => write!(f, "\"{string}\""),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ResizeError {
    MixedTypes,
    TooWide,
}

impl<T> Datum<T> {
    /// Returns the string inside this datum as a mutable borrow, or `None` if
    /// this is a numeric datum.
    pub fn as_string_mut(&mut self) -> Option<&mut T> {
        match self {
            Self::Number(_) => None,
            Self::String(s) => Some(s.borrow_mut()),
        }
    }

    /// Removes trailing ASCII spaces from this datum, if it is a string.
    pub fn trim_end(&mut self)
    where
        T: MutRawString,
    {
        self.as_string_mut().map(|s| s.trim_end());
    }

    /// Resizes this datum to the given `width`.  Returns an error, without
    /// modifying the datum, if [is_resizable](Self::is_resizable) would return
    /// false.
    pub fn resize(&mut self, width: VarWidth) -> Result<(), ResizeError>
    where
        T: MutRawString,
    {
        match (self, width) {
            (Self::Number(_), VarWidth::Numeric) => Ok(()),
            (Self::String(s), VarWidth::String(new_width)) => s.resize(new_width as usize),
            _ => Err(ResizeError::MixedTypes),
        }
    }
}

impl<B> From<f64> for Datum<B> {
    fn from(number: f64) -> Self {
        Some(number).into()
    }
}

impl<B> From<Option<f64>> for Datum<B> {
    fn from(value: Option<f64>) -> Self {
        Self::Number(value)
    }
}

impl<'a> From<&'a str> for Datum<&'a ByteStr> {
    fn from(value: &'a str) -> Self {
        Datum::String(ByteStr::new(value.as_bytes()))
    }
}

impl<'a> From<&'a [u8]> for Datum<&'a ByteStr> {
    fn from(value: &'a [u8]) -> Self {
        Self::String(ByteStr::new(value))
    }
}

/// A case in a data set.
#[derive(Clone, Debug, Serialize)]
pub struct RawCase(
    /// One [Datum] per variable in the corresponding [Dictionary], in the same
    /// order.
    ///
    /// [Dictionary]: crate::dictionary::Dictionary
    pub Vec<Datum<ByteString>>,
);

impl RawCase {
    pub fn as_encoding(&self, encoding: &'static Encoding) -> Case<&'_ [Datum<ByteString>]> {
        Case {
            encoding,
            data: &self.0,
        }
    }
    pub fn with_encoding(self, encoding: &'static Encoding) -> Case<Vec<Datum<ByteString>>> {
        Case {
            encoding,
            data: self.0,
        }
    }
}

pub struct Case<B>
where
    B: Borrow<[Datum<ByteString>]>,
{
    encoding: &'static Encoding,
    data: B,
}

impl<B> Case<B>
where
    B: Borrow<[Datum<ByteString>]>,
{
    pub fn len(&self) -> usize {
        self.data.borrow().len()
    }
    pub fn iter(&self) -> CaseIter<'_> {
        self.into_iter()
    }
}

impl Case<Vec<Datum<ByteString>>> {
    pub fn into_unicode(self) -> Self {
        if self.encoding == UTF_8 {
            self
        } else {
            Self {
                encoding: UTF_8,
                data: self
                    .data
                    .into_iter()
                    .map(|datum| {
                        datum.map_string(|s| {
                            let mut s = s.with_encoding(self.encoding);
                            s.codepage_to_unicode();
                            s.into_inner()
                        })
                    })
                    .collect(),
            }
        }
    }
}

impl<B> Serialize for Case<B>
where
    B: Borrow<[Datum<ByteString>]>,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut seq = serializer.serialize_seq(Some(self.len()))?;
        for datum in self.iter() {
            seq.serialize_element(&datum)?;
        }
        seq.end()
    }
}

pub struct CaseIter<'a> {
    encoding: &'static Encoding,
    iter: std::slice::Iter<'a, Datum<ByteString>>,
}

impl<'a> Iterator for CaseIter<'a> {
    type Item = Datum<WithEncoding<&'a ByteStr>>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|d| d.as_encoded(self.encoding))
    }
}

impl<'a, B> IntoIterator for &'a Case<B>
where
    B: Borrow<[Datum<ByteString>]>,
{
    type Item = Datum<WithEncoding<&'a ByteStr>>;

    type IntoIter = CaseIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        CaseIter {
            encoding: self.encoding,
            iter: self.data.borrow().into_iter(),
        }
    }
}

impl IntoIterator for Case<Vec<Datum<ByteString>>> {
    type Item = Datum<WithEncoding<ByteString>>;

    type IntoIter = CaseIntoIter;

    fn into_iter(self) -> Self::IntoIter {
        CaseIntoIter {
            encoding: self.encoding,
            iter: self.data.into_iter(),
        }
    }
}

pub struct CaseIntoIter {
    encoding: &'static Encoding,
    iter: std::vec::IntoIter<Datum<ByteString>>,
}

impl Iterator for CaseIntoIter {
    type Item = Datum<WithEncoding<ByteString>>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter
            .next()
            .map(|datum| datum.with_encoding(self.encoding))
    }
}

pub struct Quoted<T>(T)
where
    T: Display;

impl<T> Display for Quoted<T>
where
    T: Display,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "\"{}\"", &self.0)
    }
}
