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
    borrow::Cow,
    cmp::Ordering,
    collections::HashMap,
    fmt::{Display, Write as _},
    fs::File,
    io::{BufWriter, Error, Write},
    path::Path,
};

use chrono::{Local, NaiveDateTime};
use libm::frexp;
use smallvec::SmallVec;

use crate::{
    data::{Datum, RawString},
    dictionary::Dictionary,
    por::PORTABLE_TO_WINDOWS_1252,
    variable::{MissingValueRange, ValueLabels},
};

/// Precision for floating-point numbers in a portable file.
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Precision(
    /// Precision in base-30 digits (the base used in portable files).
    u32,
);

impl Default for Precision {
    fn default() -> Self {
        Self::from_base_10_digits(f64::DIGITS)
    }
}

impl Precision {
    pub fn from_base_10_digits(digits: u32) -> Self {
        match digits {
            0..=1 => Self(1),
            2 => Self(2),
            3..=4 => Self(3),
            5 => Self(4),
            6..=7 => Self(5),
            8 => Self(6),
            9..=10 => Self(7),
            11 => Self(8),
            12..=13 => Self(9),
            14 => Self(10),
            15.. => Self(11),
        }
    }

    pub fn from_base_30_digits(digits: u32) -> Self {
        Self(digits.clamp(1, 10))
    }

    pub fn as_base_10_digits(&self) -> u32 {
        match self.0 {
            1 => 1,
            2 => 2,
            3 => 4,
            4 => 5,
            5 => 7,
            6 => 8,
            7 => 10,
            8 => 11,
            9 => 13,
            10 => 14,
            11 => 15,
            _ => unreachable!(),
        }
    }

    pub fn as_base_30_digits(&self) -> u32 {
        self.0
    }
}

/// Options for writing a portable file.
#[derive(Clone, Debug)]
pub struct WriteOptions {
    /// Date and time to write to the file.
    pub timestamp: NaiveDateTime,

    /// Product name.
    pub product: Cow<'static, str>,

    /// Subproduct name.
    pub product_ext: Option<Cow<'static, str>>,

    /// Author.
    pub author: Option<String>,

    /// Precision.
    pub precision: Precision,
}

impl Default for WriteOptions {
    fn default() -> Self {
        Self {
            timestamp: Local::now().naive_local(),
            product: Cow::from(concat!("GNU PSPP (Rust) ", env!("CARGO_PKG_VERSION"))),
            product_ext: None,
            author: None,
            precision: Precision::default(),
        }
    }
}

impl WriteOptions {
    /// Constructs a new set of default options.
    pub fn new() -> Self {
        Self::default()
    }

    /// Returns `self` with the timestamp to be written set to `timestamp`.
    pub fn with_timestamp(self, timestamp: NaiveDateTime) -> Self {
        Self { timestamp, ..self }
    }

    /// Returns `self` with the product set to `product`.
    pub fn with_product(self, product: Cow<'static, str>) -> Self {
        Self { product, ..self }
    }

    /// Returns `self` with the extended product set to `product_ext`.
    pub fn with_product_ext(self, product_ext: Cow<'static, str>) -> Self {
        Self {
            product_ext: Some(product_ext),
            ..self
        }
    }

    /// Returns `self` with the author set to `author`.
    pub fn with_author(self, author: String) -> Self {
        Self {
            author: Some(author),
            ..self
        }
    }

    /// Return `self` with the precision set to `precision`.
    pub fn with_precision(self, precision: Precision) -> Self {
        Self { precision, ..self }
    }

    /// Writes `dictionary` to `path` in portable file format.  Returns a [Writer]
    /// that can be used for writing cases to the new file.
    pub fn write_file(
        self,
        dictionary: &Dictionary,
        path: impl AsRef<Path>,
    ) -> Result<Writer<BufWriter<File>>, Error> {
        self.write_writer(dictionary, BufWriter::new(File::create(path)?))
    }

    /// Writes `dictionary` to `writer` in portable file format.  Returns a
    /// [Writer] that can be used for writing cases to the new file.
    pub fn write_writer<W>(self, dictionary: &Dictionary, writer: W) -> Result<Writer<W>, Error>
    where
        W: Write + 'static,
    {
        let mut writer = WriteFilter::new(writer);
        let mut dict_writer = DictionaryWriter::new(&self, &mut writer, dictionary);
        dict_writer.write()?;
        Ok(Writer {
            inner: Some(writer),
            precision: self.precision,
        })
    }

    /// Returns a [WriteOptions] with members set to fixed values so that
    /// running at different times or with different crate names or versions
    /// won't change what's written to the file.
    #[cfg(test)]
    pub(super) fn reproducible() -> Self {
        use chrono::{NaiveDate, NaiveTime};
        WriteOptions::new()
            .with_timestamp(NaiveDateTime::new(
                NaiveDate::from_ymd_opt(2025, 7, 30).unwrap(),
                NaiveTime::from_hms_opt(15, 7, 55).unwrap(),
            ))
            .with_product(Cow::from("PSPP TEST DATA FILE"))
    }
}

