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
    collections::HashMap,
    fmt::Write as _,
    fs::File,
    io::{BufWriter, Cursor, Error as IoError, ErrorKind, Seek, SeekFrom, Write},
    iter::repeat_n,
    path::Path,
};

use binrw::{BinWrite, Endian, Error as BinError};
use chrono::{Local, NaiveDateTime};
use either::Either;
use encoding_rs::Encoding;
use flate2::write::ZlibEncoder;
use itertools::zip_eq;
use smallvec::SmallVec;

use crate::{
    data::{Datum, RawString},
    dictionary::{CategoryLabels, Dictionary, MultipleResponseType},
    format::{DisplayPlain, Format},
    identifier::Identifier,
    output::spv::Zeros,
    sys::{
        encoding::codepage_from_encoding,
        raw::{
            records::{
                Compression, FloatInfoRecord, RawFormat, RawHeader, RawIntegerInfoRecord,
                RawVariableRecord, RawZHeader, RawZTrailer, ZBlock,
            },
            Magic,
        },
        ProductVersion,
    },
    variable::{Alignment, Attributes, Measure, ValueLabels, VarWidth},
};

/// System file format version.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum SystemFileVersion {
    /// Obsolete version.
    V2,

    /// Current version.
    #[default]
    V3,
}

/// Options for writing a system file.
#[derive(Clone, Debug)]
pub struct WriteOptions {
    /// How to compress (if at all) data in the system file.
    pub compression: Option<Compression>,

    /// System file version to write.
    pub sysfile_version: SystemFileVersion,

    /// Date and time to write to the file.
    pub timestamp: NaiveDateTime,

    /// Product name.
    ///
    /// Only the first 40 bytes are written.
    pub product_name: Cow<'static, str>,

    /// Product version number.
    ///
    /// The default is taken from `CARGO_PKG_VERSION`.
    pub product_version: ProductVersion,
}

impl Default for WriteOptions {
    fn default() -> Self {
        Self {
            compression: Some(Compression::Simple),
            sysfile_version: Default::default(),
            timestamp: Local::now().naive_local(),
            product_name: Cow::from(concat!("GNU PSPP (Rust) ", env!("CARGO_PKG_VERSION"))),
            product_version: ProductVersion::VERSION,
        }
    }
}

impl WriteOptions {
    /// Constructs a new set of default options.
    pub fn new() -> Self {
        Self::default()
    }

    /// Returns `self` with the compression format set to `compression`.
    pub fn with_compression(self, compression: Option<Compression>) -> Self {
        Self {
            compression,
            ..self
        }
    }

    /// Returns `self` with the timestamp to be written set to `timestamp`.
    pub fn with_timestamp(self, timestamp: NaiveDateTime) -> Self {
        Self { timestamp, ..self }
    }

    /// Returns `self` with the system file version set to `sysfile_version`.
    pub fn with_sysfile_version(self, sysfile_version: SystemFileVersion) -> Self {
        Self {
            sysfile_version,
            ..self
        }
    }

    /// Returns `self` with the product name set to `product_name`.
    pub fn with_product_name(self, product_name: Cow<'static, str>) -> Self {
        Self {
            product_name,
            ..self
        }
    }

    /// Returns `self` with the product version set to `product_version`.
    pub fn with_product_version(self, product_version: ProductVersion) -> Self {
        Self {
            product_version,
            ..self
        }
    }

    /// Writes `dictionary` to `path` in system file format.  Returns a [Writer]
    /// that can be used for writing cases to the new file.
    pub fn write_file(
        self,
        dictionary: &Dictionary,
        path: impl AsRef<Path>,
    ) -> Result<Writer<BufWriter<File>>, BinError> {
        self.write_writer(dictionary, BufWriter::new(File::create(path)?))
    }

    /// Writes `dictionary` to `writer` in system file format.  Returns a
    /// [Writer] that can be used for writing cases to the new file.
    pub fn write_writer<W>(
        self,
        dictionary: &Dictionary,
        mut writer: W,
    ) -> Result<Writer<W>, BinError>
    where
        W: Write + Seek + 'static,
    {
        let mut dict_writer = DictionaryWriter::new(&self, &mut writer, dictionary);
        dict_writer.write()?;
        let DictionaryWriter { case_vars, .. } = dict_writer;
        Writer::new(self, case_vars, writer)
    }

    /// Returns a [WriteOptions] with the given `compression` and the other
    /// members set to fixed values so that running at different times or with
    /// different crate names or versions won't change what's written to the
    /// file.
    #[cfg(test)]
    pub(super) fn reproducible(compression: Option<Compression>) -> Self {
        use chrono::{NaiveDate, NaiveTime};
        WriteOptions::new()
            .with_compression(compression)
            .with_timestamp(NaiveDateTime::new(
                NaiveDate::from_ymd_opt(2025, 7, 30).unwrap(),
                NaiveTime::from_hms_opt(15, 7, 55).unwrap(),
            ))
            .with_product_name(Cow::from("PSPP TEST DATA FILE"))
            .with_product_version(ProductVersion(1, 2, 3))
    }
}

struct DictionaryWriter<'a, W> {
    options: &'a WriteOptions,
    short_names: Vec<SmallVec<[Identifier; 1]>>,
    case_vars: Vec<CaseVar>,
    writer: &'a mut W,
    dictionary: &'a Dictionary,
}

fn put_attributes(attributes: &Attributes, s: &mut String) {
    for (name, values) in attributes.iter(true) {
        write!(s, "{name}(").unwrap();
        for value in values {
            writeln!(s, "'{value}'").unwrap();
        }
        write!(s, ")").unwrap()
    }
}

const BIAS: f64 = 100.0;

fn encode_fixed_string<const N: usize>(s: &str, encoding: &'static Encoding) -> [u8; N] {
    let mut encoded = encoding.encode(s).0.into_owned();
    encoded.resize(N, b' ');
    encoded.try_into().unwrap()
}

