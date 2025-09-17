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
    cmp::Ordering,
    fmt::{Display, Formatter},
    fs::File,
    io::{BufRead, BufReader, Error as IoError, Read, Result as IoResult, Seek, SeekFrom},
    ops::Index,
    path::Path,
};

use chrono::{NaiveDate, NaiveDateTime, NaiveTime};
use codepage_437::CP437_WINGDINGS;
use encoding_rs::WINDOWS_1252;
use indexmap::set::MutableValues;
use num::{Bounded, NumCast};
use serde::{ser::SerializeSeq, Serialize, Serializer};

use crate::{
    data::{ByteString, Case, Datum, RawString, WithEncoding},
    dictionary::{DictIndex, Dictionary},
    format::{Error as FormatError, Format, Type, UncheckedFormat},
    identifier::{Error as IdError, Identifier},
    output::pivot::{MetadataEntry, MetadataValue, PivotTable, Value},
    por::portable_to_windows_1252,
    variable::{MissingValueRange, MissingValues, MissingValuesError, VarType, VarWidth, Variable},
};
use displaydoc::Display;
use thiserror::Error as ThisError;

/// An SPSS portable file.
#[derive(Debug)]
pub struct PortableFile<R> {
    /// The system file dictionary.
    pub dictionary: Dictionary,

    /// Portable file metadata that is not part of the dictionary.
    pub metadata: Metadata,

    /// Data in the portable file.
    pub cases: Cases<ReadTranslate<ReadPad<R>>>,
}

impl<R> PortableFile<R> {
    /// Returns the individual parts of the [PortableFile].
    pub fn into_parts(self) -> (Dictionary, Metadata, Cases<ReadTranslate<ReadPad<R>>>) {
        (self.dictionary, self.metadata, self.cases)
    }
}

/// Portable file metadata that is not part of [Dictionary].
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Metadata {
    /// Creation date and time.
    ///
    /// This comes from the file header, not from the file system.
    pub creation: Option<NaiveDateTime>,

    /// Name of the product that wrote the file.
    pub product: Option<String>,

    /// Extended name of the product that wrote the file.
    pub product_ext: Option<String>,

    /// Identifies the organization licensed for the product that wrote the
    /// file.
    pub author: Option<String>,

    /// The file's embedded character encoding translation table.
    #[serde(serialize_with = "serialize_character_set")]
    pub character_set: [u8; 256],
}

fn serialize_character_set<S>(translations: &[u8; 256], serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    let mut seq = serializer.serialize_seq(Some(256))?;
    for (index, c) in translations.into_iter().enumerate() {
        let windows_1252 = *c as char;
        let cp_437 = CP437_WINGDINGS.decode(*c);
        if windows_1252 == cp_437 {
            seq.serialize_element(&(format!("{index:02x}"), windows_1252))?;
        } else {
            seq.serialize_element(&(format!("{index:02x}"), windows_1252, cp_437))?;
        }
    }
    seq.end()
}

impl From<&Metadata> for PivotTable {
    fn from(value: &Metadata) -> Self {
        fn maybe_string(name: &str, s: &Option<String>) -> MetadataEntry {
            MetadataEntry {
                name: Value::new_user_text(name),
                value: MetadataValue::Leaf(
                    s.as_ref()
                        .cloned()
                        .map(Value::new_user_text)
                        .unwrap_or_default(),
                ),
            }
        }

        MetadataEntry {
            name: Value::new_user_text("Portable File Metadata"),
            value: MetadataValue::Group(vec![
                MetadataEntry {
                    name: Value::new_user_text("Created"),
                    value: MetadataValue::Leaf(
                        value.creation.map(Value::new_date_time).unwrap_or_default(),
                    ),
                },
                maybe_string("Product", &value.product),
                maybe_string("Product 2", &value.product_ext),
                maybe_string("Author", &value.author),
            ]),
        }
        .into_pivot_table()
    }
}

/// Reader for cases in a portable file.
#[derive(Debug)]
pub struct Cases<R> {
    reader: R,
    variables: Vec<VarWidth>,
    eof: bool,
}

impl<R> Cases<R> {
    fn new(reader: R, variables: Vec<VarWidth>) -> Self {
        Self {
            reader,
            variables,
            eof: false,
        }
    }