/// Portable file case writer.
///
/// Use [WriteOptions::write_file] or [WriteOptions::write_writer] to obtain a
/// [Writer].
pub struct Writer<W> {
    precision: Precision,
    inner: Option<WriteFilter<W>>,
}

impl<W> Writer<W>
where
    W: Write,
{
    /// Finishes writing the file.
    pub fn finish(mut self) -> Result<Option<W>, Error> {
        self.try_finish()
    }

    /// Tries to finish writing the file.
    ///
    /// # Panic
    ///
    /// Attempts to write more cases after calling this function will panic.
    pub fn try_finish(&mut self) -> Result<Option<W>, Error> {
        match self.inner.take() {
            None => Ok(None),
            Some(mut inner) => {
                inner.write_end()?;
                Ok(Some(inner.into_inner()))
            }
        }
    }

    /// Writes `case` to the file.
    pub fn write_case<B>(&mut self, case: impl IntoIterator<Item = Datum<B>>) -> Result<(), Error>
    where
        B: RawString,
    {
        write_case(self.inner.as_mut().unwrap(), case, self.precision)
    }
}

fn write_case<W, B>(
    mut writer: W,
    case: impl IntoIterator<Item = Datum<B>>,
    precision: Precision,
) -> Result<(), Error>
where
    W: Write,
    B: RawString,
{
    for datum in case {
        write_datum(&mut writer, &datum, precision)?;
    }
    Ok(())
}

struct WriteFilter<W> {
    inner: W,
    line_len: usize,
}

impl<W> WriteFilter<W> {
    fn new(inner: W) -> Self {
        Self { inner, line_len: 0 }
    }

    fn into_inner(self) -> W {
        self.inner
    }
}

impl<W> WriteFilter<W>
where
    W: Write,
{
    fn write_end(&mut self) -> std::io::Result<()> {
        // Write 'Z'.
        self.write_all(b"Z")?;

        // Finish out the current line with more 'Z's.
        if self.line_len != 0 {
            let rest = std::iter::repeat_n(b'Z', 80 - self.line_len).collect::<Vec<_>>();
            self.write_all(&rest)?;
        }

        Ok(())
    }
}

impl<W> Write for WriteFilter<W>
where
    W: Write,
{
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        fn handle_error(error: std::io::Error, ofs: usize) -> std::io::Result<usize> {
            if ofs > 0 {
                Ok(ofs)
            } else {
                Err(error)
            }
        }

        fn write_chunk<W>(mut writer: W, chunk: &[u8]) -> std::io::Result<usize>
        where
            W: Write,
        {
            let mut ofs = 0;
            while ofs < chunk.len() {
                let result = if chunk[ofs] < 0x20 {
                    writer.write(&[chunk[ofs]])
                } else {
                    let n = chunk[ofs..].iter().take_while(|b| **b >= 0x20).count();
                    writer.write(&chunk[ofs..ofs + n])
                };
                match result {
                    Ok(n) => ofs += n,
                    Err(error) => return handle_error(error, ofs),
                }
            }
            Ok(ofs)
        }

        let mut ofs = 0;
        while ofs < buf.len() {
            let chunk = (buf.len() - ofs).min(80 - self.line_len);
            let n = match write_chunk(&mut self.inner, &buf[ofs..ofs + chunk]) {
                Ok(n) => n,
                Err(error) => return handle_error(error, ofs),
            };
            self.line_len += n;
            ofs += n;
            if self.line_len == 80 {
                if let Err(error) = self.inner.write_all(b"\r\n") {
                    return handle_error(error, ofs);
                }
                self.line_len = 0;
            }
        }
        Ok(ofs)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.inner.flush()
    }
}

struct DictionaryWriter<'a, W> {
    options: &'a WriteOptions,
    writer: &'a mut W,
    dictionary: &'a Dictionary,
    short_names: Vec<String>,
}

