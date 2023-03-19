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
    dictionary::{Attributes, Datum, VarWidth},
    endian::{Endian, Parse, ToBytes},
    format::{DisplayPlain, DisplayPlainF64},
    identifier::{Error as IdError, Identifier},
    sys::encoding::{default_encoding, get_encoding, Error as EncodingError},
};

use encoding_rs::{mem::decode_latin1, Encoding};
use flate2::read::ZlibDecoder;
use itertools::Itertools;
use num::Integer;
use smallvec::SmallVec;
use std::{
    borrow::{Borrow, Cow},
    cell::RefCell,
    collections::{BTreeMap, VecDeque},
    fmt::{Debug, Display, Formatter, Result as FmtResult},
    io::{empty, Error as IoError, Read, Seek, SeekFrom},
    iter::repeat_n,
    mem::take,
    num::NonZeroU8,
    ops::{Deref, Not, Range},
    str::from_utf8,
};
use thiserror::Error as ThisError;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("Not an SPSS system file")]
    NotASystemFile,

    #[error("Invalid magic number {0:?}")]
    BadMagic([u8; 4]),

    #[error("I/O error ({0})")]
    Io(#[from] IoError),

    #[error("Invalid SAV compression code {0}")]
    InvalidSavCompression(u32),

    #[error("Invalid ZSAV compression code {0}")]
    InvalidZsavCompression(u32),

    #[error("Document record at offset {offset:#x} has document line count ({n}) greater than the maximum number {max}.")]
    BadDocumentLength { offset: u64, n: usize, max: usize },

    #[error("At offset {offset:#x}, unrecognized record type {rec_type}.")]
    BadRecordType { offset: u64, rec_type: u32 },

    #[error("In variable record starting at offset {start_offset:#x}, variable width is not in the valid range -1 to 255.")]
    BadVariableWidth { start_offset: u64, width: i32 },

    #[error("In variable record starting at offset {start_offset:#x}, variable label code {code} at offset {code_offset:#x} is not 0 or 1.")]
    BadVariableLabelCode {
        start_offset: u64,
        code_offset: u64,
        code: u32,
    },

    #[error("At offset {offset:#x}, missing value code ({code}) is not -3, -2, 0, 1, 2, or 3.")]
    BadMissingValueCode { offset: u64, code: i32 },

    #[error(
        "At offset {offset:#x}, numeric missing value code ({code}) is not -3, -2, 0, 1, 2, or 3."
    )]
    BadNumericMissingValueCode { offset: u64, code: i32 },

    #[error("At offset {offset:#x}, string missing value code ({code}) is not 0, 1, 2, or 3.")]
    BadStringMissingValueCode { offset: u64, code: i32 },

    #[error("At offset {offset:#x}, number of value labels ({n}) is greater than the maximum number {max}.")]
    BadNumberOfValueLabels { offset: u64, n: u32, max: u32 },

    #[error("At offset {offset:#x}, following value label record, found record type {rec_type} instead of expected type 4 for variable index record")]
    ExpectedVarIndexRecord { offset: u64, rec_type: u32 },

    #[error("At offset {offset:#x}, number of variables indexes for value labels ({n}) is greater than the maximum number ({max}).")]
    TooManyVarIndexes { offset: u64, n: u32, max: u32 },

    #[error("At offset {offset:#x}, record type 7 subtype {subtype} is too large with element size {size} and {count} elements.")]
    ExtensionRecordTooLarge {
        offset: u64,
        subtype: u32,
        size: u32,
        count: u32,
    },

    #[error("Unexpected end of file at offset {offset:#x}, {case_ofs} bytes into a {case_len}-byte case.")]
    EofInCase {
        offset: u64,
        case_ofs: u64,
        case_len: usize,
    },

    #[error(
        "Unexpected end of file at offset {offset:#x}, {case_ofs} bytes and {n_chunks} compression chunks into a compressed case."
    )]
    EofInCompressedCase {
        offset: u64,
        case_ofs: u64,
        n_chunks: usize,
    },

    #[error("Data ends at offset {offset:#x}, {case_ofs} bytes into a compressed case.")]
    PartialCompressedCase { offset: u64, case_ofs: u64 },

    #[error("At {case_ofs} bytes into compressed case starting at offset {offset:#x}, a string was found where a number was expected.")]
    CompressedNumberExpected { offset: u64, case_ofs: u64 },

    #[error("At {case_ofs} bytes into compressed case starting at offset {offset:#x}, a number was found where a string was expected.")]
    CompressedStringExpected { offset: u64, case_ofs: u64 },

    #[error("Impossible ztrailer_offset {0:#x}.")]
    ImpossibleZTrailerOffset(u64),

    #[error("ZLIB header's zlib_offset is {actual:#x} instead of expected {expected:#x}.")]
    UnexpectedZHeaderOffset { actual: u64, expected: u64 },

    #[error("Invalid ZLIB trailer length {0}.")]
    InvalidZTrailerLength(u64),

    #[error(
        "ZLIB trailer bias {actual} is not {} as expected from file header bias.",
        DisplayPlainF64(*expected)
    )]
    WrongZlibTrailerBias { actual: i64, expected: f64 },

    #[error("ZLIB trailer \"zero\" field has nonzero value {0}.")]
    WrongZlibTrailerZero(u64),

    #[error("ZLIB trailer specifies unexpected {0}-byte block size.")]
    WrongZlibTrailerBlockSize(u32),

    #[error("Block count {n_blocks} in ZLIB trailer at offset {offset:#x} differs from expected block count {expected_n_blocks} calculated from trailer length {ztrailer_len}.")]
    BadZlibTrailerNBlocks {
        offset: u64,
        n_blocks: u32,
        expected_n_blocks: u64,
        ztrailer_len: u64,
    },

    #[error("ZLIB block descriptor {index} reported uncompressed data offset {actual:#x}, when {expected:#x} was expected.")]
    ZlibTrailerBlockWrongUncmpOfs {
        index: usize,
        actual: u64,
        expected: u64,
    },

    #[error("ZLIB block descriptor {index} reported compressed data offset {actual:#x}, when {expected:#x} was expected.")]
    ZlibTrailerBlockWrongCmpOfs {
        index: usize,
        actual: u64,
        expected: u64,
    },

    #[error("ZLIB block descriptor {index} reports compressed size {compressed_size} and uncompressed size {uncompressed_size}.")]
    ZlibExpansion {
        index: usize,
        compressed_size: u32,
        uncompressed_size: u32,
    },

    #[error("ZLIB trailer is at offset {zheader:#x} but {descriptors:#x} would be expected from block descriptors.")]
    ZlibTrailerOffsetInconsistency { descriptors: u64, zheader: u64 },

    #[error("File metadata says it contains {expected} cases, but {actual} cases were read.")]
    WrongNumberOfCases { expected: u64, actual: u64 },

    #[error("{0}")]
    EncodingError(EncodingError),
}

#[derive(ThisError, Debug)]
pub enum Warning {
    #[error("Unexpected end of data inside extension record.")]
    UnexpectedEndOfData,

    #[error("At offset {offset:#x}, at least one valid variable index for value labels is required but none were specified.")]
    NoVarIndexes { offset: u64 },

    #[error("At offset {offset:#x}, the first variable index is for a {var_type} variable but the following variable indexes are for {} variables: {wrong_types:?}", !var_type)]
    MixedVarTypes {
        offset: u64,
        var_type: VarType,
        wrong_types: Vec<u32>,
    },

    #[error("At offset {offset:#x}, one or more variable indexes for value labels were not in the valid range [1,{max}] or referred to string continuations: {invalid:?}")]
    InvalidVarIndexes {
        offset: u64,
        max: usize,
        invalid: Vec<u32>,
    },

    #[error("At offset {offset:#x}, {record} has bad size {size} bytes instead of the expected {expected_size}.")]
    BadRecordSize {
        offset: u64,
        record: String,
        size: u32,
        expected_size: u32,
    },

    #[error("At offset {offset:#x}, {record} has bad count {count} instead of the expected {expected_count}.")]
    BadRecordCount {
        offset: u64,
        record: String,
        count: u32,
        expected_count: u32,
    },

    #[error("In long string missing values record starting at offset {record_offset:#x}, value length at offset {offset:#x} is {value_len} instead of the expected 8.")]
    BadLongMissingValueLength {
        record_offset: u64,
        offset: u64,
        value_len: u32,
    },

    #[error("The encoding record at offset {offset:#x} contains an encoding name that is not valid UTF-8.")]
    BadEncodingName { offset: u64 },

    // XXX This is risky because `text` might be arbitarily long.
    #[error("Text string contains invalid bytes for {encoding} encoding: {text:?}")]
    MalformedString { encoding: String, text: String },

    #[error("Invalid variable measurement level value {0}")]
    InvalidMeasurement(u32),

    #[error("Invalid variable display alignment value {0}")]
    InvalidAlignment(u32),

    #[error("Invalid attribute name.  {0}")]
    InvalidAttributeName(IdError),

    #[error("Invalid variable name in attribute record.  {0}")]
    InvalidAttributeVariableName(IdError),

    #[error("Missing `=` separator in long variable name record.")]
    LongNameMissingEquals,

    #[error("Invalid short name in long variable name record.  {0}")]
    InvalidShortName(IdError),

    #[error("Invalid name in long variable name record.  {0}")]
    InvalidLongName(IdError),

    #[error("Invalid variable name in very long string record.  {0}")]
    InvalidLongStringName(IdError),

    #[error("Invalid variable name in variable set record.  {0}")]
    InvalidVariableSetName(IdError),

    #[error("Variable set missing name delimiter.")]
    VariableSetMissingEquals,

    #[error("Invalid multiple response set name.  {0}")]
    InvalidMrSetName(IdError),

    #[error("Invalid multiple response set variable name.  {0}")]
    InvalidMrSetVariableName(IdError),

    #[error("Invalid variable name in long string missing values record.  {0}")]
    InvalidLongStringMissingValueVariableName(IdError),

    #[error("Invalid variable name in long string value label record.  {0}")]
    InvalidLongStringValueLabelName(IdError),

    #[error("{0}")]
    EncodingError(EncodingError),

    #[error("Missing value record with range not allowed for string variable")]
    MissingValueStringRange,

    #[error("Missing value record at offset {0:#x} not allowed for long string continuation")]
    MissingValueContinuation(u64),

    #[error("Invalid multiple dichotomy label type")]
    InvalidMultipleDichotomyLabelType,

    #[error("Invalid multiple response type")]
    InvalidMultipleResponseType,

