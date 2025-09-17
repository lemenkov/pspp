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

//! Variables.

use std::{
    collections::BTreeMap,
    fmt::{Debug, Display},
    hash::{DefaultHasher, Hash, Hasher},
    ops::{Deref, Not},
    str::FromStr,
};

use displaydoc::Display;
use encoding_rs::{Encoding, UTF_8};
use hashbrown::HashMap;
use indexmap::Equivalent;
use num::integer::div_ceil;
use serde::{ser::SerializeSeq, Serialize};
use thiserror::Error as ThisError;
use unicase::UniCase;

use crate::{
    data::{
        ByteStr, ByteString, Datum, Encoded, EncodedString, RawString, ResizeError, WithEncoding,
    },
    format::{DisplayPlain, Format},
    identifier::{HasIdentifier, Identifier},
};

/// Variable type.
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
pub enum VarType {
    /// A numeric variable.
    Numeric,

    /// A string variable.
    ///
    /// The string width is unspecified; use [VarWidth] for type and width
    /// together.
    String,
}

impl Not for VarType {
    type Output = Self;

    fn not(self) -> Self::Output {
        match self {
            Self::Numeric => Self::String,
            Self::String => Self::Numeric,
        }
    }
}

impl Not for &VarType {
    type Output = VarType;

    fn not(self) -> Self::Output {
        !*self
    }
}

impl Display for VarType {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            VarType::Numeric => write!(f, "numeric"),
            VarType::String => write!(f, "string"),
        }
    }
}

/// A variable's width.
///
/// This is essentially [VarType] plus a width for [VarType::String].
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub enum VarWidth {
    /// A numeric variable.
    Numeric,

    /// A string variable.
    String(
        /// The width of the string variable.
        ///
        /// Must be in `1..=32767`, although the type system does not yet
        /// enforce this.
        u16,
    ), // XXX change to NonZeroU16, or to 1..=32767 range type
}

impl VarWidth {
    pub const MAX_STRING: u16 = 32767;

    pub fn n_dict_indexes(self) -> usize {
        match self {
            VarWidth::Numeric => 1,
            VarWidth::String(w) => div_ceil(w as usize, 8),
        }
    }

    fn width_predicate(a: VarWidth, b: VarWidth, f: impl Fn(u16, u16) -> u16) -> Option<VarWidth> {
        match (a, b) {
            (VarWidth::Numeric, VarWidth::Numeric) => Some(VarWidth::Numeric),
            (VarWidth::String(a), VarWidth::String(b)) => Some(VarWidth::String(f(a, b))),
            _ => None,
        }
    }

    /// Returns the wider of `self` and `other`:
    /// - Numerical variable widths are equally wide.
    /// - Longer strings are wider than shorter strings.
    /// - Numerical and string types are incomparable, so result in `None`.
    pub fn wider(a: VarWidth, b: VarWidth) -> Option<VarWidth> {
        Self::width_predicate(a, b, |a, b| a.max(b))
    }

    /// Returns the narrower of `self` and `other` (see [`Self::wider`]).
    pub fn narrower(a: VarWidth, b: VarWidth) -> Option<VarWidth> {
        Self::width_predicate(a, b, |a, b| a.min(b))
    }

    pub fn default_display_width(&self) -> u32 {
        match self {
            VarWidth::Numeric => 8,
            VarWidth::String(width) => *width.min(&32) as u32,
        }
    }

    pub fn is_long_string(&self) -> bool {
        if let Self::String(width) = self {
            *width > 8
        } else {
            false
        }
    }

    pub fn as_string_width(&self) -> Option<usize> {
        match self {
            VarWidth::Numeric => None,
            VarWidth::String(width) => Some(*width as usize),
        }
    }

    pub fn is_numeric(&self) -> bool {
        *self == Self::Numeric
    }

    pub fn is_string(&self) -> bool {
        !self.is_numeric()
    }

    /// Returns true if this is a very long string width, meaning wider than 255
    /// bytes, which was the limit for old versions of SPSS.
    pub fn is_very_long_string(&self) -> bool {
        match *self {
            VarWidth::Numeric => false,
            VarWidth::String(width) => width > 255,
        }
    }

