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

//! Reading SPSS/PC+ data files.
//!
//! This module enables reading [SPSS/PC+ data files], the data format for the
//! SPSS/PC+ product first released in 1984.  It is obsolete.
//!
//! Use [PcFile] to read an SPSS/PC+ file.  Writing SPSS/PC+ files is not
//! supported.
//!
//! [SPSS/PC+ data files]: https://pspp.benpfaff.org/manual/pc+.html
#![cfg_attr(not(test), warn(missing_docs))]

use std::{
    collections::VecDeque,
    fmt::{Display, Formatter},
    fs::File,
    io::{BufReader, Error as IoError, Read, Seek, SeekFrom, Take},
    path::Path,
};

use binrw::{BinRead, Endian, Error as BinError};
use chrono::{NaiveDate, NaiveDateTime, NaiveTime};
use displaydoc::Display;
use encoding_rs::WINDOWS_1252;
use serde::Serialize;
use thiserror::Error as ThisError;

use crate::{
    data::{ByteString, Case, Datum, RawString, WithEncoding},
    dictionary::Dictionary,
    format::{Error as FormatError, Format, UncheckedFormat},
    identifier::{Error as IdError, Identifier},
    output::pivot::{MetadataEntry, MetadataValue, PivotTable, Value},
    sys::raw::{self, CaseDetails, CaseVar, CompressionAction, records::RawFormat},
    variable::{MissingValues, MissingValuesError, VarWidth, Variable},
};

#[cfg(test)]
mod tests;

/// An [SPSS/PC+ data file].
///
/// [SPSS/PC+ data file]: https://pspp.benpfaff.org/manual/pc+.html
#[derive(Debug)]
pub struct PcFile<R> {
    /// The data file's dictionary.
    pub dictionary: Dictionary,

    /// SPSS/PC+ file metadata that is not part of the dictionary.
    pub metadata: Metadata,

    /// Data in the SPSS/PC+ file.
    pub cases: Cases<R>,
}

impl<R> PcFile<R> {
    /// Returns the individual parts of the [PcFile].
    pub fn into_parts(self) -> (Dictionary, Metadata, Cases<R>) {
        (self.dictionary, self.metadata, self.cases)
    }
}

/// SPSS/PC+ product family.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Family {
    /// Data analysis product family.
    ///
    /// This includes at least:
    /// - SPSS/PC+
    /// - SPSS/PC+ V3.0
    Pc,

    /// Data entry product family.
    ///
    /// This includes at least:
    /// - SPSS Data Entry
    /// - SPSS Data Entry II
    De,
}

/// SPSS/PC+ file metadata that is not part of [Dictionary].
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Metadata {
    /// Creation date and time.
    ///
    /// This comes from the file header, not from the file system.
    pub creation: NaiveDateTime,

    /// Product family.
    pub family: Family,

    /// Name of the product that wrote the file.
    pub product: Option<String>,

    /// Additional metadata that in some files identifies a file name.
    pub filename: Option<String>,

    /// Whether data in the file is bytecode compressed.
    pub compressed: bool,

    /// Number of declared cases in the file.
    pub n_cases: u16,
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
            name: Value::new_user_text("SPSS/PC+ File Metadata"),
            value: MetadataValue::Group(vec![
                MetadataEntry {
                    name: Value::new_user_text("Created"),
                    value: MetadataValue::new_leaf(Value::new_date_time(value.creation)),
                },
                maybe_string("Product", &value.product),
                maybe_string("File Name", &value.filename),
                MetadataEntry::new(
                    "Compression",
                    MetadataValue::new_leaf(if value.compressed { "Simple" } else { "None" }),
                ),
                MetadataEntry::new(
                    "Number of Cases",
                    MetadataValue::new_leaf(Value::new_integer(Some(value.n_cases as f64))),
                ),
            ]),
        }
        .into_pivot_table()
    }
}

/// Reader for cases in a SPSS/PC+ file.
#[derive(Debug)]
pub struct Cases<R> {
    reader: Take<R>,
    compressed: bool,
    case_vars: Vec<CaseVar>,
    codes: VecDeque<u8>,
    read_cases: u64,
    sysmis: f64,
    n_cases: u16,
    eof: bool,
}