    #[error("Syntax error in multiple response record ({0})")]
    MultipleResponseSyntaxError(&'static str),

    #[error("Syntax error parsing counted string (missing trailing space)")]
    CountedStringMissingSpace,

    #[error("Syntax error parsing counted string (invalid UTF-8)")]
    CountedStringInvalidUTF8,

    #[error("Syntax error parsing counted string (invalid length {0:?})")]
    CountedStringInvalidLength(String),

    #[error("Syntax error parsing counted string (length {0:?} goes past end of input)")]
    CountedStringTooLong(usize),

    #[error("Variable display record contains {count} items but should contain either {first} or {second}.")]
    InvalidVariableDisplayCount {
        count: usize,
        first: usize,
        second: usize,
    },

    #[error("Very long string record missing delimiter in {0:?}.")]
    VeryLongStringMissingDelimiter(String),

    #[error("Very long string record has invalid length in {0:?}.")]
    VeryLongStringInvalidLength(String),

    #[error("Attribute record missing left parenthesis, in {0:?}.")]
    AttributeMissingLParen(String),

    #[error("Attribute for {name}[{}] lacks value.", index + 1)]
    AttributeMissingValue { name: Identifier, index: usize },

    #[error("Attribute for {name}[{}] missing quotations.", index + 1)]
    AttributeMissingQuotes { name: Identifier, index: usize },

    #[error("Duplicate attributes for variable {variable}: {}.", attributes.iter().join(", "))]
    DuplicateVariableAttributes {
        variable: Identifier,
        attributes: Vec<Identifier>,
    },

    #[error("Duplicate dataset attributes with names: {}.", attributes.iter().join(", "))]
    DuplicateFileAttributes { attributes: Vec<Identifier> },

    #[error("Compression bias is {0} instead of the usual values of 0 or 100.")]
    UnexpectedBias(f64),

    #[error("ZLIB block descriptor {index} reported block size {actual:#x}, when {expected:#x} was expected.")]
    ZlibTrailerBlockWrongSize {
        index: usize,
        actual: u32,
        expected: u32,
    },

    #[error("ZLIB block descriptor {index} reported block size {actual:#x}, when at most {max_expected:#x} was expected.")]
    ZlibTrailerBlockTooBig {
        index: usize,
        actual: u32,
        max_expected: u32,
    },

    #[error("Details TBD (raw)")]
    TBD,
}

impl From<IoError> for Warning {
    fn from(_source: IoError) -> Self {
        Self::UnexpectedEndOfData
    }
}

#[derive(Clone, Debug)]
pub enum Record {
    Header(HeaderRecord<RawString>),
    Variable(VariableRecord<RawString>),
    ValueLabel(ValueLabelRecord<RawStrArray<8>, RawString>),
    Document(DocumentRecord<RawDocumentLine>),
    IntegerInfo(IntegerInfoRecord),
    FloatInfo(FloatInfoRecord),
    VarDisplay(VarDisplayRecord),
    MultipleResponse(MultipleResponseRecord<RawString, RawString>),
    LongStringValueLabels(LongStringValueLabelRecord<RawString, RawString>),
    LongStringMissingValues(LongStringMissingValueRecord<RawString>),
    Encoding(EncodingRecord),
    NumberOfCases(NumberOfCasesRecord),
    VariableSets(RawVariableSetRecord),
    ProductInfo(RawProductInfoRecord),
    LongNames(RawLongNamesRecord),
    VeryLongStrings(RawVeryLongStringsRecord),
    FileAttributes(RawFileAttributesRecord),
    VariableAttributes(RawVariableAttributesRecord),
    OtherExtension(Extension),
    EndOfHeaders(u32),
    ZHeader(ZHeader),
    ZTrailer(ZTrailer),
}

#[derive(Clone, Debug)]
pub enum DecodedRecord {
    Header(HeaderRecord<String>),
    Variable(VariableRecord<String>),
    ValueLabel(ValueLabelRecord<RawStrArray<8>, String>),
    Document(DocumentRecord<String>),
    IntegerInfo(IntegerInfoRecord),
    FloatInfo(FloatInfoRecord),
    VarDisplay(VarDisplayRecord),
    MultipleResponse(MultipleResponseRecord<Identifier, String>),
    LongStringValueLabels(LongStringValueLabelRecord<Identifier, String>),
    LongStringMissingValues(LongStringMissingValueRecord<Identifier>),
    Encoding(EncodingRecord),
    NumberOfCases(NumberOfCasesRecord),
    VariableSets(VariableSetRecord),
    ProductInfo(ProductInfoRecord),
    LongNames(LongNamesRecord),
    VeryLongStrings(VeryLongStringsRecord),
    FileAttributes(FileAttributesRecord),
    VariableAttributes(VariableAttributesRecord),
    OtherExtension(Extension),
    EndOfHeaders(u32),
    ZHeader(ZHeader),
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
        R: Read + Seek,
    {
        let rec_type: u32 = endian.parse(read_bytes(reader)?);
        match rec_type {
            2 => Ok(Some(VariableRecord::read(reader, endian, warn)?)),
            3 => Ok(ValueLabelRecord::read(reader, endian, var_types, warn)?),
            6 => Ok(Some(DocumentRecord::read(reader, endian)?)),
            7 => Extension::read(reader, endian, var_types, warn),
            999 => Ok(Some(Record::EndOfHeaders(
                endian.parse(read_bytes(reader)?),
            ))),
            _ => Err(Error::BadRecordType {
                offset: reader.stream_position()?,
                rec_type,
            }),
        }
    }

    pub fn decode(self, decoder: &mut Decoder) -> Result<DecodedRecord, Error> {
        Ok(match self {
            Record::Header(record) => record.decode(decoder),
            Record::Variable(record) => record.decode(decoder),
            Record::ValueLabel(record) => DecodedRecord::ValueLabel(record.decode(decoder)),
            Record::Document(record) => record.decode(decoder),
            Record::IntegerInfo(record) => DecodedRecord::IntegerInfo(record.clone()),
            Record::FloatInfo(record) => DecodedRecord::FloatInfo(record.clone()),
            Record::VarDisplay(record) => DecodedRecord::VarDisplay(record.clone()),
            Record::MultipleResponse(record) => record.decode(decoder),
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
        })
    }
}

pub fn encoding_from_headers(
    headers: &Vec<Record>,
    warn: &mut impl FnMut(Warning),
) -> Result<&'static Encoding, Error> {
    let mut encoding_record = None;
    let mut integer_info_record = None;
    for record in headers {
        match record {
            Record::Encoding(record) => encoding_record = Some(record),
            Record::IntegerInfo(record) => integer_info_record = Some(record),
            _ => (),
        }
    }
    let encoding = encoding_record.map(|record| record.0.as_str());
    let character_code = integer_info_record.map(|record| record.character_code);
    match get_encoding(encoding, character_code) {
        Ok(encoding) => Ok(encoding),
        Err(err @ EncodingError::Ebcdic) => Err(Error::EncodingError(err)),
        Err(err) => {
            warn(Warning::EncodingError(err));
            // Warn that we're using the default encoding.
            Ok(default_encoding())
        }
    }
}

// If `s` is valid UTF-8, returns it decoded as UTF-8, otherwise returns it
// decoded as Latin-1 (actually bytes interpreted as Unicode code points).
fn default_decode(s: &[u8]) -> Cow<str> {
    from_utf8(s).map_or_else(|_| decode_latin1(s), Cow::from)
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Compression {
    Simple,
    ZLib,
}

#[derive(Clone)]
pub struct HeaderRecord<S>
where
    S: Debug,
{
    /// Offset in file.
    pub offsets: Range<u64>,

    /// Magic number.
    pub magic: Magic,

    /// Eye-catcher string, product name, in the file's encoding.  Padded
    /// on the right with spaces.
    pub eye_catcher: S,

    /// Layout code, normally either 2 or 3.
    pub layout_code: u32,

    /// Number of variable positions, or `None` if the value in the file is
    /// questionably trustworthy.
    pub nominal_case_size: Option<u32>,

    /// Compression type, if any,
    pub compression: Option<Compression>,

    /// 1-based variable index of the weight variable, or `None` if the file is
    /// unweighted.
    pub weight_index: Option<u32>,

    /// Claimed number of cases, if known.
    pub n_cases: Option<u32>,

    /// Compression bias, usually 100.0.
    pub bias: f64,

    /// `dd mmm yy` in the file's encoding.
    pub creation_date: S,

    /// `HH:MM:SS` in the file's encoding.
    pub creation_time: S,

    /// File label, in the file's encoding.  Padded on the right with spaces.
    pub file_label: S,

    /// Endianness of the data in the file header.
    pub endian: Endian,
}

impl<S> HeaderRecord<S>
where
    S: Debug,
{
    fn debug_field<T>(&self, f: &mut Formatter, name: &str, value: T) -> FmtResult
    where
        T: Debug,
    {
        writeln!(f, "{name:>17}: {:?}", value)
    }
}

impl<S> Debug for HeaderRecord<S>
where
    S: Debug,
{
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        writeln!(f, "File header record:")?;
        self.debug_field(f, "Magic", self.magic)?;
        self.debug_field(f, "Product name", &self.eye_catcher)?;
        self.debug_field(f, "Layout code", self.layout_code)?;
        self.debug_field(f, "Nominal case size", self.nominal_case_size)?;
        self.debug_field(f, "Compression", self.compression)?;
        self.debug_field(f, "Weight index", self.weight_index)?;
        self.debug_field(f, "Number of cases", self.n_cases)?;
        self.debug_field(f, "Compression bias", self.bias)?;
        self.debug_field(f, "Creation date", &self.creation_date)?;
        self.debug_field(f, "Creation time", &self.creation_time)?;
        self.debug_field(f, "File label", &self.file_label)?;
        self.debug_field(f, "Endianness", self.endian)
    }
}

impl HeaderRecord<RawString> {
    fn read<R: Read + Seek>(r: &mut R, warn: &mut dyn FnMut(Warning)) -> Result<Self, Error> {
        let start = r.stream_position()?;

        let magic: [u8; 4] = read_bytes(r)?;
        let magic: Magic = magic.try_into().map_err(|_| Error::NotASystemFile)?;

        let eye_catcher = RawString(read_vec(r, 60)?);
        let layout_code: [u8; 4] = read_bytes(r)?;
        let endian = Endian::identify_u32(2, layout_code)
            .or_else(|| Endian::identify_u32(2, layout_code))
            .ok_or(Error::NotASystemFile)?;
        let layout_code = endian.parse(layout_code);

        let nominal_case_size: u32 = endian.parse(read_bytes(r)?);
        let nominal_case_size = (1..i32::MAX as u32 / 16)
            .contains(&nominal_case_size)
            .then_some(nominal_case_size);

        let compression_code: u32 = endian.parse(read_bytes(r)?);
        let compression = match (magic, compression_code) {
            (Magic::Zsav, 2) => Some(Compression::ZLib),
            (Magic::Zsav, code) => return Err(Error::InvalidZsavCompression(code)),
            (_, 0) => None,
            (_, 1) => Some(Compression::Simple),
            (_, code) => return Err(Error::InvalidSavCompression(code)),
        };

        let weight_index: u32 = endian.parse(read_bytes(r)?);
        let weight_index = (weight_index > 0).then_some(weight_index);

        let n_cases: u32 = endian.parse(read_bytes(r)?);
        let n_cases = (n_cases < i32::MAX as u32 / 2).then_some(n_cases);

        let bias: f64 = endian.parse(read_bytes(r)?);
        if bias != 100.0 && bias != 0.0 {
            warn(Warning::UnexpectedBias(bias));
        }

        let creation_date = RawString(read_vec(r, 9)?);
        let creation_time = RawString(read_vec(r, 8)?);
        let file_label = RawString(read_vec(r, 64)?);
        let _: [u8; 3] = read_bytes(r)?;

        Ok(HeaderRecord {
            offsets: start..r.stream_position()?,
            magic,
            layout_code,
            nominal_case_size,
            compression,
            weight_index,
            n_cases,
            bias,
            creation_date,
            creation_time,
            eye_catcher,
            file_label,
            endian,
        })
    }

    pub fn decode(self, decoder: &mut Decoder) -> DecodedRecord {
        let eye_catcher = decoder.decode(&self.eye_catcher).to_string();
        let file_label = decoder.decode(&self.file_label).to_string();
        let creation_date = decoder.decode(&self.creation_date).to_string();
        let creation_time = decoder.decode(&self.creation_time).to_string();
        DecodedRecord::Header(HeaderRecord {
            eye_catcher,
            weight_index: self.weight_index,
            n_cases: self.n_cases,
            file_label,
            offsets: self.offsets.clone(),
            magic: self.magic,
            layout_code: self.layout_code,
            nominal_case_size: self.nominal_case_size,
            compression: self.compression,
            bias: self.bias,
            creation_date,
            creation_time,
            endian: self.endian,
        })
    }
}