    /// Number of bytes per segment by which the amount of space for very long
    /// string variables is allocated.
    pub const SEGMENT_SIZE: usize = 252;

    /// Returns an iterator over the "segments" used for writing case data for a
    /// variable with this width.  A segment is a physical variable in the
    /// system file that represents some piece of a logical variable as seen by
    /// a PSPP user.  Most variables have one segment whose width is their own
    /// width, but very long string variables, with width greater than 255, have
    /// multiple segments each with width 255 or less.
    pub fn segments(&self) -> Segments {
        Segments::new(*self)
    }

    /// Returns the number of 8-byte chunks used for writing case data for a
    /// variable with this width.  Very long string variables (wider than 255
    /// bytes) cannot directly be divided into chunks: they must first be
    /// divided into multiple [segments](Self::segments), which can then be
    /// divided into chunks.
    pub fn n_chunks(&self) -> Option<usize> {
        match *self {
            VarWidth::Numeric => Some(1),
            VarWidth::String(w) if w <= 255 => Some(w.div_ceil(8) as usize),
            VarWidth::String(_) => None,
        }
    }

    /// Returns the width to allocate to the segment with the given
    /// `segment_idx` within this variable.  A segment is a physical variable in
    /// the system file that represents some piece of a logical variable as seen
    /// by a PSPP user.
    pub fn segment_alloc_width(&self, segment_idx: usize) -> usize {
        debug_assert!(segment_idx < self.segments().len());
        debug_assert!(self.is_very_long_string());

        if segment_idx < self.segments().len() - 1 {
            255
        } else {
            self.as_string_width().unwrap() - segment_idx * Self::SEGMENT_SIZE
        }
    }

    pub fn display_adjective(&self) -> VarWidthAdjective {
        VarWidthAdjective(*self)
    }

    pub fn codepage_to_unicode(&mut self) {
        match self {
            VarWidth::Numeric => (),
            VarWidth::String(width) => *width = width.saturating_mul(3).min(Self::MAX_STRING),
        }
    }
}

pub struct Segments {
    width: VarWidth,
    i: usize,
    n: usize,
}
impl Segments {
    pub fn new(width: VarWidth) -> Self {
        Self {
            width,
            i: 0,
            n: if width.is_very_long_string() {
                width
                    .as_string_width()
                    .unwrap()
                    .div_ceil(VarWidth::SEGMENT_SIZE)
            } else {
                1
            },
        }
    }
}

impl Iterator for Segments {
    type Item = VarWidth;

    fn next(&mut self) -> Option<Self::Item> {
        let i = self.i;
        if i >= self.n {
            None
        } else {
            self.i += 1;
            match self.width {
                VarWidth::Numeric => Some(VarWidth::Numeric),
                VarWidth::String(_) if i < self.n - 1 => Some(VarWidth::String(255)),
                VarWidth::String(width) => Some(VarWidth::String(
                    width - (self.n as u16 - 1) * VarWidth::SEGMENT_SIZE as u16,
                )),
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let n = self.n - self.i;
        (n, Some(n))
    }
}

impl ExactSizeIterator for Segments {}

impl From<VarWidth> for VarType {
    fn from(source: VarWidth) -> Self {
        match source {
            VarWidth::Numeric => VarType::Numeric,
            VarWidth::String(_) => VarType::String,
        }
    }
}

pub struct VarWidthAdjective(VarWidth);

impl Display for VarWidthAdjective {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            VarWidth::Numeric => write!(f, "numeric"),
            VarWidth::String(width) => write!(f, "{width}-byte string"),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize)]
pub enum Role {
    #[default]
    Input,
    Target,
    Both,
    None,
    Partition,
    Split,
}

impl Role {
    pub fn as_str(&self) -> &'static str {
        match self {
            Role::Input => "Input",
            Role::Target => "Target",
            Role::Both => "Both",
            Role::None => "None",
            Role::Partition => "Partition",
            Role::Split => "Split",
        }
    }
}

impl FromStr for Role {
    type Err = InvalidRole;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        for (string, value) in [
            ("input", Role::Input),
            ("target", Role::Target),
            ("both", Role::Both),
            ("none", Role::None),
            ("partition", Role::Partition),
            ("split", Role::Split),
        ] {
            if string.eq_ignore_ascii_case(s) {
                return Ok(value);
            }
        }
        Err(InvalidRole::UnknownRole(s.into()))
    }
}

