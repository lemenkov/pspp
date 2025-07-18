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

//! Raw system file record reader.
//!
//! This module facilitates reading records from system files in all of their
//! raw details.  Most readers will want to use higher-level interfaces.

use crate::{
    data::{ByteStr, ByteString, Datum, MutRawString, RawCase, RawString},
    endian::{FromBytes, ToBytes},
    identifier::{Error as IdError, Identifier},
    output::{
        pivot::{Axis3, Dimension, Group, PivotTable, Value},
        Details, Item, Text,
    },
    sys::{
        encoding::{default_encoding, get_encoding, Error as EncodingError},
        raw::records::{
            AttributeWarning, Compression, DocumentRecord, EncodingRecord, Extension,
            ExtensionWarning, FileAttributesRecord, FileHeader, FloatInfoRecord, HeaderWarning,
            IntegerInfoRecord, LongNameWarning, LongNamesRecord, LongStringMissingValueRecord,
            LongStringMissingValuesWarning, LongStringValueLabelRecord,
            LongStringValueLabelWarning, MultipleResponseRecord, MultipleResponseWarning,
            NumberOfCasesRecord, ProductInfoRecord, RawDocumentLine, RawFileAttributesRecord,
            RawLongNamesRecord, RawProductInfoRecord, RawVariableAttributesRecord,
            RawVariableSetRecord, RawVeryLongStringsRecord, ValueLabelRecord, ValueLabelWarning,
            VarDisplayRecord, VariableAttributesRecord, VariableDisplayWarning, VariableRecord,
            VariableSetRecord, VariableSetWarning, VariableWarning, VeryLongStringWarning,
            VeryLongStringsRecord, ZHeader, ZHeaderError, ZTrailer, ZTrailerError,
            ZlibTrailerWarning,
        },
    },
    variable::{VarType, VarWidth},
};

use binrw::Endian;
use encoding_rs::{
    Encoding, BIG5, EUC_JP, EUC_KR, GB18030, IBM866, ISO_2022_JP, ISO_8859_10, ISO_8859_13,
    ISO_8859_14, ISO_8859_16, ISO_8859_2, ISO_8859_3, ISO_8859_4, ISO_8859_5, ISO_8859_6,
    ISO_8859_7, ISO_8859_8, KOI8_R, KOI8_U, MACINTOSH, SHIFT_JIS, UTF_8, WINDOWS_1250,
    WINDOWS_1251, WINDOWS_1252, WINDOWS_1253, WINDOWS_1254, WINDOWS_1255, WINDOWS_1256,
    WINDOWS_1257, WINDOWS_1258, WINDOWS_874,
};
use flate2::bufread::ZlibDecoder;
use indexmap::IndexMap;
use itertools::{EitherOrBoth, Itertools};
use serde::Serialize;
use smallvec::SmallVec;
use std::{
    borrow::Cow,
    cell::RefCell,
    collections::VecDeque,
    fmt::{Debug, Display, Formatter, Result as FmtResult},
    io::{empty, BufRead, Error as IoError, Read, Seek, SeekFrom},
    iter::repeat_n,
    mem::take,
    num::NonZeroU8,
    ops::Range,
};
use thiserror::Error as ThisError;

pub mod records;

/// An error encountered reading raw system file records.
///
/// Any error prevents reading further data from the system file.
#[derive(Debug)]
pub struct Error {
    /// Range of file offsets where the error occurred.
    pub offsets: Option<Range<u64>>,

    /// Details of the error.
    pub details: ErrorDetails,
}

impl std::error::Error for Error {}

impl Error {
    /// Constructs an error from `offsets` and `details`.
    pub fn new(offsets: Option<Range<u64>>, details: ErrorDetails) -> Self {
        Self { offsets, details }
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if let Some(offsets) = &self.offsets
            && !offsets.is_empty()
        {
            if offsets.end > offsets.start.wrapping_add(1) {
                write!(
                    f,
                    "Error at file offsets {:#x} to {:#x}: ",
                    offsets.start, offsets.end
                )?;
            } else {
                write!(f, "Error at file offset {:#x}: ", offsets.start)?;
            }
        }
        write!(f, "{}", &self.details)
    }
}

impl From<IoError> for Error {
    fn from(value: IoError) -> Self {
        Self::new(None, value.into())
    }
}

/// Details of an [Error].
#[derive(ThisError, Debug)]
pub enum ErrorDetails {
    /// Not an SPSS system file.
    #[error("Not an SPSS system file")]
    NotASystemFile,

    /// Encrypted.
    #[error("File is encrypted but no password was supplied.")]
    Encrypted,

    /// Bad [Magic].
    #[error("Invalid magic number {0:?}")]
    BadMagic([u8; 4]),

    /// I/O error.
    #[error("I/O error ({0})")]
    Io(#[from] IoError),

    /// Invalid SAV compression code.
    #[error("Invalid SAV compression code {0}")]
    InvalidSavCompression(u32),

    /// Invalid ZSAV compression code {0}.
    #[error("Invalid ZSAV compression code {0}")]
    InvalidZsavCompression(u32),

    /// Document record has document line count ({n}) greater than the maximum number {max}.
    #[error(
        "Document record has document line count ({n}) greater than the maximum number {max}."
    )]
    BadDocumentLength {
        /// Number of document lines.
        n: usize,
        /// Maximum number of document lines.
        max: usize,
    },

    /// Unrecognized record type.
    #[error("Unrecognized record type {0}.")]
    BadRecordType(u32),

    /// Variable width in variable record is not in the valid range -1 to 255.
    #[error("Variable width {0} in variable record is not in the valid range -1 to 255.")]
    BadVariableWidth(i32),

    /// In variable record, variable label code is not 0 or 1.
    #[error("In variable record, variable label code {0} is not 0 or 1.")]
    BadVariableLabelCode(u32),

    /// Missing value code is not -3, -2, 0, 1, 2, or 3.
    #[error("Missing value code ({0}) is not -3, -2, 0, 1, 2, or 3.")]
    BadMissingValueCode(i32),

    /// Numeric missing value code is not -3, -2, 0, 1, 2, or 3.
    #[error("Numeric missing value code ({0}) is not -3, -2, 0, 1, 2, or 3.")]
    BadNumericMissingValueCode(i32),

    /// String missing value code is not 0, 1, 2, or 3.
    #[error("String missing value code ({0}) is not 0, 1, 2, or 3.")]
    BadStringMissingValueCode(i32),

    /// Number of value labels ({n}) is greater than the maximum number {max}.
    #[error("Number of value labels ({n}) is greater than the maximum number {max}.")]
    BadNumberOfValueLabels {
        /// Number of value labels.
        n: u32,
        /// Maximum number of value labels.
        max: u32,
    },

    /// Following value label record, found record type {0} instead of expected
    /// type 4 for variable index record.
    #[
        error(
            "Following value label record, found record type {0} instead of expected type 4 for variable index record"
        )]
    ExpectedVarIndexRecord(
        /// Record type.
        u32,
    ),

    /// Number of variables indexes for value labels ({n}) is greater than the
    /// maximum number ({max}).
    #[error(
        "Number of variables indexes for value labels ({n}) is greater than the maximum number ({max})."
    )]
    TooManyVarIndexes {
        /// Number of variable indexes.
        n: u32,
        /// Maximum number of variable indexes.
        max: u32,
    },

    /// Record type 7 subtype {subtype} is too large with element size {size} and {count} elements.
    #[error(
        "Record type 7 subtype {subtype} is too large with element size {size} and {count} elements."
    )]
    ExtensionRecordTooLarge {
        /// Subtype.
        subtype: u32,
        /// Element size in bytes.
        size: u32,
        /// Number of elements.
        count: u32,
    },

    /// Unexpected end of file {case_ofs} bytes into a {case_len}-byte case.
    #[error("Unexpected end of file {case_ofs} bytes into case {case_number} with expected length {case_len} bytes.")]
    EofInCase {
        /// Offset into case in bytes.
        case_ofs: u64,
        /// Expected case length in bytes.
        case_len: usize,
        /// 1-based case number in file.
        case_number: u64,
    },

    /// Unexpected end of file {case_ofs} bytes and {n_chunks} compression
    /// chunks into a compressed case.
    #[error(
        "Unexpected end of file {case_ofs} bytes and {n_chunks} compression chunks into compressed case {case_number}."
    )]
    EofInCompressedCase {
        /// Offset into case in bytes.
        case_ofs: u64,
        /// Number of compression codes consumed.
        n_chunks: usize,
        /// 1-based case number in file.
        case_number: u64,
    },

    /// Error reading a [ZHeader].
    #[error("Error reading ZLIB header: {0}")]
    ZHeader(#[from] ZHeaderError),

    /// Error reading a [ZTrailer].
    #[error("Error reading ZLIB trailer: {0}")]
    ZTrailer(#[from] ZTrailerError),

    /// File metadata says it contains {expected} cases, but {actual} cases were read.
    #[error("File metadata says it contains {expected} cases, but {actual} cases were read.")]
    WrongNumberOfCases {
        /// Expected number of cases.
        expected: u64,
        /// Actual number of cases.
        actual: u64,
    },

    /// Encoding error.
    #[error(transparent)]
    EncodingError(
        /// The error.
        #[from]
        EncodingError,
    ),
}

/// A warning reading a raw system file record.
///
/// Warnings indicate that something may be amiss, but they do not prevent
/// reading further records.
#[derive(Debug)]
pub struct Warning {
    /// Range of file offsets where the warning occurred.
    pub offsets: Option<Range<u64>>,

    /// Details of the warning.
    pub details: WarningDetails,
}

impl std::error::Error for Warning {}

impl Warning {
    /// Constructs a new [Warning] from `offsets` and `details`.
    pub fn new(offsets: Option<Range<u64>>, details: impl Into<WarningDetails>) -> Self {
        Self {
            offsets,
            details: details.into(),
        }
    }
}

impl Display for Warning {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if let Some(offsets) = &self.offsets
            && !offsets.is_empty()
        {
            if offsets.end > offsets.start.wrapping_add(1) {
                write!(
                    f,
                    "Warning at file offsets {:#x} to {:#x}: ",
                    offsets.start, offsets.end
                )?;
            } else {
                write!(f, "Warning at file offset {:#x}: ", offsets.start)?;
            }
        }
        write!(f, "{}", &self.details)
    }
}