    fn read_case(&mut self) -> Result<Option<Case<Vec<Datum<ByteString>>>>, ErrorDetails>
    where
        R: Read,
    {
        let mut values = Vec::with_capacity(self.variables.len());

        // Check whether we're at end of file.
        let peek = read_byte(&mut self.reader)?;
        if peek == b'Z' {
            return Ok(None);
        }

        // We're not at EOF, so glue the lookahead byte onto the front of the
        // reader and then read a case.
        let peek = [peek];
        let mut reader = peek.chain(&mut self.reader);
        for width in &self.variables {
            match width {
                VarWidth::Numeric => values.push(Datum::Number(read_f64_or_missing(&mut reader)?)),
                VarWidth::String(width) => {
                    let mut string = read_raw_string(&mut reader)?;
                    string.resize(*width as usize, b' ');
                    values.push(Datum::String(string.into()));
                }
            }
        }
        Ok(Some(Case::new(values, WINDOWS_1252)))
    }
}

impl<R> Iterator for Cases<R>
where
    R: Read + Seek,
{
    type Item = Result<Case<Vec<Datum<ByteString>>>, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.eof || self.variables.is_empty() {
            return None;
        }

        match self.read_case().transpose() {
            Some(Ok(case)) => Some(Ok(case)),
            None => {
                self.eof = true;
                None
            }
            Some(Err(details)) => {
                self.eof = true;
                Some(Err(Error {
                    offset: self.reader.stream_position().ok(),
                    details,
                }))
            }
        }
    }
}

/// An error encountered reading a portable file.
#[derive(Debug)]
pub struct Error {
    /// Offset where the error occurred.
    pub offset: Option<u64>,

    /// Details of the error.
    pub details: ErrorDetails,
}

impl std::error::Error for Error {}

impl Error {
    /// Constructs an error from `offset` and `details`.
    pub fn new(offset: Option<u64>, details: ErrorDetails) -> Self {
        Self { offset, details }
    }
}

impl From<IoError> for Error {
    fn from(value: IoError) -> Self {
        Self::new(None, value.into())
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        if let Some(offset) = self.offset {
            write!(f, "Error at file offset {:#x}: ", offset)?;
        }
        write!(f, "{}", &self.details)
    }
}

/// An error for reading a [PortableFile].
#[derive(Display, ThisError, Debug)]
pub enum ErrorDetails {
    /// Not an SPSS portable file.
    NotAPortableFile,

    /// Unrecognized version code '{0}'.
    UnrecognizedVersionCode(char),

    /// I/O error ({0}).
    Io(#[from] IoError),

    /// Number expected.
    NumberExpected,

    /// Integer expected.
    InvalidInteger,

    /// Expected integer between {min_value} and {max_value}, instead of {float}.
    OutOfRangeInteger {
        /// Value actually read.
        float: f64,
        /// Minimum valid integer value.
        min_value: String,
        /// Maximum valid integer value.
        max_value: String,
    },

    /// Missing numeric terminator.
    MissingSlash,

    /// Invalid string length {0}.
    InvalidStringLength(i32),

    /// Expected variable count record with tag 4 (instead of tag {0:?}).
    ExpectedVariableCountRecord(char),

    /// Invalid number of variables {0}.
    InvalidNumberOfVariables(i32),

    /// Expected variable record.
    ExpectedVariableRecord,

    /// Invalid width {width} for variable {name}.
    InvalidVariableWidth {
        /// Declared width.
        width: i32,
        /// Variable name.
        name: Identifier,
    },

    /// System-missing value where number expected.
    UnexpectedSysmis,

    /// Data record expected.
    DataRecordExpected,

    /// Value label record had no valid variable indexes.
    NoValueLabelVariables,
}

/// A warning while reading a [PortableFile].
#[derive(Display, ThisError, Debug)]
pub enum Warning {
    /// Invalid date {0}.
    InvalidDate(String),

    /// Invalid time {0}.
    InvalidTime(String),

    /// Invalid variable name.
    InvalidVariableName {
        /// Identifier error.
        id_error: IdError,
        /// New name.
        new_name: Identifier,
    },

    /// Renaming variable with duplicate name {duplicate_name} to {new_name}.
    DuplicateVariableName {
        /// Duplicate name.
        duplicate_name: Identifier,
        /// New name.
        new_name: Identifier,
    },