impl TryFrom<i32> for Role {
    type Error = InvalidRole;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Role::Input),
            1 => Ok(Role::Target),
            2 => Ok(Role::Both),
            3 => Ok(Role::None),
            4 => Ok(Role::Partition),
            5 => Ok(Role::Split),
            _ => Err(InvalidRole::UnknownRole(value.to_string())),
        }
    }
}

impl From<Role> for i32 {
    fn from(value: Role) -> Self {
        match value {
            Role::Input => 0,
            Role::Target => 1,
            Role::Both => 2,
            Role::None => 3,
            Role::Partition => 4,
            Role::Split => 5,
        }
    }
}

#[derive(Clone, Default, PartialEq, Eq, Serialize)]
pub struct Attributes(pub BTreeMap<Identifier, Vec<String>>);

impl Attributes {
    pub fn new() -> Self {
        Self(BTreeMap::new())
    }

    pub fn contains_name(&self, name: &Identifier) -> bool {
        self.0.contains_key(name)
    }

    pub fn insert(&mut self, name: Identifier, values: Vec<String>) {
        self.0.insert(name, values);
    }

    pub fn with(mut self, name: Identifier, values: Vec<String>) -> Self {
        self.insert(name, values);
        self
    }

    pub fn append(&mut self, other: &mut Self) {
        self.0.append(&mut other.0)
    }

    pub fn role(&self) -> Result<Option<Role>, InvalidRole> {
        self.try_into()
    }

    pub fn iter(&self, include_at: bool) -> impl Iterator<Item = (&Identifier, &[String])> {
        self.0.iter().filter_map(move |(name, values)| {
            if include_at || !name.0.starts_with('@') {
                Some((name, values.as_slice()))
            } else {
                None
            }
        })
    }

    pub fn has_any(&self, include_at: bool) -> bool {
        self.iter(include_at).next().is_some()
    }

    pub fn codepage_to_unicode(&mut self) {
        let mut new = BTreeMap::new();
        while let Some((mut name, value)) = self.0.pop_first() {
            name.codepage_to_unicode();
            new.insert(name, value);
        }
        self.0 = new;
    }
}

impl Debug for Attributes {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(Clone, Debug, ThisError, PartialEq, Eq)]
pub enum InvalidRole {
    #[error("Unknown role {0:?}.")]
    UnknownRole(String),

    #[error("Role attribute $@Role must have exactly one value (not {0}).")]
    InvalidValues(usize),
}

impl TryFrom<&Attributes> for Option<Role> {
    type Error = InvalidRole;

    fn try_from(value: &Attributes) -> Result<Self, Self::Error> {
        let role = Identifier::new("$@Role").unwrap();
        value.0.get(&role).map_or(Ok(None), |attribute| {
            if let Ok([string]) = <&[String; 1]>::try_from(attribute.as_slice()) {
                match string.parse::<i32>() {
                    Ok(integer) => Ok(Some(Role::try_from(integer)?)),
                    Err(_) => Err(InvalidRole::UnknownRole(string.clone())),
                }
            } else {
                Err(InvalidRole::InvalidValues(attribute.len()))
            }
        })
    }
}

/// A variable, usually inside a [Dictionary].
///
/// [Dictionary]: crate::dictionary::Dictionary
#[derive(Clone, Debug, Serialize)]
pub struct Variable {
    /// The variable's name.
    ///
    /// PSPP variable names are case-insensitive.
    pub name: Identifier,

    /// Variable width.
    pub width: VarWidth,

    /// User-missing values.
    ///
    /// Numeric variables also have a system-missing value (represented as
    /// `None`).
    ///
    /// Both kinds of missing values are excluded from most analyses.
    missing_values: MissingValues,

    /// Output format used in most contexts.
    pub print_format: Format,

    /// Output format used on the `WRITE` command.
    pub write_format: Format,

    /// Value labels.
    pub value_labels: ValueLabels,

    /// Variable label, an optional meaningful description for the variable
    /// itself.
    pub label: Option<String>,

    /// Measurement level for the variable's data.
    pub measure: Option<Measure>,