/// Details of a [Warning].
#[derive(ThisError, Debug)]
pub enum WarningDetails {
    /// Warning for file header.
    #[error("In file header: {0}")]
    Header(#[from] HeaderWarning),

    /// Warning for variable records.
    #[error("In variable record: {0}")]
    Variable(#[from] VariableWarning),

    /// Warning for extension records.
    #[error("In extension record: {0}")]
    Extension(#[from] ExtensionWarning),

    /// Warning for value labels.
    #[error("In value label record: {0}")]
    ValueLabel(#[from] ValueLabelWarning),

    /// Warning for long string missing values.
    #[error("In long string missing values record: {0}")]
    LongStringMissingValues(#[from] LongStringMissingValuesWarning),

    /// Warning for long string value labels.
    #[error("In long string value label record: {0}")]
    LongStringValueLabel(#[from] LongStringValueLabelWarning),

    /// Warning for long variable names.
    #[error("In long variable name record: {0}")]
    LongName(#[from] LongNameWarning),

    /// Warning for very long strings.
    #[error("In very long string record: {0}")]
    VeryLongString(#[from] VeryLongStringWarning),

    /// Warning for multiple response record.
    #[error("In multiple response set record: {0}")]
    MultipleResponse(#[from] MultipleResponseWarning),

    /// Warning for attribute record.
    #[error("In file or variable attribute record: {0}")]
    Attribute(#[from] AttributeWarning),

    /// Warning for variable display record.
    #[error("In variable display record: {0}")]
    VariableDisplay(#[from] VariableDisplayWarning),

    /// Warning for variable set record.
    #[error("In variable set record: {0}")]
    VariableSet(#[from] VariableSetWarning),

    /// Warning for ZLIB trailer.
    #[error("In ZLIB trailer: {0}")]
    ZlibTrailer(#[from] ZlibTrailerWarning),

    /// Bad encoding name.
    #[error("Encoding record contains an encoding name that is not valid UTF-8.")]
    BadEncodingName,

    /// Mis-encoded bytes in string.
    // XXX This is risky because `text` might be arbitarily long.
    #[error("Text string contains invalid bytes for {encoding} encoding: {text:?}")]
    MalformedString {
        /// The encoding.
        encoding: String,
        /// The problematic string.
        text: String,
    },

    /// Encoding error.
    #[error(transparent)]
    EncodingError(#[from] EncodingError),
}

impl From<IoError> for WarningDetails {
    fn from(_source: IoError) -> Self {
        Self::Extension(ExtensionWarning::UnexpectedEndOfData)
    }
}

/// A raw record in a system file.
#[derive(Clone, Debug, Serialize)]
pub enum Record {
    /// Variable record.
    ///
    /// Each numeric variable has one variable record.  Each string variable has
    /// one variable record per 8-byte segment.
    Variable(
        /// The record.
        VariableRecord<ByteString>,
    ),

    /// Value labels for numeric and short string variables.
    ///
    /// These appear after the variable records.
    ValueLabel(
        /// The record.
        ValueLabelRecord<RawDatum, ByteString>,
    ),

    /// Document record.
    Document(
        /// The record.
        DocumentRecord<RawDocumentLine>,
    ),

    /// Integer info record.
    IntegerInfo(
        /// The record.
        IntegerInfoRecord,
    ),

    /// Floating-point info record.
    FloatInfo(
        /// The record.
        FloatInfoRecord,
    ),

    /// Variable display record.
    VarDisplay(
        /// The record.
        VarDisplayRecord,
    ),

    /// Multiple response variable record.
    MultipleResponse(
        /// The record.
        MultipleResponseRecord<ByteString, ByteString>,
    ),

    /// Value labels for long string variables.
    LongStringValueLabels(
        /// The record.
        LongStringValueLabelRecord<ByteString, ByteString>,
    ),

    /// Missing values for long string variables.
    ///
    /// Missing values for numeric and short string variables appear in the
    /// variable records.
    LongStringMissingValues(
        /// The record.
        LongStringMissingValueRecord<ByteString>,
    ),

    /// Encoding record.
    ///
    /// All the strings in the file are encoded in this encoding, even for
    /// strings that precede this record.
    Encoding(
        /// The record.
        EncodingRecord,
    ),

    /// Extended number of cases.
    ///
    /// The header record records the number of cases but it only uses a 32-bit
    /// field.
    NumberOfCases(
        /// The record.
        NumberOfCasesRecord,
    ),

    /// Variable sets.
    VariableSets(
        /// The record.
        RawVariableSetRecord,
    ),

    /// Product info.
    ///
    /// This supplements the product in the header record.
    ProductInfo(
        /// The record.
        RawProductInfoRecord,
    ),

    /// Long variable names.
    LongNames(
        /// The record.
        RawLongNamesRecord,
    ),

    /// Very long string variables, for strings longer than 255 bytes.
    VeryLongStrings(
        /// The record.
        RawVeryLongStringsRecord,
    ),

    /// File attributes.
    FileAttributes(
        /// The record.
        RawFileAttributesRecord,
    ),

    /// Variable attributes.
    VariableAttributes(
        /// The record.
        RawVariableAttributesRecord,
    ),

    /// Extension records not otherwise supported.
    OtherExtension(
        /// The record.
        Extension,
    ),

    /// End of headers.
    EndOfHeaders(
        /// The record.
        u32,
    ),

    /// Header record for ZLIB-compressed data.
    ZHeader(
        /// The record.
        ZHeader,
    ),

    /// Trailer record for ZLIB-compressed data.
    ZTrailer(
        /// The record.
        ZTrailer,
    ),
}

impl Record {
    /// Returns the inner [EncodingRecord], if any.
    pub fn as_encoding_record(&self) -> Option<&EncodingRecord> {
        match self {
            Record::Encoding(encoding_record) => Some(encoding_record),
            _ => None,
        }
    }

    /// Returns the inner [IntegerInfoRecord], if any.
    pub fn as_integer_info_record(&self) -> Option<&IntegerInfoRecord> {
        match self {
            Record::IntegerInfo(integer_info_record) => Some(integer_info_record),
            _ => None,
        }
    }

    /// Returns the inner [LongStringMissingValueRecord], if any.
    pub fn as_long_string_missing_values(
        &self,
    ) -> Option<&LongStringMissingValueRecord<ByteString>> {
        match self {
            Record::LongStringMissingValues(long_string_missing_value_record) => {
                Some(long_string_missing_value_record)
            }
            _ => None,
        }
    }

    /// Returns [RecordString]s for the record.  These are useful for producing
    /// an [EncodingReport].
    pub fn get_strings(&self) -> Vec<RecordString> {
        let mut strings = Vec::new();
        match self {
            Record::Variable(variable_record) => {
                strings.push(RecordString::new(
                    "Variable Name",
                    &variable_record.name,
                    true,
                ));
                if let Some(label) = &variable_record.label {
                    strings.push(RecordString::new("Variable Label", label, false));
                }
                for missing_value in &variable_record.missing_values.values {
                    if let Some(string) = missing_value.as_string() {
                        strings.push(RecordString::new("Missing Value", string, false));
                    }
                }
            }
            Record::ValueLabel(value_label_record) => {
                for label in &value_label_record.labels {
                    strings.push(RecordString::new("Value Label", &label.label, false));
                }
            }
            Record::Document(document_record) => {
                for (line, index) in document_record.lines.iter().zip(1..) {
                    strings.push(RecordString::new(
                        format!("Document Line {index}"),
                        line,
                        false,
                    ));
                }
            }
            Record::MultipleResponse(multiple_response_record) => {
                for set in &multiple_response_record.sets {
                    strings.push(RecordString::new(
                        "Multiple Response Set Name",
                        &set.name,
                        true,
                    ));
                    if !set.label.is_empty() {
                        strings.push(RecordString::new(
                            "Multiple Response Set Label",
                            &set.label,
                            false,
                        ));
                    }
                    match &set.mr_type {
                        records::MultipleResponseType::MultipleDichotomy { value, .. } => {
                            strings.push(RecordString::new(
                                "Multiple Response Set Counted Value",
                                value,
                                false,
                            ));
                        }
                        _ => (),
                    }
                }
            }
            Record::LongStringValueLabels(long_string_value_label_record) => {
                for labels in &long_string_value_label_record.labels {
                    for (_value, label) in &labels.labels {
                        strings.push(RecordString::new("Value Label", label, false));
                    }
                }
            }
            Record::ProductInfo(raw_product_info_record) => {
                strings.push(RecordString::new(
                    "Extra Product Info",
                    &raw_product_info_record.0.text,
                    false,
                ));
            }
            Record::IntegerInfo(_)
            | Record::FloatInfo(_)
            | Record::VarDisplay(_)
            | Record::LongStringMissingValues(_)
            | Record::Encoding(_)
            | Record::NumberOfCases(_)
            | Record::VariableSets(_)
            | Record::LongNames(_)
            | Record::VeryLongStrings(_)
            | Record::FileAttributes(_)
            | Record::VariableAttributes(_)
            | Record::OtherExtension(_)
            | Record::EndOfHeaders(_)
            | Record::ZHeader(_)
            | Record::ZTrailer(_) => (),
        }
        strings
    }
}

/// A [Record] that has been decoded to a more usable form.
///
/// Some records can be understand raw, but others need to have strings decoded
/// (and interpreted as identifiers) or raw data interpreted as either numbers
/// or strings.
#[derive(Clone, Debug, Serialize)]
pub enum DecodedRecord {
    /// Variable record, with strings decoded.
    Variable(VariableRecord<String>),

    /// Value label, with strings decoded.
    ValueLabel(ValueLabelRecord<RawDatum, String>),

    /// Documents, with strings decoded.
    Document(DocumentRecord<String>),

    /// Integer info.
    IntegerInfo(IntegerInfoRecord),

    /// Floating-point info.
    FloatInfo(FloatInfoRecord),

    /// Variable display info.
    VarDisplay(VarDisplayRecord),

    /// Multiple response sets, with strings decoded.
    MultipleResponse(MultipleResponseRecord<Identifier, String>),

    /// Long string value labels, with strings decoded.
    LongStringValueLabels(LongStringValueLabelRecord<Identifier, String>),

    /// Long string missing values, with strings decoded.
    LongStringMissingValues(LongStringMissingValueRecord<Identifier>),

    /// Encoding record.
    Encoding(EncodingRecord),

    /// Number of cases record.
    NumberOfCases(NumberOfCasesRecord),

    /// Variable sets.
    VariableSets(VariableSetRecord),

    /// Product info.
    ProductInfo(ProductInfoRecord),

    /// Long variable names.
    LongNames(LongNamesRecord),

    /// Very long string variables.
    VeryLongStrings(VeryLongStringsRecord),

    /// File attributes.
    FileAttributes(FileAttributesRecord),

    /// Variable attributes.
    VariableAttributes(VariableAttributesRecord),

    /// Extension records not otherwise supported.
    OtherExtension(Extension),

    /// End of headers.
    EndOfHeaders(u32),

    /// Header record for ZLIB-compressed data.
    ZHeader(ZHeader),

    /// Trailer record for ZLIB-compressed data.
    ZTrailer(ZTrailer),
}

impl Record {
    fn read<R>(
        reader: &mut R,
        endian: Endian,
        var_types: &VarTypes,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<Record>, Error>
    where
        R: BufRead + Seek,
    {
        let rec_type: u32 = endian.parse(read_bytes(reader)?);
        match rec_type {
            2 => Ok(Some(Record::Variable(VariableRecord::read(
                reader, endian, warn,
            )?))),
            3 => Ok(
                ValueLabelRecord::read(reader, endian, var_types, warn)?.map(Record::ValueLabel)
            ),
            6 => Ok(Some(Record::Document(DocumentRecord::read(
                reader, endian,
            )?))),
            7 => Extension::read(reader, endian, var_types, warn),
            999 => Ok(Some(Record::EndOfHeaders(
                endian.parse(read_bytes(reader)?),
            ))),
            _ => Err(Error::new(
                {
                    let offset = reader.stream_position()?;
                    Some(offset - 4..offset)
                },
                ErrorDetails::BadRecordType(rec_type),
            )),
        }
    }

    /// Decodes this record into a [DecodedRecord] using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> DecodedRecord {
        match self {
            Record::Variable(record) => DecodedRecord::Variable(record.decode(decoder)),
            Record::ValueLabel(record) => DecodedRecord::ValueLabel(record.decode(decoder)),
            Record::Document(record) => DecodedRecord::Document(record.decode(decoder)),
            Record::IntegerInfo(record) => DecodedRecord::IntegerInfo(record.clone()),
            Record::FloatInfo(record) => DecodedRecord::FloatInfo(record.clone()),
            Record::VarDisplay(record) => DecodedRecord::VarDisplay(record.clone()),
            Record::MultipleResponse(record) => {
                DecodedRecord::MultipleResponse(record.decode(decoder))
            }
            Record::LongStringValueLabels(record) => {
                DecodedRecord::LongStringValueLabels(record.decode(decoder))
            }
            Record::LongStringMissingValues(record) => {
                DecodedRecord::LongStringMissingValues(record.decode(decoder))
            }
            Record::Encoding(record) => DecodedRecord::Encoding(record.clone()),
            Record::NumberOfCases(record) => DecodedRecord::NumberOfCases(record.clone()),
            Record::VariableSets(record) => DecodedRecord::VariableSets(record.decode(decoder)),
            Record::ProductInfo(record) => DecodedRecord::ProductInfo(record.decode(decoder)),
            Record::LongNames(record) => DecodedRecord::LongNames(record.decode(decoder)),
            Record::VeryLongStrings(record) => {
                DecodedRecord::VeryLongStrings(record.decode(decoder))
            }
            Record::FileAttributes(record) => DecodedRecord::FileAttributes(record.decode(decoder)),
            Record::VariableAttributes(record) => {
                DecodedRecord::VariableAttributes(record.decode(decoder))
            }
            Record::OtherExtension(record) => DecodedRecord::OtherExtension(record.clone()),
            Record::EndOfHeaders(record) => DecodedRecord::EndOfHeaders(record),
            Record::ZHeader(record) => DecodedRecord::ZHeader(record.clone()),
            Record::ZTrailer(record) => DecodedRecord::ZTrailer(record.clone()),
        }
    }
}

/// Given the raw `records` read from a system file, this tries to figure out
/// the intended character encoding used in the system file:
///
/// - If there is a character encoding record, it uses that encoding.
///
/// - If there is an integer info record, it uses that encoding.
///
/// - Otherwise, it falls back to a default encoding and issuses a warning with
///   `warn`.
///
/// If the records specify an EBCDIC encoding, this fails with an error because
/// PSPP only supports ASCII-based encodings.
pub fn infer_encoding(
    records: &[Record],
    mut warn: impl FnMut(Warning),
) -> Result<&'static Encoding, Error> {
    let (encoding, character_code) = get_encoding_info(records);
    match get_encoding(encoding, character_code) {
        Ok(encoding) => Ok(encoding),
        Err(err @ EncodingError::Ebcdic) => Err(Error::new(None, err.into())),
        Err(err) => {
            warn(Warning::new(None, err));
            // Warn that we're using the default encoding.
            Ok(default_encoding())
        }
    }
}

/// Returns the encoding name from the (first) [EncodingRecord] in `records`, if
/// any, and the codepage from the (first) [IntegerInfoRecord] in `records`, if
/// any.
pub fn get_encoding_info(records: &[Record]) -> (Option<&str>, Option<i32>) {
    (
        get_encoding_record(records).map(|r| r.0.as_str()),
        get_integer_info_record(records).map(|r| r.inner.character_code),
    )
}

/// Returns the (first) [EncodingRecord] in `iter`, if any.
pub fn get_encoding_record<'a, I>(iter: I) -> Option<&'a EncodingRecord>
where
    I: IntoIterator<Item = &'a Record>,
{
    iter.into_iter()
        .filter_map(|record| record.as_encoding_record())
        .next()
}

/// Returns the (first) [IntegerInfoRecord] in `iter`, if any.
pub fn get_integer_info_record<'a, I>(iter: I) -> Option<&'a IntegerInfoRecord>
where
    I: IntoIterator<Item = &'a Record>,
{
    iter.into_iter()
        .filter_map(|record| record.as_integer_info_record())
        .next()
}

/// An [Encoding] along with a function to report decoding errors.
///
/// This is used by functions that decode raw records.
pub struct Decoder<'a> {
    /// The character encoding to use.
    pub encoding: &'static Encoding,

    /// Used to reporting [Warning]s during decoding.
    pub warn: Box<dyn FnMut(Warning) + 'a>,
}

impl<'de> Decoder<'de> {
    /// Constructs a decoder for an encoding read or inferred from `records`
    /// (using [infer_encoding]).  This can fail if the headers specify an
    /// EBCDIC encoding, since this crate only supports ASCII-based encodings.
    ///
    /// `warn` will be used to report warnings while decoding records.
    pub fn with_inferred_encoding<F>(records: &[Record], mut warn: F) -> Result<Self, Error>
    where
        F: FnMut(Warning) + 'de,
    {
        let encoding = infer_encoding(records, &mut warn)?;
        Ok(Self::new(encoding, warn))
    }

    /// Construct a decoder using `encoding`.
    ///
    /// `warn` will be used to report warnings while decoding records.
    pub fn new<F>(encoding: &'static Encoding, warn: F) -> Self
    where
        F: FnMut(Warning) + 'de,
    {
        Self {
            encoding,
            warn: Box::new(warn),
        }
    }

    /// Drops this decoder, returning its encoding.
    pub fn into_encoding(self) -> &'static Encoding {
        self.encoding
    }

    fn warn(&mut self, warning: Warning) {
        (self.warn)(warning)
    }

    fn decode_slice<'a>(&mut self, input: &'a [u8]) -> Cow<'a, str> {
        let (output, malformed) = self.encoding.decode_without_bom_handling(input);
        if malformed {
            self.warn(Warning::new(
                None,
                WarningDetails::MalformedString {
                    encoding: self.encoding.name().into(),
                    text: output.clone().into(),
                },
            ));
        }
        output
    }

    fn decode<'a>(&mut self, input: &'a ByteString) -> Cow<'a, str> {
        self.decode_slice(input.0.as_slice())
    }

    /// Decodes `input` to an [Identifier] using our encoding.
    pub fn decode_identifier(&mut self, input: &ByteString) -> Result<Identifier, IdError> {
        let decoded = &self.decode(input);
        self.new_identifier(decoded)
    }

    /// Constructs an [Identifier] from `name` using our encoding.
    pub fn new_identifier(&self, name: &str) -> Result<Identifier, IdError> {
        Identifier::from_encoding(name, self.encoding)
    }
}

/// System file type, inferred from its "magic number".
///
/// The magic number is the first four bytes of the file.
#[derive(Copy, Clone, PartialEq, Eq, Hash, Serialize)]
pub enum Magic {
    /// Regular system file.
    Sav,

    /// System file with Zlib-compressed data.
    Zsav,

    /// EBCDIC-encoded system file.
    Ebcdic,
}

impl Magic {
    /// Magic number for a regular system file.
    pub const SAV: [u8; 4] = *b"$FL2";

    /// Magic number for a system file that contains zlib-compressed data.
    pub const ZSAV: [u8; 4] = *b"$FL3";

    /// Magic number for an EBCDIC-encoded system file.  This is `$FL2` encoded
    /// in EBCDIC.
    pub const EBCDIC: [u8; 4] = [0x5b, 0xc6, 0xd3, 0xf2];
}

impl Debug for Magic {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        let s = match *self {
            Magic::Sav => "$FL2",
            Magic::Zsav => "$FL3",
            Magic::Ebcdic => "($FL2 in EBCDIC)",
        };
        write!(f, "{s}")
    }
}

impl From<Magic> for [u8; 4] {
    fn from(value: Magic) -> Self {
        match value {
            Magic::Sav => Magic::SAV,
            Magic::Zsav => Magic::ZSAV,
            Magic::Ebcdic => Magic::EBCDIC,
        }
    }
}

impl TryFrom<[u8; 4]> for Magic {
    type Error = ErrorDetails;

    fn try_from(value: [u8; 4]) -> Result<Self, Self::Error> {
        match value {
            Magic::SAV => Ok(Magic::Sav),
            Magic::ZSAV => Ok(Magic::Zsav),
            Magic::EBCDIC => Ok(Magic::Ebcdic),
            _ => Err(ErrorDetails::BadMagic(value)),
        }
    }
}

impl TryFrom<RawWidth> for VarType {
    type Error = ();

    fn try_from(value: RawWidth) -> Result<Self, Self::Error> {
        match value {
            RawWidth::Continuation => Err(()),
            RawWidth::Numeric => Ok(VarType::Numeric),
            RawWidth::String(_) => Ok(VarType::String),
        }
    }
}

impl TryFrom<RawWidth> for VarWidth {
    type Error = ();

    fn try_from(value: RawWidth) -> Result<Self, Self::Error> {
        match value {
            RawWidth::Continuation => Err(()),
            RawWidth::Numeric => Ok(Self::Numeric),
            RawWidth::String(width) => Ok(Self::String(width.get() as u16)),
        }
    }
}

/// An 8-byte [Datum] but we don't know the string width or character encoding.
#[derive(Copy, Clone)]
pub enum RawDatum {
    /// Number.
    Number(
        /// Numeric value.
        ///
        /// `None` represents the system-missing value.
        Option<f64>,
    ),
    /// String.
    String(
        // String value.
        //
        // The true string width and character encoding are unknown.
        [u8; 8],
    ),
}

impl Debug for RawDatum {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        match self {
            RawDatum::Number(Some(number)) => write!(f, "{number:?}"),
            RawDatum::Number(None) => write!(f, "SYSMIS"),
            RawDatum::String(s) => write!(f, "{:?}", ByteStr(s)),
        }
    }
}

impl Serialize for RawDatum {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        match self {
            RawDatum::Number(number) => number.serialize(serializer),
            RawDatum::String(s) => ByteStr(s).serialize(serializer),
        }
    }
}

impl RawDatum {
    /// Constructs a `RawDatum` from `raw` given that we now know the variable
    /// type and endianness.
    pub fn from_raw(raw: &UntypedDatum, var_type: VarType, endian: Endian) -> Self {
        match var_type {
            VarType::String => RawDatum::String(raw.0),
            VarType::Numeric => RawDatum::Number(endian.parse(raw.0)),
        }
    }

    /// Decodes a `RawDatum` into a [Datum] given that we now know the string
    /// width.
    pub fn decode(&self, width: VarWidth) -> Datum<ByteString> {
        match self {
            Self::Number(x) => Datum::Number(*x),
            Self::String(s) => {
                let width = width.as_string_width().unwrap();
                Datum::String(ByteString::from(&s[..width]))
            }
        }
    }
}

impl Datum<ByteString> {
    fn read_case<R: Read + Seek>(
        reader: &mut R,
        case_number: u64,
        case_vars: &[CaseVar],
        endian: Endian,
    ) -> Result<Option<RawCase>, Error> {
        fn eof<R: Seek>(
            reader: &mut R,
            case_number: u64,
            case_vars: &[CaseVar],
            case_start: u64,
        ) -> Result<Option<RawCase>, Error> {
            let offset = reader.stream_position()?;
            if offset == case_start {
                Ok(None)
            } else {
                Err(Error::new(
                    Some(case_start..offset),
                    ErrorDetails::EofInCase {
                        case_ofs: offset - case_start,
                        case_len: case_vars.iter().map(CaseVar::bytes).sum(),
                        case_number,
                    },
                ))
            }
        }

        let case_start = reader.stream_position()?;
        let mut values = Vec::with_capacity(case_vars.len());
        for var in case_vars {
            match var {
                CaseVar::Numeric => {
                    let Some(raw) = try_read_bytes(reader)? else {
                        return eof(reader, case_number, case_vars, case_start);
                    };
                    values.push(Datum::Number(endian.parse(raw)));
                }
                CaseVar::String { width, encoding } => {
                    let mut datum = vec![0; *width];
                    let mut offset = 0;
                    for segment in encoding {
                        if !try_read_bytes_into(
                            reader,
                            &mut datum[offset..offset + segment.data_bytes],
                        )? {
                            return eof(reader, case_number, case_vars, case_start);
                        }
                        skip_bytes(reader, segment.padding_bytes)?;
                        offset += segment.data_bytes;
                    }
                    values.push(Datum::String(datum.into()));
                }
            }
        }
        Ok(Some(RawCase(values)))
    }

    fn read_compressed_chunk<R: Read>(
        reader: &mut R,
        codes: &mut VecDeque<u8>,
        endian: Endian,
        bias: f64,
    ) -> Result<Option<[u8; 8]>, Error> {
        loop {
            match codes.pop_front() {
                Some(0) => (),
                Some(252) => return Ok(None),
                Some(253) => return Ok(Some(read_bytes(reader)?)),
                Some(254) => return Ok(Some([b' '; 8])),
                Some(255) => return Ok(Some(endian.to_bytes(-f64::MAX))),
                Some(code) => return Ok(Some(endian.to_bytes(code as f64 - bias))),
                None => {
                    match try_read_bytes::<8, _>(reader)? {
                        Some(new_codes) => codes.extend(new_codes.into_iter()),
                        None => return Ok(None),
                    };
                }
            };
        }
    }
    fn read_compressed_case<R: Read + Seek>(
        reader: &mut R,
        case_number: u64,
        case_vars: &[CaseVar],
        codes: &mut VecDeque<u8>,
        endian: Endian,
        bias: f64,
    ) -> Result<Option<RawCase>, Error> {
        fn eof<R: Seek>(
            reader: &mut R,
            case_number: u64,
            case_start: u64,
            n_chunks: usize,
        ) -> Result<Option<RawCase>, Error> {
            let offset = reader.stream_position()?;
            if n_chunks > 0 {
                Err(Error::new(
                    Some(case_start..offset),
                    ErrorDetails::EofInCompressedCase {
                        case_ofs: offset - case_start,
                        n_chunks,
                        case_number,
                    },
                ))
            } else {
                Ok(None)
            }
        }

        let case_start = reader.stream_position()?;
        let mut n_chunks = 0;
        let mut values = Vec::with_capacity(case_vars.len());
        for var in case_vars {
            match var {
                CaseVar::Numeric => {
                    let Some(raw) = Self::read_compressed_chunk(reader, codes, endian, bias)?
                    else {
                        return eof(reader, case_number, case_start, n_chunks);
                    };
                    n_chunks += 1;
                    values.push(Datum::Number(endian.parse(raw)));
                }
                CaseVar::String { width, encoding } => {
                    let mut datum = Vec::with_capacity(*width);
                    for segment in encoding {
                        let mut data_bytes = segment.data_bytes;
                        let mut padding_bytes = segment.padding_bytes;
                        while data_bytes > 0 || padding_bytes > 0 {
                            let Some(raw) =
                                Self::read_compressed_chunk(reader, codes, endian, bias)?
                            else {
                                return eof(reader, case_number, case_start, n_chunks);
                            };
                            let n_data = data_bytes.min(8);
                            datum.extend_from_slice(&raw[..n_data]);
                            data_bytes -= n_data;
                            padding_bytes -= 8 - n_data;
                            n_chunks += 1;
                        }
                    }
                    values.push(Datum::String(datum.into()));
                }
            }
        }
        Ok(Some(RawCase(values)))
    }
}

struct ZlibDecodeMultiple<R>
where
    R: BufRead + Seek,
{
    reader: Option<ZlibDecoder<R>>,
    limit: u64,
}

impl<R> ZlibDecodeMultiple<R>
where
    R: BufRead + Seek,
{
    fn new(reader: R, limit: u64) -> ZlibDecodeMultiple<R> {
        ZlibDecodeMultiple {
            reader: Some(ZlibDecoder::new(reader)),
            limit,
        }
    }
}

impl<R> Read for ZlibDecodeMultiple<R>
where
    R: BufRead + Seek,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, IoError> {
        loop {
            match self.reader.as_mut().unwrap().read(buf) {
                Err(error) => return Err(error),
                Ok(0) => {
                    let mut inner = self.reader.take().unwrap().into_inner();
                    let position = inner.stream_position();
                    self.reader = Some(ZlibDecoder::new(inner));
                    if position? >= self.limit {
                        return Ok(0);
                    }
                }
                Ok(n) => return Ok(n),
            };
        }
    }
}

impl<R> Seek for ZlibDecodeMultiple<R>
where
    R: BufRead + Seek,
{
    fn seek(&mut self, pos: SeekFrom) -> Result<u64, IoError> {
        self.reader.as_mut().unwrap().get_mut().seek(pos)
    }
}

enum ReaderState {
    Headers,
    ZlibHeader,
    ZlibTrailer(ZHeader),
    End,
}

/// Reads records from a system file in their raw form.
pub struct Reader<'a, R>
where
    R: BufRead + Seek + 'static,
{
    reader: Option<R>,
    warn: Box<dyn FnMut(Warning) + 'a>,

    header: FileHeader<ByteString>,
    var_types: VarTypes,

    state: ReaderState,
    cases: Option<RawCases>,
}

impl<'a, R> Reader<'a, R>
where
    R: BufRead + Seek + 'static,
{
    /// Constructs a new [Reader] from the underlying `reader`.  Any warnings
    /// encountered while reading the system file will be reported with `warn`.
    ///
    /// To read an encrypted system file, wrap `reader` in
    /// [EncryptedReader](crate::crypto::EncryptedReader).
    pub fn new(mut reader: R, mut warn: impl FnMut(Warning) + 'a) -> Result<Self, Error> {
        let header = FileHeader::read(&mut reader, &mut warn)?;
        Ok(Self {
            reader: Some(reader),
            warn: Box::new(warn),
            header,
            var_types: VarTypes::new(),
            state: ReaderState::Headers,
            cases: None,
        })
    }

    /// Returns the header in this reader.
    pub fn header(&self) -> &FileHeader<ByteString> {
        &self.header
    }

    /// Returns a structure for reading the system file's records.
    pub fn records<'b>(&'b mut self) -> Records<'a, 'b, R> {
        Records(self)
    }

    /// Returns a structure for reading the system file's cases.
    ///
    /// The cases are only available once all the headers have been read.  If
    /// there is an error reading the headers, or if [cases](Self::cases) is
    /// called before all of the headers have been read, the returned [RawCases]
    /// will be empty.
    pub fn cases(self) -> RawCases {
        self.cases.unwrap_or_default()
    }
}

/// Reads raw records from a system file.
pub struct Records<'a, 'b, R>(&'b mut Reader<'a, R>)
where
    R: BufRead + Seek + 'static;

impl<'a, 'b, R> Records<'a, 'b, R>
where
    R: BufRead + Seek + 'static,
{
    fn cases(&mut self, ztrailer_offset: Option<u64>) {
        self.0.state = ReaderState::End;
        self.0.cases = Some(RawCases::new(
            self.0.reader.take().unwrap(),
            take(&mut self.0.var_types),
            &self.0.header,
            ztrailer_offset,
        ));
    }

    fn next_inner(&mut self) -> Option<<Self as Iterator>::Item> {
        match self.0.state {
            ReaderState::Headers => {
                let record = loop {
                    match Record::read(
                        self.0.reader.as_mut().unwrap(),
                        self.0.header.endian,
                        &self.0.var_types,
                        &mut self.0.warn,
                    ) {
                        Ok(Some(record)) => break record,
                        Ok(None) => (),
                        Err(error) => return Some(Err(error)),
                    }
                };
                match record {
                    Record::Variable(VariableRecord { width, .. }) => {
                        if let Ok(width) = width.try_into() {
                            self.0.var_types.push(width)
                        }
                    }
                    Record::EndOfHeaders(_) => {
                        self.0.state = if let Some(Compression::ZLib) = self.0.header.compression {
                            ReaderState::ZlibHeader
                        } else {
                            self.cases(None);
                            ReaderState::End
                        };
                    }
                    _ => (),
                };
                Some(Ok(record))
            }
            ReaderState::ZlibHeader => {
                let zheader =
                    match ZHeader::read(self.0.reader.as_mut().unwrap(), self.0.header.endian) {
                        Ok(zheader) => zheader,
                        Err(error) => return Some(Err(error)),
                    };
                self.0.state = ReaderState::ZlibTrailer(zheader.clone());
                Some(Ok(Record::ZHeader(zheader)))
            }
            ReaderState::ZlibTrailer(ref zheader) => {
                match ZTrailer::read(
                    self.0.reader.as_mut().unwrap(),
                    self.0.header.endian,
                    self.0.header.bias,
                    &zheader.inner,
                    &mut self.0.warn,
                ) {
                    Ok(None) => {
                        self.cases(Some(zheader.inner.ztrailer_offset));
                        None
                    }
                    Ok(Some(ztrailer)) => {
                        self.cases(Some(zheader.inner.ztrailer_offset));
                        Some(Ok(Record::ZTrailer(ztrailer)))
                    }
                    Err(error) => Some(Err(error)),
                }
            }
            ReaderState::End => None,
        }
    }
}

impl<'a, 'b, R> Iterator for Records<'a, 'b, R>
where
    R: BufRead + Seek + 'static,
{
    type Item = Result<Record, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        self.next_inner().inspect(|retval| {
            if retval.is_err() {
                self.0.state = ReaderState::End;
            }
        })
    }
}

trait ReadSeek: Read + Seek {}
impl<T> ReadSeek for T where T: Read + Seek {}

#[derive(Debug)]
struct StringSegment {
    data_bytes: usize,
    padding_bytes: usize,
}

fn segment_widths(width: usize) -> impl Iterator<Item = usize> {
    let n_segments = if width > 255 { width.div_ceil(252) } else { 1 };
    repeat_n(255, n_segments - 1)
        .chain(if n_segments > 1 {
            std::iter::once(width - (n_segments - 1) * 252)
        } else {
            std::iter::once(width)
        })
        .map(|w| w.next_multiple_of(8))
}

enum CaseVar {
    Numeric,
    String {
        width: usize,
        encoding: SmallVec<[StringSegment; 1]>,
    },
}

impl CaseVar {
    fn new(width: VarWidth) -> Self {
        match width {
            VarWidth::Numeric => Self::Numeric,
            VarWidth::String(width) => {
                let width = width as usize;
                let mut encoding = SmallVec::<[StringSegment; 1]>::new();
                let mut remaining = width;
                for segment in segment_widths(width) {
                    let data_bytes = remaining.min(segment).min(255);
                    let padding_bytes = segment - data_bytes;
                    if data_bytes > 0 {
                        encoding.push(StringSegment {
                            data_bytes,
                            padding_bytes,
                        });
                        remaining -= data_bytes;
                    } else {
                        encoding.last_mut().unwrap().padding_bytes += padding_bytes;
                    }
                }
                CaseVar::String { width, encoding }
            }
        }
    }

    fn bytes(&self) -> usize {
        match self {
            CaseVar::Numeric => 8,
            CaseVar::String { width: _, encoding } => encoding
                .iter()
                .map(|segment| segment.data_bytes + segment.padding_bytes)
                .sum(),
        }
    }
}

/// Reader for cases in a system file.
///
/// - [Reader::cases] returns [RawCases] in which very long string variables (those
///   over 255 bytes wide) are still in their raw format, which means that they
///   are divided into multiple, adjacent string variables, approximately one
///   variable for each 252 bytes.
///
/// - In the [RawCases] in [SystemFile], each [Dictionary] variable corresponds to
///   one [Datum], even for long string variables.
///
/// [Dictionary]: crate::dictionary::Dictionary
/// [SystemFile]: crate::sys::cooked::SystemFile
pub struct RawCases {
    reader: Box<dyn ReadSeek>,
    case_vars: Vec<CaseVar>,
    compression: Option<Compression>,
    bias: f64,
    endian: Endian,
    codes: VecDeque<u8>,
    eof: bool,
    expected_cases: Option<u64>,
    read_cases: u64,
}

impl Debug for RawCases {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "Cases")
    }
}