impl<'a, W> DictionaryWriter<'a, W>
where
    W: Write + Seek,
{
    pub fn new(options: &'a WriteOptions, writer: &'a mut W, dictionary: &'a Dictionary) -> Self {
        Self {
            options,
            short_names: dictionary.short_names(),
            case_vars: dictionary
                .variables
                .iter()
                .map(|variable| CaseVar::new(variable.width))
                .collect::<Vec<_>>(),
            writer,
            dictionary,
        }
    }

    pub fn write(&mut self) -> Result<(), BinError> {
        self.write_header()?;
        self.write_variables()?;
        self.write_value_labels()?;
        self.write_documents()?;
        self.write_integer_record()?;
        self.write_float_record()?;
        self.write_variable_sets()?;
        self.write_mrsets(true)?;
        self.write_variable_display_parameters()?;
        self.write_long_variable_names()?;
        self.write_very_long_strings()?;
        self.write_long_string_value_labels()?;
        self.write_long_string_missing_values()?;
        self.write_data_file_attributes()?;
        self.write_variable_attributes()?;
        self.write_mrsets(false)?;
        self.write_encoding()?;
        (999u32, 0u32).write_le(self.writer)
    }

    fn write_header(&mut self) -> Result<(), BinError> {
        fn as_byte_array<const N: usize>(s: String) -> [u8; N] {
            let mut bytes = s.into_bytes();
            bytes.resize(N, b' ');
            bytes.try_into().unwrap()
        }

        fn count_variable_positions(case_vars: &[CaseVar]) -> u32 {
            case_vars
                .iter()
                .map(CaseVar::n_variable_positions)
                .sum::<usize>() as u32
        }

        let header = RawHeader {
            magic: if self.options.compression == Some(Compression::ZLib) {
                Magic::Zsav
            } else {
                Magic::Sav
            }
            .into(),
            eye_catcher: encode_fixed_string(
                &format!("@(#) SPSS DATA FILE {}", &self.options.product_name),
                self.dictionary.encoding(),
            ),
            layout_code: 2,
            nominal_case_size: count_variable_positions(&self.case_vars),
            compression_code: match self.options.compression {
                Some(Compression::Simple) => 1,
                Some(Compression::ZLib) => 2,
                None => 0,
            },
            weight_index: if let Some(weight_index) = self.dictionary.weight_index() {
                count_variable_positions(&self.case_vars[..weight_index]) + 1
            } else {
                0
            },
            n_cases: u32::MAX,
            bias: BIAS,
            creation_date: as_byte_array(self.options.timestamp.format("%d %b %y").to_string()),
            creation_time: as_byte_array(self.options.timestamp.format("%H:%M:%S").to_string()),
            file_label: as_byte_array(self.dictionary.file_label.clone().unwrap_or_default()),
        };
        header.write_le(self.writer)
    }

    fn write_variables(&mut self) -> Result<(), BinError> {
        for (variable, short_names) in self
            .dictionary
            .variables
            .iter()
            .zip(self.short_names.iter())
        {
            let mut segments = variable.width.segments();
            let mut short_names = short_names.iter();
            let seg0_width = segments.next().unwrap();
            let name0 = short_names.next().unwrap();
            let record = RawVariableRecord {
                width: seg0_width.as_string_width().unwrap_or(0) as i32,
                has_variable_label: variable.label.is_some() as u32,
                missing_value_code: if !variable.width.is_long_string() {
                    let n = variable.missing_values().values().len() as i32;
                    match variable.missing_values().range() {
                        Some(_) => -(n + 2),
                        None => n,
                    }
                } else {
                    0
                },
                print_format: to_raw_format(variable.print_format, seg0_width),
                write_format: to_raw_format(variable.write_format, seg0_width),
                name: encode_fixed_string(name0, variable.encoding()),
            };
            (2u32, record).write_le(self.writer)?;

            // Variable label.
            if let Some(label) = variable.label() {
                let label = variable.encoding().encode(label).0;
                let len = label.len().min(255) as u32;
                let padded_len = len.next_multiple_of(4);
                (len, &*label, Zeros((padded_len - len) as usize)).write_le(self.writer)?;
            }

            // Missing values.
            if !variable.width.is_long_string() {
                if let Some(range) = variable.missing_values().range() {
                    (
                        range.low().unwrap_or(f64::MIN),
                        range.high().unwrap_or(f64::MAX),
                    )
                        .write_le(self.writer)?;
                }
                let pad = variable
                    .width
                    .as_string_width()
                    .map_or(0, |width| 8 - width);
                for value in variable.missing_values().values() {
                    (value, Zeros(pad)).write_le(self.writer)?;
                }
            }
            write_variable_continuation_records(&mut self.writer, seg0_width)?;

            // Write additional segments for very long string variables.
            for (width, name) in segments.zip(short_names) {
                let format: RawFormat = Format::default_for_width(width).try_into().unwrap();
                (
                    2u32,
                    RawVariableRecord {
                        width: width.as_string_width().unwrap() as i32,
                        has_variable_label: 0,
                        missing_value_code: 0,
                        print_format: format,
                        write_format: format,
                        name: encode_fixed_string(name, variable.encoding()),
                    },
                )
                    .write_le(self.writer)?;
                write_variable_continuation_records(&mut self.writer, width)?;
            }
        }

        fn write_variable_continuation_records<W>(
            mut writer: W,
            width: VarWidth,
        ) -> Result<(), BinError>
        where
            W: Write + Seek,
        {
            let continuation = (
                2u32,
                RawVariableRecord {
                    width: -1,
                    has_variable_label: 0,
                    missing_value_code: 0,
                    print_format: RawFormat(0),
                    write_format: RawFormat(0),
                    name: [0; 8],
                },
            );
            for _ in 1..width.n_chunks().unwrap() {
                continuation.write_le(&mut writer)?;
            }
            Ok(())
        }

        fn to_raw_format(mut format: Format, width: VarWidth) -> RawFormat {
            format.resize(width);
            RawFormat::try_from(format).unwrap()
        }

        Ok(())
    }

    /// Writes value label records, except for long string variables.
    fn write_value_labels(&mut self) -> Result<(), BinError> {
        // Collect identical sets of value labels.
        let mut sets = HashMap::<&ValueLabels, Vec<_>>::new();
        let mut index = 1usize;
        for variable in &self.dictionary.variables {
            if !variable.width.is_long_string() && !variable.value_labels.is_empty() {
                sets.entry(&variable.value_labels)
                    .or_default()
                    .push(index as u32);
            }
            index += variable
                .width
                .segments()
                .map(|w| w.n_chunks().unwrap())
                .sum::<usize>();
        }

        for (value_labels, variables) in sets {
            // Label record.
            (3u32, value_labels.0.len() as u32).write_le(self.writer)?;
            for (datum, label) in &value_labels.0 {
                let datum_padding = datum.width().as_string_width().map_or(0, |width| 8 - width);
                let label = &*self.dictionary.encoding().encode(label).0;
                let label = if label.len() > 255 {
                    &label[..255]
                } else {
                    label
                };
                let label_padding = (1 + label.len()).next_multiple_of(8) - (1 + label.len());
                (
                    datum,
                    Zeros(datum_padding),
                    label.len() as u8,
                    label,
                    Zeros(label_padding),
                )
                    .write_le(self.writer)?;
            }

            // Variable record.
            (4u32, variables.len() as u32, variables).write_le(self.writer)?;
        }
        Ok(())
    }

    fn write_documents(&mut self) -> Result<(), BinError> {
        if !self.dictionary.documents.is_empty() {
            (6u32, self.dictionary.documents.len() as u32).write_le(self.writer)?;
            for line in &self.dictionary.documents {
                Padded::exact(&self.dictionary.encoding().encode(line).0, 80, b' ')
                    .write_le(self.writer)?;
            }
        }
        Ok(())
    }

    fn write_integer_record(&mut self) -> Result<(), BinError> {
        (
            7u32,
            3u32,
            4u32,
            8u32,
            RawIntegerInfoRecord {
                version: self.options.product_version,
                machine_code: -1,
                floating_point_rep: 1,
                compression_code: 1,
                endianness: {
                    // We always write files in little-endian.
                    2
                },
                character_code: codepage_from_encoding(self.dictionary.encoding()) as i32,
            },
        )
            .write_le(self.writer)
    }

    fn write_float_record(&mut self) -> Result<(), BinError> {
        (
            7u32,
            4u32,
            8u32,
            3u32,
            FloatInfoRecord {
                sysmis: f64::MIN,
                highest: f64::MAX,
                lowest: f64::MIN.next_up(),
            },
        )
            .write_le(self.writer)
    }

    fn write_variable_sets(&mut self) -> Result<(), BinError> {
        let mut s = String::new();
        for set in &self.dictionary.variable_sets() {
            write!(&mut s, "{}= ", set.name()).unwrap();
            for (index, variable) in set.variables().iter().enumerate() {
                let prefix = if index > 0 { " " } else { "" };
                write!(&mut s, "{prefix}{}", &variable.name).unwrap();
            }
            writeln!(&mut s).unwrap();
        }
        self.write_string_record(5, &s)
    }

    /// If `pre_v14` is true, writes only sets supported by SPSS before release
    /// 14, otherwise writes sets supported only by later versions.
    fn write_mrsets(&mut self, pre_v14: bool) -> Result<(), BinError> {
        let mut output = Vec::new();
        for set in self
            .dictionary
            .mrsets()
            .iter()
            .filter(|set| set.mr_type().supported_before_v14() == pre_v14)
        {
            output.extend_from_slice(&self.dictionary.encoding().encode(set.name()).0[..]);
            output.push(b'=');
            match set.mr_type() {
                MultipleResponseType::MultipleDichotomy { datum, labels } => {
                    let leader = match labels {
                        CategoryLabels::VarLabels => b"D".as_slice(),
                        CategoryLabels::CountedValues {
                            use_var_label_as_mrset_label: true,
                        } => b"E 11 ".as_slice(),
                        CategoryLabels::CountedValues {
                            use_var_label_as_mrset_label: false,
                        } => b"E 1 ".as_slice(),
                    };
                    output.extend_from_slice(leader);

                    let mut value = match datum {
                        Datum::Number(Some(number)) => {
                            number.display_plain().to_string().into_bytes()
                        }
                        Datum::Number(None) => vec![b'.'],
                        Datum::String(raw_string) => raw_string.0.clone(),
                    };
                    write!(&mut output, "{} ", value.len()).unwrap();
                    output.append(&mut value);
                    output.push(b' ');
                }
                MultipleResponseType::MultipleCategory => write!(&mut output, "C ").unwrap(),
            }

            let label = if set.mr_type().label_from_var_label() {
                Cow::from(&[])
            } else {
                self.dictionary.encoding().encode(set.label()).0
            };
            write!(&mut output, "{} ", label.len()).unwrap();
            output.extend_from_slice(&label[..]);

            for variable in set.variables().dict_indexes().iter().copied() {
                // Only lowercase ASCII characters because other characters
                // might expand upon lowercasing.
                let short_name = self.short_names[variable][0].as_str().to_ascii_lowercase();
                output.push(b' ');
                output.extend_from_slice(&self.dictionary.encoding().encode(&short_name).0);
            }
            output.push(b'\n');
        }
        self.write_bytes_record(if pre_v14 { 7 } else { 19 }, &output)
    }

    fn write_variable_display_parameters(&mut self) -> Result<(), BinError> {
        (
            7u32,
            11u32,
            4u32,
            self.case_vars
                .iter()
                .map(CaseVar::n_segments)
                .sum::<usize>() as u32
                * 3,
        )
            .write_le(self.writer)?;
        for variable in &self.dictionary.variables {
            let measure = match variable.measure {
                None => 0,
                Some(Measure::Nominal) => 1,
                Some(Measure::Ordinal) => 2,
                Some(Measure::Scale) => 3,
            };
            let alignment = match variable.alignment {
                Alignment::Left => 0,
                Alignment::Right => 1,
                Alignment::Center => 2,
            };
            for (index, segment) in variable.width.segments().enumerate() {
                let display_width = match index {
                    0 => variable.display_width,
                    _ => segment.default_display_width(),
                };
                (measure, display_width, alignment).write_le(self.writer)?;
            }
        }
        Ok(())
    }

    fn write_long_variable_names(&mut self) -> Result<(), BinError> {
        if self.options.sysfile_version == SystemFileVersion::V2 {
            return Ok(());
        }

        let mut s = String::new();
        for (index, variable) in self.dictionary.variables.iter().enumerate() {
            if index > 0 {
                s.push('\t');
            }
            write!(&mut s, "{}={}", &self.short_names[index][0], variable.name).unwrap();
        }
        self.write_string_record(13, &s)
    }

    fn write_very_long_strings(&mut self) -> Result<(), BinError> {
        let mut s = String::new();
        for (index, variable) in self.dictionary.variables.iter().enumerate() {
            if variable.width.is_very_long_string() {
                let width = variable.width.as_string_width().unwrap();
                write!(&mut s, "{}={width:05}\0\t", &self.short_names[index][0],).unwrap();
            }
        }
        self.write_string_record(14, &s)
    }

    fn write_long_string_value_labels(&mut self) -> Result<(), BinError> {
        let mut body = Vec::new();
        let mut cursor = Cursor::new(&mut body);
        for variable in &self.dictionary.variables {
            if variable.value_labels.is_empty() || !variable.width.is_long_string() {
                continue;
            }
            let name = self.dictionary.encoding().encode(&variable.name).0;
            (
                name.len() as u32,
                &name[..],
                variable.width.as_string_width().unwrap() as u32,
                variable.value_labels.0.len() as u32,
            )
                .write_le(&mut cursor)?;

            for (value, label) in &variable.value_labels.0 {
                let value = value.as_string().unwrap();
                let label = self.dictionary.encoding().encode(label).0;
                (
                    value.len() as u32,
                    value.raw_string_bytes(),
                    label.len() as u32,
                    &label[..],
                )
                    .write_le(&mut cursor)?;
            }
        }
        self.write_bytes_record(21, &body)
    }

    fn write_long_string_missing_values(&mut self) -> Result<(), BinError> {
        let mut body = Vec::new();
        let mut cursor = Cursor::new(&mut body);
        for variable in &self.dictionary.variables {
            if variable.missing_values().is_empty() || !variable.width.is_long_string() {
                break;
            }
            let name = self.dictionary.encoding().encode(&variable.name).0;
            (
                name.len() as u32,
                &name[..],
                variable.missing_values().values().len() as u8,
                8u32,
            )
                .write_le(&mut cursor)?;

            for value in variable.missing_values().values() {
                let value = value.as_string().unwrap().raw_string_bytes();
                let bytes = value.get(..8).unwrap_or(value);
                Padded::exact(bytes, 8, b' ').write_le(&mut cursor).unwrap();
            }
        }
        self.write_bytes_record(22, &body)
    }

    fn write_data_file_attributes(&mut self) -> Result<(), BinError> {
        if self.options.sysfile_version != SystemFileVersion::V3 {
            return Ok(());
        }
        let mut s = String::new();
        put_attributes(&self.dictionary.attributes, &mut s);
        self.write_string_record(17, &s)
    }

    fn write_variable_attributes(&mut self) -> Result<(), BinError> {
        if self.options.sysfile_version != SystemFileVersion::V3 {
            return Ok(());
        }
        let mut s = String::new();
        for (index, variable) in self.dictionary.variables.iter().enumerate() {
            let mut attributes = variable.attributes.clone();
            attributes.0.insert(
                Identifier::new("$@Role").unwrap(),
                vec![i32::from(variable.role).to_string()],
            );

            if index > 0 {
                s.push('/');
            }
            write!(&mut s, "{}:", &variable.name).unwrap();
            put_attributes(&attributes, &mut s);
        }
        self.write_string_record(18, &s)
    }

    fn write_encoding(&mut self) -> Result<(), BinError> {
        self.write_string_record(20, self.dictionary.encoding().name())
    }

    fn write_bytes_record(&mut self, subtype: u32, bytes: &[u8]) -> Result<(), BinError> {
        if !bytes.is_empty() {
            (7u32, subtype, 1u32, bytes.len() as u32, bytes).write_le(self.writer)
        } else {
            Ok(())
        }
    }

    fn write_string_record(&mut self, subtype: u32, s: &str) -> Result<(), BinError> {
        self.write_bytes_record(subtype, &self.dictionary.encoding().encode(s).0)
    }
}