impl<'a, W> DictionaryWriter<'a, W>
where
    W: Write,
{
    pub fn new(options: &'a WriteOptions, writer: &'a mut W, dictionary: &'a Dictionary) -> Self {
        Self {
            options,
            writer,
            dictionary,
            short_names: dictionary
                .short_names()
                .into_iter()
                .map(|names| {
                    names
                        .into_iter()
                        .next()
                        .unwrap()
                        .0
                        .into_inner()
                        .to_ascii_uppercase()
                })
                .collect(),
        }
    }

    pub fn write(&mut self) -> Result<(), Error> {
        self.write_header()?;
        self.write_version()?;
        self.write_identification()?;
        self.write_variable_count()?;
        self.write_precision()?;
        self.write_case_weight()?;
        self.write_variables()?;
        self.write_value_labels()?;
        self.write_documents()?;
        Ok(())
    }

    pub fn write_header(&mut self) -> Result<(), Error> {
        for _ in 0..5 {
            self.writer
                .write_all(b"ASCII SPSS PORT FILE                    ")?;
        }
        for (index, c) in PORTABLE_TO_WINDOWS_1252.iter().enumerate() {
            let c = if *c == b' ' && index != 0x7e {
                b'0'
            } else {
                *c
            };
            self.writer.write_all(&[c])?;
        }
        self.writer.write_all(b"SPSSPORT")
    }

    pub fn write_version(&mut self) -> Result<(), Error> {
        self.writer.write_all(b"A")?;
        write_string(
            &mut self.writer,
            self.options.timestamp.format("%Y%m%d").to_string(),
        )?;
        write_string(
            &mut self.writer,
            self.options.timestamp.format("%H%M%S").to_string(),
        )
    }

    pub fn write_identification(&mut self) -> Result<(), Error> {
        self.writer.write_all(b"1")?;
        write_string(&mut self.writer, self.options.product.as_bytes())?;
        if let Some(product_ext) = self.options.product_ext.as_ref() {
            self.writer.write_all(b"2")?;
            write_string(&mut self.writer, product_ext.as_bytes())?;
        }
        if let Some(author) = self.options.author.as_ref() {
            self.writer.write_all(b"3")?;
            write_string(&mut self.writer, author.as_bytes())?;
        }
        Ok(())
    }

    pub fn write_variable_count(&mut self) -> Result<(), Error> {
        write!(
            &mut self.writer,
            "4{}",
            TrigesimalInt::new(self.dictionary.variables.len() as i64)
        )
    }

    pub fn write_precision(&mut self) -> Result<(), Error> {
        write!(
            &mut self.writer,
            "5{}",
            TrigesimalInt::new(self.options.precision.as_base_30_digits() as i64)
        )
    }

    pub fn write_case_weight(&mut self) -> Result<(), Error> {
        if let Some(weight_index) = self.dictionary.weight_index() {
            self.writer.write_all(b"6")?;
            write_string(&mut self.writer, &self.short_names[weight_index].as_bytes())?;
        }
        Ok(())
    }

    pub fn write_variables(&mut self) -> Result<(), Error> {
        let float = |value| TrigesimalFloat::new(value, self.options.precision);
        for (variable, short_name) in self.dictionary.variables.iter().zip(&self.short_names) {
            let width = variable.width.as_string_width().unwrap_or_default() as i64;
            write!(&mut self.writer, "7{}", TrigesimalInt::new(width))?;
            write_string(&mut self.writer, short_name.as_bytes())?;
            for format in [variable.print_format, variable.write_format] {
                let type_ = u16::from(format.type_()) as i64;
                write!(
                    &mut self.writer,
                    "{}{}{}",
                    TrigesimalInt::new(type_),
                    TrigesimalInt::new(format.w() as i64),
                    TrigesimalInt::new(format.d() as i64)
                )?;
            }
            if let Some(range) = variable.missing_values().range() {
                match range {
                    MissingValueRange::In { low, high } => {
                        write!(&mut self.writer, "B{}{}", float(*low), float(*high))?
                    }
                    MissingValueRange::From { low } => {
                        write!(&mut self.writer, "A{}", float(*low))?
                    }
                    MissingValueRange::To { high } => {
                        write!(&mut self.writer, "9{}", float(*high))?
                    }
                }
            }
            for value in variable.missing_values().values() {
                write!(&mut self.writer, "8")?;
                write_datum(&mut self.writer, value, self.options.precision)?;
            }
            if let Some(label) = variable.label() {
                write!(&mut self.writer, "C")?;
                write_string(&mut self.writer, label.as_bytes())?;
            }
        }
        Ok(())
    }

    fn write_value_labels(&mut self) -> Result<(), Error> {
        // Collect identical sets of value labels.
        let mut sets = HashMap::<&ValueLabels, Vec<_>>::new();
        for (variable, short_name) in self.dictionary.variables.iter().zip(&self.short_names) {
            if !variable.value_labels.is_empty() {
                sets.entry(&variable.value_labels)
                    .or_default()
                    .push(short_name);
            }
        }

        for (value_labels, variables) in sets {
            write!(
                &mut self.writer,
                "D{}",
                TrigesimalInt::new(variables.len() as i64)
            )?;
            for variable in variables {
                write_string(&mut self.writer, variable)?;
            }

            write!(
                &mut self.writer,
                "{}",
                TrigesimalInt::new(value_labels.len() as i64)
            )?;
            for (value, label) in value_labels {
                write_datum(&mut self.writer, value, self.options.precision)?;
                write_string(&mut self.writer, label.as_bytes())?;
            }
        }
        Ok(())
    }

    fn write_documents(&mut self) -> Result<(), Error> {
        if !self.dictionary.documents.is_empty() {
            write!(
                &mut self.writer,
                "E{}",
                TrigesimalInt::new(self.dictionary.documents.len() as i64)
            )?;
            for line in &self.dictionary.documents {
                write_string(&mut self.writer, line.as_bytes())?;
            }
        }
        Ok(())
    }
}