impl Default for RawCases {
    fn default() -> Self {
        Self {
            reader: Box::new(empty()),
            case_vars: Vec::new(),
            compression: None,
            bias: 100.0,
            endian: Endian::Little,
            codes: VecDeque::new(),
            eof: false,
            expected_cases: None,
            read_cases: 0,
        }
    }
}

impl RawCases {
    fn new<R>(
        reader: R,
        var_types: VarTypes,
        header: &FileHeader<ByteString>,
        ztrailer_offset: Option<u64>,
    ) -> Self
    where
        R: BufRead + Seek + 'static,
    {
        Self {
            reader: if header.compression == Some(Compression::ZLib) {
                Box::new(ZlibDecodeMultiple::new(reader, ztrailer_offset.unwrap()))
            } else {
                Box::new(reader)
            },
            eof: false,
            case_vars: var_types
                .types
                .iter()
                .flatten()
                .copied()
                .map(CaseVar::new)
                .collect::<Vec<_>>(),
            compression: header.compression,
            bias: header.bias,
            endian: header.endian,
            codes: VecDeque::with_capacity(8),
            expected_cases: None,
            read_cases: 0,
        }
    }

    /// Returns this [RawCases] with its notion of variable widths updated from
    /// `widths`.
    ///
    /// [Records::decode](crate::sys::Records::decode) uses this to properly handle
    /// very long string variables (see [RawCases] for details).
    pub fn with_widths(self, widths: impl IntoIterator<Item = VarWidth>) -> Self {
        Self {
            case_vars: widths.into_iter().map(CaseVar::new).collect::<Vec<_>>(),
            ..self
        }
    }

