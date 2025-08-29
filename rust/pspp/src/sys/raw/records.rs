//! Raw records.
//!
//! Separated into a submodule just to reduce clutter.

use std::{
    borrow::Cow,
    collections::BTreeMap,
    fmt::{Debug, Display, Formatter},
    io::{Cursor, ErrorKind, Read, Seek, SeekFrom},
    ops::Range,
    str::from_utf8,
};

use crate::{
    data::{ByteStrArray, ByteString, Datum},
    dictionary::CategoryLabels,
    endian::FromBytes,
    format::{DisplayPlain, Format, Type},
    identifier::{Error as IdError, Identifier},
    sys::{
        raw::{
            read_bytes, read_string, read_vec, Decoder, Error, ErrorDetails, Magic, RawDatum,
            RawWidth, Record, RecordString, UntypedDatum, VarTypes, Warning, WarningDetails,
        },
        serialize_endian, ProductVersion,
    },
    variable::{
        Alignment, Attributes, Measure, MissingValueRange, MissingValues, MissingValuesError,
        VarType, VarWidth,
    },
};

use binrw::{binrw, BinRead, BinWrite, Endian, Error as BinError};
use clap::ValueEnum;
use encoding_rs::Encoding;
use itertools::Itertools;
use serde::{ser::SerializeTuple, Serialize, Serializer};
use thiserror::Error as ThisError;

/// Type of compression in a system file.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Serialize, ValueEnum)]
pub enum Compression {
    /// Simple bytecode-based compression.
    Simple,
    /// [ZLIB] compression.
    ///
    /// [ZLIB]: https://www.zlib.net/
    #[value(name = "zlib", help = "ZLIB space-efficient compression")]
    ZLib,
}

/// A warning for a file header.
#[derive(ThisError, Debug)]
pub enum HeaderWarning {
    /// Unexpected compression bias.
    #[error("Compression bias is {0} instead of the usual values of 0 or 100.")]
    UnexpectedBias(f64),
}

/// A file header record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct FileHeader<S>
where
    S: Debug + Serialize,
{
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
    #[serde(serialize_with = "serialize_endian")]
    pub endian: Endian,
}

/// Raw file header.
#[derive(BinRead, BinWrite)]
pub struct RawHeader {
    /// Magic number.
    pub magic: [u8; 4],

    /// Eye-catcher string and product name.
    pub eye_catcher: [u8; 60],

    /// Layout code, normally either 2 or 3.
    pub layout_code: u32,

    /// Claimed number of variable positions (not always accurate).
    pub nominal_case_size: u32,

    /// Compression type.
    pub compression_code: u32,

    /// 1-based variable index of the weight variable, or 0 if the file is
    /// unweighted.
    pub weight_index: u32,

    /// Claimed number of cases, or [u32::MAX] if unknown.
    pub n_cases: u32,

    /// Compression bias, usually 100.0.
    pub bias: f64,

    /// `dd mmm yy` in the file's encoding.
    pub creation_date: [u8; 9],

    /// `HH:MM:SS` in the file's encoding.
    pub creation_time: [u8; 8],

    /// File label, in the file's encoding.  Padded on the right with spaces.
    #[brw(pad_after = 3)]
    pub file_label: [u8; 64],
}

impl FileHeader<ByteString> {
    /// Reads a header record from `r`, reporting any warnings via `warn`.
    pub fn read<R>(r: &mut R, warn: &mut dyn FnMut(Warning)) -> Result<Self, Error>
    where
        R: Read + Seek,
    {
        let header_bytes = read_vec(r, 176).map_err(|e| {
            Error::new(
                None,
                if e.kind() == ErrorKind::UnexpectedEof {
                    ErrorDetails::NotASystemFile
                } else {
                    e.into()
                },
            )
        })?;
        Self::read_inner(&header_bytes, warn).map_err(|details| Error::new(Some(0..176), details))
    }

    fn read_inner(
        header_bytes: &[u8],
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Self, ErrorDetails> {
        if &header_bytes[8..20] == b"ENCRYPTEDSAV" {
            return Err(ErrorDetails::Encrypted);
        }

        let be_header = RawHeader::read_be(&mut Cursor::new(&header_bytes)).unwrap();
        let le_header = RawHeader::read_le(&mut Cursor::new(&header_bytes)).unwrap();

        let magic: Magic = be_header
            .magic
            .try_into()
            .map_err(|_| ErrorDetails::NotASystemFile)?;

        let (endian, header) = if be_header.layout_code == 2 {
            (Endian::Big, &be_header)
        } else if le_header.layout_code == 2 {
            (Endian::Little, &le_header)
        } else {
            return Err(ErrorDetails::NotASystemFile);
        };

        let nominal_case_size = (1..i32::MAX.cast_unsigned() / 16)
            .contains(&header.nominal_case_size)
            .then_some(header.nominal_case_size);

        let compression = match (magic, header.compression_code) {
            (Magic::Zsav, 2) => Some(Compression::ZLib),
            (Magic::Zsav, code) => return Err(ErrorDetails::InvalidZsavCompression(code)),
            (_, 0) => None,
            (_, 1) => Some(Compression::Simple),
            (_, code) => return Err(ErrorDetails::InvalidSavCompression(code)),
        };

        let weight_index = (header.weight_index > 0).then_some(header.weight_index);

        let n_cases = (header.n_cases <= u32::MAX / 2).then_some(header.n_cases);

        if header.bias != 100.0 && header.bias != 0.0 {
            warn(Warning::new(
                Some(84..92),
                HeaderWarning::UnexpectedBias(header.bias),
            ));
        }

        Ok(FileHeader {
            magic,
            layout_code: header.layout_code,
            nominal_case_size,
            compression,
            weight_index,
            n_cases,
            bias: header.bias,
            creation_date: header.creation_date.into(),
            creation_time: header.creation_time.into(),
            eye_catcher: header.eye_catcher.into(),
            file_label: header.file_label.into(),
            endian,
        })
    }

    /// Decodes this record with `decoder` and returns the decoded version.
    pub fn decode(self, decoder: &mut Decoder) -> FileHeader<String> {
        let eye_catcher = decoder.decode(&self.eye_catcher).to_string();
        let file_label = decoder.decode(&self.file_label).to_string();
        let creation_date = decoder.decode(&self.creation_date).to_string();
        let creation_time = decoder.decode(&self.creation_time).to_string();
        FileHeader {
            eye_catcher,
            weight_index: self.weight_index,
            n_cases: self.n_cases,
            file_label,
            magic: self.magic,
            layout_code: self.layout_code,
            nominal_case_size: self.nominal_case_size,
            compression: self.compression,
            bias: self.bias,
            creation_date,
            creation_time,
            endian: self.endian,
        }
    }

    /// Returns [RecordString]s for this file header.
    pub fn get_strings(&self) -> Vec<RecordString> {
        vec![
            RecordString::new("Product", &self.eye_catcher.0[5..], false),
            RecordString::new("File Label", &self.file_label, false),
        ]
    }
}

/// [Format] as represented in a system file.
#[derive(Copy, Clone, PartialEq, Eq, Hash, BinRead, BinWrite)]
pub struct RawFormat(
    /// The most-significant 16 bits are the type, the next 8 bytes are the
    /// width, and the least-significant 8 bits are the number of decimals.
    pub u32,
);

/// Cannot convert very long string (wider than 255 bytes) to [RawFormat].
#[derive(Copy, Clone, Debug)]
pub struct VeryLongStringError;

impl TryFrom<Format> for RawFormat {
    type Error = VeryLongStringError;

    fn try_from(value: Format) -> Result<Self, Self::Error> {
        let type_ = u16::from(value.type_()) as u32;
        let w = match value.var_width() {
            VarWidth::Numeric => value.w() as u8,
            VarWidth::String(w) if w > 255 => return Err(VeryLongStringError),
            VarWidth::String(w) if value.type_() == Type::AHex => (w * 2).min(255) as u8,
            VarWidth::String(w) => w as u8,
        } as u32;
        let d = value.d() as u32;
        Ok(Self((type_ << 16) | (w << 8) | d))
    }
}

struct RawFormatDisplayMeaning(RawFormat);

impl Display for RawFormatDisplayMeaning {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        let type_ = format_name(self.0 .0 >> 16);
        let w = (self.0 .0 >> 8) & 0xff;
        let d = self.0 .0 & 0xff;
        write!(f, "{type_}{w}.{d}")
    }
}

impl Debug for RawFormat {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        write!(f, "{:06x} ({})", self.0, RawFormatDisplayMeaning(*self))
    }
}

impl Serialize for RawFormat {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut tuple = serializer.serialize_tuple(2)?;
        tuple.serialize_element(&self.0)?;
        tuple.serialize_element(&RawFormatDisplayMeaning(*self).to_string())?;
        tuple.end()
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

/// Missing values in a [VariableRecord].
///
/// This is the format used before we know the character encoding for the system
/// file.
#[derive(Clone, Debug, Default, Serialize)]
pub struct RawMissingValues {
    /// Individual missing values, up to 3 of them.
    pub values: Vec<Datum<ByteString>>,

    /// Optional range of missing values.
    pub range: Option<MissingValueRange>,
}

impl RawMissingValues {
    /// Constructs new raw missing values.
    pub fn new(values: Vec<Datum<ByteString>>, range: Option<MissingValueRange>) -> Self {
        Self { values, range }
    }