    /// Substituting {new_format} for invalid print format on variable {variable}.  {format_error}
    InvalidPrintFormat {
        /// New format.
        new_format: Format,
        /// Variable.
        variable: Identifier,
        /// Underlying error.
        format_error: FormatError,
    },

    /// Substituting {new_format} for invalid write format on variable {variable}.  {format_error}
    InvalidWriteFormat {
        /// New format.
        new_format: Format,
        /// Variable.
        variable: Identifier,
        /// Underlying error.
        format_error: FormatError,
    },

    /// Missing value range may not contain system-missing value.
    MissingValueRangeSysmis,

    /// Invalid missing values for variable {name}: {error}.
    InvalidMissingValues {
        /// Variable name.
        name: Identifier,
        /// Kind of error with missing values.
        error: MissingValuesError,
    },

    /// Unknown weight variable {0}.
    UnknownWeightVariable(Identifier),

    /// Invalid identifier {string}.  {error}
    InvalidIdentifier {
        /// String that should be an identifier.
        string: String,
        /// Kind of error with the string.
        error: IdError,
    },

    /// Unknown variable name {0}.
    UnknownVariableName(Identifier),

    /// Mixed variable types in value labels.
    MixedVariableTypes,
}

/// Translation table from file bytes to [WINDOWS_1252].
///
/// A byte in the file with value `x` is interpreted in [WINDOWS_1252] as
/// `self.0[x]`.
#[derive(Debug)]
pub struct TranslationTable(
    /// Translation table.
    [u8; 256],
);

impl TranslationTable {
    // Create the translation table, given the character set in a portable file.
    fn new(character_set: &[u8; 256]) -> Self {
        // Skip the first 64 characters of the character set.  They are probably
        // all set to '0', marking them as untranslatable, and that would screw
        // up our actual translation of the real '0'.
        let mut translations = [0; 256];
        for portable in 64..=255 {
            let c = character_set[portable] as usize;
            if translations[c] == 0 {
                translations[c] = portable_to_windows_1252(portable as u8);
            }
        }
        Self(translations)
    }
}

impl Index<u8> for TranslationTable {
    type Output = u8;