pub struct Decoder<'a> {
    pub encoding: &'static Encoding,
    pub warn: Box<dyn FnMut(Warning) + 'a>,
}

impl<'de> Decoder<'de> {
    pub fn new<F>(encoding: &'static Encoding, warn: F) -> Self
    where
        F: FnMut(Warning) + 'de,
    {
        Self {
            encoding,
            warn: Box::new(warn),
        }
    }
    fn warn(&mut self, warning: Warning) {
        (self.warn)(warning)
    }
    fn decode_slice<'a>(&mut self, input: &'a [u8]) -> Cow<'a, str> {
        let (output, malformed) = self.encoding.decode_without_bom_handling(input);
        if malformed {
            self.warn(Warning::MalformedString {
                encoding: self.encoding.name().into(),
                text: output.clone().into(),
            });
        }
        output
    }

    fn decode<'a>(&mut self, input: &'a RawString) -> Cow<'a, str> {
        self.decode_slice(input.0.as_slice())
    }

    pub fn decode_identifier(&mut self, input: &RawString) -> Result<Identifier, IdError> {
        let decoded = &self.decode(input);
        self.new_identifier(decoded)
    }

    pub fn new_identifier(&self, name: &str) -> Result<Identifier, IdError> {
        Identifier::from_encoding(name, self.encoding)
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Hash)]
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

impl TryFrom<[u8; 4]> for Magic {
    type Error = Error;

    fn try_from(value: [u8; 4]) -> Result<Self, Self::Error> {
        match value {
            Magic::SAV => Ok(Magic::Sav),
            Magic::ZSAV => Ok(Magic::Zsav),
            Magic::EBCDIC => Ok(Magic::Ebcdic),
            _ => Err(Error::BadMagic(value)),
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum VarType {
    Numeric,
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
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        match self {
            VarType::Numeric => write!(f, "numeric"),
            VarType::String => write!(f, "string"),
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

type RawDatum = Datum<RawStrArray<8>>;

impl RawDatum {
    pub fn from_raw(raw: &UntypedDatum, var_type: VarType, endian: Endian) -> Self {
        match var_type {
            VarType::String => Datum::String(RawStrArray(raw.0)),
            VarType::Numeric => Datum::Number(endian.parse(raw.0)),
        }
    }
}

impl Datum {
    fn read_case<R: Read + Seek>(
        reader: &mut R,
        case_vars: &[CaseVar],
        endian: Endian,
    ) -> Result<Option<Vec<Self>>, Error> {
        fn eof<R: Seek>(
            reader: &mut R,
            case_vars: &[CaseVar],
            case_start: u64,
        ) -> Result<Option<Vec<Datum>>, Error> {
            let offset = reader.stream_position()?;
            if offset == case_start {
                Ok(None)
            } else {
                Err(Error::EofInCase {
                    offset,
                    case_ofs: offset - case_start,
                    case_len: case_vars.iter().map(CaseVar::bytes).sum(),
                })
            }
        }

        let case_start = reader.stream_position()?;
        let mut values = Vec::with_capacity(case_vars.len());
        for var in case_vars {
            match var {
                CaseVar::Numeric => {
                    let Some(raw) = try_read_bytes(reader)? else {
                        return eof(reader, case_vars, case_start);
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
                            return eof(reader, case_vars, case_start);
                        }
                        skip_bytes(reader, segment.padding_bytes)?;
                        offset += segment.data_bytes;
                    }
                    values.push(Datum::String(RawString(datum)));
                }
            }
        }
        Ok(Some(values))
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
        case_vars: &[CaseVar],
        codes: &mut VecDeque<u8>,
        endian: Endian,
        bias: f64,
    ) -> Result<Option<Vec<Self>>, Error> {
        fn eof<R: Seek>(
            reader: &mut R,
            case_start: u64,
            n_chunks: usize,
        ) -> Result<Option<Vec<Datum>>, Error> {
            let offset = reader.stream_position()?;
            if n_chunks > 0 {
                Err(Error::EofInCompressedCase {
                    case_ofs: offset - case_start,
                    n_chunks,
                    offset,
                })
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
                        return eof(reader, case_start, n_chunks);
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
                                return eof(reader, case_start, n_chunks);
                            };
                            let n_data = data_bytes.min(8);
                            datum.extend_from_slice(&raw[..n_data]);
                            data_bytes -= n_data;
                            padding_bytes -= 8 - n_data;
                            n_chunks += 1;
                        }
                    }
                    values.push(Datum::String(RawString(datum)));
                }
            }
        }
        Ok(Some(values))
    }
}

impl RawDatum {
    pub fn decode(&self, width: VarWidth) -> Datum {
        match self {
            Self::Number(x) => Datum::Number(*x),
            Self::String(s) => {
                let width = width.as_string_width().unwrap();
                Datum::String(RawString::from(&s.0[..width]))
            }
        }
    }
}

struct ZlibDecodeMultiple<R>
where
    R: Read + Seek,
{
    reader: Option<ZlibDecoder<R>>,
}

impl<R> ZlibDecodeMultiple<R>
where
    R: Read + Seek,
{
    fn new(reader: R) -> ZlibDecodeMultiple<R> {
        ZlibDecodeMultiple {
            reader: Some(ZlibDecoder::new(reader)),
        }
    }
}

impl<R> Read for ZlibDecodeMultiple<R>
where
    R: Read + Seek,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, IoError> {
        loop {
            match self.reader.as_mut().unwrap().read(buf)? {
                0 => {
                    let inner = self.reader.take().unwrap().into_inner();
                    self.reader = Some(ZlibDecoder::new(inner));
                }
                n => return Ok(n),
            };
        }
    }
}

impl<R> Seek for ZlibDecodeMultiple<R>
where
    R: Read + Seek,
{
    fn seek(&mut self, pos: SeekFrom) -> Result<u64, IoError> {
        self.reader.as_mut().unwrap().get_mut().seek(pos)
    }
}

enum ReaderState {
    Start,
    Headers,
    ZlibHeader,
    ZlibTrailer(ZHeader),
    End,
}

pub struct Reader<'a, R>
where
    R: Read + Seek + 'static,
{
    reader: Option<R>,
    warn: Box<dyn FnMut(Warning) + 'a>,

    header: HeaderRecord<RawString>,
    var_types: VarTypes,

    state: ReaderState,
    cases: Option<Cases>,
}

impl<'a, R> Reader<'a, R>
where
    R: Read + Seek + 'static,
{
    pub fn new(mut reader: R, mut warn: impl FnMut(Warning) + 'a) -> Result<Self, Error> {
        let header = HeaderRecord::read(&mut reader, &mut warn)?;
        Ok(Self {
            reader: Some(reader),
            warn: Box::new(warn),
            header,
            var_types: VarTypes::new(),
            state: ReaderState::Start,
            cases: None,
        })
    }
    pub fn headers<'b>(&'b mut self) -> ReadHeaders<'a, 'b, R> {
        ReadHeaders(self)
    }
    pub fn cases(self) -> Cases {
        self.cases.unwrap_or_default()
    }
}

pub struct ReadHeaders<'a, 'b, R>(&'b mut Reader<'a, R>)
where
    R: Read + Seek + 'static;

impl<'a, 'b, R> ReadHeaders<'a, 'b, R>
where
    R: Read + Seek + 'static,
{
    fn cases(&mut self) {
        self.0.state = ReaderState::End;
        self.0.cases = Some(Cases::new(
            self.0.reader.take().unwrap(),
            take(&mut self.0.var_types),
            &self.0.header,
        ));
    }

    fn _next(&mut self) -> Option<<Self as Iterator>::Item> {
        match self.0.state {
            ReaderState::Start => {
                self.0.state = ReaderState::Headers;
                Some(Ok(Record::Header(self.0.header.clone())))
            }
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
                    Record::Variable(VariableRecord { width, .. }) => self.0.var_types.push(width),
                    Record::EndOfHeaders(_) => {
                        self.0.state = if let Some(Compression::ZLib) = self.0.header.compression {
                            ReaderState::ZlibHeader
                        } else {
                            self.cases();
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
                    zheader,
                    &mut self.0.warn,
                ) {
                    Ok(None) => {
                        self.cases();
                        None
                    }
                    Ok(Some(ztrailer)) => {
                        self.cases();
                        Some(Ok(Record::ZTrailer(ztrailer)))
                    }
                    Err(error) => Some(Err(error)),
                }
            }
            ReaderState::End => None,
        }
    }
}

impl<'a, 'b, R> Iterator for ReadHeaders<'a, 'b, R>
where
    R: Read + Seek + 'static,
{
    type Item = Result<Record, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        let retval = self._next();
        if matches!(retval, Some(Err(_))) {
            self.0.state = ReaderState::End;
        }
        retval
    }
}

trait ReadSeek: Read + Seek {}
impl<T> ReadSeek for T where T: Read + Seek {}

pub struct Case(pub Vec<Datum>);

#[derive(Debug)]
struct StringSegment {
    data_bytes: usize,
    padding_bytes: usize,
}

fn segment_widths(width: usize) -> impl Iterator<Item = usize> {
    let n_segments = width.div_ceil(252);
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

pub struct Cases {
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

impl Debug for Cases {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "Cases")
    }
}