    fn read<R>(
        r: &mut R,
        offsets: Range<u64>,
        raw_width: RawWidth,
        code: i32,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Self, Error>
    where
        R: Read + Seek,
    {
        let (individual_values, has_range) = match code {
            0 => return Ok(Self::default()),
            1..=3 => (code as usize, false),
            -2 => (0, true),
            -3 => (1, true),
            _ => {
                return Err(Error::new(
                    Some(offsets),
                    ErrorDetails::BadMissingValueCode(code),
                ))
            }
        };

        Self::read_inner(
            r,
            offsets.clone(),
            raw_width,
            individual_values,
            has_range,
            endian,
            warn,
        )
        .map_err(|details| {
            Error::new(
                {
                    let n = individual_values + if has_range { 2 } else { 0 };
                    Some(offsets.start..offsets.end + 8 * n as u64)
                },
                details,
            )
        })
    }

    fn read_inner<R>(
        r: &mut R,
        offsets: Range<u64>,
        raw_width: RawWidth,
        individual_values: usize,
        has_range: bool,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Self, ErrorDetails>
    where
        R: Read + Seek,
    {
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
                return Ok(Self::new(values, range));
            }
            Ok(VarWidth::String(_)) if range.is_some() => warn(Warning::new(
                Some(offsets),
                VariableWarning::MissingValueStringRange,
            )),
            Ok(VarWidth::String(width)) => {
                let width = width.min(8) as usize;
                let values = values
                    .into_iter()
                    .map(|value| Datum::String(ByteString::from(&value[..width])))
                    .collect();
                return Ok(Self::new(values, None));
            }
            Err(()) => warn(Warning::new(
                Some(offsets),
                VariableWarning::MissingValueContinuation,
            )),
        }
        Ok(Self::default())
    }

    /// Returns [MissingValues] for these raw missing values, using `encoding`.
    pub fn decode(&self, encoding: &'static Encoding) -> Result<MissingValues, MissingValuesError> {
        MissingValues::new(
            self.values
                .iter()
                .map(|datum| datum.clone().with_encoding(encoding))
                .collect(),
            self.range,
        )
    }
}

/// Warning for a variable record.
#[derive(ThisError, Debug)]
pub enum VariableWarning {
    /// Missing value record with range not allowed for string variable.
    #[error("Missing value record with range not allowed for string variable.")]
    MissingValueStringRange,

    /// Missing value not allowed for long string continuation.
    #[error("Missing value not allowed for long string continuation")]
    MissingValueContinuation,
}

/// A variable record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VariableRecord<S>
where
    S: Debug + Serialize,
{
    /// Range of offsets in file.
    pub offsets: Range<u64>,

    /// Variable width, in the range -1..=255.
    pub width: RawWidth,

    /// Variable name, padded on the right with spaces.
    pub name: S,

    /// Print format.
    pub print_format: RawFormat,

    /// Write format.
    pub write_format: RawFormat,

    /// Missing values.
    pub missing_values: RawMissingValues,

    /// Optional variable label.
    pub label: Option<S>,
}

/// Raw variable record.
#[derive(BinRead, BinWrite)]
pub struct RawVariableRecord {
    /// Variable width, in the range -1..=255.
    pub width: i32,

    /// 1 if the variable has a label, 0 otherwise.
    pub has_variable_label: u32,

    /// - 0 for no missing values.
    /// - 1 for one missing value.
    /// - 2 for two missing values.
    /// - 3 for three missing values.
    /// - -2 for a range of missing values.
    /// - -3 for an individual missing value plus a range.
    pub missing_value_code: i32,

    /// Print format.
    pub print_format: RawFormat,

    /// Write format.
    pub write_format: RawFormat,

    /// Variable name, padded with spaces.
    pub name: [u8; 8],
}

impl VariableRecord<ByteString> {
    /// Reads a variable record from `r`.
    pub fn read<R>(r: &mut R, endian: Endian, warn: &mut dyn FnMut(Warning)) -> Result<Self, Error>
    where
        R: Read + Seek,
    {
        let start_offset = r.stream_position()?;
        let offsets = start_offset..start_offset + 28;
        let raw_record =
            read_vec(r, 28).map_err(|e| Error::new(Some(offsets.clone()), e.into()))?;
        let raw_record =
            RawVariableRecord::read_options(&mut Cursor::new(&raw_record), endian, ()).unwrap();

        let width: RawWidth = raw_record.width.try_into().map_err(|_| {
            Error::new(
                Some(offsets.clone()),
                ErrorDetails::BadVariableWidth(raw_record.width),
            )
        })?;

        let label = match raw_record.has_variable_label {
            0 => None,
            1 => {
                let len: u32 = endian.parse(read_bytes(r)?);
                let read_len = len.min(65535) as usize;
                let label = read_vec(r, read_len)?;

                let padding_bytes = len.next_multiple_of(4) - len;
                let _ = read_vec(r, padding_bytes as usize)?;

                Some(label.into())
            }
            _ => {
                return Err(Error::new(
                    Some(offsets),
                    ErrorDetails::BadVariableLabelCode(raw_record.has_variable_label),
                ));
            }
        };

        let missing_values = RawMissingValues::read(
            r,
            offsets,
            width,
            raw_record.missing_value_code,
            endian,
            warn,
        )?;

        let end_offset = r.stream_position()?;

        Ok(Self {
            offsets: start_offset..end_offset,
            width,
            name: raw_record.name.into(),
            print_format: raw_record.print_format,
            write_format: raw_record.write_format,
            missing_values,
            label,
        })
    }

    /// Decodes a variable record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> VariableRecord<String> {
        VariableRecord {
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
        }
    }
}

/// Warning for a value label record.
#[derive(ThisError, Debug)]
pub enum ValueLabelWarning {
    /// At least one valid variable index for value labels is required but none were specified.
    #[error("At least one valid variable index is required but none were specified.")]
    NoVarIndexes,

    /// Mixed variable types in value label record.
    #[error("First variable index is for a {var_type} variable but the following variable indexes are for {} variables: {wrong_types:?}", !var_type)]
    MixedVarTypes {
        /// Variable type.
        var_type: VarType,
        /// Indexes of variables with the other type.
        wrong_types: Vec<u32>,
    },

    /// Value label invalid variable indexes.
    #[error(
        "One or more variable indexes were not in the valid range [1,{max}] or referred to string continuations: {invalid:?}"
    )]
    InvalidVarIndexes {
        /// Maximum variable index.
        max: usize,
        /// Invalid variable indexes.
        invalid: Vec<u32>,
    },
}

/// A value and label in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct ValueLabel<D, S>
where
    D: Debug + Serialize,
    S: Debug + Serialize,
{
    /// The value being labeled.
    pub datum: D,
    /// The label.
    pub label: S,
}

/// A value label record in a system file.
///
/// This represents both the type-3 and type-4 records together, since they are
/// always paired anyway.
#[derive(Clone, Debug, Serialize)]
pub struct ValueLabelRecord<D, S>
where
    D: Debug + Serialize,
    S: Debug + Serialize,
{
    /// Range of offsets in file.
    pub offsets: Range<u64>,

    /// The labels.
    pub labels: Vec<ValueLabel<D, S>>,

    /// The 1-based indexes of the variable indexes.
    pub dict_indexes: Vec<u32>,

    /// The types of the variables.
    pub var_type: VarType,
}

impl<D, S> ValueLabelRecord<D, S>
where
    D: Debug + Serialize,
    S: Debug + Serialize,
{
    /// Maximum number of value labels in a record.
    pub const MAX_LABELS: u32 = u32::MAX / 8;

    /// Maximum number of variable indexes in a record.
    pub const MAX_INDEXES: u32 = u32::MAX / 8;
}