#[derive(BinWrite)]
struct Padded<'a> {
    bytes: &'a [u8],
    padding: Pad,
}

impl<'a> Padded<'a> {
    pub fn exact(bytes: &'a [u8], length: usize, pad: u8) -> Self {
        let min = bytes.len().min(length);
        Self {
            bytes: &bytes[..min],
            padding: Pad::new(length - min, pad),
        }
    }
}

pub struct Pad {
    n: usize,
    pad: u8,
}

impl Pad {
    pub fn new(n: usize, pad: u8) -> Self {
        Self { n, pad }
    }
}

impl BinWrite for Pad {
    type Args<'a> = ();

    fn write_options<W: Write + Seek>(
        &self,
        writer: &mut W,
        _endian: Endian,
        _args: Self::Args<'_>,
    ) -> binrw::BinResult<()> {
        for _ in 0..self.n {
            writer.write_all(&[self.pad])?;
        }
        Ok(())
    }
}

impl<B> BinWrite for Datum<B>
where
    B: RawString,
{
    type Args<'a> = ();

    fn write_options<W: Write + Seek>(
        &self,
        writer: &mut W,
        endian: binrw::Endian,
        _: (),
    ) -> binrw::BinResult<()> {
        match self {
            Datum::Number(number) => number.unwrap_or(f64::MIN).write_options(writer, endian, ()),
            Datum::String(raw_string) => {
                raw_string
                    .raw_string_bytes()
                    .write_options(writer, endian, ())
            }
        }
    }
}