impl Default for Cases {
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

impl Cases {
    fn new<R>(reader: R, var_types: VarTypes, header: &HeaderRecord<RawString>) -> Self
    where
        R: Read + Seek + 'static,
    {
        Self {
            reader: if header.compression == Some(Compression::ZLib) {
                Box::new(ZlibDecodeMultiple::new(reader))
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

    pub fn with_widths(self, widths: impl IntoIterator<Item = VarWidth>) -> Self {
        Self {
            case_vars: widths.into_iter().map(CaseVar::new).collect::<Vec<_>>(),
            ..self
        }
    }

    pub fn with_expected_cases(self, expected_cases: u64) -> Self {
        Self {
            expected_cases: Some(expected_cases),
            ..self
        }
    }
}

impl Iterator for Cases {
    type Item = Result<Vec<Datum>, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.eof {
            return None;
        }

        let retval = if self.case_vars.is_empty() {
            None
        } else if self.compression.is_some() {
            Datum::read_compressed_case(
                &mut self.reader,
                &self.case_vars,
                &mut self.codes,
                self.endian,
                self.bias,
            )
            .transpose()
        } else {
            Datum::read_case(&mut self.reader, &self.case_vars, self.endian).transpose()
        };
        match &retval {
            None => {
                self.eof = true;
                if let Some(expected_cases) = self.expected_cases
                    && expected_cases != self.read_cases
                {
                    return Some(Err(Error::WrongNumberOfCases {
                        expected: expected_cases,
                        actual: self.read_cases,
                    }));
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

#[derive(Copy, Clone, PartialEq, Eq, Hash)]
pub struct Spec(pub u32);

impl Debug for Spec {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        let type_ = format_name(self.0 >> 16);
        let w = (self.0 >> 8) & 0xff;
        let d = self.0 & 0xff;
        write!(f, "{:06x} ({type_}{w}.{d})", self.0)
    }
}

fn format_name(type_: u32) -> Cow<'static, str> {
    match type_ {
        1 => "A",
        2 => "AHEX",
        3 => "COMMA",
        4 => "DOLLAR",
        5 => "F",
        6 => "IB",
        7 => "PIBHEX",
        8 => "P",
        9 => "PIB",
        10 => "PK",
        11 => "RB",
        12 => "RBHEX",
        15 => "Z",
        16 => "N",
        17 => "E",
        20 => "DATE",
        21 => "TIME",
        22 => "DATETIME",
        23 => "ADATE",
        24 => "JDATE",
        25 => "DTIME",
        26 => "WKDAY",
        27 => "MONTH",
        28 => "MOYR",
        29 => "QYR",
        30 => "WKYR",
        31 => "PCT",
        32 => "DOT",
        33 => "CCA",
        34 => "CCB",
        35 => "CCC",
        36 => "CCD",
        37 => "CCE",
        38 => "EDATE",
        39 => "SDATE",
        40 => "MTIME",
        41 => "YMDHMS",
        _ => return format!("<unknown format {type_}>").into(),
    }
    .into()
}

#[derive(Clone, Default)]
pub struct MissingValues {
    /// Individual missing values, up to 3 of them.
    values: Vec<Datum>,

    /// Optional range of missing values.
    range: Option<MissingValueRange>,
}

impl Debug for MissingValues {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        DisplayMissingValues {
            mv: self,
            encoding: None,
        }
        .fmt(f)
    }
}

#[derive(Copy, Clone, Debug)]
pub enum MissingValuesError {
    TooMany,
    TooWide,
    MixedTypes,
}

impl MissingValues {
    pub fn new(
        mut values: Vec<Datum>,
        range: Option<MissingValueRange>,
    ) -> Result<Self, MissingValuesError> {
        if values.len() > 3 {
            return Err(MissingValuesError::TooMany);
        }

        let mut var_type = None;
        for value in values.iter_mut() {
            value.trim_end();
            match value.width() {
                VarWidth::String(w) if w > 8 => return Err(MissingValuesError::TooWide),
                _ => (),
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

    pub fn contains(&self, value: &Datum) -> bool {
        if self
            .values
            .iter()
            .any(|datum| datum.eq_ignore_trailing_spaces(value))
        {
            return true;
        }

        match value {
            Datum::Number(Some(number)) => self.range.is_some_and(|range| range.contains(*number)),
            _ => false,
        }
    }

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        self.values.iter().all(|datum| datum.is_resizable(width))
            && self.range.iter().all(|range| range.is_resizable(width))
    }

    pub fn resize(&mut self, width: VarWidth) {
        for datum in &mut self.values {
            datum.resize(width);
        }
        if let Some(range) = &mut self.range {
            range.resize(width);
        }
    }

    fn read<R: Read + Seek>(
        r: &mut R,
        offset: u64,
        raw_width: RawWidth,
        code: i32,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Self, Error> {
        let (individual_values, has_range) = match code {
            0 => return Ok(Self::default()),
            1..=3 => (code as usize, false),
            -2 => (0, true),
            -3 => (1, true),
            _ => return Err(Error::BadMissingValueCode { offset, code }),
        };

        let mut values = Vec::with_capacity(individual_values);
        let range = if has_range {
            let low = read_bytes::<8, _>(r)?;
            let high = read_bytes::<8, _>(r)?;
            Some((low, high))
        } else {
            None
        };
        for _ in 0..individual_values {
            values.push(read_bytes::<8, _>(r)?);
        }

        match VarWidth::try_from(raw_width) {
            Ok(VarWidth::Numeric) => {
                let values = values
                    .into_iter()
                    .map(|v| Datum::Number(endian.parse(v)))
                    .collect();

                let range = range.map(|(low, high)| {
                    MissingValueRange::new(endian.parse(low), endian.parse(high))
                });
                return Ok(Self::new(values, range).unwrap());
            }
            Ok(VarWidth::String(_)) if range.is_some() => warn(Warning::MissingValueStringRange),
            Ok(VarWidth::String(width)) => {
                let width = width.min(8) as usize;
                let values = values
                    .into_iter()
                    .map(|value| Datum::String(RawString::from(&value[..width])))
                    .collect();
                return Ok(Self::new(values, None).unwrap());
            }
            Err(()) => warn(Warning::MissingValueContinuation(offset)),
        }
        Ok(Self::default())
    }

    pub fn display(&self, encoding: &'static Encoding) -> DisplayMissingValues<'_> {
        DisplayMissingValues {
            mv: self,
            encoding: Some(encoding),
        }
    }
}

pub struct DisplayMissingValues<'a> {
    mv: &'a MissingValues,
    encoding: Option<&'static Encoding>,
}

impl<'a> Display for DisplayMissingValues<'a> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if let Some(range) = &self.mv.range {
            write!(f, "{range}")?;
            if !self.mv.values.is_empty() {
                write!(f, "; ")?;
            }
        }

        for (i, value) in self.mv.values.iter().enumerate() {
            if i > 0 {
                write!(f, "; ")?;
            }
            match self.encoding {
                Some(encoding) => value.display_plain(encoding).fmt(f)?,
                None => value.fmt(f)?,
            }
        }

        if self.mv.is_empty() {
            write!(f, "none")?;
        }
        Ok(())
    }
}

#[derive(Copy, Clone)]
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

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        width.is_numeric()
    }

    pub fn resize(&self, width: VarWidth) {
        assert_eq!(width, VarWidth::Numeric);
    }
}

impl Display for MissingValueRange {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
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

#[derive(Clone)]
pub struct VariableRecord<S>
where
    S: Debug,
{
    /// Range of offsets in file.
    pub offsets: Range<u64>,

    /// Variable width, in the range -1..=255.
    pub width: RawWidth,

    /// Variable name, padded on the right with spaces.
    pub name: S,

    /// Print format.
    pub print_format: Spec,

    /// Write format.
    pub write_format: Spec,

    /// Missing values.
    pub missing_values: MissingValues,

    /// Optional variable label.
    pub label: Option<S>,
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RawWidth {
    Continuation,
    Numeric,
    String(NonZeroU8),
}

impl RawWidth {
    pub fn n_values(&self) -> Option<usize> {
        match self {
            RawWidth::Numeric => Some(1),
            RawWidth::String(width) => Some((width.get() as usize).div_ceil(8)),
            _ => None,
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

impl<S> Debug for VariableRecord<S>
where
    S: Debug,
{
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        writeln!(f, "Width: {}", self.width,)?;
        writeln!(f, "Print format: {:?}", self.print_format)?;
        writeln!(f, "Write format: {:?}", self.write_format)?;
        writeln!(f, "Name: {:?}", &self.name)?;
        writeln!(f, "Variable label: {:?}", self.label)?;
        writeln!(f, "Missing values: {:?}", self.missing_values)
    }
}

impl VariableRecord<RawString> {
    fn read<R: Read + Seek>(
        r: &mut R,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Record, Error> {
        let start_offset = r.stream_position()?;
        let width: i32 = endian.parse(read_bytes(r)?);
        let width: RawWidth = width.try_into().map_err(|_| Error::BadVariableWidth {
            start_offset,
            width,
        })?;
        let code_offset = r.stream_position()?;
        let has_variable_label: u32 = endian.parse(read_bytes(r)?);
        let missing_value_code: i32 = endian.parse(read_bytes(r)?);
        let print_format = Spec(endian.parse(read_bytes(r)?));
        let write_format = Spec(endian.parse(read_bytes(r)?));
        let name = RawString(read_vec(r, 8)?);

        let label = match has_variable_label {
            0 => None,
            1 => {
                let len: u32 = endian.parse(read_bytes(r)?);
                let read_len = len.min(65535) as usize;
                let label = RawString(read_vec(r, read_len)?);

                let padding_bytes = Integer::next_multiple_of(&len, &4) - len;
                let _ = read_vec(r, padding_bytes as usize)?;

                Some(label)
            }
            _ => {
                return Err(Error::BadVariableLabelCode {
                    start_offset,
                    code_offset,
                    code: has_variable_label,
                })
            }
        };

        let missing_values =
            MissingValues::read(r, start_offset, width, missing_value_code, endian, warn)?;

        let end_offset = r.stream_position()?;

        Ok(Record::Variable(VariableRecord {
            offsets: start_offset..end_offset,
            width,
            name,
            print_format,
            write_format,
            missing_values,
            label,
        }))
    }

    pub fn decode(self, decoder: &mut Decoder) -> DecodedRecord {
        DecodedRecord::Variable(VariableRecord {
            offsets: self.offsets.clone(),
            width: self.width,
            name: decoder.decode(&self.name).to_string(),
            print_format: self.print_format,
            write_format: self.write_format,
            missing_values: self.missing_values,
            label: self
                .label
                .as_ref()
                .map(|label| decoder.decode(label).to_string()),
        })
    }
}

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
        write!(f, "{number}")?;

        let string = default_decode(&self.0);
        let string = string
            .split(|c: char| c == '\0' || c.is_control())
            .next()
            .unwrap();
        write!(f, "{string:?}")?;
        Ok(())
    }
}

/// An owned string in an unspecified encoding.
///
/// We assume that the encoding is one supported by [encoding_rs] with byte
/// units (that is, not a `UTF-16` encoding).  All of these encodings have some
/// basic ASCII compatibility.
///
/// A [RawString] owns its contents and can grow and shrink, like a [Vec] or
/// [String].  For a borrowed raw string, see [RawStr].
#[derive(Clone, PartialEq, Default, Eq, PartialOrd, Ord, Hash)]
pub struct RawString(pub Vec<u8>);

impl RawString {
    pub fn spaces(n: usize) -> Self {
        Self(std::iter::repeat_n(b' ', n).collect())
    }
    pub fn as_encoded(&self, encoding: &'static Encoding) -> EncodedStr<'_> {
        EncodedStr::new(&self.0, encoding)
    }
    pub fn resize(&mut self, len: usize) {
        self.0.resize(len, b' ');
    }
    pub fn len(&self) -> usize {
        self.0.len()
    }
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
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{:?}", default_decode(self.0.as_slice()))
    }
}

/// A borrowed string in an unspecified encoding.
///
/// We assume that the encoding is one supported by [encoding_rs] with byte
/// units (that is, not a `UTF-16` encoding).  All of these encodings have some
/// basic ASCII compatibility.
///
/// For an owned raw string, see [RawString].
#[repr(transparent)]
#[derive(PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct RawStr(pub [u8]);

impl RawStr {
    pub fn from_bytes(bytes: &[u8]) -> &Self {
        // SAFETY: `RawStr` is a transparent wrapper around `[u8]`, so we can
        // turn a reference to the wrapped type into a reference to the wrapper
        // type.
        unsafe { &*(bytes as *const [u8] as *const Self) }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }

    /// Returns an object that implements [Display] for printing this [RawStr],
    /// given that it is encoded in `encoding`.
    pub fn display(&self, encoding: &'static Encoding) -> DisplayRawString {
        DisplayRawString(encoding.decode_without_bom_handling(&self.0).0)
    }

    pub fn decode(&self, encoding: &'static Encoding) -> Cow<'_, str> {
        encoding.decode_without_bom_handling(&self.0).0
    }

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
}

pub struct DisplayRawString<'a>(Cow<'a, str>);

impl<'a> Display for DisplayRawString<'a> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}", &self.0)
    }
}

impl Debug for RawStr {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{:?}", default_decode(self.as_bytes()))
    }
}

#[derive(Copy, Clone)]
pub struct RawStrArray<const N: usize>(pub [u8; N]);

impl<const N: usize> From<[u8; N]> for RawStrArray<N> {
    fn from(source: [u8; N]) -> Self {
        Self(source)
    }
}

impl<const N: usize> Debug for RawStrArray<N> {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{:?}", default_decode(&self.0))
    }
}

#[derive(Clone, Debug)]
pub enum EncodedString {
    Encoded {
        bytes: Vec<u8>,
        encoding: &'static Encoding,
    },
    Utf8 {
        s: String,
    },
}

impl EncodedString {
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

pub enum EncodedStr<'a> {
    Encoded {
        bytes: &'a [u8],
        encoding: &'static Encoding,
    },
    Utf8 {
        s: &'a str,
    },
}

impl<'a> EncodedStr<'a> {
    pub fn new(bytes: &'a [u8], encoding: &'static Encoding) -> Self {
        Self::Encoded { bytes, encoding }
    }
    pub fn as_str(&self) -> Cow<'_, str> {
        match self {
            EncodedStr::Encoded { bytes, encoding } => {
                encoding.decode_without_bom_handling(bytes).0
            }
            EncodedStr::Utf8 { s } => Cow::from(*s),
        }
    }
    pub fn as_bytes(&self) -> &[u8] {
        match self {
            EncodedStr::Encoded { bytes, .. } => bytes,
            EncodedStr::Utf8 { s } => s.as_bytes(),
        }
    }
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
    pub fn is_empty(&self) -> bool {
        match self {
            EncodedStr::Encoded { bytes, .. } => bytes.is_empty(),
            EncodedStr::Utf8 { s } => s.is_empty(),
        }
    }
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

pub struct QuotedEncodedStr<'a>(&'a EncodedStr<'a>);

impl Display for QuotedEncodedStr<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.0.as_str())
    }
}