impl ValueLabelRecord<RawDatum, ByteString> {
    /// Reads a value label record from `r`, with the given `endian`, given that
    /// the variables in the system file have the types in `var_types`, and
    /// using `warn` to report warnings.
    pub fn read<R: Read + Seek>(
        r: &mut R,
        endian: Endian,
        var_types: &VarTypes,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<Self>, Error> {
        let label_offset = r.stream_position()?;
        let n: u32 = endian.parse(read_bytes(r)?);
        if n > Self::MAX_LABELS {
            return Err(Error::new(
                Some(label_offset..label_offset + 4),
                ErrorDetails::BadNumberOfValueLabels {
                    n,
                    max: Self::MAX_LABELS,
                },
            ));
        }

        let mut labels = Vec::new();
        for _ in 0..n {
            let value = UntypedDatum(read_bytes(r)?);
            let label_len: u8 = endian.parse(read_bytes(r)?);
            let label_len = label_len as usize;
            let padded_len = (label_len + 1).next_multiple_of(8);

            let mut label = read_vec(r, padded_len - 1)?;
            label.truncate(label_len);
            labels.push((value, label.into()));
        }

        let index_offset = r.stream_position()?;
        let rec_type: u32 = endian.parse(read_bytes(r)?);
        if rec_type != 4 {
            return Err(Error::new(
                Some(index_offset..index_offset + 4),
                ErrorDetails::ExpectedVarIndexRecord(rec_type),
            ));
        }

        let n: u32 = endian.parse(read_bytes(r)?);
        let n_offsets = index_offset + 4..index_offset + 8;
        if n > Self::MAX_INDEXES {
            return Err(Error::new(
                Some(n_offsets),
                ErrorDetails::TooManyVarIndexes {
                    n,
                    max: Self::MAX_INDEXES,
                },
            ));
        } else if n == 0 {
            warn(Warning::new(
                Some(n_offsets),
                ValueLabelWarning::NoVarIndexes,
            ));
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
        let index_offsets = index_offset..r.stream_position()?;
        if !invalid_indexes.is_empty() {
            warn(Warning::new(
                Some(index_offsets.clone()),
                ValueLabelWarning::InvalidVarIndexes {
                    max: var_types.n_values(),
                    invalid: invalid_indexes,
                },
            ));
        }

        let Some(&first_index) = dict_indexes.first() else {
            return Ok(None);
        };
        let var_type = var_types.var_type_at(first_index as usize).unwrap();
        let mut wrong_type_indexes = Vec::new();
        dict_indexes.retain(|&index| {
            if var_types.var_type_at(index as usize) != Some(var_type) {
                wrong_type_indexes.push(index);
                false
            } else {
                true
            }
        });
        if !wrong_type_indexes.is_empty() {
            warn(Warning::new(
                Some(index_offsets),
                ValueLabelWarning::MixedVarTypes {
                    var_type,
                    wrong_types: wrong_type_indexes,
                },
            ));
        }

        let labels = labels
            .into_iter()
            .map(|(value, label)| ValueLabel {
                datum: RawDatum::from_raw(&value, var_type, endian),
                label,
            })
            .collect();

        let end_offset = r.stream_position()?;
        Ok(Some(ValueLabelRecord {
            offsets: label_offset..end_offset,
            labels,
            dict_indexes,
            var_type,
        }))
    }

    /// Decodes a value label record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> ValueLabelRecord<RawDatum, String> {
        let labels = self
            .labels
            .iter()
            .map(
                |ValueLabel {
                     datum: value,
                     label,
                 }| ValueLabel {
                    datum: *value,
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

/// A document record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct DocumentRecord<S>
where
    S: Debug + Serialize,
{
    /// The range of file offsets occupied by the record.
    pub offsets: Range<u64>,

    /// The document, as an array of lines.  Raw lines are exactly 80 bytes long
    /// and are right-padded with spaces without any new-line termination.
    pub lines: Vec<S>,
}

/// One line in a document.
pub type RawDocumentLine = ByteStrArray<DOC_LINE_LEN>;

/// Length of a line in a document.  Document lines are fixed-length and
/// padded on the right with spaces.
pub const DOC_LINE_LEN: usize = 80;

impl DocumentRecord<RawDocumentLine> {
    /// Maximum number of lines we will accept in a document.  This is simply
    /// the maximum number that will fit in a 32-bit space.
    pub const MAX_LINES: usize = i32::MAX as usize / DOC_LINE_LEN;

    /// Reads a document record from `r`.
    pub fn read<R>(r: &mut R, endian: Endian) -> Result<Self, Error>
    where
        R: Read + Seek,
    {
        let start_offset = r.stream_position()?;
        let n: u32 = endian.parse(read_bytes(r)?);
        let n = n as usize;
        if n > Self::MAX_LINES {
            Err(Error::new(
                Some(start_offset..start_offset + 4),
                ErrorDetails::BadDocumentLength {
                    n,
                    max: Self::MAX_LINES,
                },
            ))
        } else {
            let offsets = start_offset..start_offset.saturating_add((n * DOC_LINE_LEN) as u64);
            let mut lines = Vec::with_capacity(n);
            for _ in 0..n {
                lines.push(ByteStrArray(
                    read_bytes(r).map_err(|e| Error::new(Some(offsets.clone()), e.into()))?,
                ));
            }
            Ok(DocumentRecord { offsets, lines })
        }
    }

    /// Decodes the document record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> DocumentRecord<String> {
        DocumentRecord {
            offsets: self.offsets.clone(),
            lines: self
                .lines
                .iter()
                .map(|s| decoder.decode_slice(&s.0).to_string())
                .collect(),
        }
    }
}

/// Constraints on an extension record in a system file.
pub struct ExtensionRecord<'a> {
    /// The allowed size for elements in the extension record, or `None` to not
    /// enforce a particular size.
    pub size: Option<u32>,

    /// The allowed number elements in the extension record, or `None` to not
    /// enforce a particular count.
    pub count: Option<u32>,

    /// The name of the record, for error messages.
    pub name: &'a str,
}

/// An integer info record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct IntegerInfoRecord {
    /// File offsets occupied by the record.
    pub offsets: Range<u64>,

    /// Details.
    #[serde(flatten)]
    pub inner: RawIntegerInfoRecord,
}

/// Machine integer info record in [mod@binrw] format.
#[derive(Clone, Debug, BinRead, BinWrite, Serialize)]
pub struct RawIntegerInfoRecord {
    /// Version number.
    pub version: ProductVersion,

    /// Identifies the type of machine.
    ///
    /// Mostly useless.  PSPP uses value -1.
    pub machine_code: i32,

    /// Floating point representation (1 for IEEE 754).
    pub floating_point_rep: i32,

    /// [Compression].
    pub compression_code: i32,

    /// Endianness.
    pub endianness: i32,

    /// Character encoding (usually a code page number).
    pub character_code: i32,
}

impl IntegerInfoRecord {
    /// Parses this record from `ext`.
    pub fn parse(ext: &Extension, endian: Endian) -> Result<Record, WarningDetails> {
        ext.check_size(Some(4), Some(8), "integer record")?;

        let inner =
            RawIntegerInfoRecord::read_options(&mut Cursor::new(ext.data.as_slice()), endian, ())
                .unwrap();
        Ok(Record::IntegerInfo(IntegerInfoRecord {
            offsets: ext.offsets.clone(),
            inner,
        }))
    }
}

impl FloatInfoRecord {
    /// Parses this record from `ext`.
    pub fn parse(ext: &Extension, endian: Endian) -> Result<Record, WarningDetails> {
        ext.check_size(Some(8), Some(3), "floating point record")?;

        let data = FloatInfoRecord::read_options(&mut Cursor::new(ext.data.as_slice()), endian, ())
            .unwrap();
        Ok(Record::FloatInfo(data))
    }
}

/// A floating-point info record.
#[derive(Clone, Debug, BinRead, BinWrite, Serialize)]
pub struct FloatInfoRecord {
    /// Value used for system-missing values.
    pub sysmis: f64,

    /// Highest numeric value (e.g. [f64::MAX]).
    pub highest: f64,

    /// Smallest numeric value (e.g. -[f64::MAX]).
    pub lowest: f64,
}

/// Long variable names record.
#[derive(Clone, Debug, Serialize)]
pub struct RawLongNamesRecord(
    /// Text contents of record.
    pub TextRecord,
);

impl RawLongNamesRecord {
    /// Parses this record from `extension`.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::LongNames(Self(TextRecord::parse(
            extension,
            "long names record",
        )?)))
    }

    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> LongNamesRecord {
        let input = decoder.decode(&self.0.text);
        let mut names = Vec::new();
        for pair in input.split('\t').filter(|s| !s.is_empty()) {
            if let Some(long_name) =
                LongName::parse(pair, decoder).issue_warning(&self.0.offsets, &mut decoder.warn)
            {
                names.push(long_name);
            }
        }
        LongNamesRecord(names)
    }
}

/// An extension record whose contents are a text string.
#[derive(Clone, Debug, Serialize)]
pub struct TextRecord {
    /// Range of file offsets for this record in bytes.
    pub offsets: Range<u64>,

    /// The text content of the record.
    pub text: ByteString,
}

impl TextRecord {
    /// Parses this record from `extension`.
    pub fn parse(extension: Extension, name: &'static str) -> Result<TextRecord, WarningDetails> {
        extension.check_size(Some(1), None, name)?;
        Ok(Self {
            offsets: extension.offsets,
            text: extension.data.into(),
        })
    }
}

/// Warning for a very long string variable record.
#[derive(ThisError, Debug)]
pub enum VeryLongStringWarning {
    /// Invalid variable name.
    #[error("Invalid variable name.  {0}")]
    InvalidLongStringName(
        /// Variable name error.
        IdError,
    ),

    /// Missing delimiter.
    #[error("Missing delimiter in {0:?}.")]
    VeryLongStringMissingDelimiter(String),

    /// Invalid length.
    #[error("Invalid length in {0:?}.")]
    VeryLongStringInvalidLength(
        /// Length.
        String,
    ),
}

/// A very long string parsed from a [VeryLongStringsRecord].
#[derive(Clone, Debug, Serialize)]
pub struct VeryLongString {
    /// Short name of very long string variable.
    pub short_name: Identifier,

    /// Length of very long string variable (in `256..=32767`).
    pub length: u16,
}

impl VeryLongString {
    /// Parses a [VeryLongString] from `input` using `decoder`.
    pub fn parse(decoder: &Decoder, input: &str) -> Result<VeryLongString, WarningDetails> {
        let Some((short_name, length)) = input.split_once('=') else {
            return Err(VeryLongStringWarning::VeryLongStringMissingDelimiter(input.into()).into());
        };
        let short_name = decoder
            .new_identifier(short_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(VeryLongStringWarning::InvalidLongStringName)?;
        let length = length
            .parse()
            .map_err(|_| VeryLongStringWarning::VeryLongStringInvalidLength(input.into()))?;
        Ok(VeryLongString { short_name, length })
    }
}

/// A very long string record as text.
#[derive(Clone, Debug, Serialize)]
pub struct RawVeryLongStringsRecord(pub TextRecord);

/// A parsed very long string record.
#[derive(Clone, Debug, Serialize)]
pub struct VeryLongStringsRecord(
    /// The very long strings.
    pub Vec<VeryLongString>,
);

impl RawVeryLongStringsRecord {
    /// Parses this record from `extension`.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::VeryLongStrings(Self(TextRecord::parse(
            extension,
            "very long strings record",
        )?)))
    }

    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> VeryLongStringsRecord {
        let input = decoder.decode(&self.0.text);
        let mut very_long_strings = Vec::new();
        for tuple in input
            .split('\0')
            .map(|s| s.trim_start_matches('\t'))
            .filter(|s| !s.is_empty())
        {
            if let Some(vls) = VeryLongString::parse(decoder, tuple)
                .issue_warning(&self.0.offsets, &mut decoder.warn)
            {
                very_long_strings.push(vls)
            }
        }
        VeryLongStringsRecord(very_long_strings)
    }
}