#[derive(Debug)]
struct StringSegment {
    data_bytes: usize,
    padding_bytes: usize,
}

enum CaseVar {
    Numeric,
    String(SmallVec<[StringSegment; 1]>),
}

impl CaseVar {
    fn new(width: VarWidth) -> Self {
        match width {
            VarWidth::Numeric => Self::Numeric,
            VarWidth::String(w) => {
                let mut encoding = SmallVec::<[StringSegment; 1]>::new();
                let mut remaining = w as usize;
                for segment in width.segments() {
                    let segment = segment.as_string_width().unwrap().next_multiple_of(8);
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
                CaseVar::String(encoding)
            }
        }
    }
    fn n_segments(&self) -> usize {
        match self {
            CaseVar::Numeric => 1,
            CaseVar::String(encoding) => encoding.len(),
        }
    }
    fn n_variable_positions(&self) -> usize {
        match self {
            CaseVar::Numeric => 1,
            CaseVar::String(encoding) => encoding
                .iter()
                .map(|segment| (segment.data_bytes + segment.padding_bytes) / 8)
                .sum(),
        }
    }
}

/// System file writer.
pub struct Writer<W>
where
    W: Write + Seek,
{
    compression: Option<Compression>,
    case_vars: Vec<CaseVar>,
    opcodes: Vec<u8>,
    data: Vec<u8>,
    inner: Option<Either<W, ZlibWriter<W>>>,
    n_cases: u64,
}

pub struct WriterInner<'a, W: Write> {
    case_vars: &'a [CaseVar],
    opcodes: &'a mut Vec<u8>,
    data: &'a mut Vec<u8>,
    inner: &'a mut W,
}