fn write_datum<W, T>(mut writer: W, datum: &Datum<T>, precision: Precision) -> Result<(), Error>
where
    W: Write,
    T: RawString,
{
    match datum {
        Datum::Number(number) => write!(
            writer,
            "{}",
            TrigesimalFloat::new_optional(*number, precision)
        ),
        Datum::String(string) => write_string(writer, string.raw_string_bytes()),
    }
}

fn write_string<W, S>(mut writer: W, s: S) -> Result<(), Error>
where
    W: Write,
    S: AsRef<[u8]>,
{
    let s = s.as_ref();
    write!(&mut writer, "{}", TrigesimalInt::new(s.len() as i64))?;
    writer.write_all(s)
}

fn trig_to_char(trig: u8) -> char {
    b"0123456789ABCDEFGHIJKLMNOPQRST"[trig as usize] as char
}

struct TrigesimalInt {
    value: i64,
    force_sign: bool,
    add_slash: bool,
}

impl TrigesimalInt {
    fn new(value: i64) -> Self {
        Self {
            value,
            force_sign: false,
            add_slash: true,
        }
    }
}

impl Display for TrigesimalInt {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.value < 0 {
            f.write_char('-')?;
        } else if self.force_sign {
            f.write_char('+')?;
        }
        let value = self.value.unsigned_abs();

        fn recursive_format_int(f: &mut std::fmt::Formatter<'_>, value: u64) -> std::fmt::Result {
            let trig = value % 30;
            if value >= 30 {
                recursive_format_int(f, value / 30)?;
            }
            f.write_char(trig_to_char(trig as u8))
        }

        recursive_format_int(f, value)?;
        if self.add_slash {
            f.write_char('/')?;
        }
        Ok(())
    }
}

struct TrigesimalFloat {
    value: f64,
    precision: Precision,
}

impl TrigesimalFloat {
    fn new(value: f64, precision: Precision) -> Self {
        Self { value, precision }
    }
    fn new_optional(value: Option<f64>, precision: Precision) -> Self {
        Self::new(value.unwrap_or(f64::INFINITY), precision)
    }
}

impl Display for TrigesimalFloat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let (value, negative) = match self.value.classify() {
            std::num::FpCategory::Nan | std::num::FpCategory::Infinite => {
                return write!(f, "*.");
            }
            std::num::FpCategory::Zero | std::num::FpCategory::Subnormal => {
                return write!(f, "0/");
            }
            std::num::FpCategory::Normal if self.value < 0.0 => (-self.value, true),
            std::num::FpCategory::Normal => (self.value, false),
        };

        // Adjust `value` to roughly 30**3, by shifting the trigesimal point left or
        // right as necessary.  We approximate the base-30 exponent by obtaining the
        // base-2 exponent, then multiplying by log30(2).  This approximation is
        // sufficient to ensure that the adjusted value is always in the range
        // 0...30**6, an invariant of the loop below.
        let binary_exponent = frexp(value).1;

        // This is floor(log30(2**31)), the minimum number of trigesimal
        // digits that `i32` can hold.
        const CHUNK_SIZE: usize = 6;

        // Number of trigesimal places for trigs:
        //
        // * trigs[0] has coefficient 30**(trig_places - 1),
        // * trigs[1] has coefficient 30**(trig_places - 2),
        // * ...
        //
        // In other words, the trigesimal point is just before trigs[0].
        let trig_places = (binary_exponent * 20_379 / 100_000) + CHUNK_SIZE as i32 / 2;
        let mut value = value * 30f64.powi(CHUNK_SIZE as i32 - trig_places);

        let mut trigs = SmallVec::<[u8; 32]>::new();

        // Dump all the trigs to buffer[], CHUNK_SIZE at a time.
        let mut trigs_to_output =
            (f64::DIGITS * 2).div_ceil(3) as i32 + 1 + (CHUNK_SIZE as i32 / 2);
        while trigs_to_output > 0 {
            // The current chunk is just the integer part of `value`, truncated to the
            // nearest integer.  It fits in `usize`.  Append it in base 30.
            let mut chunk = value as usize;
            for _ in 0..CHUNK_SIZE {
                trigs.push((chunk % 30) as u8);
                chunk /= 30;
            }
            let len = trigs.len();
            trigs[len - CHUNK_SIZE..].reverse();

            // Proceed to the next chunk.
            value = value.fract();
            if value == 0.0 {
                break;
            }
            value *= 30.0f64.powi(CHUNK_SIZE as i32);
            trigs_to_output -= CHUNK_SIZE as i32;
        }

        // Strip leading zeros.
        let leading_zeros = trigs.iter().take_while(|trig| **trig == 0).count();
        let trigs = &mut trigs[leading_zeros..];
        let trig_places = trig_places - leading_zeros as i32;

        // Round to requested precision, conservatively estimating the required
        // base-30 precision as 2/3 of the base-10 precision (log30(10) = .68).
        let base_30_precision = self.precision.as_base_30_digits() as usize;
        let trigs = if trigs.len() > base_30_precision {
            if should_round_up(&trigs[base_30_precision - 1..]) {
                if try_round_up(&mut trigs[..base_30_precision]) {
                    &trigs[..base_30_precision]
                } else {
                    // Couldn't round up because we ran out of trigs to carry into.  Do the carry here instead.
                    &[1]
                }
            } else {
                // Round down.
                &trigs[..base_30_precision]
            }
        } else {
            // No rounding required: fewer digits available than requested.
            &trigs[..]
        };

        // Strip trailing zeros.
        let trailing_zeros = trigs
            .iter()
            .rev()
            .take_while(|trig| **trig == 0)
            .count()
            .min(trigs.len().saturating_sub(1));
        let trigs = &trigs[..trigs.len() - trailing_zeros];

        if negative {
            write!(f, "-")?;
        }
        if (-1..trigs.len() as i32 + 3).contains(&trig_places) {
            // Use conventional notation.
            format_trig_digits(f, trigs, trig_places)?;
        } else {
            // Use scientific notation.
            format_trig_digits(f, trigs, trigs.len() as i32)?;
            write!(
                f,
                "{}",
                TrigesimalInt {
                    value: (trig_places - trigs.len() as i32) as i64,
                    force_sign: true,
                    add_slash: false
                }
            )?;
        }
        f.write_char('/')
    }
}