    /// Role in data analysis.
    pub role: Role,

    /// Width of data column in GUI.
    pub display_width: u32,

    /// Data alignment in GUI.
    pub alignment: Alignment,

    /// Whether to retain values of the variable from one case to the next.
    pub leave: bool,

    /// For compatibility with old software that supported at most 8-character
    /// variable names.
    pub short_names: Vec<Identifier>,

    /// Variable attributes.
    pub attributes: Attributes,

    /// Encoding for [Value]s inside this variable.
    ///
    /// The variables in a [Dictionary] must all use the same encoding as the
    /// dictionary.
    encoding: &'static Encoding,
}

impl Variable {
    pub fn new(name: Identifier, width: VarWidth, encoding: &'static Encoding) -> Self {
        let var_type = VarType::from(width);
        let leave = name.class().must_leave();
        Self {
            name,
            width,
            missing_values: MissingValues::default(),
            print_format: Format::default_for_width(width),
            write_format: Format::default_for_width(width),
            value_labels: ValueLabels::new(),
            label: None,
            measure: Measure::default_for_type(var_type),
            role: Role::default(),
            display_width: width.default_display_width(),
            alignment: Alignment::default_for_type(var_type),
            leave,
            short_names: Vec::new(),
            attributes: Attributes::new(),
            encoding,
        }
    }

    pub fn encoding(&self) -> &'static Encoding {
        self.encoding
    }

    pub fn is_numeric(&self) -> bool {
        self.width.is_numeric()
    }

    pub fn is_string(&self) -> bool {
        self.width.is_string()
    }

    pub fn label(&self) -> Option<&String> {
        self.label.as_ref()
    }

    pub fn resize(&mut self, width: VarWidth) {
        let _ = self.missing_values.resize(width);

        self.value_labels.resize(width);

        self.print_format.resize(width);
        self.write_format.resize(width);

        self.width = width;
    }

    pub fn missing_values(&self) -> &MissingValues {
        &self.missing_values
    }

    pub fn missing_values_mut(&mut self) -> MissingValuesMut<'_> {
        MissingValuesMut {
            inner: &mut self.missing_values,
            width: self.width,
        }
    }

    pub fn codepage_to_unicode(&mut self) {
        self.name.codepage_to_unicode();
        self.width.codepage_to_unicode();
        self.missing_values.codepage_to_unicode();
        self.print_format.codepage_to_unicode();
        self.write_format.codepage_to_unicode();
        self.attributes.codepage_to_unicode();
        self.encoding = UTF_8;

        // Anything old enough to not support long names is old enough not to
        // support Unicode.
        self.short_names.clear();
    }
}

impl HasIdentifier for Variable {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

/// Associates values of a variable with meaningful labels.
///
/// For example, 1 => strongly disagree, 2 => disagree, 3 => neither agree nor
/// disagree, ...
#[derive(Clone, Default, PartialEq, Eq)]
pub struct ValueLabels(pub HashMap<Datum<ByteString>, String>);

impl Equivalent<Datum<ByteString>> for Datum<&ByteStr> {
    fn equivalent(&self, key: &Datum<ByteString>) -> bool {
        self == key
    }
}

impl ValueLabels {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }

    pub fn get<T>(&self, value: &Datum<T>) -> Option<&str>
    where
        T: RawString,
    {
        self.0.get(&value.as_raw()).map(|s| s.as_str())
    }

    pub fn insert(&mut self, value: Datum<ByteString>, label: impl Into<String>) -> Option<String> {
        self.0.insert(value, label.into())
    }

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        self.0.keys().all(|datum| datum.is_resizable(width))
    }

    pub fn resize(&mut self, width: VarWidth) {
        self.0 = self
            .0
            .drain()
            .filter_map(|(mut datum, string)| {
                datum.resize(width).is_ok().then_some((datum, string))
            })
            .collect();
    }

    pub fn codepage_to_unicode(&mut self, encoding: &'static Encoding) {
        self.0 = self
            .0
            .drain()
            .map(|(key, value)| {
                let mut key = key.with_encoding(encoding);
                key.codepage_to_unicode();
                (key.without_encoding(), value)
            })
            .collect();
    }
}