impl<'a, W> WriterInner<'a, W>
where
    W: Write + Seek,
{
    fn new(
        case_vars: &'a [CaseVar],
        opcodes: &'a mut Vec<u8>,
        data: &'a mut Vec<u8>,
        inner: &'a mut W,
    ) -> Self {
        Self {
            case_vars,
            opcodes,
            data,
            inner,
        }
    }
    fn flush_compressed(&mut self) -> Result<(), BinError> {
        if !self.opcodes.is_empty() {
            self.opcodes.resize(8, 0);
            self.inner.write_all(self.opcodes)?;
            self.inner.write_all(self.data)?;
            self.opcodes.clear();
            self.data.clear();
        }
        Ok(())
    }
    fn put_opcode(&mut self, opcode: u8) -> Result<(), BinError> {
        if self.opcodes.len() >= 8 {
            self.flush_compressed()?;
        }
        self.opcodes.push(opcode);
        Ok(())
    }

    fn write_case_uncompressed<B>(
        &mut self,
        case: impl Iterator<Item = Datum<B>>,
    ) -> Result<(), BinError>
    where
        B: RawString,
    {
        for (var, datum) in zip_eq(self.case_vars, case) {
            match var {
                CaseVar::Numeric => datum
                    .as_number()
                    .unwrap()
                    .unwrap_or(f64::MIN)
                    .write_le(&mut self.inner)?,
                CaseVar::String(encoding) => {
                    let mut s = datum.as_string().unwrap().raw_string_bytes();
                    for segment in encoding {
                        let spaces = segment.data_bytes.saturating_sub(s.len());
                        let data_bytes = segment.data_bytes - spaces;

                        let data;
                        (data, s) = s.split_at(data_bytes);
                        (
                            data,
                            Pad::new(spaces, b' '),
                            Pad::new(segment.padding_bytes, 0),
                        )
                            .write_le(&mut self.inner)?;
                    }
                }
            }
        }
        Ok(())
    }
    fn write_case_compressed<B>(
        &mut self,
        case: impl Iterator<Item = Datum<B>>,
    ) -> Result<(), BinError>
    where
        B: RawString,
    {
        for (var, datum) in zip_eq(self.case_vars, case) {
            match var {
                CaseVar::Numeric => match datum.as_number().unwrap() {
                    None => self.put_opcode(255)?,
                    Some(number) => {
                        if (1.0 - BIAS..=251.0 - BIAS).contains(&number) && number == number.trunc()
                        {
                            self.put_opcode((number + BIAS) as u8)?
                        } else {
                            self.put_opcode(253)?;
                            self.data.extend_from_slice(&number.to_le_bytes());
                        }
                    }
                },

                CaseVar::String(encoding) => {
                    let mut s = datum.as_string().unwrap().raw_string_bytes();
                    for segment in encoding {
                        let excess = segment.data_bytes.saturating_sub(s.len());
                        let data_bytes = segment.data_bytes - excess;
                        let padding_bytes = segment.padding_bytes + excess;

                        let data;
                        (data, s) = s.split_at(data_bytes);

                        let (chunks, remainder) = data.as_chunks::<8>();
                        for chunk in chunks {
                            if chunk == b"        " {
                                self.put_opcode(254)?;
                            } else {
                                self.put_opcode(253)?;
                                self.data.extend_from_slice(chunk);
                            }
                        }
                        if !remainder.is_empty() {
                            if remainder.iter().all(|c| *c == b' ') {
                                self.put_opcode(254)?;
                            } else {
                                self.put_opcode(253)?;
                                self.data.extend_from_slice(remainder);
                                self.data.extend(repeat_n(b' ', 8 - remainder.len()));
                            }
                        }
                        for _ in 0..padding_bytes / 8 {
                            self.put_opcode(254)?;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

impl<W> Writer<W>
where
    W: Write + Seek,
{
    fn new(options: WriteOptions, case_vars: Vec<CaseVar>, inner: W) -> Result<Self, BinError> {
        Ok(Self {
            compression: options.compression,
            case_vars,
            opcodes: Vec::with_capacity(8),
            data: Vec::with_capacity(64),
            n_cases: 0,
            inner: match options.compression {
                Some(Compression::ZLib) => Some(Either::Right(ZlibWriter::new(inner)?)),
                _ => Some(Either::Left(inner)),
            },
        })
    }

    /// Finishes writing the file, flushing buffers and updating headers to
    /// match the final case counts.
    pub fn finish(mut self) -> Result<Option<W>, BinError> {
        self.try_finish()
    }

    /// Tries to finish writing the file, flushing buffers and updating headers
    /// to match the final case counts.
    ///
    /// # Panic
    ///
    /// Attempts to write more cases after calling this function may will panic.
    pub fn try_finish(&mut self) -> Result<Option<W>, BinError> {
        let Some(inner) = self.inner.take() else {
            return Ok(None);
        };

        let mut inner = match inner {
            Either::Left(mut inner) => {
                WriterInner::new(
                    &self.case_vars,
                    &mut self.opcodes,
                    &mut self.data,
                    &mut inner,
                )
                .flush_compressed()?;
                inner
            }
            Either::Right(mut zlib_writer) => {
                WriterInner::new(
                    &self.case_vars,
                    &mut self.opcodes,
                    &mut self.data,
                    &mut zlib_writer,
                )
                .flush_compressed()?;
                zlib_writer.finish()?
            }
        };
        if let Ok(n_cases) = u32::try_from(self.n_cases) {
            if inner.seek(SeekFrom::Start(80)).is_ok() {
                let _ = inner.write_all(&n_cases.to_le_bytes());
            }
        }
        Ok(Some(inner))
    }

    /// Writes `case` to the system file.
    ///
    /// # Panic
    ///
    /// Panics if [try_finish](Self::try_finish) has been called.
    pub fn write_case<B>(
        &mut self,
        case: impl IntoIterator<Item = Datum<B>>,
    ) -> Result<(), BinError>
    where
        B: RawString,
    {
        match self.inner.as_mut().unwrap() {
            Either::Left(inner) => {
                let mut inner =
                    WriterInner::new(&self.case_vars, &mut self.opcodes, &mut self.data, inner);
                match self.compression {
                    Some(_) => inner.write_case_compressed(case.into_iter())?,
                    None => inner.write_case_uncompressed(case.into_iter())?,
                }
            }
            Either::Right(inner) => {
                WriterInner::new(&self.case_vars, &mut self.opcodes, &mut self.data, inner)
                    .write_case_compressed(case.into_iter())?
            }
        }
        self.n_cases += 1;
        Ok(())
    }
}

impl<W> Drop for Writer<W>
where
    W: Write + Seek,
{
    fn drop(&mut self) {
        let _ = self.try_finish();
    }
}

struct ZlibWriter<W>
where
    W: Write + Seek,
{
    header: RawZHeader,
    trailer: RawZTrailer,
    encoder: ZlibEncoder<Vec<u8>>,
    inner: W,
}

impl<W> ZlibWriter<W>
where
    W: Write + Seek,
{
    fn new(mut inner: W) -> Result<Self, BinError> {
        let header = RawZHeader {
            zheader_offset: inner.stream_position()?,
            ztrailer_offset: 0,
            ztrailer_len: 0,
        };
        header.write_le(&mut inner)?;
        Ok(Self {
            header,
            trailer: RawZTrailer {
                int_bias: -BIAS as i64,
                zero: 0,
                block_size: ZBLOCK_SIZE as u32,
                blocks: Vec::new(),
            },
            encoder: ZlibEncoder::new(Vec::new(), flate2::Compression::new(1)),
            inner,
        })
    }

    fn flush_block(&mut self) -> std::io::Result<()> {
        let total_in = self.encoder.total_in();
        if total_in > 0 {
            let buf = self.encoder.reset(Vec::new())?;
            let total_out = buf.len();
            self.inner.write_all(&buf)?;
            self.encoder.reset(buf).unwrap();

            self.trailer.blocks.push(ZBlock {
                uncompressed_size: total_in as u32,
                compressed_size: total_out as u32,
                uncompressed_ofs: match self.trailer.blocks.last() {
                    Some(prev) => prev.uncompressed_ofs + prev.uncompressed_size as u64,
                    None => self.header.zheader_offset,
                },
                compressed_ofs: match self.trailer.blocks.last() {
                    Some(prev) => prev.compressed_ofs + prev.compressed_size as u64,
                    None => self.header.zheader_offset + 24,
                },
            });
        }
        Ok(())
    }

    fn finish(mut self) -> Result<W, BinError> {
        self.flush_block()?;
        let ztrailer_offset = self.inner.stream_position()?;
        self.trailer.write_le(&mut self.inner)?;
        let header = RawZHeader {
            zheader_offset: self.header.zheader_offset,
            ztrailer_offset,
            ztrailer_len: self.trailer.len() as u64,
        };
        self.inner.seek(SeekFrom::Start(header.zheader_offset))?;
        header.write_le(&mut self.inner)?;
        Ok(self.inner)
    }
}

const ZBLOCK_SIZE: u64 = 0x3ff000;

impl<W> Write for ZlibWriter<W>
where
    W: Write + Seek,
{
    fn write(&mut self, mut buf: &[u8]) -> Result<usize, IoError> {
        let n = buf.len();
        while !buf.is_empty() {
            if self.encoder.total_in() >= ZBLOCK_SIZE {
                self.flush_block()?;
            }

            let chunk = buf
                .len()
                .min((ZBLOCK_SIZE - self.encoder.total_in()) as usize);
            self.encoder.write_all(&buf[..chunk])?;
            buf = &buf[chunk..];
        }
        Ok(n)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl<W> Seek for ZlibWriter<W>
where
    W: Write + Seek,
{
    fn seek(&mut self, _pos: std::io::SeekFrom) -> Result<u64, IoError> {
        Err(IoError::from(ErrorKind::NotSeekable))
    }
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use binrw::{BinRead, Endian};
    use encoding_rs::UTF_8;
    use itertools::Itertools;
    use unicase::UniCase;

    use crate::{
        data::{ByteString, Datum, RawString},
        dictionary::{
            CategoryLabels, DictIndexMultipleResponseSet, DictIndexVariableSet, Dictionary,
            MultipleResponseType,
        },
        identifier::Identifier,
        sys::{
            raw::{
                records::{
                    DocumentRecord, Extension, RawHeader, RawVariableRecord, VariableRecord,
                },
                Decoder, VarTypes,
            },
            write::DictionaryWriter,
            ReadOptions, WriteOptions,
        },
        variable::{Alignment, Attributes, Measure, MissingValueRange, VarWidth, Variable},
    };

    /// Checks that the header record has the right nominal case size and weight
    /// index, even with long and very long string variables.
    #[test]
    fn header() {
        for variables in [
            (VarWidth::Numeric, 1),
            (VarWidth::String(1), 1),
            (VarWidth::String(8), 1),
            (VarWidth::String(15), 2),
            (VarWidth::String(255), 32),
            (VarWidth::String(256), 33),
            (VarWidth::String(20000), 79 * 32 + 12),
        ]
        .iter()
        .copied()
        .combinations_with_replacement(4)
        {
            let mut dictionary = Dictionary::new(UTF_8);
            let mut expected_case_size = 0;
            let mut weight_indexes = vec![(None, 0)];
            for (index, (width, n_chunks)) in variables.into_iter().enumerate() {
                let index = dictionary
                    .add_var(Variable::new(
                        Identifier::new(format!("v{index}")).unwrap(),
                        width,
                        UTF_8,
                    ))
                    .unwrap();
                if width.is_numeric() {
                    weight_indexes.push((Some(index), expected_case_size + 1));
                }
                expected_case_size += n_chunks;
            }
            for (weight_index, expected_weight_index) in weight_indexes {
                dictionary.set_weight(weight_index).unwrap();

                let mut raw = Vec::new();
                DictionaryWriter::new(
                    &WriteOptions::reproducible(None),
                    &mut Cursor::new(&mut raw),
                    &dictionary,
                )
                .write_header()
                .unwrap();
                let header = RawHeader::read_le(&mut Cursor::new(&raw)).unwrap();
                assert_eq!(header.weight_index, expected_weight_index as u32);
                assert_eq!(header.nominal_case_size, expected_case_size as u32);
            }
        }
    }

    /// Checks that variable records are followed by the right number of
    /// continuation records, and that very long string variables have the right
    /// number of segment variables.
    #[test]
    fn variables_widths() {
        let variables = [
            (VarWidth::Numeric, vec![0]),
            (VarWidth::String(1), vec![1]),
            (VarWidth::String(8), vec![8]),
            (VarWidth::String(15), vec![15, -1]),
            (
                VarWidth::String(255),
                std::iter::once(255)
                    .chain(std::iter::repeat_n(-1, 31))
                    .collect(),
            ),
            (
                VarWidth::String(256),
                std::iter::once(255)
                    .chain(std::iter::repeat_n(-1, 31))
                    .chain(std::iter::once(4))
                    .collect(),
            ),
            (
                VarWidth::String(20000),
                std::iter::once(255)
                    .chain(std::iter::repeat_n(-1, 31))
                    .cycle()
                    .take(32 * 79)
                    .chain(std::iter::once(92))
                    .chain(std::iter::repeat_n(-1, 11))
                    .collect(),
            ),
        ];
        for variables in variables.iter().combinations_with_replacement(3) {
            let mut dictionary = Dictionary::new(UTF_8);
            for (index, (width, _)) in variables.iter().enumerate() {
                dictionary
                    .add_var(Variable::new(
                        Identifier::new(format!("v{index}")).unwrap(),
                        *width,
                        UTF_8,
                    ))
                    .unwrap();
            }

            let widths = variables
                .into_iter()
                .map(|(_, w)| w.iter())
                .flatten()
                .copied();

            let mut raw = Vec::new();
            DictionaryWriter::new(
                &WriteOptions::reproducible(None),
                &mut Cursor::new(&mut raw),
                &dictionary,
            )
            .write_variables()
            .unwrap();

            let mut cursor = Cursor::new(&raw);
            let mut records = Vec::new();
            while cursor.position() < raw.len() as u64 {
                assert_eq!(u32::read_le(&mut cursor).unwrap(), 2);
                records.push(RawVariableRecord::read_le(&mut cursor).unwrap());
            }
            for (record, expected_width) in records.iter().zip_eq(widths.into_iter()) {
                assert_eq!(record.width, expected_width);
            }
        }
    }

    /// Checks that missing values are written correctly.
    #[test]
    fn variables_missing_values() {
        let test_cases = [
            (VarWidth::Numeric, vec![Datum::Number(Some(1.0))], None),
            (
                VarWidth::Numeric,
                vec![Datum::Number(Some(1.0)), Datum::Number(Some(2.0))],
                None,
            ),
            (
                VarWidth::Numeric,
                vec![
                    Datum::Number(Some(1.0)),
                    Datum::Number(Some(2.0)),
                    Datum::Number(Some(3.0)),
                ],
                None,
            ),
            (
                VarWidth::Numeric,
                vec![],
                Some(MissingValueRange::In {
                    low: 10.0,
                    high: 20.0,
                }),
            ),
            (
                VarWidth::Numeric,
                vec![],
                Some(MissingValueRange::From { low: 100.0 }),
            ),
            (
                VarWidth::Numeric,
                vec![],
                Some(MissingValueRange::To { high: 200.0 }),
            ),
            (
                VarWidth::Numeric,
                vec![Datum::Number(Some(1.0))],
                Some(MissingValueRange::In {
                    low: 10.0,
                    high: 20.0,
                }),
            ),
            (
                VarWidth::Numeric,
                vec![Datum::Number(Some(1.0))],
                Some(MissingValueRange::From { low: 100.0 }),
            ),
            (
                VarWidth::Numeric,
                vec![Datum::Number(Some(1.0))],
                Some(MissingValueRange::To { high: 200.0 }),
            ),
            (
                VarWidth::String(5),
                vec![Datum::String(ByteString::from("abcde"))],
                None,
            ),
            (
                VarWidth::String(5),
                vec![
                    Datum::String(ByteString::from("abcde")),
                    Datum::String(ByteString::from("qwioe")),
                ],
                None,
            ),
            (
                VarWidth::String(5),
                vec![
                    Datum::String(ByteString::from("abcde")),
                    Datum::String(ByteString::from("qwioe")),
                    Datum::String(ByteString::from("jksld")),
                ],
                None,
            ),
            (
                VarWidth::String(9),
                vec![
                    Datum::String(ByteString::from("abcdeasd")),
                    Datum::String(ByteString::from("qwioejdf")),
                    Datum::String(ByteString::from("jksldiwe")),
                ],
                None,
            ),
            (
                VarWidth::String(10),
                vec![
                    Datum::String(ByteString::from("abcdeasd")),
                    Datum::String(ByteString::from("qwioejdf")),
                ],
                None,
            ),
            (
                VarWidth::String(11),
                vec![Datum::String(ByteString::from("abcdeasd"))],
                None,
            ),
        ];

        for (width, values, range) in test_cases {
            let mut dictionary = Dictionary::new(UTF_8);
            let mut variable = Variable::new(Identifier::new("var").unwrap(), width, UTF_8);
            variable
                .missing_values_mut()
                .add_values(values.iter().map(|value| value.as_encoded(UTF_8).cloned()))
                .unwrap();
            if let Some(range) = &range {
                variable
                    .missing_values_mut()
                    .add_range(range.clone())
                    .unwrap();
            }
            dictionary.add_var(variable).unwrap();

            // Write and check variable records.
            let mut raw_variables = Vec::new();
            DictionaryWriter::new(
                &WriteOptions::reproducible(None),
                &mut Cursor::new(&mut raw_variables),
                &dictionary,
            )
            .write_variables()
            .unwrap();

            let mut cursor = Cursor::new(&raw_variables[4..]);
            let record =
                VariableRecord::read(&mut cursor, Endian::Little, &mut |_| panic!()).unwrap();
            if !width.is_long_string() {
                assert_eq!(&record.missing_values.values, &values);
            } else {
                assert_eq!(&record.missing_values.values, &vec![]);
            }
            assert_eq!(&record.missing_values.range, &range);

            // Write and check long string missing value record.
            let mut raw_long_missing = Vec::new();
            DictionaryWriter::new(
                &WriteOptions::reproducible(None),
                &mut Cursor::new(&mut raw_long_missing),
                &dictionary,
            )
            .write_long_string_missing_values()
            .unwrap();

            if width.is_long_string() {
                let mut cursor = Cursor::new(&raw_long_missing[4..]);
                let record = Extension::read(
                    &mut cursor,
                    Endian::Little,
                    &VarTypes::new(),
                    &mut |_| panic!(),
                )
                .unwrap()
                .unwrap()
                .as_long_string_missing_values()
                .unwrap()
                .clone()
                .decode(&mut Decoder::new(UTF_8, |_| panic!()));

                assert_eq!(record.values.len(), 1);
                assert_eq!(&record.values[0].var_name.0, &UniCase::new("var"));
                let actual = record.values[0]
                    .missing_values
                    .iter()
                    .map(|v| v.raw_string_bytes());
                let expected = values
                    .iter()
                    .map(|v| v.as_string().unwrap().raw_string_bytes());
                for (actual, expected) in actual.zip_eq(expected) {
                    assert_eq!(actual, expected);
                }
            } else {
                assert_eq!(raw_long_missing.len(), 0);
            }
        }
    }

    /// Checks that value labels are written correctly.
    #[test]
    fn variables_value_labels() {
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
                    (Datum::Number(Some(3.0)), "Three"),
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

        for test_case in variables.iter().combinations_with_replacement(3) {
            let mut dictionary = Dictionary::new(UTF_8);
            for (index, (width, value_labels)) in test_case.iter().enumerate() {
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

            let raw = WriteOptions::new()
                .write_writer(&dictionary, Cursor::new(Vec::new()))
                .unwrap()
                .finish()
                .unwrap()
                .unwrap()
                .into_inner();
            let dictionary2 = ReadOptions::new(|_| panic!())
                .open_reader(Cursor::new(raw))
                .unwrap()
                .dictionary;

            for (expected, actual) in dictionary
                .variables
                .iter()
                .zip_eq(dictionary2.variables.iter())
            {
                assert_eq!(&expected.value_labels, &actual.value_labels);
            }
        }
    }

    #[test]
    fn documents() {
        let expected = vec![String::from("Line one"), String::from("Line two")];
        let mut dictionary = Dictionary::new(UTF_8);
        dictionary.documents = expected.clone();

        let mut raw = Vec::new();
        DictionaryWriter::new(
            &WriteOptions::reproducible(None),
            &mut Cursor::new(&mut raw),
            &dictionary,
        )
        .write_documents()
        .unwrap();

        let actual = DocumentRecord::read(&mut Cursor::new(&raw[4..]), Endian::Little)
            .unwrap()
            .decode(&mut Decoder::new(UTF_8, |_| panic!()))
            .lines
            .into_iter()
            .map(|mut s| {
                s.truncate(s.trim_end().len());
                s
            })
            .collect::<Vec<_>>();
        assert_eq!(&actual, &expected);
    }

    #[test]
    fn variable_sets() {
        let mut expected = Dictionary::new(UTF_8);
        for index in 0..10 {
            expected
                .add_var(Variable::new(
                    Identifier::new(format!("var{index}")).unwrap(),
                    VarWidth::Numeric,
                    UTF_8,
                ))
                .unwrap();
        }

        for (index, variables) in [vec![0], vec![1, 2], vec![3, 4, 5], vec![6, 7, 8, 9]]
            .into_iter()
            .enumerate()
        {
            expected.add_variable_set(DictIndexVariableSet {
                name: format!("Variable Set {index}"),
                variables,
            });
        }

        let raw = WriteOptions::new()
            .write_writer(&expected, Cursor::new(Vec::new()))
            .unwrap()
            .finish()
            .unwrap()
            .unwrap()
            .into_inner();
        let actual = ReadOptions::new(|_| panic!())
            .open_reader(Cursor::new(raw))
            .unwrap()
            .dictionary;

        assert!(actual
            .variable_sets()
            .iter()
            .eq(expected.variable_sets().iter()),);
    }

    /// Test writing multiple response sets.
    ///
    /// This is the example given in the documentation for the system file
    /// format.
    #[test]
    fn mrsets() {
        let mut dictionary = Dictionary::new(UTF_8);
        for (variables, width) in [
            ('a'..='g', VarWidth::Numeric),
            ('h'..='j', VarWidth::String(3)),
            ('k'..='m', VarWidth::Numeric),
            ('n'..='p', VarWidth::String(6)),
        ] {
            for variable in variables {
                dictionary
                    .add_var(Variable::new(
                        Identifier::new(variable.to_string()).unwrap(),
                        width,
                        UTF_8,
                    ))
                    .unwrap();
            }
        }
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("$a").unwrap(),
                label: String::from("my mcgroup"),
                mr_type: MultipleResponseType::MultipleCategory,
                variables: vec![0, 1, 2],
            })
            .unwrap();
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("$b").unwrap(),
                label: String::new(),
                mr_type: MultipleResponseType::MultipleDichotomy {
                    datum: Datum::Number(Some(55.0)),
                    labels: CategoryLabels::VarLabels,
                },
                variables: vec![6, 4, 5, 3],
            })
            .unwrap();
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("$c").unwrap(),
                label: String::from("mdgroup #2"),
                mr_type: MultipleResponseType::MultipleDichotomy {
                    datum: Datum::String("Yes".into()),
                    labels: CategoryLabels::VarLabels,
                },
                variables: vec![7, 8, 9],
            })
            .unwrap();
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("$d").unwrap(),
                label: String::from("third mdgroup"),
                mr_type: MultipleResponseType::MultipleDichotomy {
                    datum: Datum::Number(Some(34.0)),
                    labels: CategoryLabels::CountedValues {
                        use_var_label_as_mrset_label: false,
                    },
                },
                variables: vec![10, 11, 12],
            })
            .unwrap();
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("$e").unwrap(),
                label: String::new(),
                mr_type: MultipleResponseType::MultipleDichotomy {
                    datum: Datum::String("choice".into()),
                    labels: CategoryLabels::CountedValues {
                        use_var_label_as_mrset_label: true,
                    },
                },
                variables: vec![13, 14, 15],
            })
            .unwrap();

        fn get_mrsets(dictionary: &Dictionary, pre_v14: bool) -> String {
            let mut raw = Vec::new();
            DictionaryWriter::new(
                &WriteOptions::reproducible(None),
                &mut Cursor::new(&mut raw),
                dictionary,
            )
            .write_mrsets(pre_v14)
            .unwrap();

            str::from_utf8(&raw[16..]).unwrap().into()
        }

        assert_eq!(
            &get_mrsets(&dictionary, true),
            "$a=C 10 my mcgroup a b c
$b=D2 55 0  g e f d
$c=D3 Yes 10 mdgroup #2 h i j
"
        );
        assert_eq!(
            &get_mrsets(&dictionary, false),
            "$d=E 1 2 34 13 third mdgroup k l m
$e=E 11 6 choice 0  n o p
"
        );
    }

    #[test]
    fn variable_display_parameters() {
        let variables = [
            (None, Alignment::Left, 10),
            (Some(Measure::Nominal), Alignment::Right, 12),
            (Some(Measure::Ordinal), Alignment::Center, 14),
            (Some(Measure::Scale), Alignment::Right, 16),
        ];
        let mut expected = Dictionary::new(UTF_8);
        for (index, (measure, alignment, display_width)) in variables.into_iter().enumerate() {
            let mut variable = Variable::new(
                Identifier::new(format!("v{index}")).unwrap(),
                VarWidth::Numeric,
                UTF_8,
            );
            variable.measure = measure;
            variable.alignment = alignment;
            variable.display_width = display_width;
            expected.add_var(variable).unwrap();
        }

        let raw = WriteOptions::new()
            .write_writer(&expected, Cursor::new(Vec::new()))
            .unwrap()
            .finish()
            .unwrap()
            .unwrap()
            .into_inner();
        let actual = ReadOptions::new(|_| panic!())
            .open_reader(Cursor::new(raw))
            .unwrap()
            .dictionary;

        fn display_parameters(
            dictionary: &Dictionary,
        ) -> impl Iterator<Item = (Option<Measure>, Alignment, u32)> {
            dictionary
                .variables
                .iter()
                .map(|variable| (variable.measure, variable.alignment, variable.display_width))
        }
        assert!(display_parameters(&expected).eq(display_parameters(&actual)));
    }

    #[test]
    fn long_variable_names() {
        let long_name = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@$";

        let mut expected = Dictionary::new(UTF_8);
        for name in (1..=64).map(|len| long_name[..len].to_string()) {
            expected
                .add_var(Variable::new(
                    Identifier::new(name).unwrap(),
                    VarWidth::Numeric,
                    UTF_8,
                ))
                .unwrap();
        }

        let raw = WriteOptions::new()
            .write_writer(&expected, Cursor::new(Vec::new()))
            .unwrap()
            .finish()
            .unwrap()
            .unwrap()
            .into_inner();
        let actual = ReadOptions::new(|_| panic!())
            .open_reader(Cursor::new(raw))
            .unwrap()
            .dictionary;

        fn names(dictionary: &Dictionary) -> impl Iterator<Item = &Identifier> {
            dictionary.variables.iter().map(|variable| &variable.name)
        }
        assert!(names(&expected).eq(names(&actual)));
    }

    /// This tests the example from the documentation for the system file
    /// format.
    #[test]
    fn attributes() {
        let mut dictionary = Dictionary::new(UTF_8);
        let attributes = Attributes::new()
            .with(
                Identifier::new("fred").unwrap(),
                vec![String::from("23"), String::from("34")],
            )
            .with(Identifier::new("bert").unwrap(), vec![String::from("123")]);
        dictionary.attributes = attributes.clone();
        let mut variable =
            Variable::new(Identifier::new("dummy").unwrap(), VarWidth::Numeric, UTF_8);
        variable.attributes = attributes;
        dictionary.add_var(variable).unwrap();

        fn get_attributes(dictionary: &Dictionary, vars: bool) -> String {
            let mut raw = Vec::new();
            let options = WriteOptions::reproducible(None);
            let mut cursor = Cursor::new(&mut raw);
            let mut writer = DictionaryWriter::new(&options, &mut cursor, dictionary);
            if vars {
                writer.write_variable_attributes().unwrap();
            } else {
                writer.write_data_file_attributes().unwrap();
            }
            if raw.is_empty() {
                String::new()
            } else {
                str::from_utf8(&raw[16..]).unwrap().into()
            }
        }

        assert_eq!(
            &get_attributes(&dictionary, false),
            "bert('123'
)fred('23'
'34'
)"
        );
        assert_eq!(
            &get_attributes(&dictionary, true),
            "dummy:$@Role('0'
)bert('123'
)fred('23'
'34'
)"
        );
    }

    #[test]
    fn encoding() {
        let dictionary = Dictionary::new(UTF_8);
        let mut raw = Vec::new();
        DictionaryWriter::new(
            &WriteOptions::reproducible(None),
            &mut Cursor::new(&mut raw),
            &dictionary,
        )
        .write_encoding()
        .unwrap();
        assert_eq!(str::from_utf8(&raw[16..]).unwrap(), "UTF-8");
    }
}