    /// Returns this [RawCases] updated to expect `expected_cases`.  If the actual
    /// number of cases in the file differs, the reader will issue a warning.
    pub fn with_expected_cases(self, expected_cases: u64) -> Self {
        Self {
            expected_cases: Some(expected_cases),
            ..self
        }
    }
}

impl Iterator for RawCases {
    type Item = Result<RawCase, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.eof {
            return None;
        }

        let retval = if self.case_vars.is_empty() {
            None
        } else if self.compression.is_some() {
            Datum::read_compressed_case(
                &mut self.reader,
                self.read_cases + 1,
                &self.case_vars,
                &mut self.codes,
                self.endian,
                self.bias,
            )
            .transpose()
        } else {
            Datum::read_case(
                &mut self.reader,
                self.read_cases + 1,
                &self.case_vars,
                self.endian,
            )
            .transpose()
        };
        match &retval {
            None => {
                self.eof = true;
                if let Some(expected_cases) = self.expected_cases
                    && expected_cases != self.read_cases
                {
                    return Some(Err(Error::new(
                        None,
                        ErrorDetails::WrongNumberOfCases {
                            expected: expected_cases,
                            actual: self.read_cases,
                        },
                    )));
                } else {
                    return None;
                }
            }
            Some(Ok(_)) => {
                self.read_cases += 1;
            }
            Some(Err(_)) => self.eof = true,
        };
        retval
    }
}