/// Warning for a multiple response set record.
#[derive(ThisError, Debug)]
pub enum MultipleResponseWarning {
    /// Invalid multiple response set name.
    #[error("Invalid multiple response set name.  {0}")]
    InvalidMrSetName(
        /// Variable name error.
        IdError,
    ),

    /// Invalid variable name.
    #[error("Invalid variable name.  {0}")]
    InvalidMrSetVariableName(
        /// Variable name error.
        IdError,
    ),

    /// Invalid multiple dichotomy label type.
    #[error("Invalid multiple dichotomy label type.")]
    InvalidMultipleDichotomyLabelType,

    /// Invalid multiple response type.
    #[error("Invalid multiple response type.")]
    InvalidMultipleResponseType,

    /// Syntax error.
    #[error("Syntax error ({0}).")]
    MultipleResponseSyntaxError(
        /// Detailed error.
        &'static str,
    ),

    /// Syntax error parsing counted string (missing trailing space).
    #[error("Syntax error parsing counted string (missing trailing space).")]
    CountedStringMissingSpace,

    /// Syntax error parsing counted string (invalid UTF-8).
    #[error("Syntax error parsing counted string (invalid UTF-8).")]
    CountedStringInvalidUTF8,

    /// Syntax error parsing counted string (invalid length).
    #[error("Syntax error parsing counted string (invalid length {0:?}).")]
    CountedStringInvalidLength(
        /// Length.
        String,
    ),

    /// Syntax error parsing counted string (length goes past end of input).
    #[error("Syntax error parsing counted string (length {0:?} goes past end of input).")]
    CountedStringTooLong(
        /// Length.
        usize,
    ),
}

/// The type of a multiple-response set.
#[derive(Clone, Debug, Serialize)]
pub enum MultipleResponseType {
    /// Multiple-dichotomy set.
    MultipleDichotomy {
        /// The value that is counted in the set.
        value: ByteString,

        /// What categories are labeled.
        labels: CategoryLabels,
    },

    /// Multiple-category set.
    MultipleCategory,
}

impl MultipleResponseType {
    /// Parses a [MultipleResponseType] from `input`, returning the type and the
    /// input remaining to be parsed.
    fn parse(input: &[u8]) -> Result<(MultipleResponseType, &[u8]), WarningDetails> {
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
                let (use_var_label_as_mrset_label, input) = if let Some(rest) =
                    input.strip_prefix(b" 1 ")
                {
                    (false, rest)
                } else if let Some(rest) = input.strip_prefix(b" 11 ") {
                    (true, rest)
                } else {
                    return Err(MultipleResponseWarning::InvalidMultipleDichotomyLabelType.into());
                };
                let (value, input) = parse_counted_string(input)?;
                (
                    MultipleResponseType::MultipleDichotomy {
                        value,
                        labels: CategoryLabels::CountedValues {
                            use_var_label_as_mrset_label,
                        },
                    },
                    input,
                )
            }
            _ => return Err(MultipleResponseWarning::InvalidMultipleResponseType.into()),
        };
        Ok((mr_type, input))
    }
}

/// A multiple-response set in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct MultipleResponseSet<I, S>
where
    I: Debug + Serialize,
    S: Debug + Serialize,
{
    /// The set's name.
    pub name: I,
    /// The set's label.
    pub label: S,
    /// The type of multiple-response set.
    pub mr_type: MultipleResponseType,
    /// Short names of the variables in the set.
    pub short_names: Vec<I>,
}