#[derive(Clone, Debug)]
pub struct ValueLabel<V, S>
where
    V: Debug,
    S: Debug,
{
    pub datum: Datum<V>,
    pub label: S,
}

#[derive(Clone)]
pub struct ValueLabelRecord<V, S>
where
    V: Debug,
    S: Debug,
{
    /// Range of offsets in file.
    pub offsets: Range<u64>,

    /// The labels.
    pub labels: Vec<ValueLabel<V, S>>,

    /// The 1-based indexes of the variable indexes.
    pub dict_indexes: Vec<u32>,

    /// The types of the variables.
    pub var_type: VarType,
}

impl<V, S> Debug for ValueLabelRecord<V, S>
where
    V: Debug,
    S: Debug,
{
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        writeln!(f, "labels: ")?;
        for label in self.labels.iter() {
            writeln!(f, "{label:?}")?;
        }
        write!(f, "apply to {} variables", self.var_type)?;
        for dict_index in self.dict_indexes.iter() {
            write!(f, " #{dict_index}")?;
        }
        Ok(())
    }
}

impl<V, S> ValueLabelRecord<V, S>
where
    V: Debug,
    S: Debug,
{
    /// Maximum number of value labels in a record.
    pub const MAX_LABELS: u32 = u32::MAX / 8;

    /// Maximum number of variable indexes in a record.
    pub const MAX_INDEXES: u32 = u32::MAX / 8;
}

impl ValueLabelRecord<RawStrArray<8>, RawString> {
    fn read<R: Read + Seek>(
        r: &mut R,
        endian: Endian,
        var_types: &VarTypes,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<Record>, Error> {
        let label_offset = r.stream_position()?;
        let n: u32 = endian.parse(read_bytes(r)?);
        if n > Self::MAX_LABELS {
            return Err(Error::BadNumberOfValueLabels {
                offset: label_offset,
                n,
                max: Self::MAX_LABELS,
            });
        }

        let mut labels = Vec::new();
        for _ in 0..n {
            let value = UntypedDatum(read_bytes(r)?);
            let label_len: u8 = endian.parse(read_bytes(r)?);
            let label_len = label_len as usize;
            let padded_len = Integer::next_multiple_of(&(label_len + 1), &8);

            let mut label = read_vec(r, padded_len - 1)?;
            label.truncate(label_len);
            labels.push((value, RawString(label)));
        }

        let index_offset = r.stream_position()?;
        let rec_type: u32 = endian.parse(read_bytes(r)?);
        if rec_type != 4 {
            return Err(Error::ExpectedVarIndexRecord {
                offset: index_offset,
                rec_type,
            });
        }

        let n: u32 = endian.parse(read_bytes(r)?);
        if n > Self::MAX_INDEXES {
            return Err(Error::TooManyVarIndexes {
                offset: index_offset,
                n,
                max: Self::MAX_INDEXES,
            });
        } else if n == 0 {
            dbg!();
            warn(Warning::NoVarIndexes {
                offset: index_offset,
            });
            return Ok(None);
        }

        let index_offset = r.stream_position()?;
        let mut dict_indexes = Vec::with_capacity(n as usize);
        let mut invalid_indexes = Vec::new();
        for _ in 0..n {
            let index: u32 = endian.parse(read_bytes(r)?);
            if var_types.is_valid_index(index as usize) {
                dict_indexes.push(index);
            } else {
                invalid_indexes.push(index);
            }
        }
        if !invalid_indexes.is_empty() {
            warn(Warning::InvalidVarIndexes {
                offset: index_offset,
                max: var_types.n_values(),
                invalid: invalid_indexes,
            });
        }

        let Some(&first_index) = dict_indexes.first() else {
            return Ok(None);
        };
        let var_type = VarType::from(var_types.types[first_index as usize - 1].unwrap());
        let mut wrong_type_indexes = Vec::new();
        dict_indexes.retain(|&index| {
            if var_types.types[index as usize - 1].map(VarType::from) != Some(var_type) {
                wrong_type_indexes.push(index);
                false
            } else {
                true
            }
        });
        if !wrong_type_indexes.is_empty() {
            warn(Warning::MixedVarTypes {
                offset: index_offset,
                var_type,
                wrong_types: wrong_type_indexes,
            });
        }

        let labels = labels
            .into_iter()
            .map(|(value, label)| ValueLabel {
                datum: Datum::from_raw(&value, var_type, endian),
                label,
            })
            .collect();

        let end_offset = r.stream_position()?;
        Ok(Some(Record::ValueLabel(ValueLabelRecord {
            offsets: label_offset..end_offset,
            labels,
            dict_indexes,
            var_type,
        })))
    }

    fn decode(self, decoder: &mut Decoder) -> ValueLabelRecord<RawStrArray<8>, String> {
        let labels = self
            .labels
            .iter()
            .map(
                |ValueLabel {
                     datum: value,
                     label,
                 }| ValueLabel {
                    datum: value.clone(),
                    label: decoder.decode(label).to_string(),
                },
            )
            .collect();
        ValueLabelRecord {
            offsets: self.offsets.clone(),
            labels,
            dict_indexes: self.dict_indexes.clone(),
            var_type: self.var_type,
        }
    }
}

#[derive(Clone, Debug)]
pub struct DocumentRecord<S>
where
    S: Debug,
{
    pub offsets: Range<u64>,

    /// The document, as an array of lines.  Raw lines are exactly 80 bytes long
    /// and are right-padded with spaces without any new-line termination.
    pub lines: Vec<S>,
}

pub type RawDocumentLine = RawStrArray<DOC_LINE_LEN>;

/// Length of a line in a document.  Document lines are fixed-length and
/// padded on the right with spaces.
pub const DOC_LINE_LEN: usize = 80;

impl DocumentRecord<RawDocumentLine> {
    /// Maximum number of lines we will accept in a document.  This is simply
    /// the maximum number that will fit in a 32-bit space.
    pub const MAX_LINES: usize = i32::MAX as usize / DOC_LINE_LEN;

    fn read<R: Read + Seek>(r: &mut R, endian: Endian) -> Result<Record, Error> {
        let start_offset = r.stream_position()?;
        let n: u32 = endian.parse(read_bytes(r)?);
        let n = n as usize;
        if n > Self::MAX_LINES {
            Err(Error::BadDocumentLength {
                offset: start_offset,
                n,
                max: Self::MAX_LINES,
            })
        } else {
            let mut lines = Vec::with_capacity(n);
            for _ in 0..n {
                lines.push(RawStrArray(read_bytes(r)?));
            }
            let end_offset = r.stream_position()?;
            Ok(Record::Document(DocumentRecord {
                offsets: start_offset..end_offset,
                lines,
            }))
        }
    }

    pub fn decode(self, decoder: &mut Decoder) -> DecodedRecord {
        DecodedRecord::Document(DocumentRecord {
            offsets: self.offsets.clone(),
            lines: self
                .lines
                .iter()
                .map(|s| decoder.decode_slice(&s.0).to_string())
                .collect(),
        })
    }
}

struct ExtensionRecord<'a> {
    size: Option<u32>,
    count: Option<u32>,
    name: &'a str,
}

#[derive(Clone, Debug)]
pub struct IntegerInfoRecord {
    pub offsets: Range<u64>,
    pub version: (i32, i32, i32),
    pub machine_code: i32,
    pub floating_point_rep: i32,
    pub compression_code: i32,
    pub endianness: i32,
    pub character_code: i32,
}

static INTEGER_INFO_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(4),
    count: Some(8),
    name: "integer record",
};

impl IntegerInfoRecord {
    fn parse(ext: &Extension, endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&INTEGER_INFO_RECORD)?;

        let mut input = &ext.data[..];
        let data: Vec<i32> = (0..8)
            .map(|_| endian.parse(read_bytes(&mut input).unwrap()))
            .collect();
        Ok(Record::IntegerInfo(IntegerInfoRecord {
            offsets: ext.offsets.clone(),
            version: (data[0], data[1], data[2]),
            machine_code: data[3],
            floating_point_rep: data[4],
            compression_code: data[5],
            endianness: data[6],
            character_code: data[7],
        }))
    }
}

#[derive(Clone, Debug)]
pub struct FloatInfoRecord {
    pub sysmis: f64,
    pub highest: f64,
    pub lowest: f64,
}

static FLOAT_INFO_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(8),
    count: Some(3),
    name: "floating point record",
};

impl FloatInfoRecord {
    fn parse(ext: &Extension, endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&FLOAT_INFO_RECORD)?;