/// Width of a variable record.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Serialize)]
pub enum RawWidth {
    /// String continuation.
    ///
    /// One variable record of this type is present for each 8 bytes after
    /// the first 8 bytes of a string variable, as a kind of placeholder.
    Continuation,

    /// Numeric.
    Numeric,

    /// String.
    String(NonZeroU8),
}

impl RawWidth {
    /// Returns the number of value positions corresponding to a variable with
    /// this type.
    pub fn n_values(&self) -> Option<usize> {
        match self {
            RawWidth::Numeric => Some(1),
            RawWidth::String(width) => Some((width.get() as usize).div_ceil(8)),
            RawWidth::Continuation => None,
        }
    }
}

impl TryFrom<i32> for RawWidth {
    type Error = ();

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            -1 => Ok(Self::Continuation),
            0 => Ok(Self::Numeric),
            1..=255 => Ok(Self::String(NonZeroU8::new(value as u8).unwrap())),
            _ => Err(()),
        }
    }
}

impl Display for RawWidth {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self {
            RawWidth::Continuation => write!(f, "long string continuation"),
            RawWidth::Numeric => write!(f, "numeric"),
            RawWidth::String(width) => write!(f, "{width}-byte string"),
        }
    }
}

/// 8 bytes that represent a number or a string (but that's all we know).
///
/// Used when we don't know whether it's a number or a string, or the numerical
/// endianness, or the string width, or the character encoding.  Really all we
/// know is that it's 8 bytes that mean something.
#[derive(Copy, Clone)]
pub struct UntypedDatum(pub [u8; 8]);