impl<R> Cases<R> {
    fn new(reader: Take<R>, dictionary: &Dictionary, metadata: &Metadata, sysmis: f64) -> Self {
        Self {
            reader,
            compressed: metadata.compressed,
            case_vars: dictionary
                .variables
                .iter()
                .map(|var| var.width.into())
                .collect(),
            codes: VecDeque::new(),
            sysmis,
            read_cases: 0,
            n_cases: metadata.n_cases,
            eof: false,
        }
    }

    fn read_case(&mut self) -> Result<Case<Vec<Datum<ByteString>>>, raw::Error<CaseDetails>>
    where
        R: Read + Seek,
    {
        let result = if !self.compressed {
            Datum::read_case(
                &mut self.reader,
                self.read_cases + 1,
                &self.case_vars,
                Endian::Little,
            )
        } else {
            Datum::read_compressed_case(
                &mut self.reader,
                self.read_cases + 1,
                &self.case_vars,
                &mut self.codes,
                CompressionAction::from_pc,
                Endian::Little,
            )
        };

        match result {
            Ok(Some(mut raw_case)) => {
                for datum in &mut raw_case.0 {
                    if let Datum::Number(Some(number)) = datum
                        && *number == self.sysmis
                    {
                        *datum = Datum::Number(None);
                    }
                }
                Ok(raw_case.with_encoding(WINDOWS_1252))
            }
            Ok(None) => Err(raw::Error::new(
                None,
                CaseDetails::WrongNumberOfCases {
                    expected: self.n_cases as u64,
                    actual: self.read_cases,
                },
            )),
            Err(error) => Err(error),
        }
    }
}

impl CompressionAction {
    /// Interprets an SPSS/PC+ system file compression opcode.
    fn from_pc(code: u8) -> Self {
        match code {
            0 => Self::Sysmis,
            1 => Self::Literal,
            other => Self::CompressedInt(other as f64 - 100.0),
        }
    }
}

impl<R> Iterator for Cases<R>
where
    R: Read + Seek,
{
    type Item = Result<Case<Vec<Datum<ByteString>>>, raw::Error<CaseDetails>>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.eof || self.case_vars.is_empty() || self.read_cases >= self.n_cases as u64 {
            return None;
        }

        match self.read_case() {
            Ok(case) => {
                self.read_cases += 1;
                Some(Ok(case))
            }
            Err(error) => {
                self.eof = true;
                Some(Err(error))
            }
        }
    }
}

/// An error encountered reading a SPSS/PC+ file.
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

/// An error for reading a [PcFile].
#[derive(Display, ThisError, Debug)]
pub enum ErrorDetails {
    /// Not an SPSS/PC+ data file.
    NotPc,

    /// I/O error ({0}).
    Io(#[from] IoError),

    /// {0}
    BinError(DisplayBinError),

    /// Invalid variable format: {0}.
    InvalidFormat(FormatError),

    /// File header record declares {nominal_case_size} variable segments but the variable records contain more than that (at least {n_chunks}).
    TooManyVariables {
        /// Declared number of variable segments.
        nominal_case_size: u16,
        /// Actual number of variable segments.
        n_chunks: usize,
    },

    /// Labels record ({record}) extends beyond end of file with length {file_size}.
    InvalidLabelsRecord {
        /// Labels record location.
        record: Record,
        /// File size.
        file_size: u64,
    },
}

impl From<BinError> for ErrorDetails {
    fn from(value: BinError) -> Self {
        Self::BinError(DisplayBinError(value))
    }
}

/// Newtype that implements [Display] for [BinError].
#[derive(Debug)]
pub struct DisplayBinError(BinError);

impl Display for DisplayBinError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        if self.0.is_eof() {
            write!(f, "Unexpected end-of-file reading {}", self.0)
        } else {
            write!(f, "Error reading SPSS/PC+ file: {}", self.0.root_cause())
        }
    }
}
/// A warning while reading a [PcFile].
#[derive(Display, ThisError, Debug)]
pub enum Warning {
    /// Invalid creation date {0}.
    InvalidCreationDate(String),

    /// Invalid creation time {0}.
    InvalidCreationTime(String),

    /// Invalid variable name.  {id_error}  Substituting {new_name} instead.
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