        let mut input = &ext.data[..];
        let data: Vec<f64> = (0..3)
            .map(|_| endian.parse(read_bytes(&mut input).unwrap()))
            .collect();
        Ok(Record::FloatInfo(FloatInfoRecord {
            sysmis: data[0],
            highest: data[1],
            lowest: data[2],
        }))
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum CategoryLabels {
    VarLabels,
    CountedValues,
}

#[derive(Clone, Debug)]
pub enum MultipleResponseType {
    MultipleDichotomy {
        value: RawString,
        labels: CategoryLabels,
    },
    MultipleCategory,
}

impl MultipleResponseType {
    fn parse(input: &[u8]) -> Result<(MultipleResponseType, &[u8]), Warning> {
        let (mr_type, input) = match input.split_first() {
            Some((b'C', input)) => (MultipleResponseType::MultipleCategory, input),
            Some((b'D', input)) => {
                let (value, input) = parse_counted_string(input)?;
                (
                    MultipleResponseType::MultipleDichotomy {
                        value,
                        labels: CategoryLabels::VarLabels,
                    },
                    input,
                )
            }
            Some((b'E', input)) => {
                let (labels, input) = if let Some(rest) = input.strip_prefix(b" 1 ") {
                    (CategoryLabels::CountedValues, rest)
                } else if let Some(rest) = input.strip_prefix(b" 11 ") {
                    (CategoryLabels::VarLabels, rest)
                } else {
                    return Err(Warning::InvalidMultipleDichotomyLabelType);
                };
                let (value, input) = parse_counted_string(input)?;
                (
                    MultipleResponseType::MultipleDichotomy { value, labels },
                    input,
                )
            }
            _ => return Err(Warning::InvalidMultipleResponseType),
        };
        Ok((mr_type, input))
    }
}

#[derive(Clone, Debug)]
pub struct MultipleResponseSet<I, S>
where
    I: Debug,
    S: Debug,
{
    pub name: I,
    pub label: S,
    pub mr_type: MultipleResponseType,
    pub short_names: Vec<I>,
}

impl MultipleResponseSet<RawString, RawString> {
    fn parse(input: &[u8]) -> Result<(Self, &[u8]), Warning> {
        let Some(equals) = input.iter().position(|&b| b == b'=') else {
            return Err(Warning::MultipleResponseSyntaxError("missing `=`"));
        };
        let (name, input) = input.split_at(equals);
        let input = input.strip_prefix(b"=").unwrap();
        let (mr_type, input) = MultipleResponseType::parse(input)?;
        let Some(input) = input.strip_prefix(b" ") else {
            return Err(Warning::MultipleResponseSyntaxError(
                "missing space after multiple response type",
            ));
        };
        let (label, mut input) = parse_counted_string(input)?;
        let mut vars = Vec::new();
        while input.first() != Some(&b'\n') {
            match input.split_first() {
                Some((b' ', rest)) => {
                    let Some(length) = rest.iter().position(|b| b" \n".contains(b)) else {
                        return Err(Warning::MultipleResponseSyntaxError(
                            "missing variable name delimiter",
                        ));
                    };
                    let (var, rest) = rest.split_at(length);
                    if !var.is_empty() {
                        vars.push(var.into());
                    }
                    input = rest;
                }
                _ => {
                    return Err(Warning::MultipleResponseSyntaxError(
                        "missing space preceding variable name",
                    ))
                }
            }
        }
        while input.first() == Some(&b'\n') {
            input = &input[1..];
        }
        Ok((
            MultipleResponseSet {
                name: name.into(),
                label,
                mr_type,
                short_names: vars,
            },
            input,
        ))
    }

    fn decode(
        &self,
        decoder: &mut Decoder,
    ) -> Result<MultipleResponseSet<Identifier, String>, Warning> {
        let mut short_names = Vec::with_capacity(self.short_names.len());
        for short_name in self.short_names.iter() {
            if let Some(short_name) = decoder
                .decode_identifier(short_name)
                .map_err(Warning::InvalidMrSetName)
                .issue_warning(&mut decoder.warn)
            {
                short_names.push(short_name);
            }
        }
        Ok(MultipleResponseSet {
            name: decoder
                .decode_identifier(&self.name)
                .map_err(Warning::InvalidMrSetVariableName)?,
            label: decoder.decode(&self.label).to_string(),
            mr_type: self.mr_type.clone(),
            short_names,
        })
    }
}

#[derive(Clone, Debug)]
pub struct MultipleResponseRecord<I, S>(pub Vec<MultipleResponseSet<I, S>>)
where
    I: Debug,
    S: Debug;

static MULTIPLE_RESPONSE_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(1),
    count: None,
    name: "multiple response set record",
};

impl MultipleResponseRecord<RawString, RawString> {
    fn parse(ext: &Extension, _endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&MULTIPLE_RESPONSE_RECORD)?;

        let mut input = &ext.data[..];
        let mut sets = Vec::new();
        loop {
            while let Some(suffix) = input.strip_prefix(b"\n") {
                input = suffix;
            }
            if input.is_empty() {
                break;
            }
            let (set, rest) = MultipleResponseSet::parse(input)?;
            sets.push(set);
            input = rest;
        }
        Ok(Record::MultipleResponse(MultipleResponseRecord(sets)))
    }
}

impl MultipleResponseRecord<RawString, RawString> {
    fn decode(self, decoder: &mut Decoder) -> DecodedRecord {
        let mut sets = Vec::new();
        for set in self.0.iter() {
            if let Some(set) = set.decode(decoder).issue_warning(&mut decoder.warn) {
                sets.push(set);
            }
        }
        DecodedRecord::MultipleResponse(MultipleResponseRecord(sets))
    }
}

fn parse_counted_string(input: &[u8]) -> Result<(RawString, &[u8]), Warning> {
    let Some(space) = input.iter().position(|&b| b == b' ') else {
        return Err(Warning::CountedStringMissingSpace);
    };
    let Ok(length) = from_utf8(&input[..space]) else {
        return Err(Warning::CountedStringInvalidUTF8);
    };
    let Ok(length): Result<usize, _> = length.parse() else {
        return Err(Warning::CountedStringInvalidLength(length.into()));
    };

    let Some((string, rest)) = input[space + 1..].split_at_checked(length) else {
        return Err(Warning::CountedStringTooLong(length));
    };
    Ok((string.into(), rest))
}

/// [Level of measurement](https://en.wikipedia.org/wiki/Level_of_measurement).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
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

    fn try_decode(source: u32) -> Result<Option<Measure>, Warning> {
        match source {
            0 => Ok(None),
            1 => Ok(Some(Measure::Nominal)),
            2 => Ok(Some(Measure::Ordinal)),
            3 => Ok(Some(Measure::Scale)),
            _ => Err(Warning::InvalidMeasurement(source)),
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

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Alignment {
    Left,
    Right,
    Center,
}

impl Alignment {
    fn try_decode(source: u32) -> Result<Option<Alignment>, Warning> {
        match source {
            0 => Ok(Some(Alignment::Left)),
            1 => Ok(Some(Alignment::Right)),
            2 => Ok(Some(Alignment::Center)),
            _ => Err(Warning::InvalidAlignment(source)),
        }
    }

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

#[derive(Clone, Debug)]
pub struct VarDisplay {
    pub measure: Option<Measure>,
    pub width: Option<u32>,
    pub alignment: Option<Alignment>,
}

#[derive(Clone, Debug)]
pub struct VarDisplayRecord(pub Vec<VarDisplay>);

impl VarDisplayRecord {
    fn parse(
        ext: &Extension,
        var_types: &VarTypes,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Record, Warning> {
        if ext.size != 4 {
            return Err(Warning::BadRecordSize {
                offset: ext.offsets.start,
                record: String::from("variable display record"),
                size: ext.size,
                expected_size: 4,
            });
        }

        let n_vars = var_types.n_vars();
        let has_width = if ext.count as usize == 3 * n_vars {
            true
        } else if ext.count as usize == 2 * n_vars {
            false
        } else {
            return Err(Warning::InvalidVariableDisplayCount {
                count: ext.count as usize,
                first: 2 * n_vars,
                second: 3 * n_vars,
            });
        };

        let mut var_displays = Vec::new();
        let mut input = &ext.data[..];
        for _ in 0..n_vars {
            let measure = Measure::try_decode(endian.parse(read_bytes(&mut input).unwrap()))
                .issue_warning(warn)
                .flatten();
            let width = has_width.then(|| endian.parse(read_bytes(&mut input).unwrap()));
            let alignment = Alignment::try_decode(endian.parse(read_bytes(&mut input).unwrap()))
                .issue_warning(warn)
                .flatten();
            var_displays.push(VarDisplay {
                measure,
                width,
                alignment,
            });
        }
        Ok(Record::VarDisplay(VarDisplayRecord(var_displays)))
    }
}

#[derive(Clone, Debug)]
pub struct LongStringMissingValues<N>
where
    N: Debug,
{
    /// Variable name.
    pub var_name: N,

    /// Missing values.
    pub missing_values: Vec<RawStrArray<8>>,
}

impl LongStringMissingValues<RawString> {
    fn decode(
        &self,
        decoder: &mut Decoder,
    ) -> Result<LongStringMissingValues<Identifier>, IdError> {
        Ok(LongStringMissingValues {
            var_name: decoder.decode_identifier(&self.var_name)?,
            missing_values: self.missing_values.clone(),
        })
    }
}

#[derive(Clone, Debug)]
pub struct LongStringMissingValueRecord<N>(pub Vec<LongStringMissingValues<N>>)
where
    N: Debug;

static LONG_STRING_MISSING_VALUE_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(1),
    count: None,
    name: "long string missing values record",
};

impl LongStringMissingValueRecord<RawString> {
    fn parse(
        ext: &Extension,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Record, Warning> {
        ext.check_size(&LONG_STRING_MISSING_VALUE_RECORD)?;

        let mut input = &ext.data[..];
        let mut missing_value_set = Vec::new();
        while !input.is_empty() {
            let var_name = read_string(&mut input, endian)?;
            dbg!(&var_name);
            let n_missing_values: u8 = endian.parse(read_bytes(&mut input)?);
            let value_len: u32 = endian.parse(read_bytes(&mut input)?);
            if value_len != 8 {
                let offset = (ext.data.len() - input.len() - 8) as u64 + ext.offsets.start;
                warn(Warning::BadLongMissingValueLength {
                    record_offset: ext.offsets.start,
                    offset,
                    value_len,
                });
                read_vec(
                    &mut input,
                    dbg!(value_len as usize * n_missing_values as usize),
                )?;
                continue;
            }
            let mut missing_values = Vec::new();
            for i in 0..n_missing_values {
                if i > 0 {
                    // Tolerate files written by old, buggy versions of PSPP
                    // where we believed that the value_length was repeated
                    // before each missing value.
                    let mut peek = input;
                    let number: u32 = endian.parse(read_bytes(&mut peek)?);
                    if number == 8 {
                        input = peek;
                    }
                }

                let value: [u8; 8] = read_bytes(&mut input)?;
                missing_values.push(RawStrArray(value));
            }
            missing_value_set.push(LongStringMissingValues {
                var_name,
                missing_values,
            });
        }
        Ok(Record::LongStringMissingValues(
            LongStringMissingValueRecord(missing_value_set),
        ))
    }
}

impl LongStringMissingValueRecord<RawString> {
    pub fn decode(self, decoder: &mut Decoder) -> LongStringMissingValueRecord<Identifier> {
        let mut mvs = Vec::with_capacity(self.0.len());
        for mv in self.0.iter() {
            if let Some(mv) = mv
                .decode(decoder)
                .map_err(Warning::InvalidLongStringMissingValueVariableName)
                .issue_warning(&mut decoder.warn)
            {
                mvs.push(mv);
            }
        }
        LongStringMissingValueRecord(mvs)
    }
}

#[derive(Clone, Debug)]
pub struct EncodingRecord(pub String);

static ENCODING_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(1),
    count: None,
    name: "encoding record",
};

impl EncodingRecord {
    fn parse(ext: &Extension, _endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&ENCODING_RECORD)?;

        Ok(Record::Encoding(EncodingRecord(
            String::from_utf8(ext.data.clone()).map_err(|_| Warning::BadEncodingName {
                offset: ext.offsets.start,
            })?,
        )))
    }
}

#[derive(Clone, Debug)]
pub struct NumberOfCasesRecord {
    /// Always observed as 1.
    pub one: u64,

    /// Number of cases.
    pub n_cases: u64,
}

static NUMBER_OF_CASES_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(8),
    count: Some(2),
    name: "extended number of cases record",
};

impl NumberOfCasesRecord {
    fn parse(ext: &Extension, endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&NUMBER_OF_CASES_RECORD)?;

        let mut input = &ext.data[..];
        let one = endian.parse(read_bytes(&mut input)?);
        let n_cases = endian.parse(read_bytes(&mut input)?);

        Ok(Record::NumberOfCases(NumberOfCasesRecord { one, n_cases }))
    }
}

#[derive(Clone, Debug)]
pub struct RawVariableSetRecord(TextRecord);