    fn index(&self, index: u8) -> &Self::Output {
        &self.0[index as usize]
    }
}

impl PortableFile<BufReader<File>> {
    /// Opens the file at `path`.
    pub fn open_file<P, F>(path: P, warn: F) -> Result<Self, Error>
    where
        P: AsRef<Path>,
        F: FnMut(Warning),
    {
        let reader = BufReader::new(File::open(path)?);
        Self::open(reader, warn)
    }
}

impl<R> PortableFile<R>
where
    R: Read + Seek,
{
    /// Reads `reader`, which should be in the SPSS portable file format.
    /// Following the file header and character set, counts the incidence of
    /// each byte value in the file.  Returns a table with those counts, plus a
    /// [TranslationTable] derived from the character set in the file header.
    pub fn read_histogram(reader: R) -> Result<([usize; 256], TranslationTable), Error>
    where
        R: BufRead,
    {
        let mut reader = ReadPad::new(reader);

        // Read and ignore header.
        reader.read_exact(&mut [0; 200])?;
        let mut character_set = [0; 256];
        reader.read_exact(&mut character_set)?;
        reader.read_exact(&mut [0; 8])?;

        let mut buf = [0; 4096];
        let mut histogram = [0; 256];
        loop {
            let n = reader.read(&mut buf)?;
            if n == 0 {
                break;
            }

            for c in buf[..n].iter().copied() {
                histogram[c as usize] += 1;
            }
        }
        Ok((histogram, TranslationTable::new(&character_set)))
    }

    /// Opens `reader` as a portable file, invoking `warn` with any warnings
    /// diagnosed while reading it.
    pub fn open<F>(reader: R, mut warn: F) -> Result<Self, Error>
    where
        F: FnMut(Warning),
    {
        fn read_inner<R, F>(
            mut reader: R,
            mut warn: F,
            character_set: [u8; 256],
        ) -> Result<(Dictionary, Metadata), ErrorDetails>
        where
            R: Read + Seek,
            F: FnMut(Warning),
        {
            let mut signature = [0; 8];
            reader.read_exact(&mut signature)?;
            if &signature != b"SPSSPORT" {
                return Err(ErrorDetails::NotAPortableFile);
            }
            let (c, metadata) = read_version(&mut reader, &mut warn, character_set)?;
            let (mut c, mut dictionary) = read_variables(&mut reader, c, &mut warn)?;
            while c == b'D' {
                read_value_label(&mut reader, &mut dictionary, &mut warn)?;
                c = read_byte(&mut reader)?;
            }
            if c == b'E' {
                read_documents(&mut reader, &mut dictionary)?;
                c = read_byte(&mut reader)?;
            }
            if c != b'F' {
                return Err(ErrorDetails::DataRecordExpected);
            }
            Ok((dictionary, metadata))
        }
        fn read_version<R, F>(
            mut reader: R,
            mut warn: F,
            character_set: [u8; 256],
        ) -> Result<(u8, Metadata), ErrorDetails>
        where
            R: Read,
            F: FnMut(Warning),
        {
            let byte = read_byte(&mut reader)?;
            if byte != b'A' {
                return Err(ErrorDetails::UnrecognizedVersionCode(byte as char));
            }

            let date = read_string(&mut reader)?;
            let date = if date.len() == 8
                && date.is_ascii()
                && let Ok(year) = date[..4].parse()
                && let Ok(month) = date[4..6].parse()
                && let Ok(day) = date[6..].parse()
                && let Some(date) = NaiveDate::from_ymd_opt(year, month, day)
            {
                Some(date)
            } else {
                warn(Warning::InvalidDate(date));
                None
            };
            let time = read_string(&mut reader)?;
            let time = if let Ok(hms) = time.trim().parse::<u32>()
                && let Some(time) =
                    NaiveTime::from_hms_opt(hms / 10000, (hms % 10000) / 100, hms % 100)
            {
                Some(time)
            } else {
                if !time.trim().is_empty() {
                    warn(Warning::InvalidTime(time));
                }
                None
            };
            let creation = date.map(|date| NaiveDateTime::new(date, time.unwrap_or_default()));

            let mut c = read_byte(&mut reader)?;
            let product = if c == b'1' {
                let product = read_string(&mut reader)?;
                c = read_byte(&mut reader)?;
                Some(product)
            } else {
                None
            };
            let author = if c == b'2' {
                let author = read_string(&mut reader)?;
                c = read_byte(&mut reader)?;
                Some(author)
            } else {
                None
            };

            let product_ext = if c == b'3' {
                let product_ext = read_string(&mut reader)?;
                c = read_byte(&mut reader)?;
                Some(product_ext)
            } else {
                None
            };

            Ok((
                c,
                Metadata {
                    creation,
                    product,
                    product_ext,
                    author,
                    character_set,
                },
            ))
        }

        fn read_format<R, F>(
            mut reader: R,
            width: VarWidth,
            warn: F,
        ) -> Result<Format, ErrorDetails>
        where
            R: Read,
            F: FnOnce(Format, FormatError),
        {
            let type_: u16 = read_integer(&mut reader)?;
            let w: u16 = read_integer(&mut reader)?;
            let d: u8 = read_integer(&mut reader)?;
            Ok(Type::try_from(type_)
                .map(|type_| UncheckedFormat { type_, w, d })
                .and_then(Format::try_from)
                .and_then(|x| x.check_width_compatibility(width))
                .unwrap_or_else(|error| {
                    let new_format = Format::default_for_width(width);
                    warn(new_format, error);
                    new_format
                }))
        }

        fn read_variables<R, F>(
            mut reader: R,
            mut c: u8,
            mut warn: F,
        ) -> Result<(u8, Dictionary), ErrorDetails>
        where
            R: Read + Seek,
            F: FnMut(Warning),
        {
            let mut dictionary = Dictionary::new(WINDOWS_1252);

            if c != b'4' {
                return Err(ErrorDetails::ExpectedVariableCountRecord(c as char));
            }
            let n_vars: usize = read_integer(&mut reader)?;

            c = read_byte(&mut reader)?;
            if c == b'5' {
                let _ = read_f64(&mut reader)?;
                c = read_byte(&mut reader)?;
            }
            let weight_name = if c == b'6' {
                let weight_name = read_identifier(&mut reader, &mut warn)?;
                c = read_byte(&mut reader)?;
                weight_name
            } else {
                None
            };

            let mut n_generated_names = 0;
            fn generate_name(dictionary: &Dictionary, n_generated_names: &mut usize) -> Identifier {
                loop {
                    *n_generated_names = n_generated_names.checked_add(1).unwrap();
                    let name = Identifier::from_encoding(
                        format!("VAR{:03}", *n_generated_names),
                        WINDOWS_1252,
                    )
                    .unwrap();
                    if !dictionary.variables.contains(&name.0) {
                        return name;
                    }
                }
            }

            for _ in 0..n_vars {
                if c != b'7' {
                    return Err(ErrorDetails::ExpectedVariableRecord);
                }
                let width: u16 = read_integer(&mut reader)?;
                let name = read_string(&mut reader)?;
                let name = match Identifier::from_encoding(name, WINDOWS_1252)
                    .and_then(Identifier::must_be_ordinary)
                {
                    Ok(name) => {
                        if !dictionary.variables.contains(&name.0) {
                            name
                        } else {
                            let new_name = generate_name(&dictionary, &mut n_generated_names);
                            warn(Warning::DuplicateVariableName {
                                duplicate_name: name.clone(),
                                new_name: new_name.clone(),
                            });
                            new_name
                        }
                    }
                    Err(id_error) => {
                        let new_name = generate_name(&dictionary, &mut n_generated_names);
                        warn(Warning::InvalidVariableName {
                            id_error,
                            new_name: new_name.clone(),
                        });
                        new_name
                    }
                };
                let width = match width {
                    0 => VarWidth::Numeric,
                    width => VarWidth::String(width as u16),
                };

                let print = read_format(&mut reader, width, |new_spec, format_error| {
                    warn(Warning::InvalidPrintFormat {
                        new_format: new_spec,
                        variable: name.clone(),
                        format_error,
                    })
                })?;
                let write = read_format(&mut reader, width, |new_spec, format_error| {
                    warn(Warning::InvalidWriteFormat {
                        new_format: new_spec,
                        variable: name.clone(),
                        format_error,
                    })
                })?;

                c = read_byte(&mut reader)?;
                let range = match c {
                    b'B' => Some(MissingValueRange::In {
                        low: read_f64(&mut reader)?,
                        high: read_f64(&mut reader)?,
                    }),
                    b'A' => Some(MissingValueRange::From {
                        low: read_f64(&mut reader)?,
                    }),
                    b'9' => Some(MissingValueRange::To {
                        high: read_f64(&mut reader)?,
                    }),
                    _ => None,
                };
                if range.is_some() {
                    c = read_byte(&mut reader)?;
                }
                let mut values = Vec::new();
                while c == b'8' {
                    values.push(read_value(&mut reader, width.into())?);
                    c = read_byte(&mut reader)?;
                }
                let missing_values = MissingValues::new(values, range)
                    .inspect_err(|error| {
                        warn(Warning::InvalidMissingValues {
                            name: name.clone(),
                            error: *error,
                        })
                    })
                    .unwrap_or_default();

                let label = if c == b'C' {
                    let label = read_string(&mut reader)?;
                    c = read_byte(&mut reader)?;
                    Some(label)
                } else {
                    None
                };

                let mut variable = Variable::new(name, width, WINDOWS_1252);
                variable.print_format = print;
                variable.write_format = write;
                if let Err(error) = variable.missing_values_mut().replace(missing_values) {
                    warn(Warning::InvalidMissingValues {
                        name: variable.name.clone(),
                        error,
                    })
                }
                variable.label = label;
                dictionary.add_var(variable).unwrap();
            }

            if let Some(weight_name) = weight_name {
                if let Some(dict_index) = dictionary.variables.get_index_of(&weight_name.0) {
                    let _ = dictionary.set_weight(Some(dict_index));
                } else {
                    warn(Warning::UnknownWeightVariable(weight_name))
                }
            }
            Ok((c, dictionary))
        }

        fn read_value_label<R, F>(
            mut reader: R,
            dictionary: &mut Dictionary,
            mut warn: F,
        ) -> Result<(), ErrorDetails>
        where
            R: Read,
            F: FnMut(Warning),
        {
            let n_variables = read_integer(&mut reader)?;
            let mut dict_indexes = Vec::with_capacity(n_variables);
            let mut var_type = None;
            for _ in 0..n_variables {
                if let Some(dict_index) = read_variable_name(&mut reader, dictionary, &mut warn)? {
                    let type_ = VarType::from(dictionary.variables[dict_index].width);
                    if var_type.is_none() {
                        var_type = Some(type_);
                    } else if var_type != Some(type_) {
                        warn(Warning::MixedVariableTypes);
                        continue;
                    }
                    dict_indexes.push(dict_index);
                }
            }
            let Some(var_type) = var_type else {
                return Err(ErrorDetails::NoValueLabelVariables);
            };

            let n_labels = read_integer(&mut reader)?;
            for _ in 0..n_labels {
                let value = read_value(&mut reader, var_type)?.without_encoding();
                let label = read_string(&mut reader)?;
                for dict_index in dict_indexes.iter().copied() {
                    dictionary
                        .variables
                        .get_index_mut2(dict_index)
                        .unwrap()
                        .value_labels
                        .insert(value.clone(), label.clone());
                }
            }
            Ok(())
        }

        fn read_documents<R>(mut reader: R, dictionary: &mut Dictionary) -> Result<(), ErrorDetails>
        where
            R: Read,
        {
            let n_lines: usize = read_integer(&mut reader)?;
            for _ in 0..n_lines {
                dictionary.documents.push(read_string(&mut reader)?);
            }
            Ok(())
        }

        let mut reader = ReadPad::new(reader);

        // Read and ignore vanity splash strings.
        reader.read_exact(&mut [0; 200])?;

        // Read the character set.
        let mut character_set = [0; 256];
        reader.read_exact(&mut character_set)?;
        let translations = TranslationTable::new(&character_set);

        let mut reader = ReadTranslate::new(reader, translations);
        let (dictionary, metadata) =
            read_inner(&mut reader, &mut warn, character_set).map_err(|details| Error {
                offset: reader.stream_position().ok(),
                details,
            })?;
        let variables = dictionary.variables.iter().map(|var| var.width).collect();
        Ok(PortableFile {
            dictionary,
            metadata,
            cases: Cases::new(reader, variables),
        })
    }
}

fn read_raw_string<R>(mut reader: R) -> Result<Vec<u8>, ErrorDetails>
where
    R: Read,
{
    let n: u16 = read_integer(&mut reader)?;
    let mut vec = vec![0u8; n as usize];
    reader.read_exact(&mut vec)?;
    Ok(vec)
}

fn read_string<R>(reader: R) -> Result<String, ErrorDetails>
where
    R: Read,
{
    // This `unwrap()` can't panic because the translation table only
    // translates to ASCII characters
    Ok(String::from_utf8(read_raw_string(reader)?).unwrap())
}

fn read_identifier<R, F>(reader: R, mut warn: F) -> Result<Option<Identifier>, ErrorDetails>
where
    R: Read,
    F: FnMut(Warning),
{
    let string = read_string(reader)?;
    match Identifier::from_encoding(string.clone(), WINDOWS_1252) {
        Ok(identifier) => Ok(Some(identifier)),
        Err(error) => {
            warn(Warning::InvalidIdentifier { string, error });
            Ok(None)
        }
    }
}

fn read_variable_name<R, F>(
    reader: R,
    dictionary: &Dictionary,
    mut warn: F,
) -> Result<Option<DictIndex>, ErrorDetails>
where
    R: Read,
    F: FnMut(Warning),
{
    let Some(var_name) = read_identifier(reader, &mut warn)? else {
        return Ok(None);
    };
    let dict_index = dictionary.variables.get_index_of(&var_name.0);
    if dict_index.is_none() {
        warn(Warning::UnknownVariableName(var_name));
    }
    Ok(dict_index)
}

fn read_integer<T, R>(reader: R) -> Result<T, ErrorDetails>
where
    R: Read,
    T: NumCast + Bounded + Display,
{
    let float = read_f64(reader)?;
    if float.trunc() == float && float >= i64::MIN as f64 && float <= i64::MAX as f64 {
        if let Some(integer) = num::cast(float) {
            Ok(integer)
        } else {
            Err(ErrorDetails::OutOfRangeInteger {
                float,
                min_value: T::min_value().to_string(),
                max_value: T::max_value().to_string(),
            })
        }
    } else {
        Err(ErrorDetails::InvalidInteger)
    }
}

fn read_value<R>(
    reader: R,
    var_type: VarType,
) -> Result<Datum<WithEncoding<ByteString>>, ErrorDetails>
where
    R: Read,
{
    match var_type {
        VarType::Numeric => Ok(Datum::Number(read_f64_or_missing(reader)?)),
        VarType::String => Ok(Datum::String(
            ByteString::from(Vec::from(read_string(reader)?)).with_encoding(WINDOWS_1252),
        )),
    }
}

fn read_f64<R>(reader: R) -> Result<f64, ErrorDetails>
where
    R: Read,
{
    match read_f64_or_missing(reader)? {
        Some(value) => Ok(value),
        None => Err(ErrorDetails::UnexpectedSysmis),
    }
}

fn read_f64_or_missing<R>(mut reader: R) -> Result<Option<f64>, ErrorDetails>
where
    R: Read,
{
    let mut c = read_byte(&mut reader)?;
    while c == b' ' {
        c = read_byte(&mut reader)?;
    }
    if c == b'*' {
        let _ = read_byte(&mut reader)?;
        return Ok(None);
    }
    let negative = if c == b'-' {
        c = read_byte(&mut reader)?;
        true
    } else {
        false
    };
    let mut significand = 0;
    let mut exponent = 0i32;
    let mut saw_dot = false;
    let mut saw_digit = false;
    loop {
        if let Some(digit) = (c as char).to_digit(30) {
            saw_digit = true;
            if significand >= u64::MAX / 30 - 30 {
                // The value of the digit doesn't matter, since we have already
                // recorded more digits as can be represented in `f64`.
                // We just need to record that there was another digit so that
                // we can multiply by 30 later.
                exponent += 1;
            } else {
                significand = significand * 30 + digit as u64;
            }

            if saw_dot {
                exponent -= 1;
            }
        } else if c == b'.' && !saw_dot {
            saw_dot = true;
        } else {
            break;
        }

        c = read_byte(&mut reader)?;
    }
    if !saw_digit {
        return Err(ErrorDetails::NumberExpected);
    }

    if c == b'+' || c == b'-' {
        let exp_sign = c;
        let mut exp = 0i32;
        c = read_byte(&mut reader)?;
        while let Some(digit) = (c as char).to_digit(30) {
            exp = exp * 30 + digit as i32;
            c = read_byte(&mut reader)?;
        }
        if exp_sign == b'+' {
            exponent -= exp;
        } else {
            exponent += exp;
        }
    }

    if c != b'/' {
        return Err(ErrorDetails::MissingSlash);
    }

    let significand = significand as f64;
    let num = match exponent.cmp(&0) {
        Ordering::Less => significand * 30.0f64.powi(exponent),
        Ordering::Equal => significand,
        Ordering::Greater if significand > f64::MAX * 30.0f64.powi(-exponent) => f64::MAX,
        Ordering::Greater => significand * 30.0f64.powi(exponent),
    };
    Ok(Some(if negative { -num } else { num }))
}

fn read_byte<R>(mut reader: R) -> IoResult<u8>
where
    R: Read,
{
    let mut byte = 0;
    reader.read_exact(std::slice::from_mut(&mut byte))?;
    Ok(byte)
}

/// A [Read] wrapper that translates the bytes it reads using a
/// [TranslationTable].
#[derive(Debug)]
pub struct ReadTranslate<R> {
    inner: R,
    translations: TranslationTable,
}

impl<R> ReadTranslate<R> {
    /// Create a new [ReadTranslate] with `inner` and `translations`.
    pub fn new(inner: R, translations: TranslationTable) -> Self {
        Self {
            inner,
            translations,
        }
    }