    /// Ignoring missing value for long string variable {0}.
    LongStringMissingValue(Identifier),

    /// Invalid missing values for variable {name}: {error}.
    InvalidMissingValues {
        /// Variable name.
        name: Identifier,
        /// Kind of error with missing values.
        error: MissingValuesError,
    },

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

    /// Cannot weight by string variable {0}.
    StringWeight(Identifier),

    /// File's specified weight index {0} does not refer to any variable.
    InvalidWeightIndex(u16),

    /// Variable record for {name} refers to invalid variable label starting at offset {offset} in label record.
    InvalidVarLabel {
        /// Variable name.
        name: Identifier,
        /// Offset into label record.
        offset: usize,
    },

    /// Value labels for {name}, at file offsets {start:#x}..{end:#x}, end with last value label (starting at file offset {offset:#x}) running past end offset.  (This warning appears for some system files written by SPSS Data Entry products.)
    ValueLabelOverflow {
        /// Variable name.
        name: Identifier,
        /// File starting offset for variable's value labels.
        start: usize,
        /// File ending offset for variable's value labels.
        end: usize,
        /// File offset for last value label.
        offset: u64,
    },

    /// Ignoring value labels for long string variable {0}.
    LongStringValueLabel(
        /// Variable name.
        Identifier,
    ),

    /// Value label for {name} specifies invalid range {start}..{end} into labels record with length {len}.
    InvalidValueLabelRange {
        /// Variable name.
        name: Identifier,
        /// Starting offset in labels record.
        start: usize,
        /// Ending offset in labels record.
        end: usize,
        /// Length of labels record.
        len: usize,
    },

    /// File header inconsistently reports {0} cases in one place and {1} in another; assuming {0} cases.
    InconsistentCaseCount(u16, u16),
}

#[derive(Debug, BinRead)]
#[br(little)]
struct FileHeader {
    two: u32,
    zero: u32,
    main_record: Record,
    variables_record: Record,
    labels_record: Record,
    data_record: Record,
    _other_records: [Record; 11],
    filename: [u8; 128],
}

/// A record in an SPSS/PC+ system file.
#[derive(Copy, Clone, Debug, PartialEq, Eq, BinRead)]
#[br(little)]
pub struct Record {
    /// File starting offset of the record.
    offset: u32,
    /// Length of the record in bytes.
    len: u32,
}

impl Record {
    fn new(offset: u32, len: u32) -> Self {
        Self { offset, len }
    }
}

impl Display for Record {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "offset {}, length {}", self.offset, self.len)
    }
}

#[derive(Debug, BinRead)]
#[br(little)]
struct MainHeader {
    one0: u16,
    family: [u8; 2],
    product: [u8; 60],
    sysmis: f64,
    zero0: u32,
    zero1: u32,
    one1: u16,
    compressed: u16,
    nominal_case_size: u16,
    n_cases0: u16,
    weight_index: u16,
    _unknown: u16,
    n_cases1: u16,
    zero2: u16,
    creation_date: [u8; 8],
    creation_time: [u8; 8],
    file_label: [u8; 64],
}

#[derive(BinRead)]
#[br(little)]
struct VariableRecord {
    value_label_start: u32,
    value_label_end: u32,
    var_label_ofs: u32,
    format: RawFormat,
    name: [u8; 8],
    missing: [u8; 8],
}