impl MultipleResponseSet<ByteString, ByteString> {
    /// Parses a multiple-response set from `input`.  Returns the set and the
    /// input remaining to be parsed following the set.
    fn parse(input: &[u8]) -> Result<(Self, &[u8]), WarningDetails> {
        let Some(equals) = input.iter().position(|&b| b == b'=') else {
            return Err(MultipleResponseWarning::MultipleResponseSyntaxError("missing `=`").into());
        };
        let (name, input) = input.split_at(equals);
        let input = input.strip_prefix(b"=").unwrap();
        let (mr_type, input) = MultipleResponseType::parse(input)?;
        let Some(input) = input.strip_prefix(b" ") else {
            return Err(MultipleResponseWarning::MultipleResponseSyntaxError(
                "missing space after multiple response type",
            )
            .into());
        };
        let (label, mut input) = parse_counted_string(input)?;
        let mut vars = Vec::new();
        while input.first() != Some(&b'\n') {
            match input.split_first() {
                Some((b' ', rest)) => {
                    let Some(length) = rest.iter().position(|b| b" \n".contains(b)) else {
                        return Err(MultipleResponseWarning::MultipleResponseSyntaxError(
                            "missing variable name delimiter",
                        )
                        .into());
                    };
                    let (var, rest) = rest.split_at(length);
                    if !var.is_empty() {
                        vars.push(var.into());
                    }
                    input = rest;
                }
                _ => {
                    return Err(MultipleResponseWarning::MultipleResponseSyntaxError(
                        "missing space preceding variable name",
                    )
                    .into());
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

    /// Decodes this multiple-response set using `decoder`.  `offsets` is used
    /// for issuing warnings.
    fn decode(
        &self,
        offsets: &Range<u64>,
        decoder: &mut Decoder,
    ) -> Result<MultipleResponseSet<Identifier, String>, WarningDetails> {
        let mut short_names = Vec::with_capacity(self.short_names.len());
        for short_name in self.short_names.iter() {
            if let Some(short_name) = decoder
                .decode_identifier(short_name)
                .map_err(MultipleResponseWarning::InvalidMrSetName)
                .issue_warning(offsets, &mut decoder.warn)
            {
                short_names.push(short_name);
            }
        }
        Ok(MultipleResponseSet {
            name: decoder
                .decode_identifier(&self.name)
                .map_err(MultipleResponseWarning::InvalidMrSetVariableName)?,
            label: decoder.decode(&self.label).to_string(),
            mr_type: self.mr_type.clone(),
            short_names,
        })
    }
}

/// A multiple-response set record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct MultipleResponseRecord<I, S>
where
    I: Debug + Serialize,
    S: Debug + Serialize,
{
    /// File offsets of the record.
    pub offsets: Range<u64>,

    /// The multiple-response sets.
    pub sets: Vec<MultipleResponseSet<I, S>>,
}

impl MultipleResponseRecord<ByteString, ByteString> {
    /// Parses a multiple-response set from `ext`.
    pub fn parse(ext: &Extension) -> Result<Record, WarningDetails> {
        ext.check_size(Some(1), None, "multiple response set record")?;

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
        Ok(Record::MultipleResponse(MultipleResponseRecord {
            offsets: ext.offsets.clone(),
            sets,
        }))
    }
}

impl MultipleResponseRecord<ByteString, ByteString> {
    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> MultipleResponseRecord<Identifier, String> {
        let mut sets = Vec::new();
        for set in self.sets.iter() {
            if let Some(set) = set
                .decode(&self.offsets, decoder)
                .issue_warning(&self.offsets, &mut decoder.warn)
            {
                sets.push(set);
            }
        }
        MultipleResponseRecord {
            offsets: self.offsets,
            sets,
        }
    }
}

fn parse_counted_string(input: &[u8]) -> Result<(ByteString, &[u8]), WarningDetails> {
    let Some(space) = input.iter().position(|&b| b == b' ') else {
        return Err(MultipleResponseWarning::CountedStringMissingSpace.into());
    };
    let Ok(length) = from_utf8(&input[..space]) else {
        return Err(MultipleResponseWarning::CountedStringInvalidUTF8.into());
    };
    let Ok(length): Result<usize, _> = length.parse() else {
        return Err(MultipleResponseWarning::CountedStringInvalidLength(length.into()).into());
    };

    let Some((string, rest)) = input[space + 1..].split_at_checked(length) else {
        return Err(MultipleResponseWarning::CountedStringTooLong(length).into());
    };
    Ok((string.into(), rest))
}

/// Warning for a variable display record.
#[derive(ThisError, Debug)]
pub enum VariableDisplayWarning {
    /// Wrong number of variable display items.
    #[error("Record contains {count} items but should contain either {first} or {second}.")]
    InvalidVariableDisplayCount {
        /// Actual count.
        count: usize,
        /// First valid count.
        first: usize,
        /// Second valid count.
        second: usize,
    },

    /// Invalid variable measurement level value.
    #[error("Invalid variable measurement level value {0}.")]
    InvalidMeasurement(
        /// Invalid value.
        u32,
    ),

    /// Invalid variable display alignment value.
    #[error("Invalid variable display alignment value {0}.")]
    InvalidAlignment(
        /// Invalid value.
        u32,
    ),
}

impl Measure {
    fn try_decode(source: u32) -> Result<Option<Measure>, WarningDetails> {
        match source {
            0 => Ok(None),
            1 => Ok(Some(Measure::Nominal)),
            2 => Ok(Some(Measure::Ordinal)),
            3 => Ok(Some(Measure::Scale)),
            _ => Err(VariableDisplayWarning::InvalidMeasurement(source).into()),
        }
    }
}

impl Alignment {
    fn try_decode(source: u32) -> Result<Option<Alignment>, WarningDetails> {
        match source {
            0 => Ok(Some(Alignment::Left)),
            1 => Ok(Some(Alignment::Right)),
            2 => Ok(Some(Alignment::Center)),
            _ => Err(VariableDisplayWarning::InvalidAlignment(source).into()),
        }
    }
}

/// Variable display settings for one variable, in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VarDisplay {
    /// Measurement level.
    pub measure: Option<Measure>,

    /// Variable display width.
    pub width: Option<u32>,

    /// Variable alignment.
    pub alignment: Option<Alignment>,
}

/// A variable display record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VarDisplayRecord(
    /// Variable display settings for each variable.
    pub Vec<VarDisplay>,
);

impl VarDisplayRecord {
    /// Parses a variable display record from `ext` given variable types `var_types`.
    fn parse(
        ext: &Extension,
        var_types: &VarTypes,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Record, WarningDetails> {
        ext.check_size(Some(4), None, "variable display record")?;

        let n_vars = var_types.n_vars();
        let has_width = if ext.count as usize == 3 * n_vars {
            true
        } else if ext.count as usize == 2 * n_vars {
            false
        } else {
            return Err(VariableDisplayWarning::InvalidVariableDisplayCount {
                count: ext.count as usize,
                first: 2 * n_vars,
                second: 3 * n_vars,
            }
            .into());
        };

        let mut var_displays = Vec::new();
        let mut input = &ext.data[..];
        for _ in 0..n_vars {
            let measure = Measure::try_decode(endian.parse(read_bytes(&mut input).unwrap()))
                .issue_warning(&ext.offsets, warn)
                .flatten();
            let width = has_width.then(|| endian.parse(read_bytes(&mut input).unwrap()));
            let alignment = Alignment::try_decode(endian.parse(read_bytes(&mut input).unwrap()))
                .issue_warning(&ext.offsets, warn)
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

/// Warning for a long string missing value record.
#[derive(ThisError, Debug)]
pub enum LongStringMissingValuesWarning {
    /// Invalid value length.
    #[error("Value length at offset {offset:#x} is {value_len} instead of the expected 8.")]
    BadValueLength {
        /// Offset of the value length.
        offset: u64,
        /// Actual value length.
        value_len: u32,
    },

    /// Invalid variable name.
    #[error("Invalid variable name.  {0}")]
    InvalidVariableName(
        /// Variable name error.
        IdError,
    ),
}

/// Missing values for one long string variable.
#[derive(Clone, Debug, Serialize)]
pub struct LongStringMissingValues<N>
where
    N: Debug + Serialize,
{
    /// Variable name.
    pub var_name: N,

    /// Missing values.
    pub missing_values: Vec<ByteStrArray<8>>,
}

impl LongStringMissingValues<ByteString> {
    /// Decodes these settings using `decoder`.
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

/// Long string missing values record in a sytem file.
#[derive(Clone, Debug, Serialize)]
pub struct LongStringMissingValueRecord<N>
where
    N: Debug + Serialize,
{
    /// The record's file offsets.
    pub offsets: Range<u64>,

    /// The long string missing values.
    pub values: Vec<LongStringMissingValues<N>>,
}

impl LongStringMissingValueRecord<ByteString> {
    /// Parses this record from `ext`.
    pub fn parse(
        ext: &Extension,
        endian: Endian,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Record, WarningDetails> {
        ext.check_size(Some(1), None, "long string missing values record")?;

        let mut input = &ext.data[..];
        let mut missing_value_set = Vec::new();
        while !input.is_empty() {
            let var_name = read_string(&mut input, endian)?;
            let n_missing_values: u8 = endian.parse(read_bytes(&mut input)?);
            let value_len: u32 = endian.parse(read_bytes(&mut input)?);
            if value_len != 8 {
                let offset = (ext.data.len() - input.len() - 8) as u64 + ext.offsets.start;
                warn(Warning::new(
                    Some(ext.offsets.clone()),
                    LongStringMissingValuesWarning::BadValueLength { offset, value_len },
                ));
                read_vec(&mut input, value_len as usize * n_missing_values as usize)?;
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
                missing_values.push(ByteStrArray(value));
            }
            missing_value_set.push(LongStringMissingValues {
                var_name,
                missing_values,
            });
        }
        Ok(Record::LongStringMissingValues(
            LongStringMissingValueRecord {
                offsets: ext.offsets.clone(),
                values: missing_value_set,
            },
        ))
    }

    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> LongStringMissingValueRecord<Identifier> {
        let mut mvs = Vec::with_capacity(self.values.len());
        for mv in self.values.iter() {
            if let Some(mv) = mv
                .decode(decoder)
                .map_err(LongStringMissingValuesWarning::InvalidVariableName)
                .issue_warning(&self.offsets, &mut decoder.warn)
            {
                mvs.push(mv);
            }
        }
        LongStringMissingValueRecord {
            offsets: self.offsets,
            values: mvs,
        }
    }
}

/// A character encoding record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct EncodingRecord(
    /// The encoding name.
    pub String,
);

impl EncodingRecord {
    /// Parses this record from `ext`.
    pub fn parse(ext: &Extension) -> Result<Record, WarningDetails> {
        ext.check_size(Some(1), None, "encoding record")?;

        Ok(Record::Encoding(EncodingRecord(
            String::from_utf8(ext.data.clone()).map_err(|_| WarningDetails::BadEncodingName)?,
        )))
    }
}

/// The extended number of cases record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct NumberOfCasesRecord {
    /// Always observed as 1.
    pub one: u64,

    /// Number of cases.
    pub n_cases: Option<u64>,
}

impl NumberOfCasesRecord {
    /// Parses a number of cases record from `ext` using `endian`.
    pub fn parse(ext: &Extension, endian: Endian) -> Result<Record, WarningDetails> {
        ext.check_size(Some(8), Some(2), "extended number of cases record")?;

        let mut input = &ext.data[..];
        let one = endian.parse(read_bytes(&mut input)?);
        let n_cases = endian.parse(read_bytes(&mut input)?);
        let n_cases = (n_cases < u64::MAX).then_some(n_cases);

        Ok(Record::NumberOfCases(NumberOfCasesRecord { one, n_cases }))
    }
}

/// Warning for a variable sets record.
#[derive(ThisError, Debug)]
pub enum VariableSetWarning {
    /// Invalid variable name.
    #[error("Invalid variable name.  {0}")]
    InvalidVariableSetName(
        /// Variable name error.
        IdError,
    ),

    /// Missing name delimiter.
    #[error("Missing name delimiter.")]
    VariableSetMissingEquals,
}

/// Raw (text) version of the variable set record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct RawVariableSetRecord(TextRecord);

impl RawVariableSetRecord {
    /// Parses the record from `extension`.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::VariableSets(Self(TextRecord::parse(
            extension,
            "variable sets record",
        )?)))
    }

    /// Decodes the record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> VariableSetRecord {
        let mut sets = Vec::new();
        let input = decoder.decode(&self.0.text);
        for line in input.lines() {
            if let Some(set) = VariableSet::parse(line, decoder, &self.0.offsets)
                .issue_warning(&self.0.offsets, &mut decoder.warn)
            {
                sets.push(set)
            }
        }
        VariableSetRecord {
            offsets: self.0.offsets,
            sets,
        }
    }
}

/// Raw (text) version of a product info record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct RawProductInfoRecord(pub TextRecord);

impl RawProductInfoRecord {
    /// Parses the record from `extension`.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::ProductInfo(Self(TextRecord::parse(
            extension,
            "product info record",
        )?)))
    }

    /// Decodes the record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> ProductInfoRecord {
        ProductInfoRecord(decoder.decode(&self.0.text).into())
    }
}

/// Warning for a file or variable attribute record.
#[derive(ThisError, Debug)]
pub enum AttributeWarning {
    /// Invalid attribute name.
    #[error("Invalid attribute name.  {0}")]
    InvalidAttributeName(
        /// Attribute name error.
        IdError,
    ),

    /// Invalid variable name in attribute record.
    #[error("Invalid variable name in attribute record.  {0}")]
    InvalidAttributeVariableName(
        /// Variable name error.
        IdError,
    ),

    /// Attribute record missing left parenthesis.
    #[error("Attribute record missing left parenthesis, in {0:?}.")]
    AttributeMissingLParen(
        /// Bad syntax.
        String,
    ),

    /// Attribute lacks value.
    #[error("Attribute for {name}[{}] lacks value.", index + 1)]
    AttributeMissingValue {
        /// Attribute name.
        name: Identifier,
        /// 0-based index.
        index: usize,
    },

    /// Attribute missing quotations.
    #[error("Attribute for {name}[{}] missing quotations.", index + 1)]
    AttributeMissingQuotes {
        /// Attribute name.
        name: Identifier,
        /// 0-based index.
        index: usize,
    },

    /// Variable attribute missing `:`.
    #[error("Variable attribute missing `:`.")]
    VariableAttributeMissingColon,

    /// Duplicate attributes for variable.
    #[error("Duplicate attributes for variable {variable}: {}.", attributes.iter().join(", "))]
    DuplicateVariableAttributes {
        /// Variable name.
        variable: Identifier,
        /// Attributes with duplicates.
        attributes: Vec<Identifier>,
    },