    /// Consumes this [ReadTranslate], returning the inner reader.
    pub fn into_inner(self) -> R {
        self.inner
    }
}

impl<R> Read for ReadTranslate<R>
where
    R: Read,
{
    fn read(&mut self, buf: &mut [u8]) -> IoResult<usize> {
        let n = self.inner.read(buf)?;
        for c in &mut buf[..n] {
            *c = self.translations[*c];
        }
        Ok(n)
    }
}

impl<R> Seek for ReadTranslate<R>
where
    R: Seek,
{
    fn seek(&mut self, pos: SeekFrom) -> IoResult<u64> {
        self.inner.seek(pos)
    }
}

/// A [Read] wrapper that skips newlines and pads lines to 80 bytes with spaces.
#[derive(Debug)]
pub struct ReadPad<R> {
    inner: R,
    at_newline: bool,
    line_length: usize,
}

impl<R> ReadPad<R> {
    /// Constructs a [ReadPad] wrapper for `inner`.
    pub fn new(inner: R) -> Self {
        Self {
            inner,
            at_newline: false,
            line_length: 0,
        }
    }

    /// Consumes this [ReadPad], returning the inner reader.
    pub fn into_inner(self) -> R {
        self.inner
    }
}

impl<R> Read for ReadPad<R>
where
    R: Read,
{
    fn read(&mut self, buf: &mut [u8]) -> IoResult<usize> {
        for (i, c) in buf.into_iter().enumerate() {
            if self.at_newline {
                *c = b' ';
                self.line_length += 1;
                if self.line_length >= 80 {
                    self.at_newline = false;
                    self.line_length = 0;
                }
            } else {
                loop {
                    match self.inner.read(std::slice::from_mut(c)) {
                        Ok(1) => (),
                        other => return if i > 0 { Ok(i) } else { other },
                    };
                    match *c {
                        b'\r' => continue,
                        b'\n' => match self.line_length {
                            80.. => {
                                self.line_length = 0;
                                continue;
                            }
                            79 => {
                                self.line_length = 0;
                                *c = b' ';
                                break;
                            }
                            0..79 => {
                                self.at_newline = true;
                                self.line_length += 1;
                                *c = b' ';
                                break;
                            }
                        },
                        _ => {
                            self.line_length += 1;
                            break;
                        }
                    }
                }
            }
        }
        Ok(buf.len())
    }
}

impl<R> Seek for ReadPad<R>
where
    R: Seek,
{
    fn seek(&mut self, pos: SeekFrom) -> IoResult<u64> {
        self.inner.seek(pos)
    }
}

#[cfg(test)]
mod tests {
    use std::{
        io::{BufRead, BufReader, Cursor},
        path::Path,
    };