impl PcFile<BufReader<File>> {
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

impl<R> PcFile<R>
where
    R: Read + Seek,
{
    /// Opens `reader` as an SPSS/PC+ file, invoking `warn` with any warnings
    /// diagnosed while reading it.
    pub fn open<F>(mut reader: R, mut warn: F) -> Result<Self, Error>
    where
        F: FnMut(Warning),
    {
        fn read_inner<R, F>(
            mut reader: R,
            mut warn: F,
        ) -> Result<(Dictionary, Metadata, Record, f64), ErrorDetails>
        where
            R: Read + Seek,
            F: FnMut(Warning),
        {
            let file_header = FileHeader::read(&mut reader)?;
            if file_header.two != 2
                || file_header.zero != 0
                || file_header.main_record != Record::new(0x100, 0xb0)
            {
                return Err(ErrorDetails::NotPc);
            }

            reader.seek(SeekFrom::Start(file_header.main_record.offset as u64))?;
            let main_header = MainHeader::read(&mut reader)?;
            if main_header.one0 != 1
                || main_header.one1 != 1
                || main_header.zero0 != 0
                || main_header.zero1 != 0
                || main_header.zero2 != 0
            {
                return Err(ErrorDetails::NotPc);
            }
            let family = match &main_header.family {
                b"DE" => Ok(Family::De),
                b"PC" => Ok(Family::Pc),
                _ => Err(ErrorDetails::NotPc),
            }?;
            if main_header.n_cases0 != main_header.n_cases1 {
                warn(Warning::InconsistentCaseCount(
                    main_header.n_cases0,
                    main_header.n_cases1,
                ));
            }
            let sysmis = main_header.sysmis;

            let mut dictionary = Dictionary::new(WINDOWS_1252);

            let file_label = WINDOWS_1252.decode(&main_header.file_label);
            let file_label = file_label.0.trim();
            if !file_label.is_empty() {
                dictionary.file_label = Some(file_label.into());
            }

            let creation_date = WINDOWS_1252.decode(&main_header.creation_date).0;
            let creation_date = NaiveDate::parse_from_str(creation_date.trim(), "%m/%d/%y")
                .unwrap_or_else(|_| {
                    warn(Warning::InvalidCreationDate(creation_date.into_owned()));
                    Default::default()
                });
            let creation_time = WINDOWS_1252.decode(&main_header.creation_time).0;
            let creation_time = NaiveTime::parse_from_str(creation_time.trim(), "%H:%M:%S")
                .unwrap_or_else(|_| {
                    warn(Warning::InvalidCreationTime(creation_time.into_owned()));
                    Default::default()
                });
            let creation = NaiveDateTime::new(creation_date, creation_time);

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

            let file_size = reader.seek(SeekFrom::End(0))?;
            if u64::from(file_header.labels_record.offset)
                + u64::from(file_header.labels_record.len)
                > file_size
            {
                return Err(ErrorDetails::InvalidLabelsRecord {
                    record: file_header.labels_record,
                    file_size,
                });
            }
            reader.seek(SeekFrom::Start(file_header.labels_record.offset as u64))?;
            let mut labels = vec![0; file_header.labels_record.len as usize];
            reader.read_exact(&mut labels)?;

            reader.seek(SeekFrom::Start(file_header.variables_record.offset as u64))?;

            let mut index = 0;
            let mut weight_index = None;
            let mut n_overflows = 0;
            while index < main_header.nominal_case_size as usize {
                if main_header.weight_index as usize == index + 1 {
                    weight_index = Some(dictionary.variables.len());
                }

                let variable_record = VariableRecord::read(&mut reader)?;
                let mut name = String::from(WINDOWS_1252.decode(&variable_record.name).0.trim());
                if name.starts_with('$') {
                    name.replace_range(..1, "@");
                }
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

                let format = UncheckedFormat::try_from(variable_record.format)
                    .and_then(Format::try_from)
                    .map_err(ErrorDetails::InvalidFormat)?;

                let width = format.var_width();
                let mut variable = Variable::new(name, width, WINDOWS_1252);

                // This `unwrap` cannot panic because `format`, from
                // `RawFormat`, can only represent a width <= 255.
                let n_chunks = width.n_chunks().unwrap();

                fn parse_datum(
                    datum: [u8; 8],
                    width: VarWidth,
                    sysmis: f64,
                ) -> Datum<WithEncoding<ByteString>> {
                    match width {
                        VarWidth::Numeric => {
                            let value = f64::from_le_bytes(datum);
                            Datum::Number((value != sysmis).then_some(value))
                        }
                        VarWidth::String(width) => Datum::String(
                            ByteString::from(&datum[..width as usize]).with_encoding(WINDOWS_1252),
                        ),
                    }
                }

                if sysmis != f64::from_le_bytes(variable_record.missing) {
                    if !width.is_long_string() {
                        let missing_value = MissingValues::new(
                            vec![parse_datum(variable_record.missing, width, sysmis)],
                            None,
                        )
                        .unwrap();
                        variable
                            .missing_values_mut()
                            .replace(missing_value)
                            .unwrap();
                    } else {
                        warn(Warning::LongStringMissingValue(variable.name.clone()))
                    }
                }

                if variable_record.var_label_ofs != 0 {
                    let offset = variable_record.var_label_ofs as usize + 7;
                    if let Some(len) = labels.get(offset)
                        && let Some(slice) = labels.get(offset + 1..offset + 1 + *len as usize)
                    {
                        variable.label = Some(WINDOWS_1252.decode(slice).0.into_owned());
                    } else {
                        warn(Warning::InvalidVarLabel {
                            name: variable.name.clone(),
                            offset,
                        });
                    }
                }

                if variable_record.value_label_start != 0 {
                    if width.is_long_string() {
                        warn(Warning::LongStringValueLabel(variable.name.clone()));
                    } else {
                        let start = variable_record.value_label_start as usize + 7;
                        let end = variable_record.value_label_end as usize + 7;
                        if let Some(mut slice) = labels.get(start..end) {
                            while !slice.is_empty() {
                                if let Some((value, rest)) = slice.split_at_checked(8)
                                    && let Some((length, rest)) = rest.split_first()
                                    && let Some((label, rest)) =
                                        rest.split_at_checked(*length as usize)
                                {
                                    let label = WINDOWS_1252.decode(label).0.into_owned();
                                    let value =
                                        parse_datum(value.try_into().unwrap(), width, sysmis)
                                            .without_encoding();
                                    variable.value_labels.insert(value, label);
                                    slice = rest;
                                } else {
                                    if n_overflows == 0 {
                                        warn(Warning::ValueLabelOverflow {
                                            name: variable.name.clone(),
                                            start: start
                                                + file_header.labels_record.offset as usize,
                                            end: end + file_header.labels_record.offset as usize,
                                            offset: file_header.labels_record.offset as u64
                                                + variable_record.value_label_start as u64
                                                + 7
                                                + (variable_record.value_label_end as u64
                                                    - variable_record.value_label_start as u64
                                                    - slice.len() as u64),
                                        });
                                    }
                                    n_overflows += 1;
                                    break;
                                };
                            }
                        } else {
                            warn(Warning::InvalidValueLabelRange {
                                name: variable.name.clone(),
                                start,
                                end,
                                len: labels.len(),
                            });
                        }
                    }
                }

                dictionary.add_var(variable).unwrap();

                for _ in 1..n_chunks {
                    let _variable_record = VariableRecord::read(&mut reader)?;
                }
                index += n_chunks;
                if index > main_header.nominal_case_size as usize {
                    return Err(ErrorDetails::TooManyVariables {
                        nominal_case_size: main_header.nominal_case_size,
                        n_chunks: index,
                    });
                }
            }

            if let Some(weight_index) = weight_index {
                if dictionary.set_weight(Some(weight_index)).is_err() {
                    warn(Warning::StringWeight(
                        dictionary
                            .variables
                            .get_index(weight_index)
                            .unwrap()
                            .name
                            .clone(),
                    ))
                }
            } else if main_header.weight_index != 0 {
                warn(Warning::InvalidWeightIndex(main_header.weight_index))
            }

            fn decode_optional_string(s: &[u8]) -> Option<String> {
                let s = WINDOWS_1252.decode(s).0;
                let s = s.trim_matches(&[' ', '\0']);
                if s.is_empty() { None } else { Some(s.into()) }
            }
            let metadata = Metadata {
                creation,
                family,
                product: decode_optional_string(&main_header.product),
                filename: decode_optional_string(&file_header.filename),
                compressed: main_header.compressed != 0,
                n_cases: main_header.n_cases0,
            };

            Ok((
                dictionary,
                metadata,
                file_header.data_record,
                main_header.sysmis,
            ))
        }

        let (dictionary, metadata, data_record, sysmis) = read_inner(&mut reader, &mut warn)
            .map_err(|details| Error {
                offset: reader.stream_position().ok(),
                details,
            })?;

        reader.seek(SeekFrom::Start(data_record.offset as u64))?;
        let reader = reader.take(data_record.len as u64);
        let cases = Cases::new(reader, &dictionary, &metadata, sysmis);
        Ok(PcFile {
            dictionary,
            metadata,
            cases,
        })
    }
}