    /// Duplicate dataset attributes.
    #[error("Duplicate dataset attributes with names: {}.", attributes.iter().join(", "))]
    DuplicateFileAttributes {
        /// Attributes with duplicates.
        attributes: Vec<Identifier>,
    },

    /// File attributes record contains trailing garbage.
    #[error("File attributes record contains trailing garbage.")]
    FileAttributesTrailingGarbage,
}

/// A file or variable attribute in a system file.
#[derive(Clone, Debug)]
pub struct Attribute {
    /// The attribute's name.
    pub name: Identifier,

    /// The attribute's values.
    pub values: Vec<String>,
}

impl Attribute {
    /// Parses an attribute from the beginning of `input` using `decoder`.  Uses
    /// `offsets` to report warnings.  Returns the decoded attribute and the
    /// part of `input` that remains to be parsed following the attribute.
    fn parse<'a>(
        decoder: &mut Decoder,
        offsets: &Range<u64>,
        input: &'a str,
    ) -> Result<(Attribute, &'a str), WarningDetails> {
        let Some((name, mut input)) = input.split_once('(') else {
            return Err(AttributeWarning::AttributeMissingLParen(input.into()).into());
        };
        let name = decoder
            .new_identifier(name)
            .map_err(AttributeWarning::InvalidAttributeName)?;
        let mut values = Vec::new();
        loop {
            let Some((value, rest)) = input.split_once('\n') else {
                return Err(AttributeWarning::AttributeMissingValue {
                    name: name.clone(),
                    index: values.len(),
                }
                .into());
            };
            if let Some(stripped) = value
                .strip_prefix('\'')
                .and_then(|value| value.strip_suffix('\''))
            {
                values.push(stripped.into());
            } else {
                decoder.warn(Warning::new(
                    Some(offsets.clone()),
                    AttributeWarning::AttributeMissingQuotes {
                        name: name.clone(),
                        index: values.len(),
                    },
                ));
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
    /// Parses a set of varaible or file attributes from `input` using
    /// `decoder`.  Uses `offsets` for reporting warnings.  If not `None`,
    /// `sentinel` terminates the attributes. Returns the attributes and the
    /// part of `input` that remains after parsing the attributes.
    fn parse<'a>(
        decoder: &mut Decoder,
        offsets: &Range<u64>,
        mut input: &'a str,
        sentinel: Option<char>,
    ) -> Result<(Attributes, &'a str, Vec<Identifier>), WarningDetails> {
        let mut attributes = BTreeMap::new();
        let mut duplicates = Vec::new();
        let rest = loop {
            match input.chars().next() {
                None => break input,
                c if c == sentinel => break &input[1..],
                _ => {
                    let (attribute, rest) = Attribute::parse(decoder, offsets, input)?;
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

/// A raw (text) file attributes record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct RawFileAttributesRecord(TextRecord);

/// A decoded file attributes record in a system file.
#[derive(Clone, Debug, Default, Serialize)]
pub struct FileAttributesRecord(pub Attributes);

impl RawFileAttributesRecord {
    /// Parses this record from `extension`.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::FileAttributes(Self(TextRecord::parse(
            extension,
            "file attributes record",
        )?)))
    }

    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> FileAttributesRecord {
        let input = decoder.decode(&self.0.text);
        match Attributes::parse(decoder, &self.0.offsets, &input, None)
            .issue_warning(&self.0.offsets, &mut decoder.warn)
        {
            Some((set, rest, duplicates)) => {
                if !duplicates.is_empty() {
                    decoder.warn(Warning::new(
                        Some(self.0.offsets.clone()),
                        AttributeWarning::DuplicateFileAttributes {
                            attributes: duplicates,
                        },
                    ));
                }
                if !rest.is_empty() {
                    decoder.warn(Warning::new(
                        Some(self.0.offsets.clone()),
                        AttributeWarning::FileAttributesTrailingGarbage,
                    ));
                }
                FileAttributesRecord(set)
            }
            None => FileAttributesRecord::default(),
        }
    }
}

/// A set of variable attributes in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VarAttributes {
    /// The long name of the variable associated with the attributes.
    pub long_var_name: Identifier,

    /// The attributes.
    pub attributes: Attributes,
}

impl VarAttributes {
    /// Parses a variable attribute set from `input` using `decoder`.  Uses
    /// `offsets` for reporting warnings.
    fn parse<'a>(
        decoder: &mut Decoder,
        offsets: &Range<u64>,
        input: &'a str,
    ) -> Result<(VarAttributes, &'a str), WarningDetails> {
        let Some((long_var_name, rest)) = input.split_once(':') else {
            return Err(AttributeWarning::VariableAttributeMissingColon.into());
        };
        let long_var_name = decoder
            .new_identifier(long_var_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(AttributeWarning::InvalidAttributeVariableName)?;
        let (attributes, rest, duplicates) = Attributes::parse(decoder, offsets, rest, Some('/'))?;
        if !duplicates.is_empty() {
            decoder.warn(Warning::new(
                Some(offsets.clone()),
                AttributeWarning::DuplicateVariableAttributes {
                    variable: long_var_name.clone(),
                    attributes: duplicates,
                },
            ));
        }
        Ok((
            VarAttributes {
                long_var_name,
                attributes,
            },
            rest,
        ))
    }
}

/// A raw (text) variable attributes record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct RawVariableAttributesRecord(TextRecord);

/// A decoded variable attributes record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VariableAttributesRecord(pub Vec<VarAttributes>);

impl RawVariableAttributesRecord {
    /// Parses a variable attributes record.
    pub fn parse(extension: Extension) -> Result<Record, WarningDetails> {
        Ok(Record::VariableAttributes(Self(TextRecord::parse(
            extension,
            "variable attributes record",
        )?)))
    }

    /// Decodes a variable attributes record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> VariableAttributesRecord {
        let decoded = decoder.decode(&self.0.text);
        let mut input = decoded.as_ref();
        let mut var_attribute_sets = Vec::new();
        while !input.is_empty() {
            let Some((var_attribute, rest)) = VarAttributes::parse(decoder, &self.0.offsets, input)
                .issue_warning(&self.0.offsets, &mut decoder.warn)
            else {
                break;
            };
            var_attribute_sets.push(var_attribute);
            input = rest;
        }
        VariableAttributesRecord(var_attribute_sets)
    }
}

/// Warning for a long variable name record.
#[derive(ThisError, Debug)]
pub enum LongNameWarning {
    /// Missing `=`.
    #[error("Missing `=` separator.")]
    LongNameMissingEquals,

    /// Invalid short name.
    #[error("Invalid short name.  {0}")]
    InvalidShortName(
        /// Short variable name error.
        IdError,
    ),

    /// Invalid long name.
    #[error("Invalid long name.  {0}")]
    InvalidLongName(
        /// Long variable name error.
        IdError,
    ),
}

/// A long variable name in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct LongName {
    /// The variable's short name.
    pub short_name: Identifier,

    /// The variable's long name.
    pub long_name: Identifier,
}

impl LongName {
    /// Parses a long variable name from `input` using `decoder`.
    pub fn parse(input: &str, decoder: &Decoder) -> Result<Self, WarningDetails> {
        let Some((short_name, long_name)) = input.split_once('=') else {
            return Err(LongNameWarning::LongNameMissingEquals.into());
        };
        let short_name = decoder
            .new_identifier(short_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(LongNameWarning::InvalidShortName)?;
        let long_name = decoder
            .new_identifier(long_name)
            .and_then(Identifier::must_be_ordinary)
            .map_err(LongNameWarning::InvalidLongName)?;
        Ok(LongName {
            short_name,
            long_name,
        })
    }
}

/// A long variable name record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct LongNamesRecord(pub Vec<LongName>);

/// A product info record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct ProductInfoRecord(pub String);

/// A variable set in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VariableSet {
    /// Name of the variable set.
    pub name: String,

    /// The long variable names of the members of the set.
    pub variable_names: Vec<Identifier>,
}