/// Formats `trigs` into `f`, inserting the trigesimal point after `trig_places`
/// characters have been printed, if necessary adding extra zeros at either end
/// for correctness.
fn format_trig_digits(
    f: &mut std::fmt::Formatter<'_>,
    trigs: &[u8],
    mut trig_places: i32,
) -> std::fmt::Result {
    if trig_places < 0 {
        f.write_char('.')?;
        for _ in trig_places..0 {
            f.write_char('0')?;
        }
        for trig in trigs {
            f.write_char(trig_to_char(*trig))?;
        }
    } else {
        for trig in trigs {
            if trig_places == 0 {
                f.write_char('.')?;
            }
            trig_places -= 1;
            f.write_char(trig_to_char(*trig))?;
        }
        for _ in 0..trig_places {
            f.write_char('0')?;
        }
    }
    Ok(())
}

/// Determines whether `trigs[1..]` warrant rounding up or down.  Returns true
/// if `trigs[1..]` represents a value greater than half, false if less than
/// half.  If `trigs[1..]` is exactly half, examines `trigs[0]` and returns true
/// if odd, false if even ("round to even").
fn should_round_up(trigs: &[u8]) -> bool {
    match trigs[1].cmp(&15) {
        Ordering::Less => {
            // Less than half: round down.
            false
        }
        Ordering::Greater => {
            // More than half: round up.
            true
        }
        Ordering::Equal => {
            // About half: look more closely.
            if trigs[2..].iter().any(|trig| *trig != 0) {
                // Slightly greater than half: round up
                true
            } else {
                // Exactly half: round to even.
                trigs[0] % 2 != 0
            }
        }
    }
}

/// Rounds up the rightmost trig in `trigs`, carrying to the left as necessary.
/// Returns true if successful, false on failure (due to a carry out of the
/// leftmost position).
fn try_round_up(trigs: &mut [u8]) -> bool {
    for trig in trigs.iter_mut().rev() {
        if *trig != 29 {
            // Round this trig up to the next value.
            *trig += 1;
            return true;
        }

        // Carry over to the next trig to the left.
        *trig = 0;
    }

    // Ran out of trigs to carry.
    false
}

#[cfg(test)]
mod tests {
    use core::f64;
    use std::borrow::Cow;

    use encoding_rs::{UTF_8, WINDOWS_1252};
    use indexmap::set::MutableValues;
    use itertools::{zip_eq, Itertools};

    use crate::{
        data::{ByteString, Datum, RawString},
        dictionary::Dictionary,
        identifier::Identifier,
        por::{
            write::{write_case, DictionaryWriter, Precision, TrigesimalFloat, TrigesimalInt},
            WriteOptions,
        },
        variable::{MissingValueRange, MissingValues, VarWidth, Variable},
    };