impl Debug for UntypedDatum {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        let little: f64 = Endian::Little.parse(self.0);
        let little = format!("{:?}", little);
        let big: f64 = Endian::Big.parse(self.0);
        let big = format!("{:?}", big);
        let number = if little.len() <= big.len() {
            little
        } else {
            big
        };
        write!(f, "{number}/{:?}", ByteStr(&self.0))
    }
}

fn skip_bytes<R: Read>(r: &mut R, mut n: usize) -> Result<(), IoError> {
    thread_local! {
        static BUF: RefCell<[u8; 256]> = RefCell::new([0u8; 256]);
    }
    BUF.with_borrow_mut(|buf| {
        while n > 0 {
            let chunk = n.min(buf.len());
            r.read_exact(&mut buf[..n])?;
            n -= chunk;
        }
        Ok(())
    })
}

fn try_read_bytes_into<R: Read>(r: &mut R, buf: &mut [u8]) -> Result<bool, IoError> {
    let n = r.read(buf)?;
    if n > 0 {
        if n < buf.len() {
            r.read_exact(&mut buf[n..])?;
        }
        Ok(true)
    } else {
        Ok(false)
    }
}

fn try_read_bytes<const N: usize, R: Read>(r: &mut R) -> Result<Option<[u8; N]>, IoError> {
    let mut buf = [0; N];
    match try_read_bytes_into(r, &mut buf)? {
        true => Ok(Some(buf)),
        false => Ok(None),
    }
}

fn read_bytes<const N: usize, R: Read>(r: &mut R) -> Result<[u8; N], IoError> {
    let mut buf = [0; N];
    r.read_exact(&mut buf)?;
    Ok(buf)
}

fn read_vec<R: Read>(r: &mut R, n: usize) -> Result<Vec<u8>, IoError> {
    let mut vec = vec![0; n];
    r.read_exact(&mut vec)?;
    Ok(vec)
}

fn read_string<R: Read>(r: &mut R, endian: Endian) -> Result<ByteString, IoError> {
    let length: u32 = endian.parse(read_bytes(r)?);
    Ok(read_vec(r, length as usize)?.into())
}