    use itertools::Itertools;

    use crate::{
        data::cases_to_output,
        output::{
            pivot::{tests::assert_lines_eq, PivotTable},
            Details, Item, Text,
        },
        por::{PortableFile, ReadPad},
    };

    #[test]
    fn read_wrapper() {
        let mut lines = BufReader::new(ReadPad::new(Cursor::new(
            b"abcdefghijklmnop\r\n0123456789\r\n",
        )))
        .lines();
        assert_eq!(lines.next().unwrap().unwrap(), "abcdefghijklmnop                                                                0123456789                                                                      ");
    }

    fn test_porfile(name: &str) {
        let base_filename = Path::new("src/por/testdata").join(name);
        let input_filename = base_filename.with_extension("por");
        let expected_filename = base_filename.with_extension("expected");

        let mut warnings = Vec::new();
        let output = match PortableFile::open_file(input_filename, |warning| warnings.push(warning))
        {
            Ok(portable_file) => {
                let (dictionary, metadata, cases) = portable_file.into_parts();

                let mut output = Vec::new();
                output.extend(
                    warnings
                        .into_iter()
                        .map(|warning| Item::from(Text::new_log(warning.to_string()))),
                );
                output.push(PivotTable::from(&metadata).into());
                output.extend(dictionary.all_pivot_tables().into_iter().map_into());
                output.extend(cases_to_output(&dictionary, cases));
                Item::new(Details::Group(output.into_iter().map_into().collect()))
            }
            Err(error) => Item::new(Details::Text(Box::new(Text::new_log(error.to_string())))),
        };

        let actual = output.to_string();
        let expected = std::fs::read_to_string(&expected_filename).unwrap();
        if expected != actual {
            if std::env::var("PSPP_REFRESH_EXPECTED").is_ok() {
                std::fs::write(&expected_filename, actual).unwrap();
                panic!("{}: refreshed output", expected_filename.display());
            } else {
                eprintln!("note: rerun with PSPP_REFRESH_EXPECTED=1 to refresh expected output");
            }
        }
        assert_lines_eq(&expected, expected_filename.display(), &actual, "actual");
    }

    #[test]
    fn porfile_test1() {
        test_porfile("test1");
    }

    #[test]
    fn porfile_test2() {
        test_porfile("test2");
    }
}