    #[test]
    fn format_int() {
        #[track_caller]
        fn check(value: i64, force_sign: bool, expected: &str) {
            let s = TrigesimalInt {
                value,
                force_sign,
                add_slash: false,
            };
            assert_eq!(&s.to_string(), expected);
        }
        check(0, false, "0");
        check(0, true, "+0");
        check(1, false, "1");
        check(2, false, "2");
        check(10, false, "A");
        check(29, false, "T");
        check(123456789, false, "52CE69");
        check(1, true, "+1");
        check(2, true, "+2");
        check(10, true, "+A");
        check(29, true, "+T");
        check(123456789, true, "+52CE69");
        check(-1, false, "-1");
        check(-2, false, "-2");
        check(-10, false, "-A");
        check(-29, false, "-T");
        check(-123456789, false, "-52CE69");
        check(-1, true, "-1");
        check(-2, true, "-2");
        check(-10, true, "-A");
        check(-29, true, "-T");
        check(-123456789, true, "-52CE69");
    }

    #[test]
    fn format_float() {
        #[track_caller]
        fn check(value: f64, precision: Precision, expected: &str) {
            let s = TrigesimalFloat { value, precision };
            assert_eq!(&s.to_string(), expected);
        }

        fn p(base_30_digits: u32) -> Precision {
            Precision::from_base_30_digits(base_30_digits)
        }

        check(0.0, p(10), "0/");
        check(-0.0, p(10), "0/");
        check(1.0, p(10), "1/");
        check(f64::INFINITY, p(10), "*.");
        check(f64::MIN_POSITIVE / 2.0, p(10), "0/");
        check(0.5, p(10), ".F/");
        check(1234.5, p(10), "1B4.F/");
        check(0.123456789, p(9), ".3L39TT5CR/");
        check(0.123456789, p(8), ".3L39TT5D/");
        check(0.123456789, p(7), ".3L39TT5/");
        check(0.123456789, p(6), ".3L39TT/");
        check(0.123456789, p(4), ".3L3A/");
        check(0.123456789, p(3), ".3L3/");
        check(0.123456789, p(2), ".3L/");
        check(0.123456789, p(1), ".4/");
        check(-0.123456789, p(9), "-.3L39TT5CR/");
        check(-0.123456789, p(8), "-.3L39TT5D/");
        check(-0.123456789, p(7), "-.3L39TT5/");
        check(-0.123456789, p(6), "-.3L39TT/");
        check(-0.123456789, p(4), "-.3L3A/");
        check(-0.123456789, p(3), "-.3L3/");
        check(-0.123456789, p(2), "-.3L/");
        check(-0.123456789, p(1), "-.4/");
        check(123456789.123456789, p(10), "52CE69.3L3A/");
        check(123456789.123456789, p(9), "52CE69.3L3/");
        check(123456789.123456789, p(8), "52CE69.3L/");
        check(123456789.123456789, p(7), "52CE69.4/");
        check(123456789.123456789, p(6), "52CE69/");
        check(123456789.123456789, p(5), "52CE60/");
        check(123456789.123456789, p(4), "52CE00/");
        check(123456789.123456789, p(3), "52C+3/");
        check(123456789.123456789, p(2), "52+4/");
        check(123456789.123456789, p(1), "5+5/");
        check(0.00000000987654, p(2), "76-7/");
    }

    #[test]
    fn header() {
        let dictionary = Dictionary::new(UTF_8);
        let mut output = Vec::new();
        let options = WriteOptions::reproducible();
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_header().unwrap();
        assert_eq!(output.len(), 200 + 256 + 8);
        assert_eq!(
            &output[..200],
            b"ASCII SPSS PORT FILE                    \
ASCII SPSS PORT FILE                    \
ASCII SPSS PORT FILE                    \
ASCII SPSS PORT FILE                    \
ASCII SPSS PORT FILE                    "
        );
        assert_eq!(&output[200 + 256..], b"SPSSPORT");
        assert_eq!(
            &output[200..200 + 64],
            b"0000000000000000000000000000000000000000000000000000000000000000"
        );
        assert_eq!(
            &output[200 + 64..200 + 128],
            b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ."
        );
        assert_eq!(&output[200 + 128..200 + 192], b"<(+|&[]!$*);^-/|,%_>?`:#@'=\"00\xb10\xb0\x86~\x960000\xb9\xb2\xb3456789000\x97()0{}\\\xa2\x95000");
        assert_eq!(
            &output[200 + 192..200 + 256],
            b"0000000000000000000000000000000000000000000000000000000000000000"
        );
    }

    #[test]
    fn version() {
        let dictionary = Dictionary::new(UTF_8);
        let mut output = Vec::new();
        let options = WriteOptions::reproducible();
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_version().unwrap();
        assert_eq!(&String::from_utf8(output).unwrap(), "A8/202507306/150755");
    }

    #[test]
    fn identification() {
        let dictionary = Dictionary::new(UTF_8);
        let mut output = Vec::new();
        let options = WriteOptions::reproducible()
            .with_product_ext(Cow::from("Extra product"))
            .with_author(String::from("Author"));
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_identification().unwrap();
        assert_eq!(
            &String::from_utf8(output).unwrap(),
            "1J/PSPP TEST DATA FILE2D/Extra product36/Author"
        );
    }