impl RawVariableSetRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::VariableSets(Self(TextRecord::parse(
            extension,
            "variable sets record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> VariableSetRecord {
        let mut sets = Vec::new();
        let input = decoder.decode(&self.0.text);
        for line in input.lines() {
            if let Some(set) = VariableSet::parse(line, decoder).issue_warning(&mut decoder.warn) {
                sets.push(set)
            }
        }
        VariableSetRecord {
            offsets: self.0.offsets,
            sets,
        }
    }
}

#[derive(Clone, Debug)]
pub struct RawProductInfoRecord(TextRecord);

impl RawProductInfoRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::ProductInfo(Self(TextRecord::parse(
            extension,
            "product info record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> ProductInfoRecord {
        ProductInfoRecord(decoder.decode(&self.0.text).into())
    }
}

#[derive(Clone, Debug)]
pub struct RawLongNamesRecord(TextRecord);

impl RawLongNamesRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::LongNames(Self(TextRecord::parse(
            extension,
            "long names record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> LongNamesRecord {
        let input = decoder.decode(&self.0.text);
        let mut names = Vec::new();
        for pair in input.split('\t').filter(|s| !s.is_empty()) {
            if let Some(long_name) = LongName::parse(pair, decoder).issue_warning(&mut decoder.warn)
            {
                names.push(long_name);
            }
        }
        LongNamesRecord(names)
    }
}

#[derive(Clone, Debug)]
pub struct TextRecord {
    pub offsets: Range<u64>,

    /// The text content of the record.
    pub text: RawString,
}

impl TextRecord {
    fn parse(extension: Extension, name: &str) -> Result<TextRecord, Warning> {
        extension.check_size(&ExtensionRecord {
            size: Some(1),
            count: None,
            name,
        })?;
        Ok(Self {
            offsets: extension.offsets,
            text: extension.data.into(),
        })
    }
}

#[derive(Clone, Debug)]
pub struct VeryLongString {
    pub short_name: Identifier,
    pub length: u16,
}

impl VeryLongString {
    fn parse(decoder: &Decoder, input: &str) -> Result<VeryLongString, Warning> {
        let Some((short_name, length)) = input.split_once('=') else {
            return Err(Warning::VeryLongStringMissingDelimiter(input.into()));
        };
        let short_name = decoder
            .new_identifier(short_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(Warning::InvalidLongStringName)?;
        let length = length
            .parse()
            .map_err(|_| Warning::VeryLongStringInvalidLength(input.into()))?;
        Ok(VeryLongString { short_name, length })
    }
}

#[derive(Clone, Debug)]
pub struct RawVeryLongStringsRecord(TextRecord);

#[derive(Clone, Debug)]
pub struct VeryLongStringsRecord(pub Vec<VeryLongString>);

impl RawVeryLongStringsRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::VeryLongStrings(Self(TextRecord::parse(
            extension,
            "very long strings record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> VeryLongStringsRecord {
        let input = decoder.decode(&self.0.text);
        let mut very_long_strings = Vec::new();
        for tuple in input
            .split('\0')
            .map(|s| s.trim_start_matches('\t'))
            .filter(|s| !s.is_empty())
        {
            if let Some(vls) =
                VeryLongString::parse(decoder, tuple).issue_warning(&mut decoder.warn)
            {
                very_long_strings.push(vls)
            }
        }
        VeryLongStringsRecord(very_long_strings)
    }
}

#[derive(Clone, Debug)]
pub struct Attribute {
    pub name: Identifier,
    pub values: Vec<String>,
}

impl Attribute {
    fn parse<'a>(decoder: &mut Decoder, input: &'a str) -> Result<(Attribute, &'a str), Warning> {
        let Some((name, mut input)) = input.split_once('(') else {
            return Err(Warning::AttributeMissingLParen(input.into()));
        };
        let name = decoder
            .new_identifier(name)
            .map_err(Warning::InvalidAttributeName)?;
        let mut values = Vec::new();
        loop {
            let Some((value, rest)) = input.split_once('\n') else {
                return Err(Warning::AttributeMissingValue {
                    name: name.clone(),
                    index: values.len(),
                });
            };
            if let Some(stripped) = value
                .strip_prefix('\'')
                .and_then(|value| value.strip_suffix('\''))
            {
                values.push(stripped.into());
            } else {
                decoder.warn(Warning::AttributeMissingQuotes {
                    name: name.clone(),
                    index: values.len(),
                });
                values.push(value.into());
            }
            if let Some(rest) = rest.strip_prefix(')') {
                let attribute = Attribute { name, values };
                return Ok((attribute, rest));
            };
            input = rest;
        }
    }
}

impl Attributes {
    fn parse<'a>(
        decoder: &mut Decoder,
        mut input: &'a str,
        sentinel: Option<char>,
    ) -> Result<(Attributes, &'a str, Vec<Identifier>), Warning> {
        let mut attributes = BTreeMap::new();
        let mut duplicates = Vec::new();
        let rest = loop {
            match input.chars().next() {
                None => break input,
                c if c == sentinel => break &input[1..],
                _ => {
                    let (attribute, rest) = Attribute::parse(decoder, input)?;
                    if attributes.contains_key(&attribute.name) {
                        duplicates.push(attribute.name.clone());
                    }
                    attributes.insert(attribute.name, attribute.values);
                    input = rest;
                }
            }
        };
        Ok((Attributes(attributes), rest, duplicates))
    }
}

#[derive(Clone, Debug)]
pub struct RawFileAttributesRecord(TextRecord);

#[derive(Clone, Debug, Default)]
pub struct FileAttributesRecord(pub Attributes);

impl RawFileAttributesRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::FileAttributes(Self(TextRecord::parse(
            extension,
            "file attributes record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> FileAttributesRecord {
        let input = decoder.decode(&self.0.text);
        match Attributes::parse(decoder, &input, None).issue_warning(&mut decoder.warn) {
            Some((set, rest, duplicates)) => {
                if !duplicates.is_empty() {
                    decoder.warn(Warning::DuplicateFileAttributes {
                        attributes: duplicates,
                    });
                }
                if !rest.is_empty() {
                    decoder.warn(dbg!(Warning::TBD));
                }
                FileAttributesRecord(set)
            }
            None => FileAttributesRecord::default(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct VarAttributes {
    pub long_var_name: Identifier,
    pub attributes: Attributes,
}

impl VarAttributes {
    fn parse<'a>(
        decoder: &mut Decoder,
        input: &'a str,
    ) -> Result<(VarAttributes, &'a str), Warning> {
        let Some((long_var_name, rest)) = input.split_once(':') else {
            return Err(dbg!(Warning::TBD));
        };
        let long_var_name = decoder
            .new_identifier(long_var_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(Warning::InvalidAttributeVariableName)?;
        let (attributes, rest, duplicates) = Attributes::parse(decoder, rest, Some('/'))?;
        if !duplicates.is_empty() {
            decoder.warn(Warning::DuplicateVariableAttributes {
                variable: long_var_name.clone(),
                attributes: duplicates,
            });
        }
        let var_attribute = VarAttributes {
            long_var_name,
            attributes,
        };
        Ok((var_attribute, rest))
    }
}

#[derive(Clone, Debug)]
pub struct RawVariableAttributesRecord(TextRecord);

#[derive(Clone, Debug)]
pub struct VariableAttributesRecord(pub Vec<VarAttributes>);

impl RawVariableAttributesRecord {
    fn parse(extension: Extension) -> Result<Record, Warning> {
        Ok(Record::VariableAttributes(Self(TextRecord::parse(
            extension,
            "variable attributes record",
        )?)))
    }
    fn decode(self, decoder: &mut Decoder) -> VariableAttributesRecord {
        let decoded = decoder.decode(&self.0.text);
        let mut input = decoded.as_ref();
        let mut var_attribute_sets = Vec::new();
        while !input.is_empty() {
            let Some((var_attribute, rest)) =
                VarAttributes::parse(decoder, input).issue_warning(&mut decoder.warn)
            else {
                break;
            };
            var_attribute_sets.push(var_attribute);
            input = rest;
        }
        VariableAttributesRecord(var_attribute_sets)
    }
}

#[derive(Clone, Debug)]
pub struct LongName {
    pub short_name: Identifier,
    pub long_name: Identifier,
}

impl LongName {
    fn parse(input: &str, decoder: &Decoder) -> Result<Self, Warning> {
        let Some((short_name, long_name)) = input.split_once('=') else {
            return Err(dbg!(Warning::LongNameMissingEquals));
        };
        let short_name = decoder
            .new_identifier(short_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(Warning::InvalidShortName)?;
        let long_name = decoder
            .new_identifier(long_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(Warning::InvalidLongName)?;
        Ok(LongName {
            short_name,
            long_name,
        })
    }
}

#[derive(Clone, Debug)]
pub struct LongNamesRecord(pub Vec<LongName>);

#[derive(Clone, Debug)]
pub struct ProductInfoRecord(pub String);

#[derive(Clone, Debug)]
pub struct VariableSet {
    pub name: String,
    pub variable_names: Vec<Identifier>,
}

impl VariableSet {
    fn parse(input: &str, decoder: &mut Decoder) -> Result<Self, Warning> {
        let (name, input) = input
            .split_once('=')
            .ok_or(Warning::VariableSetMissingEquals)?;
        let mut vars = Vec::new();
        for var in input.split_ascii_whitespace() {
            if let Some(identifier) = decoder
                .new_identifier(var)
                .and_then(Identifier::must_be_ordinary)
                .map_err(Warning::InvalidVariableSetName)
                .issue_warning(&mut decoder.warn)
            {
                vars.push(identifier);
            }
        }
        Ok(VariableSet {
            name: name.to_string(),
            variable_names: vars,
        })
    }
}

#[derive(Clone, Debug)]
pub struct VariableSetRecord {
    pub offsets: Range<u64>,
    pub sets: Vec<VariableSet>,
}

trait IssueWarning<T> {
    fn issue_warning(self, warn: &mut dyn FnMut(Warning)) -> Option<T>;
}
impl<T> IssueWarning<T> for Result<T, Warning> {
    fn issue_warning(self, warn: &mut dyn FnMut(Warning)) -> Option<T> {
        match self {
            Ok(result) => Some(result),
            Err(error) => {
                warn(error);
                None
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct Extension {
    pub offsets: Range<u64>,

    /// Record subtype.
    pub subtype: u32,

    /// Size of each data element.
    pub size: u32,

    /// Number of data elements.
    pub count: u32,

    /// `size * count` bytes of data.
    pub data: Vec<u8>,
}

impl Extension {
    fn check_size(&self, expected: &ExtensionRecord) -> Result<(), Warning> {
        match expected.size {
            Some(expected_size) if self.size != expected_size => {
                return Err(Warning::BadRecordSize {
                    offset: self.offsets.start,
                    record: expected.name.into(),
                    size: self.size,
                    expected_size,
                });
            }
            _ => (),
        }
        match expected.count {
            Some(expected_count) if self.count != expected_count => {
                return Err(Warning::BadRecordCount {
                    offset: self.offsets.start,
                    record: expected.name.into(),
                    count: self.count,
                    expected_count,
                });
            }
            _ => (),
        }
        Ok(())
    }

    fn read<R: Read + Seek>(
        r: &mut R,
        endian: Endian,
        var_types: &VarTypes,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<Record>, Error> {
        let subtype = endian.parse(read_bytes(r)?);
        let header_offset = r.stream_position()?;
        let size: u32 = endian.parse(read_bytes(r)?);
        let count = endian.parse(read_bytes(r)?);
        let Some(product) = size.checked_mul(count) else {
            return Err(Error::ExtensionRecordTooLarge {
                offset: header_offset,
                subtype,
                size,
                count,
            });
        };
        let start_offset = r.stream_position()?;
        let data = read_vec(r, product as usize)?;
        let end_offset = start_offset + product as u64;
        let extension = Extension {
            offsets: start_offset..end_offset,
            subtype,
            size,
            count,
            data,
        };
        let result = match subtype {
            3 => IntegerInfoRecord::parse(&extension, endian),
            4 => FloatInfoRecord::parse(&extension, endian),
            11 => VarDisplayRecord::parse(&extension, var_types, endian, warn),
            7 | 19 => MultipleResponseRecord::parse(&extension, endian),
            21 => LongStringValueLabelRecord::parse(&extension, endian),
            22 => LongStringMissingValueRecord::parse(&extension, endian, warn),
            20 => EncodingRecord::parse(&extension, endian),
            16 => NumberOfCasesRecord::parse(&extension, endian),
            5 => RawVariableSetRecord::parse(extension),
            10 => RawProductInfoRecord::parse(extension),
            13 => RawLongNamesRecord::parse(extension),
            14 => RawVeryLongStringsRecord::parse(extension),
            17 => RawFileAttributesRecord::parse(extension),
            18 => RawVariableAttributesRecord::parse(extension),
            _ => Ok(Record::OtherExtension(extension)),
        };
        match result {
            Ok(result) => Ok(Some(result)),
            Err(error) => {
                warn(error);
                Ok(None)
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct ZHeader {
    /// File offset to the start of the record.
    pub offset: u64,

    /// File offset to the ZLIB data header.
    pub zheader_offset: u64,

    /// File offset to the ZLIB trailer.
    pub ztrailer_offset: u64,

    /// Length of the ZLIB trailer in bytes.
    pub ztrailer_len: u64,
}

impl ZHeader {
    fn read<R: Read + Seek>(r: &mut R, endian: Endian) -> Result<ZHeader, Error> {
        let offset = r.stream_position()?;
        let zheader_offset: u64 = endian.parse(read_bytes(r)?);
        let ztrailer_offset: u64 = endian.parse(read_bytes(r)?);
        let ztrailer_len: u64 = endian.parse(read_bytes(r)?);

        if zheader_offset != offset {
            return Err(Error::UnexpectedZHeaderOffset {
                actual: zheader_offset,
                expected: offset,
            });
        }

        if ztrailer_offset < offset {
            return Err(Error::ImpossibleZTrailerOffset(ztrailer_offset));
        }

        if ztrailer_len < 24 || ztrailer_len % 24 != 0 {
            return Err(Error::InvalidZTrailerLength(ztrailer_len));
        }

        Ok(ZHeader {
            offset,
            zheader_offset,
            ztrailer_offset,
            ztrailer_len,
        })
    }
}

#[derive(Clone, Debug)]
pub struct ZTrailer {
    /// File offset to the start of the record.
    pub offset: u64,

    /// Compression bias as a negative integer, e.g. -100.
    pub int_bias: i64,

    /// Always observed as zero.
    pub zero: u64,

    /// Uncompressed size of each block, except possibly the last.  Only
    /// `0x3ff000` has been observed so far.
    pub block_size: u32,

    /// Block descriptors, always `(ztrailer_len - 24) / 24)` of them.
    pub blocks: Vec<ZBlock>,
}

#[derive(Clone, Debug)]
pub struct ZBlock {
    /// Offset of block of data if simple compression were used.
    pub uncompressed_ofs: u64,

    /// Actual offset within the file of the compressed data block.
    pub compressed_ofs: u64,

    /// The number of bytes in this data block after decompression.  This is
    /// `block_size` in every data block but the last, which may be smaller.
    pub uncompressed_size: u32,

    /// The number of bytes in this data block, as stored compressed in this
    /// file.
    pub compressed_size: u32,
}

impl ZBlock {
    fn read<R: Read + Seek>(r: &mut R, endian: Endian) -> Result<ZBlock, Error> {
        Ok(ZBlock {
            uncompressed_ofs: endian.parse(read_bytes(r)?),
            compressed_ofs: endian.parse(read_bytes(r)?),
            uncompressed_size: endian.parse(read_bytes(r)?),
            compressed_size: endian.parse(read_bytes(r)?),
        })
    }
}

impl ZTrailer {
    fn read<R: Read + Seek>(
        reader: &mut R,
        endian: Endian,
        bias: f64,
        zheader: &ZHeader,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<ZTrailer>, Error> {
        let start_offset = reader.stream_position()?;
        if reader
            .seek(SeekFrom::Start(zheader.ztrailer_offset))
            .is_err()
        {
            return Ok(None);
        }
        let int_bias = endian.parse(read_bytes(reader)?);
        if int_bias as f64 != -bias {
            return Err(Error::WrongZlibTrailerBias {
                actual: int_bias,
                expected: -bias,
            });
        }
        let zero = endian.parse(read_bytes(reader)?);
        if zero != 0 {
            return Err(Error::WrongZlibTrailerZero(zero));
        }
        let block_size = endian.parse(read_bytes(reader)?);
        if block_size != 0x3ff000 {
            return Err(Error::WrongZlibTrailerBlockSize(block_size));
        }
        let n_blocks: u32 = endian.parse(read_bytes(reader)?);
        let expected_n_blocks = (zheader.ztrailer_len - 24) / 24;
        if n_blocks as u64 != expected_n_blocks {
            return Err(Error::BadZlibTrailerNBlocks {
                offset: zheader.ztrailer_offset,
                n_blocks,
                expected_n_blocks,
                ztrailer_len: zheader.ztrailer_len,
            });
        }
        let blocks = (0..n_blocks)
            .map(|_| ZBlock::read(reader, endian))
            .collect::<Result<Vec<_>, _>>()?;

        let mut expected_uncmp_ofs = zheader.zheader_offset;
        let mut expected_cmp_ofs = zheader.zheader_offset + 24;
        for (index, block) in blocks.iter().enumerate() {
            if block.uncompressed_ofs != expected_uncmp_ofs {
                return Err(Error::ZlibTrailerBlockWrongUncmpOfs {
                    index,
                    actual: block.uncompressed_ofs,
                    expected: expected_cmp_ofs,
                });
            }
            if block.compressed_ofs != expected_cmp_ofs {
                return Err(Error::ZlibTrailerBlockWrongCmpOfs {
                    index,
                    actual: block.compressed_ofs,
                    expected: expected_cmp_ofs,
                });
            }
            if index < blocks.len() - 1 {
                if block.uncompressed_size != block_size {
                    warn(Warning::ZlibTrailerBlockWrongSize {
                        index,
                        actual: block.uncompressed_size,
                        expected: block_size,
                    });
                }
            } else {
                if block.uncompressed_size > block_size {
                    warn(Warning::ZlibTrailerBlockTooBig {
                        index,
                        actual: block.uncompressed_size,
                        max_expected: block_size,
                    });
                }
            }
            // http://www.zlib.net/zlib_tech.html says that the maximum
            // expansion from compression, with worst-case parameters, is 13.5%
            // plus 11 bytes.  This code checks for an expansion of more than
            // 14.3% plus 11 bytes.
            if block.compressed_size > block.uncompressed_size + block.uncompressed_size / 7 + 11 {
                return Err(Error::ZlibExpansion {
                    index,
                    compressed_size: block.compressed_size,
                    uncompressed_size: block.uncompressed_size,
                });
            }

            expected_cmp_ofs += block.compressed_size as u64;
            expected_uncmp_ofs += block.uncompressed_size as u64;
        }

        if expected_cmp_ofs != zheader.ztrailer_offset {
            return Err(Error::ZlibTrailerOffsetInconsistency {
                descriptors: expected_cmp_ofs,
                zheader: zheader.ztrailer_offset,
            });
        }

        reader.seek(SeekFrom::Start(start_offset))?;
        Ok(Some(ZTrailer {
            offset: zheader.ztrailer_offset,
            int_bias,
            zero,
            block_size,
            blocks,
        }))
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

fn read_string<R: Read>(r: &mut R, endian: Endian) -> Result<RawString, IoError> {
    let length: u32 = endian.parse(read_bytes(r)?);
    Ok(read_vec(r, length as usize)?.into())
}

#[derive(Clone, Debug)]
pub struct LongStringValueLabels<N, S>
where
    S: Debug,
{
    pub var_name: N,
    pub width: u32,

    /// `(value, label)` pairs, where each value is `width` bytes.
    pub labels: Vec<(RawString, S)>,
}

impl LongStringValueLabels<RawString, RawString> {
    fn decode(
        &self,
        decoder: &mut Decoder,
    ) -> Result<LongStringValueLabels<Identifier, String>, Warning> {
        let var_name = decoder.decode(&self.var_name);
        let var_name = Identifier::from_encoding(var_name.trim_end(), decoder.encoding)
            .map_err(Warning::InvalidLongStringValueLabelName)?;

        let mut labels = Vec::with_capacity(self.labels.len());
        for (value, label) in self.labels.iter() {
            let label = decoder.decode(label).to_string();
            labels.push((value.clone(), label));
        }

        Ok(LongStringValueLabels {
            var_name,
            width: self.width,
            labels,
        })
    }
}

#[derive(Clone, Debug)]
pub struct LongStringValueLabelRecord<N, S>(pub Vec<LongStringValueLabels<N, S>>)
where
    N: Debug,
    S: Debug;

static LONG_STRING_VALUE_LABEL_RECORD: ExtensionRecord = ExtensionRecord {
    size: Some(1),
    count: None,
    name: "long string value labels record",
};

impl LongStringValueLabelRecord<RawString, RawString> {
    fn parse(ext: &Extension, endian: Endian) -> Result<Record, Warning> {
        ext.check_size(&LONG_STRING_VALUE_LABEL_RECORD)?;

        let mut input = &ext.data[..];
        let mut label_set = Vec::new();
        while !input.is_empty() {
            let var_name = read_string(&mut input, endian)?;
            let width: u32 = endian.parse(read_bytes(&mut input)?);
            let n_labels: u32 = endian.parse(read_bytes(&mut input)?);
            let mut labels = Vec::new();
            for _ in 0..n_labels {
                let value = read_string(&mut input, endian)?;
                let label = read_string(&mut input, endian)?;
                labels.push((value, label));
            }
            label_set.push(LongStringValueLabels {
                var_name,
                width,
                labels,
            })
        }
        Ok(Record::LongStringValueLabels(LongStringValueLabelRecord(
            label_set,
        )))
    }
}

impl LongStringValueLabelRecord<RawString, RawString> {
    fn decode(self, decoder: &mut Decoder) -> LongStringValueLabelRecord<Identifier, String> {
        let mut labels = Vec::with_capacity(self.0.len());
        for label in &self.0 {
            match label.decode(decoder) {
                Ok(set) => labels.push(set),
                Err(error) => decoder.warn(error),
            }
        }
        LongStringValueLabelRecord(labels)
    }
}

#[derive(Default)]
pub struct VarTypes {
    pub types: Vec<Option<VarWidth>>,
}

impl VarTypes {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn push(&mut self, width: RawWidth) {
        if let Ok(var_width) = VarWidth::try_from(width) {
            self.types.push(Some(var_width));
            for _ in 1..width.n_values().unwrap() {
                self.types.push(None);
            }
        }
    }

    pub fn n_values(&self) -> usize {
        self.types.len()
    }

    pub fn is_valid_index(&self, index: usize) -> bool {
        self.var_type_at(index).is_some()
    }

    pub fn var_type_at(&self, index: usize) -> Option<VarType> {
        if index >= 1 && index <= self.types.len() {
            self.types[index - 1].map(VarType::from)
        } else {
            None
        }
    }

    pub fn n_vars(&self) -> usize {
        self.types.iter().flatten().count()
    }
}