impl Serialize for ValueLabels {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut map = serializer.serialize_seq(Some(self.0.len()))?;
        for tuple in &self.0 {
            map.serialize_element(&tuple)?;
        }
        map.end()
    }
}

impl Debug for ValueLabels {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

impl Hash for ValueLabels {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let mut hash = 0;
        for (k, v) in &self.0 {
            let mut hasher = DefaultHasher::new();
            k.hash(&mut hasher);
            v.hash(&mut hasher);
            hash ^= hasher.finish();
        }
        state.write_u64(hash);
    }
}

impl<'a> IntoIterator for &'a ValueLabels {
    type Item = (&'a Datum<ByteString>, &'a String);

    type IntoIter = hashbrown::hash_map::Iter<'a, Datum<ByteString>, String>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.iter()
    }
}

pub struct MissingValuesMut<'a> {
    inner: &'a mut MissingValues,
    width: VarWidth,
}

impl<'a> Deref for MissingValuesMut<'a> {
    type Target = MissingValues;

    fn deref(&self) -> &Self::Target {
        self.inner
    }
}

impl<'a> MissingValuesMut<'a> {
    pub fn replace(&mut self, mut new: MissingValues) -> Result<(), MissingValuesError> {
        new.resize(self.width)?;
        *self.inner = new;
        Ok(())
    }

    pub fn add_value(
        &mut self,
        mut value: Datum<WithEncoding<ByteString>>,
    ) -> Result<(), MissingValuesError> {
        if self.inner.values.len() > 2
            || (self.inner.range().is_some() && self.inner.values.len() > 1)
        {
            Err(MissingValuesError::TooMany)
        } else if value.var_type() != VarType::from(self.width) {
            Err(MissingValuesError::MixedTypes)
        } else if value == Datum::Number(None) {
            Err(MissingValuesError::SystemMissing)
        } else if value.resize(self.width.min(VarWidth::String(8))).is_err() {
            Err(MissingValuesError::TooWide)
        } else {
            value.trim_end();
            self.inner.values.push(value);
            Ok(())
        }
    }

    pub fn add_values(
        &mut self,
        values: impl IntoIterator<Item = Datum<WithEncoding<ByteString>>>,
    ) -> Result<(), MissingValuesError> {
        let n = self.inner.values.len();
        for value in values {
            self.add_value(value)
                .inspect_err(|_| self.inner.values.truncate(n))?;
        }
        Ok(())
    }

    pub fn add_range(&mut self, range: MissingValueRange) -> Result<(), MissingValuesError> {
        if self.inner.range.is_some() || self.inner.values().len() > 1 {
            Err(MissingValuesError::TooMany)
        } else if self.width != VarWidth::Numeric {
            Err(MissingValuesError::MixedTypes)
        } else {
            self.inner.range = Some(range);
            Ok(())
        }
    }
}

// Currently doesn't filter out duplicates (should it?).
#[derive(Clone, Default, Serialize, PartialEq)]
pub struct MissingValues {
    /// Individual missing values, up to 3 of them.
    values: Vec<Datum<WithEncoding<ByteString>>>,

    /// Optional range of missing values.
    range: Option<MissingValueRange>,
}

impl Debug for MissingValues {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{self}")
    }
}

impl Display for MissingValues {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(range) = &self.range {
            write!(f, "{range}")?;
            if !self.values.is_empty() {
                write!(f, "; ")?;
            }
        }

        for (i, value) in self.values.iter().enumerate() {
            if i > 0 {
                write!(f, "; ")?;
            }
            write!(f, "{}", value.quoted())?;
        }

        if self.is_empty() {
            write!(f, "none")?;
        }
        Ok(())
    }
}

/// Invalid missing values.
#[derive(Display, Copy, Clone, Debug, ThisError)]
pub enum MissingValuesError {
    /// Too many missing values.
    TooMany,

    /// Missing values too wide (missing values may be no wider than 8 bytes).
    TooWide,

    /// Missing values must be all string or all numeric.
    MixedTypes,

    /// The system-missing value may not be a user-missing value.
    SystemMissing,
}

impl From<ResizeError> for MissingValuesError {
    fn from(value: ResizeError) -> Self {
        match value {
            ResizeError::MixedTypes => MissingValuesError::MixedTypes,
            ResizeError::TooWide => MissingValuesError::TooWide,
        }
    }
}