    #[test]
    fn precision() {
        let dictionary = Dictionary::new(UTF_8);
        let mut output = Vec::new();
        let options =
            WriteOptions::reproducible().with_precision(Precision::from_base_30_digits(3));
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_precision().unwrap();
        assert_eq!(&String::from_utf8(output).unwrap(), "53/");
    }

    #[test]
    fn variables() {
        {
            let mut dictionary = Dictionary::new(UTF_8);
            for (index, width) in [VarWidth::Numeric, VarWidth::String(1), VarWidth::String(15)]
                .iter()
                .enumerate()
            {
                dictionary
                    .add_var(Variable::new(
                        Identifier::new(format!("v{index}")).unwrap(),
                        *width,
                        UTF_8,
                    ))
                    .unwrap();
            }
            dictionary.variables.get_index_mut2(1).unwrap().label =
                Some(String::from("Variable label."));
            dictionary.set_weight(Some(0)).unwrap();

            let mut output = Vec::new();
            let options = WriteOptions::reproducible();
            let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
            writer.write_variable_count().unwrap();
            writer.write_case_weight().unwrap();
            writer.write_variables().unwrap();

            assert_eq!(
                &String::from_utf8(output).unwrap(),
                "43/\
62/V0\
70/2/V05/8/2/5/8/2/\
71/2/V11/1/0/1/1/0/\
CF/Variable label.\
7F/2/V21/F/0/1/F/0/\
"
            );
        }
    }

    #[test]
    fn missing_values() {
        {
            let mut dictionary = Dictionary::new(UTF_8);
            let variables = [
                (VarWidth::Numeric, vec![Datum::Number(Some(0.0))], None),
                (
                    VarWidth::Numeric,
                    vec![Datum::Number(Some(0.0)), Datum::Number(Some(1.0))],
                    None,
                ),
                (
                    VarWidth::Numeric,
                    vec![
                        Datum::Number(Some(0.0)),
                        Datum::Number(Some(1.0)),
                        Datum::Number(Some(2.0)),
                    ],
                    None,
                ),
                (
                    VarWidth::Numeric,
                    vec![Datum::Number(Some(0.0))],
                    Some(MissingValueRange::new(1.0, 2.0)),
                ),
                (
                    VarWidth::Numeric,
                    Vec::new(),
                    Some(MissingValueRange::new(1.0, 2.0)),
                ),
                (
                    VarWidth::Numeric,
                    vec![Datum::Number(Some(0.0))],
                    Some(MissingValueRange::From { low: 1.0 }),
                ),
                (
                    VarWidth::Numeric,
                    Vec::new(),
                    Some(MissingValueRange::From { low: 1.0 }),
                ),
                (
                    VarWidth::Numeric,
                    vec![Datum::Number(Some(0.0))],
                    Some(MissingValueRange::To { high: 1.0 }),
                ),
                (
                    VarWidth::Numeric,
                    Vec::new(),
                    Some(MissingValueRange::To { high: 1.0 }),
                ),
                (
                    VarWidth::String(8),
                    vec![Datum::String(
                        ByteString::from("abcdefgh").with_encoding(WINDOWS_1252),
                    )],
                    None,
                ),
                (
                    VarWidth::String(8),
                    vec![
                        Datum::String(ByteString::from("abcdefgh").with_encoding(WINDOWS_1252)),
                        Datum::String(ByteString::from("ijklmnop").with_encoding(WINDOWS_1252)),
                    ],
                    None,
                ),
                (
                    VarWidth::String(8),
                    vec![
                        Datum::String(ByteString::from("abcdefgh").with_encoding(WINDOWS_1252)),
                        Datum::String(ByteString::from("ijklmnop").with_encoding(WINDOWS_1252)),
                        Datum::String(ByteString::from("qrstuvwx").with_encoding(WINDOWS_1252)),
                    ],
                    None,
                ),
            ];
            for (index, (width, values, range)) in variables.into_iter().enumerate() {
                let mut variable =
                    Variable::new(Identifier::new(format!("v{index}")).unwrap(), width, UTF_8);
                variable
                    .missing_values_mut()
                    .replace(MissingValues::new(values, range).unwrap())
                    .unwrap();
                dictionary.add_var(variable).unwrap();
            }

            let mut output = Vec::new();
            let options = WriteOptions::reproducible();
            let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
            writer.write_variable_count().unwrap();
            writer.write_case_weight().unwrap();
            writer.write_variables().unwrap();

            assert_eq!(
                &String::from_utf8(output).unwrap(),
                "4C/\
70/2/V05/8/2/5/8/2/\
80/\
70/2/V15/8/2/5/8/2/\
80/\
81/\
70/2/V25/8/2/5/8/2/\
80/\
81/\
82/\
70/2/V35/8/2/5/8/2/\
B1/2/\
80/\
70/2/V45/8/2/5/8/2/\
B1/2/\
70/2/V55/8/2/5/8/2/\
A1/\
80/\
70/2/V65/8/2/5/8/2/\
A1/\
70/2/V75/8/2/5/8/2/\
91/\
80/\
70/2/V85/8/2/5/8/2/\
91/\
78/2/V91/8/0/1/8/0/\
88/abcdefgh\
78/3/V101/8/0/1/8/0/\
88/abcdefgh\
88/ijklmnop\
78/3/V111/8/0/1/8/0/\
88/abcdefgh\
88/ijklmnop\
88/qrstuvwx\
"
            );
        }
    }