impl VariableSet {
    /// Parses a variable set from `input` using `decoder`.  Uses `offsets` to
    /// report warnings.
    fn parse(
        input: &str,
        decoder: &mut Decoder,
        offsets: &Range<u64>,
    ) -> Result<Self, WarningDetails> {
        let (name, input) = input
            .split_once('=')
            .ok_or(VariableSetWarning::VariableSetMissingEquals)?;
        let mut vars = Vec::new();
        for var in input.split_ascii_whitespace() {
            if let Some(identifier) = decoder
                .new_identifier(var)
                .and_then(Identifier::must_be_ordinary)
                .map_err(VariableSetWarning::InvalidVariableSetName)
                .issue_warning(offsets, &mut decoder.warn)
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

/// A variable set record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct VariableSetRecord {
    /// Range of file offsets occupied by the record.
    pub offsets: Range<u64>,

    /// The variable sets in the record.
    pub sets: Vec<VariableSet>,
}

trait IssueWarning<T> {
    fn issue_warning(self, offsets: &Range<u64>, warn: &mut dyn FnMut(Warning)) -> Option<T>;
}
impl<T, W> IssueWarning<T> for Result<T, W>
where
    W: Into<WarningDetails>,
{
    fn issue_warning(self, offsets: &Range<u64>, warn: &mut dyn FnMut(Warning)) -> Option<T> {
        match self {
            Ok(result) => Some(result),
            Err(error) => {
                warn(Warning::new(Some(offsets.clone()), error.into()));
                None
            }
        }
    }
}

/// Warning for an extension record.
#[derive(ThisError, Debug)]
pub enum ExtensionWarning {
    /// Unexpected end of data.
    #[error("Unexpected end of data.")]
    UnexpectedEndOfData,

    /// Invalid record size.
    #[error("{record} has bad size {size} bytes instead of the expected {expected_size}.")]
    BadRecordSize {
        /// Name of the record.
        record: &'static str,
        /// Size of the elements in the record, in bytes.
        size: u32,
        /// Expected size of the elements in the record, in bytes.
        expected_size: u32,
    },

    /// Invalid record count.
    #[error("{record} has bad count {count} instead of the expected {expected_count}.")]
    BadRecordCount {
        /// Name of the record.
        record: &'static str,
        /// Number of elements in the record.
        count: u32,
        /// Expected number of elements in the record.
        expected_count: u32,
    },
}

/// An extension record in a system file.
///
/// Most of the records in system files are "extension records".  This structure
/// collects everything in an extension record for later processing.
#[derive(Clone, Debug, Serialize)]
pub struct Extension {
    /// File offsets occupied by the extension record.
    ///
    /// These are the offsets of the `data` portion of the record, not including
    /// the header that specifies the subtype, size, and count.
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
    /// Checks that this extension has `size`-byte elements and `count` elements
    /// total.  Uses `name` for error reporting.
    pub fn check_size(
        &self,
        size: Option<u32>,
        count: Option<u32>,
        name: &'static str,
    ) -> Result<(), WarningDetails> {
        if let Some(expected_size) = size
            && self.size != expected_size
        {
            Err(ExtensionWarning::BadRecordSize {
                record: name,
                size: self.size,
                expected_size,
            }
            .into())
        } else if let Some(expected_count) = count
            && self.count != expected_count
        {
            Err(ExtensionWarning::BadRecordCount {
                record: name,
                count: self.count,
                expected_count,
            }
            .into())
        } else {
            Ok(())
        }
    }

    /// Reads an extension record from `r`, with the given `endian`, given that
    /// the variables in the system file have the types in `var_types`, and
    /// using `warn` to report warnings.
    pub fn read<R: Read + Seek>(
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
            return Err(Error::new(
                Some(header_offset..header_offset + 8),
                ErrorDetails::ExtensionRecordTooLarge {
                    subtype,
                    size,
                    count,
                },
            ));
        };
        let start_offset = r.stream_position()?;
        let data = read_vec(r, product as usize)?;
        let end_offset = start_offset + product as u64;
        let offsets = start_offset..end_offset;
        let extension = Extension {
            offsets: offsets.clone(),
            subtype,
            size,
            count,
            data,
        };
        let result = match subtype {
            3 => IntegerInfoRecord::parse(&extension, endian),
            4 => FloatInfoRecord::parse(&extension, endian),
            11 => VarDisplayRecord::parse(&extension, var_types, endian, warn),
            7 | 19 => MultipleResponseRecord::parse(&extension),
            21 => LongStringValueLabelRecord::parse(&extension, endian),
            22 => LongStringMissingValueRecord::parse(&extension, endian, warn),
            20 => EncodingRecord::parse(&extension),
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
            Err(details) => {
                warn(Warning::new(Some(offsets), details));
                Ok(None)
            }
        }
    }
}

/// Warning for a long string value label record.
#[derive(ThisError, Debug)]
pub enum LongStringValueLabelWarning {
    /// Invalid variable name.
    #[error("Invalid variable name.  {0}")]
    InvalidVariableName(
        /// Variable name error.
        IdError,
    ),
}

/// One set of long string value labels record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct LongStringValueLabels<N, S>
where
    S: Debug + Serialize,
{
    /// The variable being labeled.
    pub var_name: N,

    /// The variable's width (greater than 8, since it's a long string).
    pub width: u32,

    /// `(value, label)` pairs, where each value is `width` bytes.
    pub labels: Vec<(ByteString, S)>,
}

impl LongStringValueLabels<ByteString, ByteString> {
    /// Decodes a set of long string value labels using `decoder`.
    fn decode(
        &self,
        decoder: &mut Decoder,
    ) -> Result<LongStringValueLabels<Identifier, String>, WarningDetails> {
        let var_name = decoder.decode(&self.var_name);
        let var_name = Identifier::from_encoding(var_name.trim_end(), decoder.encoding)
            .map_err(LongStringValueLabelWarning::InvalidVariableName)?;

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

/// A long string value labels record in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct LongStringValueLabelRecord<N, S>
where
    N: Debug + Serialize,
    S: Debug + Serialize,
{
    /// File offsets occupied by the record.
    pub offsets: Range<u64>,

    /// The labels.
    pub labels: Vec<LongStringValueLabels<N, S>>,
}

impl LongStringValueLabelRecord<ByteString, ByteString> {
    /// Parses this record from `ext` using `endian`.
    fn parse(ext: &Extension, endian: Endian) -> Result<Record, WarningDetails> {
        ext.check_size(Some(1), None, "long string value labels record")?;

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
        Ok(Record::LongStringValueLabels(LongStringValueLabelRecord {
            offsets: ext.offsets.clone(),
            labels: label_set,
        }))
    }

    /// Decodes this record using `decoder`.
    pub fn decode(self, decoder: &mut Decoder) -> LongStringValueLabelRecord<Identifier, String> {
        let mut labels = Vec::with_capacity(self.labels.len());
        for label in &self.labels {
            match label.decode(decoder) {
                Ok(set) => labels.push(set),
                Err(error) => decoder.warn(Warning::new(Some(self.offsets.clone()), error)),
            }
        }
        LongStringValueLabelRecord {
            offsets: self.offsets,
            labels,
        }
    }
}

/// ZLIB header, for [Compression::ZLib].
#[derive(Clone, Debug, Serialize)]
pub struct ZHeader {
    /// File offset to the start of the record.
    pub offset: u64,

    /// Raw header.
    #[serde(flatten)]
    pub inner: RawZHeader,
}

/// A ZLIB header in a system file.
#[derive(Clone, Debug, BinRead, BinWrite, Serialize)]
pub struct RawZHeader {
    /// File offset to the ZLIB data header.
    pub zheader_offset: u64,

    /// File offset to the ZLIB trailer.
    pub ztrailer_offset: u64,

    /// Length of the ZLIB trailer in bytes.
    pub ztrailer_len: u64,
}

impl ZHeader {
    /// Reads a ZLIB header from `r` using `endian`.
    pub fn read<R>(r: &mut R, endian: Endian) -> Result<ZHeader, Error>
    where
        R: Read + Seek,
    {
        let offset = r.stream_position()?;
        let inner = RawZHeader::read_options(r, endian, ()).map_err(|e| Error {
            offsets: Some(offset..offset + 24),
            details: ZHeaderError::from(e).into(),
        })?;

        if inner.zheader_offset != offset {
            Err(ZHeaderError::UnexpectedZHeaderOffset {
                actual: inner.zheader_offset,
                expected: offset,
            }
            .into())
        } else if inner.ztrailer_offset < offset {
            Err(ZHeaderError::ImpossibleZTrailerOffset(inner.ztrailer_offset).into())
        } else if inner.ztrailer_len < 24 || inner.ztrailer_len % 24 != 0 {
            Err(ZHeaderError::InvalidZTrailerLength(inner.ztrailer_len).into())
        } else {
            Ok(ZHeader { offset, inner })
        }
        .map_err(|details| Error::new(Some(offset..offset + 12), details))
    }
}

/// Error reading a [ZHeader].
#[derive(ThisError, Debug)]
pub enum ZHeaderError {
    /// I/O error via [mod@binrw].
    #[error("{}", DisplayBinError(.0, "ZLIB header"))]
    BinError(#[from] BinError),

    /// Impossible ztrailer_offset {0:#x}.
    #[error("Impossible ztrailer_offset {0:#x}.")]
    ImpossibleZTrailerOffset(
        /// `ztrailer_offset`
        u64,
    ),

    /// zlib_offset is {actual:#x} instead of expected {expected:#x}.
    #[error("zlib_offset is {actual:#x} instead of expected {expected:#x}.")]
    UnexpectedZHeaderOffset {
        /// Actual `zlib_offset`.
        actual: u64,
        /// Expected `zlib_offset`.
        expected: u64,
    },

    /// Invalid ZLIB trailer length {0}.
    #[error("Invalid ZLIB trailer length {0}.")]
    InvalidZTrailerLength(
        /// ZLIB trailer length.
        u64,
    ),
}

/// A ZLIB trailer in a system file.
#[derive(Clone, Debug, Serialize)]
pub struct ZTrailer {
    /// File offset to the start of the record.
    pub offset: u64,

    /// The raw trailer.
    #[serde(flatten)]
    pub inner: RawZTrailer,
}

/// A ZLIB trailer in a system file.
#[binrw]
#[derive(Clone, Debug, Serialize)]
pub struct RawZTrailer {
    /// Compression bias as a negative integer, e.g. -100.
    pub int_bias: i64,

    /// Always observed as zero.
    pub zero: u64,

    /// Uncompressed size of each block, except possibly the last.  Only
    /// `0x3ff000` has been observed so far.
    pub block_size: u32,

    /// Number of blocks.
    #[bw(calc(blocks.len() as u32))]
    pub n_blocks: u32,

    /// Block descriptors, always `(ztrailer_len - 24) / 24)` of them.
    #[br(count = n_blocks)]
    pub blocks: Vec<ZBlock>,
}

impl RawZTrailer {
    /// Returns the length of the trailer when it is written, in bytes.
    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        24 + self.blocks.len() * 24
    }
}

/// Warning for a ZLIB trailer record.
#[derive(ThisError, Debug)]
pub enum ZlibTrailerWarning {
    /// Wrong block size.
    #[error(
        "Block descriptor {index} reported block size {actual:#x}, when {expected:#x} was expected."
    )]
    ZlibTrailerBlockWrongSize {
        /// 0-based block descriptor index.
        index: usize,
        /// Actual block size.
        actual: u32,
        /// Expected block size.
        expected: u32,
    },