impl MissingValues {
    pub fn clear(&mut self) {
        *self = Self::default();
    }
    pub fn values(&self) -> &[Datum<WithEncoding<ByteString>>] {
        &self.values
    }

    pub fn range(&self) -> Option<&MissingValueRange> {
        self.range.as_ref()
    }

    pub fn new(
        mut values: Vec<Datum<WithEncoding<ByteString>>>,
        range: Option<MissingValueRange>,
    ) -> Result<Self, MissingValuesError> {
        if values.len() > 3 {
            return Err(MissingValuesError::TooMany);
        }

        let mut var_type = None;
        for value in values.iter_mut() {
            value.trim_end();
            if value.width().is_long_string() {
                return Err(MissingValuesError::TooWide);
            }
            if var_type.is_some_and(|t| t != value.var_type()) {
                return Err(MissingValuesError::MixedTypes);
            }
            var_type = Some(value.var_type());
        }

        if var_type == Some(VarType::String) && range.is_some() {
            return Err(MissingValuesError::MixedTypes);
        }

        Ok(Self { values, range })
    }

    pub fn is_empty(&self) -> bool {
        self.values.is_empty() && self.range.is_none()
    }

    pub fn var_type(&self) -> Option<VarType> {
        if let Some(datum) = self.values.first() {
            Some(datum.var_type())
        } else if self.range.is_some() {
            Some(VarType::Numeric)
        } else {
            None
        }
    }

    pub fn contains<S>(&self, value: &Datum<S>) -> bool
    where
        S: EncodedString,
    {
        if self
            .values
            .iter()
            .any(|datum| datum.eq_ignore_trailing_spaces(value))
        {
            return true;
        }

        if let Some(Some(number)) = value.as_number()
            && let Some(range) = self.range
        {
            range.contains(number)
        } else {
            false
        }
    }

    pub fn resize(&mut self, width: VarWidth) -> Result<(), MissingValuesError> {
        fn inner(this: &mut MissingValues, width: VarWidth) -> Result<(), MissingValuesError> {
            for datum in &mut this.values {
                datum.resize(width)?;
                datum.trim_end();
            }
            if let Some(range) = &mut this.range {
                range.resize(width)?;
            }
            Ok(())
        }
        inner(self, width).inspect_err(|_| self.clear())
    }

    pub fn codepage_to_unicode(&mut self) {
        self.values = self
            .values
            .drain(..)
            .map(|value| match value {
                Datum::Number(number) => Datum::Number(number),
                Datum::String(s) => Datum::String(if s.encoding() != UTF_8 {
                    let mut new_s = ByteString::from(s.as_str());
                    new_s.0.truncate(8);
                    WithEncoding::new(new_s, UTF_8)
                } else {
                    s
                }),
            })
            .collect();
    }
}

#[derive(Copy, Clone, Debug, Serialize, PartialEq)]
pub enum MissingValueRange {
    In { low: f64, high: f64 },
    From { low: f64 },
    To { high: f64 },
}

impl MissingValueRange {
    pub fn new(low: f64, high: f64) -> Self {
        const LOWEST: f64 = f64::MIN.next_up();
        match (low, high) {
            (f64::MIN | LOWEST, _) => Self::To { high },
            (_, f64::MAX) => Self::From { low },
            (_, _) => Self::In { low, high },
        }
    }

    pub fn low(&self) -> Option<f64> {
        match self {
            MissingValueRange::In { low, .. } | MissingValueRange::From { low } => Some(*low),
            MissingValueRange::To { .. } => None,
        }
    }

    pub fn high(&self) -> Option<f64> {
        match self {
            MissingValueRange::In { high, .. } | MissingValueRange::To { high } => Some(*high),
            MissingValueRange::From { .. } => None,
        }
    }

    pub fn contains(&self, number: f64) -> bool {
        match self {
            MissingValueRange::In { low, high } => (*low..*high).contains(&number),
            MissingValueRange::From { low } => number >= *low,
            MissingValueRange::To { high } => number <= *high,
        }
    }

    pub fn resize(&self, width: VarWidth) -> Result<(), MissingValuesError> {
        if width.is_numeric() {
            Ok(())
        } else {
            Err(MissingValuesError::MixedTypes)
        }
    }
}