    #[test]
    fn value_labels() {
        let variables = [
            (VarWidth::Numeric, vec![(Datum::Number(Some(1.0)), "One")]),
            (
                VarWidth::Numeric,
                vec![
                    (Datum::Number(Some(1.0)), "One"),
                    (Datum::Number(Some(2.0)), "Two"),
                ],
            ),
            (
                VarWidth::Numeric,
                vec![
                    (Datum::Number(Some(1.0)), "One"),
                    (Datum::Number(Some(2.0)), "Two"),
                ],
            ),
            (
                VarWidth::String(4),
                vec![(Datum::String(ByteString::from("abcd")), "One")],
            ),
            (
                VarWidth::String(8),
                vec![(
                    Datum::String(ByteString::from("abcdefgh")),
                    "Longer value label",
                )],
            ),
            (
                VarWidth::String(9),
                vec![(
                    Datum::String(ByteString::from("abcdefghi")),
                    "value label for 9-byte value",
                )],
            ),
            (
                VarWidth::String(300),
                vec![(
                    Datum::String(ByteString::from(vec![b'x'; 300])),
                    "value label for 300-byte value",
                )],
            ),
        ];

        let mut dictionary = Dictionary::new(UTF_8);
        for (index, (width, value_labels)) in variables.iter().enumerate() {
            let mut variable = Variable::new(
                Identifier::new(format!("var{index}")).unwrap(),
                *width,
                UTF_8,
            );
            for (value, label) in value_labels {
                assert_eq!(variable.value_labels.insert(value.clone(), *label), None);
            }
            dictionary.add_var(variable).unwrap();
        }
        dbg!(&dictionary);

        let mut output = Vec::new();
        let options = WriteOptions::reproducible();
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_value_labels().unwrap();

        let output = String::from_utf8(output).unwrap();
        println!("{output}");

        let mut output = output
            .split("D")
            .filter(|s| !s.is_empty())
            .collect::<Vec<_>>();
        output.sort();

        let expected = [
   ("1/4/VAR01/", vec!["1/3/One"]),
    ("1/4/VAR31/", vec!["4/abcd3/One"]),
    ("1/4/VAR41/", vec!["8/abcdefghI/Longer value label"]),
    ("1/4/VAR51/", vec!["9/abcdefghiS/value label for 9-byte value"]),
    ("1/4/VAR61/", vec!["A0/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx10/value label for 300-byte value"]),
    ("2/4/VAR14/VAR22/", vec!["1/3/One", "2/3/Two"]),
        ];

        for (actual, (exp_prefix, exp_suffixes)) in zip_eq(output, expected) {
            if !exp_suffixes
                .iter()
                .permutations(exp_suffixes.len())
                .any(|exp_suffixes| {
                    actual
                        == std::iter::once(exp_prefix)
                            .chain(exp_suffixes.into_iter().map(|s| *s))
                            .collect::<String>()
                })
            {
                panic!(
                    "{actual:?} != {exp_prefix:?} followed by any permutation of {exp_suffixes:?}"
                );
            }
        }
    }

    #[test]
    fn documents() {
        let mut dictionary = Dictionary::new(UTF_8);
        dictionary.documents = vec![
            String::from("First document line."),
            String::from("Second document line."),
        ];

        let mut output = Vec::new();
        let options = WriteOptions::reproducible();
        let mut writer = DictionaryWriter::new(&options, &mut output, &dictionary);
        writer.write_documents().unwrap();

        assert_eq!(
            &String::from_utf8(output).unwrap(),
            "E2/\
K/First document line.\
L/Second document line."
        );
    }

    #[test]
    fn cases() {
        let mut output = Vec::new();
        write_case(
            &mut output,
            [
                Datum::Number(Some(0.0)),
                Datum::Number(Some(1.0)),
                Datum::Number(None),
                Datum::String(ByteString::from("abcdefghi")),
            ],
            Precision::default(),
        )
        .unwrap();
        assert_eq!(&String::from_utf8(output).unwrap(), "0/1/*.9/abcdefghi");
    }
}