    /// Block too big.
    #[error(
        "Block descriptor {index} reported block size {actual:#x}, when at most {max_expected:#x} was expected."
    )]
    ZlibTrailerBlockTooBig {
        /// 0-based block descriptor index.
        index: usize,
        /// Actual block size.
        actual: u32,
        /// Maximum expected block size.
        max_expected: u32,
    },
}

/// A ZLIB block descriptor in a system file.
#[derive(Clone, Debug, BinRead, BinWrite, Serialize)]
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
    /// Returns true if the uncompressed and compressed sizes are plausible.
    ///
    /// [zlib Technical Details] says that the maximum expansion from
    /// compression, with worst-case parameters, is 13.5% plus 11 bytes.  This
    /// code checks for an expansion of more than 14.3% plus 11 bytes.
    ///
    /// [zlib Technical Details]: http://www.zlib.net/zlib_tech.html
    fn has_plausible_sizes(&self) -> bool {
        self.uncompressed_size
            .checked_add(self.uncompressed_size / 7 + 11)
            .is_some_and(|max| self.compressed_size <= max)
    }
}

struct DisplayBinError<'a>(&'a BinError, &'static str);

impl<'a> Display for DisplayBinError<'a> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        if self.0.is_eof() {
            write!(f, "Unexpected end-of-file reading {}", self.1)
        } else {
            write!(f, "Error reading {}: {}", self.1, self.0.root_cause())
        }
    }
}

/// Error reading a [ZTrailer].
#[derive(ThisError, Debug)]
pub enum ZTrailerError {
    /// I/O error via [mod@binrw].
    #[error("{}", DisplayBinError(.0, "ZLIB trailer"))]
    BinError(#[from] BinError),

    /// ZLIB trailer bias {actual} is not {} as expected from file header bias.
    #[
        error(
            "Bias {actual} is not {} as expected from file header.",
            expected.display_plain()
    )]
    WrongZlibTrailerBias {
        /// ZLIB trailer bias read from file.
        actual: i64,
        /// Expected ZLIB trailer bias.
        expected: f64,
    },

    /// ZLIB trailer zero field has nonzero value {0}.
    #[error("Expected zero field has nonzero value {0}.")]
    WrongZlibTrailerZero(
        /// Actual value that should have been zero.
        u64,
    ),

    /// ZLIB trailer specifies unexpected {0}-byte block size.
    #[error("Unexpected {0:x}-byte block size (expected 0x3ff000).")]
    WrongZlibTrailerBlockSize(
        /// Block size read from file.
        u32,
    ),

    /// Block count differs from expected block count calculated from trailer
    /// length.
    #[error(
        "Block count {n_blocks} differs from expected block count {expected_n_blocks} calculated from trailer length {ztrailer_len}."
    )]
    BadZlibTrailerNBlocks {
        /// Number of blocks.
        n_blocks: usize,
        /// Expected number of blocks.
        expected_n_blocks: u64,
        /// ZLIB trailer length in bytes.
        ztrailer_len: u64,
    },

    /// ZLIB block descriptor reported uncompressed data offset different from
    /// expected.
    #[error(
        "Block descriptor {index} reported uncompressed data offset {actual:#x}, when {expected:#x} was expected."
    )]
    ZlibTrailerBlockWrongUncmpOfs {
        /// Block descriptor index.
        index: usize,
        /// Actual uncompressed data offset.
        actual: u64,
        /// Expected uncompressed data offset.
        expected: u64,
    },

    /// Block descriptor {index} reported compressed data offset
    /// {actual:#x}, when {expected:#x} was expected.
    #[error(
        "Block descriptor {index} reported compressed data offset {actual:#x}, when {expected:#x} was expected."
    )]
    ZlibTrailerBlockWrongCmpOfs {
        /// Block descriptor index.
        index: usize,
        /// Actual compressed data offset.
        actual: u64,
        /// Expected compressed data offset.
        expected: u64,
    },

    /// Block descriptor {index} reports compressed size {compressed_size}
    /// and uncompressed size {uncompressed_size}.
    #[error(
        "Block descriptor {index} reports compressed size {compressed_size} and uncompressed size {uncompressed_size}."
    )]
    ZlibExpansion {
        /// Block descriptor index.
        index: usize,
        /// Compressed size.
        compressed_size: u32,
        /// Uncompressed size.
        uncompressed_size: u32,
    },

    /// ZLIB trailer at unexpected offset.
    #[error(
        "ZLIB trailer is at offset {actual:#x} but {expected:#x} would be expected from block descriptors."
    )]
    ZlibTrailerOffsetInconsistency {
        /// Expected offset.
        expected: u64,
        /// Actual offset.
        actual: u64,
    },
}

impl ZTrailer {
    /// Reads a ZLIB trailer from `reader` using `endian`.  `bias` is the
    /// floating-point bias for confirmation against the trailer, and `zheader`
    /// is the previously read ZLIB header.  Uses `warn` to report warnings.
    pub fn read<R>(
        reader: &mut R,
        endian: Endian,
        bias: f64,
        zheader: &RawZHeader,
        warn: &mut dyn FnMut(Warning),
    ) -> Result<Option<ZTrailer>, Error>
    where
        R: Read + Seek,
    {
        let start_offset = reader.stream_position()?;
        if reader
            .seek(SeekFrom::Start(zheader.ztrailer_offset))
            .is_err()
        {
            return Ok(None);
        }
        let inner = RawZTrailer::read_options(reader, endian, ()).map_err(|e| Error {
            offsets: Some(zheader.ztrailer_offset..zheader.ztrailer_offset + zheader.ztrailer_len),
            details: ZTrailerError::from(e).into(),
        })?;
        if inner.int_bias as f64 != -bias {
            Err(ZTrailerError::WrongZlibTrailerBias {
                actual: inner.int_bias,
                expected: -bias,
            }
            .into())
        } else if inner.zero != 0 {
            Err(ZTrailerError::WrongZlibTrailerZero(inner.zero).into())
        } else if inner.block_size != 0x3ff000 {
            Err(ZTrailerError::WrongZlibTrailerBlockSize(inner.block_size).into())
        } else if let expected_n_blocks = (zheader.ztrailer_len - 24) / 24
            && inner.blocks.len() as u64 != expected_n_blocks
        {
            Err(ZTrailerError::BadZlibTrailerNBlocks {
                n_blocks: inner.blocks.len(),
                expected_n_blocks,
                ztrailer_len: zheader.ztrailer_len,
            }
            .into())
        } else {
            Ok(())
        }
        .map_err(|details| Error::new(Some(start_offset..start_offset + 24), details))?;

        let mut expected_uncmp_ofs = zheader.zheader_offset;
        let mut expected_cmp_ofs = zheader.zheader_offset + 24;
        for (index, block) in inner.blocks.iter().enumerate() {
            let block_start = start_offset + 24 + 24 * index as u64;
            let block_offsets = block_start..block_start + 24;

            if block.uncompressed_ofs != expected_uncmp_ofs {
                Err(ZTrailerError::ZlibTrailerBlockWrongUncmpOfs {
                    index,
                    actual: block.uncompressed_ofs,
                    expected: expected_cmp_ofs,
                }
                .into())
            } else if block.compressed_ofs != expected_cmp_ofs {
                Err(ZTrailerError::ZlibTrailerBlockWrongCmpOfs {
                    index,
                    actual: block.compressed_ofs,
                    expected: expected_cmp_ofs,
                }
                .into())
            } else if !block.has_plausible_sizes() {
                Err(ZTrailerError::ZlibExpansion {
                    index,
                    compressed_size: block.compressed_size,
                    uncompressed_size: block.uncompressed_size,
                }
                .into())
            } else {
                Ok(())
            }
            .map_err(|details| Error::new(Some(block_offsets.clone()), details))?;

            if index < inner.blocks.len() - 1 {
                if block.uncompressed_size != inner.block_size {
                    warn(Warning::new(
                        Some(block_offsets),
                        ZlibTrailerWarning::ZlibTrailerBlockWrongSize {
                            index,
                            actual: block.uncompressed_size,
                            expected: inner.block_size,
                        },
                    ));
                }
            } else if block.uncompressed_size > inner.block_size {
                warn(Warning::new(
                    Some(block_offsets),
                    ZlibTrailerWarning::ZlibTrailerBlockTooBig {
                        index,
                        actual: block.uncompressed_size,
                        max_expected: inner.block_size,
                    },
                ));
            }

            expected_cmp_ofs += block.compressed_size as u64;
            expected_uncmp_ofs += block.uncompressed_size as u64;
        }

        if expected_cmp_ofs != zheader.ztrailer_offset {
            return Err(Error::new(
                Some(start_offset..start_offset + 24 + 24 * inner.blocks.len() as u64),
                ZTrailerError::ZlibTrailerOffsetInconsistency {
                    expected: expected_cmp_ofs,
                    actual: zheader.ztrailer_offset,
                }
                .into(),
            ));
        }

        reader.seek(SeekFrom::Start(start_offset))?;
        Ok(Some(ZTrailer {
            offset: zheader.ztrailer_offset,
            inner,
        }))
    }
}