/// A collection of [VarWidth]s indexed for lookup.
///
/// A system file contains a series of variables.  Some parts of the system file
/// refer to variables by indexes that take the widths of string variables into
/// account, where a string variable with width 1..=8 occupies one index, with
/// width 9..=16 occupies two, and so on.  For string variables that span more
/// than one index, only references to the first are valid.  [VarTypes]
/// facilitates this kind of lookup.
#[derive(Default)]
pub struct VarTypes {
    types: Vec<Option<VarWidth>>,
}

impl VarTypes {
    /// Construct a new, empty [VarTypes].
    pub fn new() -> Self {
        Self::default()
    }

    /// Appends `width`.
    pub fn push(&mut self, width: VarWidth) {
        self.types.push(Some(width));
        for _ in 1..width.n_chunks().unwrap() {
            self.types.push(None);
        }
    }

    /// Returns the number of indexes spanned by these variables.
    pub fn n_values(&self) -> usize {
        self.types.len()
    }

    /// Returns true if 1-based `index` is valid, that is, if it's in the range
    /// of indexes and refers to the first index for a given variable.
    pub fn is_valid_index(&self, index: usize) -> bool {
        self.var_type_at(index).is_some()
    }

    /// Returns the type of variable with the given 1-based `index`, or `None`
    /// if it's out of range or doesn't refer to the first index for a variable.
    pub fn var_type_at(&self, index: usize) -> Option<VarType> {
        self.types.get(index.checked_sub(1)?)?.map(VarType::from)
    }

    /// Returns the number of variables.
    pub fn n_vars(&self) -> usize {
        self.types.iter().flatten().count()
    }
}

/// A text string with an unknown encoding in a system file.
pub struct RecordString {
    /// The name of the text string, e.g. "Variable 1".
    pub title: String,

    /// The text string.
    pub string: ByteString,

    /// Whether the text string must be a valid identifier.
    ///
    /// This can allow some otherwise valid encodings to be rejected.
    pub is_identifier: bool,
}

impl RecordString {
    /// Constructs a new [RecordString].
    pub fn new(
        title: impl Into<String>,
        string: impl Into<ByteString>,
        is_identifier: bool,
    ) -> Self {
        Self {
            title: title.into(),
            string: string.into(),
            is_identifier,
        }
    }
}

static ENCODINGS: [&Encoding; 32] = [
    UTF_8,
    WINDOWS_1252,
    ISO_8859_2,
    ISO_8859_3,
    ISO_8859_4,
    ISO_8859_5,
    ISO_8859_6,
    ISO_8859_7,
    ISO_8859_8,
    ISO_8859_10,
    ISO_8859_13,
    ISO_8859_14,
    ISO_8859_16,
    MACINTOSH,
    WINDOWS_874,
    WINDOWS_1250,
    WINDOWS_1251,
    WINDOWS_1253,
    WINDOWS_1254,
    WINDOWS_1255,
    WINDOWS_1256,
    WINDOWS_1257,
    WINDOWS_1258,
    KOI8_R,
    KOI8_U,
    IBM866,
    GB18030,
    BIG5,
    EUC_JP,
    ISO_2022_JP,
    SHIFT_JIS,
    EUC_KR,
];

/// Where the chosen encoding came from.
#[derive(Serialize)]
#[serde(rename_all = "snake_case")]
pub enum EncodingSource {
    /// From the encoding name in the system file.
    Name,
    /// From the code page in the system file.
    Codepage,
    /// Default encoding.
    Default,
}

impl EncodingSource {
    fn as_str(&self) -> &'static str {
        match self {
            EncodingSource::Name => "name",
            EncodingSource::Codepage => "codepage",
            EncodingSource::Default => "default",
        }
    }
}

/// Information about the character encodings in a system file.
///
/// This contains two kinds of data:
///
/// - Information about the encodings indicated by the system file itself and
///   how PSPP interprets it.
///
/// - Information about the text strings in the system file headers and possibly
///   some of its cases, along with the character encodings that are valid for
///   those text strings and how their interpretations differ based on encoding.
#[derive(Serialize)]
pub struct EncodingReport {
    /// If the file includes a record that names its encoding, then this is the
    /// name and how PSPP interprets that as an encoding.
    pub name: Option<(String, Result<&'static Encoding, EncodingError>)>,

    /// If the file includes a record that identifies its encoding as a code
    /// page number, then this is the number and how PSPP interprets that as an
    /// encoding.
    pub codepage: Option<(i32, Result<&'static Encoding, EncodingError>)>,

    /// The overall encoding chosen.
    pub inferred_encoding: Result<&'static Encoding, EncodingError>,

    /// Why the overall encoding was chosen.
    pub inferred_encoding_source: EncodingSource,

    /// The encodings that are valid for this file, based on looking at all the
    /// text data in the file headers and possibly some of its cases.  Each
    /// array element is a group of encodings that yield the same text data.  If
    /// there is only one element, then all valid encodings yield the same text
    /// data.
    pub valid_encodings: Vec<Vec<&'static Encoding>>,

    /// Individual strings in the file headers and cases, together with their
    /// intepretations for each group of valid encodings.  Only strings that
    /// don't have the same interpretation for every valid encoding are
    /// included.
    ///
    /// If this is empty, then either:
    ///
    /// - `valid_encodings` is also empty.  In this case, there are no valid
    ///   encodings, so there are no strings in the valid encodings.
    ///
    /// - `valid_encodings` has one element (one group of valid encodings).  In
    ///   this case, every valid encoding interprets every string the same way.
    pub strings: Vec<EncodingReportString>,
}

impl EncodingReport {
    fn metadata_pivot_table(&self) -> PivotTable {
        fn result_to_value(result: &Result<&'static Encoding, EncodingError>) -> Value {
            match result {
                Ok(encoding) => encoding.name().into(),
                Err(error) => error.to_string().into(),
            }
        }

        let cols = Group::new("Distinctions")
            .with("Value")
            .with("Interpretation");
        let rows = Group::new("Category")
            .with("Name")
            .with("Codepage")
            .with("Overall");
        let mut table = PivotTable::new([
            (Axis3::X, Dimension::new(cols)),
            (Axis3::Y, Dimension::new(rows)),
        ])
        .with_title("Character encoding information found in system file and its interpretation")
        .with_caption("A system file may identify its character encoding by name or by codepage number or both.  This table states which were found, how each was interpreted, and the overall interpretation.");
        if let Some((label, result)) = &self.name {
            table.insert(&[0, 0], label.as_str());
            table.insert(&[1, 0], result_to_value(result));
        } else {
            table.insert(&[0, 0], "(none)");
        }
        if let Some((codepage, result)) = &self.codepage {
            table.insert(&[0, 1], Value::new_integer(Some((*codepage) as f64)));
            table.insert(&[1, 1], result_to_value(result));
        } else {
            table.insert(&[0, 1], "(none)");
        }
        table.insert(&[0, 2], self.inferred_encoding_source.as_str());
        table.insert(&[1, 2], result_to_value(&self.inferred_encoding));
        table
    }
}

impl From<&EncodingReport> for Details {
    fn from(value: &EncodingReport) -> Self {
        let mut output: Vec<Item> = vec![value.metadata_pivot_table().into()];

        if !value.valid_encodings.is_empty() {
            let groups = Group::new("Group").with_label_shown().with_multiple(
                (1..=value.valid_encodings.len()).map(|i| Value::new_integer(Some(i as f64))),
            );
            let encodings = Group::new("Encoding").with_multiple(
                (1..=value
                    .valid_encodings
                    .iter()
                    .map(|encodings| encodings.len())
                    .sum())
                    .map(|i| Value::new_integer(Some(i as f64))),
            );
            let mut data = Vec::new();
            let mut index = 0;
            for (group, encodings) in value.valid_encodings.iter().enumerate() {
                for encoding in encodings {
                    data.push(([group, index], encoding.name().into()));
                    index += 1;
                }
            }
            output.push(
                PivotTable::new([
                    (Axis3::Y, Dimension::new(groups)),
                    (Axis3::Y, Dimension::new(encodings).with_all_labels_hidden()),
                ])
                .with_title("Valid Encodings")
                .with_caption("This table lists all of the encodings that were found to successfully interpret text in the file's header records.  Encodings in the same group interpret all of the text in the file the same way.")
                .with_data(data)
                .into(),
            );

            if !value.strings.is_empty() {
                let purposes = Group::with_capacity("Purpose", value.strings.len())
                    .with_label_shown()
                    .with_multiple(value.strings.iter().map(|rs| &rs.name));
                let number = Group::new("Encoding").with_label_shown().with_multiple(
                    value
                        .valid_encodings
                        .iter()
                        .map(|encodings| encodings[0].name()),
                );
                output.push(
                        PivotTable::new([
                            (Axis3::X, Dimension::new(Group::new("Text").with("Text"))),
                            (Axis3::Y, Dimension::new(number)),
                            (Axis3::Y, Dimension::new(purposes)),
                        ])
                            .with_title("Alternate Encoded Text Strings")
                            .with_caption("Text strings in the file dictionary that the previously listed encodings interpret differently, along with the interpretations.  The listed encodings are the first in each group.")
                            .with_data(value
                    .strings
                    .iter()
                    .enumerate()
                    .map(|(purpose, rs)| {
                        rs.interpretations
                            .iter()
                            .enumerate()
                            .map(move |(encoding, s)| {
                                (
                                    [0, encoding, purpose],
                                    Value::new_user_text(rs.ellipsize(s.as_str())),
                                )
                            })
                    })
                    .flatten()
                    .collect::<Vec<_>>()).into(),
                    );
            }
        } else {
            output.push(Text::new_log("No valid encodings were found.").into());
        };

        output.into_iter().collect()
    }
}

/// All of the (valid) interpretations of a given string in a system file.
#[derive(Serialize)]
pub struct EncodingReportString {
    /// Name for the string, something like "variable name 1".
    name: String,

    /// If the string's interpretations all start with a common prefix, this is
    /// it.  Only whole words are considered to be common.
    common_prefix: String,

    /// All of the interpretations of the string, one per valid encoding, in the
    /// order of [EncodingReport::valid_encodings].
    interpretations: Vec<String>,

    /// If the string's interpretations all end with a common suffix, this is
    /// it.  Only whole words are considered to be common.
    common_suffix: String,
}

impl EncodingReportString {
    fn ellipsize<'a>(&self, s: &'a str) -> Cow<'a, str> {
        if self.common_prefix.is_empty() && self.common_suffix.is_empty() {
            Cow::from(s)
        } else {
            let mut result = String::with_capacity(s.len() + 6);
            if !self.common_prefix.is_empty() {
                result.push_str("...");
            }
            result.push_str(s);
            if !self.common_suffix.is_empty() {
                result.push_str("...");
            }
            Cow::from(result)
        }
    }
}