impl Display for MissingValueRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.low() {
            Some(low) => low.display_plain().fmt(f)?,
            None => write!(f, "LOW")?,
        }

        write!(f, " THRU ")?;

        match self.high() {
            Some(high) => high.display_plain().fmt(f)?,
            None => write!(f, "HIGH")?,
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
pub enum Alignment {
    Left,
    Right,
    Center,
}

impl Alignment {
    pub fn default_for_type(var_type: VarType) -> Self {
        match var_type {
            VarType::Numeric => Self::Right,
            VarType::String => Self::Left,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Alignment::Left => "Left",
            Alignment::Right => "Right",
            Alignment::Center => "Center",
        }
    }
}

/// [Level of measurement](https://en.wikipedia.org/wiki/Level_of_measurement).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
pub enum Measure {
    /// Nominal values can only be compared for equality.
    Nominal,

    /// Ordinal values can be meaningfully ordered.
    Ordinal,

    /// Scale values can be meaningfully compared for the degree of difference.
    Scale,
}

impl Measure {
    pub fn default_for_type(var_type: VarType) -> Option<Measure> {
        match var_type {
            VarType::Numeric => None,
            VarType::String => Some(Self::Nominal),
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Measure::Nominal => "Nominal",
            Measure::Ordinal => "Ordinal",
            Measure::Scale => "Scale",
        }
    }
}

#[cfg(test)]
mod tests {
    use encoding_rs::{UTF_8, WINDOWS_1252};

    use crate::{
        data::{ByteString, Datum, RawString, WithEncoding},
        variable::{MissingValues, ValueLabels, VarWidth},
    };

    #[test]
    fn var_width_codepage_to_unicode() {
        fn check_unicode(input: VarWidth, expected: VarWidth) {
            let mut actual = input;
            actual.codepage_to_unicode();
            assert_eq!(actual, expected);
        }

        check_unicode(VarWidth::Numeric, VarWidth::Numeric);
        check_unicode(VarWidth::String(1), VarWidth::String(3));
        check_unicode(VarWidth::String(2), VarWidth::String(6));
        check_unicode(VarWidth::String(3), VarWidth::String(9));
        check_unicode(VarWidth::String(1000), VarWidth::String(3000));
        check_unicode(VarWidth::String(20000), VarWidth::String(32767));
        check_unicode(VarWidth::String(30000), VarWidth::String(32767));
    }

    #[test]
    fn missing_values_codepage_to_unicode() {
        fn windows_1252(s: &str) -> WithEncoding<ByteString> {
            ByteString::from(WINDOWS_1252.encode(s).0).with_encoding(WINDOWS_1252)
        }

        let mut actual = MissingValues::new(
            vec![
                Datum::String(windows_1252("abcdefgh")),
                Datum::String(windows_1252("éèäî   ")),
                Datum::String(windows_1252("aaéèäîdf")),
            ],
            None,
        )
        .unwrap();
        actual.codepage_to_unicode();

        fn utf_8(s: &str) -> WithEncoding<ByteString> {
            ByteString::from(s).with_encoding(UTF_8)
        }

        let expected = MissingValues::new(
            vec![
                Datum::String(utf_8("abcdefgh")),
                Datum::String(utf_8("éèäî")),
                Datum::String(utf_8("aaéèä")),
            ],
            None,
        )
        .unwrap();

        assert_eq!(&actual, &expected);
    }

    #[test]
    fn value_labels_codepage_to_unicode() {
        fn windows_1252(s: &str) -> Datum<ByteString> {
            Datum::String(ByteString::from(WINDOWS_1252.encode(s).0))
        }

        let mut actual = ValueLabels::new();
        actual.insert(windows_1252("abcd"), "Label 1");
        actual.insert(windows_1252("éèäî"), "Label 2");
        actual.codepage_to_unicode(WINDOWS_1252);

        let mut expected = ValueLabels::new();
        expected.insert(Datum::String(ByteString::from("abcd        ")), "Label 1");
        expected.insert(Datum::String(ByteString::from("éèäî    ")), "Label 2");

        assert_eq!(&actual, &expected);
    }
}