impl EncodingReport {
    /// Constructs an encoding report from `reader`, reading no more than
    /// `max_cases` from it.
    pub fn new<R>(mut reader: Reader<R>, max_cases: u64) -> Result<Self, Error>
    where
        R: BufRead + Seek + 'static,
    {
        fn inner(
            header: FileHeader<ByteString>,
            records: &[Record],
            cases: impl Iterator<Item = Result<RawCase, Error>>,
        ) -> Result<EncodingReport, Error> {
            let (encoding, codepage) = get_encoding_info(&records);
            let label = encoding
                .map(|encoding| (String::from(encoding), get_encoding(Some(encoding), None)));
            let codepage = codepage.map(|codepage| (codepage, get_encoding(None, Some(codepage))));
            let (inferred_encoding_source, inferred_encoding) = match label
                .as_ref()
                .map(|(_string, result)| (EncodingSource::Name, result.clone()))
                .or(codepage
                    .as_ref()
                    .map(|(_codepage, result)| (EncodingSource::Codepage, result.clone())))
            {
                Some((source, Ok(encoding))) => (source, Ok(encoding)),
                Some((source, Err(EncodingError::Ebcdic))) => (source, Err(EncodingError::Ebcdic)),
                _ => (EncodingSource::Default, Ok(default_encoding())),
            };

            let mut record_strings = header.get_strings();
            for record in records {
                record_strings.append(&mut record.get_strings());
            }
            for (case_number, case) in (1..).zip(cases) {
                for (variable_number, datum) in (1..).zip(case?.0) {
                    if let Some(mut string) = datum.into_string() {
                        string.trim_end();
                        if !string.is_empty() {
                            record_strings.push(RecordString::new(
                                format!("Case {case_number}, Variable {variable_number}"),
                                string,
                                false,
                            ));
                        }
                    }
                }
            }

            let record_strings = record_strings
                .into_iter()
                .unique_by(|rs| rs.string.clone())
                .collect::<Vec<_>>();

            let mut encodings: IndexMap<Vec<String>, Vec<&'static Encoding>> = IndexMap::new();
            for encoding in ENCODINGS {
                fn recode_as(
                    record_strings: &[RecordString],
                    encoding: &'static Encoding,
                ) -> Option<Vec<String>> {
                    let mut output = Vec::with_capacity(record_strings.len());
                    for rs in record_strings {
                        let mut s = encoding
                            .decode_without_bom_handling_and_without_replacement(&rs.string.0)?
                            .into_owned();
                        s.truncate(s.trim_end().len());
                        if rs.is_identifier {
                            Identifier::check_plausible(&s).ok()?;
                        }
                        output.push(s);
                    }
                    Some(output)
                }
                if let Some(strings) = recode_as(&record_strings, encoding) {
                    encodings.entry(strings).or_default().push(encoding);
                }
            }

            let mut strings = Vec::with_capacity(record_strings.len());
            if !encodings.is_empty() {
                for (index, rs) in record_strings.iter().enumerate() {
                    // Skip strings that decode the same way from every encoding.
                    if encodings.keys().map(|strings| &strings[index]).all_equal() {
                        continue;
                    }

                    /// Returns an iterator for the decoded strings for the given
                    /// `index`.
                    fn decoded_index<'a>(
                        encodings: &'a IndexMap<Vec<String>, Vec<&'static Encoding>>,
                        index: usize,
                    ) -> impl Iterator<Item = &'a str> {
                        encodings.keys().map(move |strings| strings[index].as_str())
                    }

                    let common_prefix: String = decoded_index(&encodings, index)
                        .reduce(common_prefix)
                        .unwrap()
                        .trim_end_matches(|c| c != ' ')
                        .into();
                    let common_suffix: String = decoded_index(&encodings, index)
                        .reduce(common_suffix)
                        .unwrap()
                        .trim_start_matches(|c| c != ' ')
                        .into();

                    let interpretations = decoded_index(&encodings, index)
                        .map(|s| s[common_prefix.len()..s.len() - common_suffix.len()].into())
                        .collect();

                    strings.push(EncodingReportString {
                        name: rs.title.clone(),
                        common_prefix,
                        interpretations,
                        common_suffix,
                    });
                }
            }
            Ok(EncodingReport {
                valid_encodings: encodings.values().cloned().collect(),
                strings,
                name: label,
                codepage,
                inferred_encoding,
                inferred_encoding_source,
            })
        }

        let records: Vec<Record> = reader.records().collect::<Result<Vec<_>, _>>()?;
        let header = reader.header().clone();
        inner(header, &records, reader.cases().take(max_cases as usize))
    }
}

fn common_prefix<'a>(a: &'a str, b: &'a str) -> &'a str {
    for elem in a.char_indices().zip_longest(b.char_indices()) {
        match elem {
            EitherOrBoth::Both((offset, a_char), (_, b_char)) => {
                if a_char != b_char {
                    return &a[..offset];
                }
            }
            EitherOrBoth::Left((offset, _)) | EitherOrBoth::Right((offset, _)) => {
                return &a[..offset]
            }
        }
    }
    a
}

fn common_suffix<'a>(a: &'a str, b: &'a str) -> &'a str {
    for elem in a.char_indices().rev().zip_longest(b.char_indices().rev()) {
        match elem {
            EitherOrBoth::Both((offset, a_char), (_, b_char)) => {
                if a_char != b_char {
                    return &a[offset + a_char.len_utf8()..];
                }
            }
            EitherOrBoth::Left((offset, char)) => {
                return &a[offset + char.len_utf8()..];
            }
            EitherOrBoth::Right((offset, char)) => {
                return &b[offset + char.len_utf8()..];
            }
        }
    }
    a
}

#[cfg(test)]
mod tests {
    use itertools::Itertools;

    use crate::sys::raw::{common_prefix, common_suffix, segment_widths};

    #[test]
    fn test_common_prefix() {
        assert_eq!(common_prefix("abc", "abcxyzzy"), "abc");
        assert_eq!(common_prefix("abcxyzzy", "abc"), "abc");
        assert_eq!(common_prefix("abc", "abc"), "abc");
        assert_eq!(common_prefix("", ""), "");
    }

    #[test]
    fn test_common_suffix() {
        assert_eq!(common_suffix("xyzzyabc", "abc"), "abc");
        assert_eq!(common_suffix("abc", "xyzzyabc"), "abc");
        assert_eq!(common_suffix("abc", "abc"), "abc");
        assert_eq!(common_suffix("", ""), "");
    }

    #[test]
    fn test_segment_widths() {
        // We had a bug for the range 252..=255.
        for i in 1..=255 {
            assert_eq!(segment_widths(i).collect_vec(), vec![i.next_multiple_of(8)]);
        }

        assert_eq!(
            segment_widths(20000).collect_vec(),
            std::iter::repeat_n(256, 79)
                .chain(std::iter::once(96))
                .collect_vec()
        );
    }
}
