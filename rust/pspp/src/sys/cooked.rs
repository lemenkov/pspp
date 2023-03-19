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

use std::{collections::BTreeMap, ops::Range};

use crate::{
    calendar::date_time_to_pspp,
    dictionary::{
        Datum, Dictionary, InvalidRole, MultipleResponseSet, MultipleResponseType, VarWidth,
        Variable, VariableSet,
    },
    endian::Endian,
    format::{Error as FormatError, Format, UncheckedFormat},
    hexfloat::HexFloat,
    identifier::{ByIdentifier, Error as IdError, Identifier},
    output::pivot::{Group, Value},
    sys::{
        encoding::Error as EncodingError,
        raw::{
            self, Cases, DecodedRecord, DocumentRecord, EncodingRecord, Extension,
            FileAttributesRecord, FloatInfoRecord, HeaderRecord, IntegerInfoRecord, LongName,
            LongNamesRecord, LongStringMissingValueRecord, LongStringValueLabelRecord,
            MissingValues, MissingValuesError, MultipleResponseRecord, NumberOfCasesRecord,
            ProductInfoRecord, RawStrArray, RawString, RawWidth, ValueLabel, ValueLabelRecord,
            VarDisplayRecord, VariableAttributesRecord, VariableRecord, VariableSetRecord,
            VeryLongStringsRecord, ZHeader, ZTrailer,
        },
    },
};
use chrono::{NaiveDate, NaiveDateTime, NaiveTime};
use encoding_rs::Encoding;
use indexmap::set::MutableValues;
use itertools::Itertools;
use thiserror::Error as ThisError;

pub use crate::sys::raw::{CategoryLabels, Compression};

#[derive(ThisError, Clone, Debug)]
pub enum Error {
    #[error("Missing header record")]
    MissingHeaderRecord,

    #[error("{0}")]
    EncodingError(EncodingError),

    #[error("Using default encoding {0}.")]
    UsingDefaultEncoding(String),

    #[error("Variable record from offset {:x} to {:x} specifies width {width} not in valid range [-1,255).", offsets.start, offsets.end)]
    InvalidVariableWidth { offsets: Range<u64>, width: i32 },

    #[error("This file has corrupted metadata written by a buggy version of PSPP.  To ensure that other software can read it correctly, save a new copy of the file.")]
    InvalidLongMissingValueFormat,

    #[error("File creation date {creation_date} is not in the expected format \"DD MMM YY\" format.  Using 01 Jan 1970.")]
    InvalidCreationDate { creation_date: String },

    #[error("File creation time {creation_time} is not in the expected format \"HH:MM:SS\" format.  Using midnight.")]
    InvalidCreationTime { creation_time: String },

    #[error("{id_error}  Renaming variable to {new_name}.")]
    InvalidVariableName {
        id_error: IdError,
        new_name: Identifier,
    },

    #[error(
        "Substituting {new_spec} for invalid print format on variable {variable}.  {format_error}"
    )]
    InvalidPrintFormat {
        new_spec: Format,
        variable: Identifier,
        format_error: FormatError,
    },

    #[error(
        "Substituting {new_spec} for invalid write format on variable {variable}.  {format_error}"
    )]
    InvalidWriteFormat {
        new_spec: Format,
        variable: Identifier,
        format_error: FormatError,
    },

    #[error("Renaming variable with duplicate name {duplicate_name} to {new_name}.")]
    DuplicateVariableName {
        duplicate_name: Identifier,
        new_name: Identifier,
    },

    #[error("Dictionary index {dict_index} is outside valid range [1,{max_index}].")]
    InvalidDictIndex { dict_index: usize, max_index: usize },

    #[error("Dictionary index {0} refers to a long string continuation.")]
    DictIndexIsContinuation(usize),

    #[error("At offset {offset:#x}, one or more variable indexes for value labels referred to long string continuation records: {indexes:?}")]
    LongStringContinuationIndexes { offset: u64, indexes: Vec<u32> },

    #[error("Variable index {start_index} is a {width} that should be followed by long string continuation records through index {end_index} (inclusive), but index {error_index} is not a continuation")]
    MissingLongStringContinuation {
        width: RawWidth,
        start_index: usize,
        end_index: usize,
        error_index: usize,
    },

    #[error(
        "At offsets {:#x}...{:#x}, record types 3 and 4 may not add value labels to one or more long string variables: {}", .offsets.start, .offsets.end, variables.iter().join(", ")
    )]
    InvalidLongStringValueLabels {
        offsets: Range<u64>,
        variables: Vec<Identifier>,
    },

    #[error("Variables associated with value label are not all of identical type.  Variable {numeric_var} is numeric, but variable {string_var} is string.")]
    ValueLabelsDifferentTypes {
        numeric_var: Identifier,
        string_var: Identifier,
    },

    #[error("{variable} has duplicate value labels for the following value(s): {}", values.iter().join(", "))]
    DuplicateValueLabels {
        variable: Identifier,
        values: Vec<String>,
    },

    #[error("Invalid multiple response set name.  {0}")]
    InvalidMrSetName(IdError),

    #[error("Multiple response set {mr_set} includes unknown variable {short_name}.")]
    UnknownMrSetVariable {
        mr_set: Identifier,
        short_name: Identifier,
    },

    #[error("Multiple response set {mr_set} includes variable {variable} more than once.")]
    DuplicateMrSetVariable {
        mr_set: Identifier,
        variable: Identifier,
    },

    #[error("Multiple response set {0} has no variables.")]
    EmptyMrSet(Identifier),

    #[error("Multiple response set {0} has only one variable.")]
    OneVarMrSet(Identifier),

    #[error("Multiple response set {0} contains both string and numeric variables.")]
    MixedMrSet(Identifier),

    #[error(
        "Invalid numeric format for counted value {number} in multiple response set {mr_set}."
    )]
    InvalidMDGroupCountedValue { mr_set: Identifier, number: String },

    #[error("Counted value {value} has width {width}, but it must be no wider than {max_width}, the width of the narrowest variable in multiple response set {mr_set}.")]
    TooWideMDGroupCountedValue {
        mr_set: Identifier,
        value: String,
        width: usize,
        max_width: u16,
    },

    #[error("Long string value label for variable {name} has width {width}, which is not in the valid range [{min_width},{max_width}].")]
    InvalidLongValueLabelWidth {
        name: Identifier,
        width: u32,
        min_width: u16,
        max_width: u16,
    },

    #[error("Ignoring long string value label for unknown variable {0}.")]
    UnknownLongStringValueLabelVariable(Identifier),

    #[error("Ignoring long string value label for numeric variable {0}.")]
    LongStringValueLabelNumericVariable(Identifier),

    #[error("Invalid attribute name.  {0}")]
    InvalidAttributeName(IdError),

    #[error("Invalid short name in long variable name record.  {0}")]
    InvalidShortName(IdError),

    #[error("Invalid name in long variable name record.  {0}")]
    InvalidLongName(IdError),

    #[error("Duplicate long variable name {0}.")]
    DuplicateLongName(Identifier),

    #[error("Invalid variable name in very long string record.  {0}")]
    InvalidLongStringName(IdError),

    #[error("Variable with short name {short_name} listed in very long string record with width {width}, which requires only one segment.")]
    ShortVeryLongString { short_name: Identifier, width: u16 },

    #[error("Variable with short name {short_name} listed in very long string record with width {width} requires string segments for {n_segments} dictionary indexes starting at index {index}, but the dictionary only contains {len} indexes.")]
    VeryLongStringOverflow {
        short_name: Identifier,
        width: u16,
        index: usize,
        n_segments: usize,
        len: usize,
    },

    #[error("Variable with short name {short_name} listed in very long string record with width {width} has segment {index} of width {actual} (expected {expected}).")]
    VeryLongStringInvalidSegmentWidth {
        short_name: Identifier,
        width: u16,
        index: usize,
        actual: usize,
        expected: usize,
    },

    #[error("Invalid variable name in long string value label record.  {0}")]
    InvalidLongStringValueLabelName(IdError),

    #[error("Invalid variable name in attribute record.  {0}")]
    InvalidAttributeVariableName(IdError),

    // XXX This is risky because `text` might be arbitarily long.
    #[error("Text string contains invalid bytes for {encoding} encoding: {text}")]
    MalformedString { encoding: String, text: String },

    #[error("File contains multiple {0:?} records.")]
    MoreThanOne(&'static str),

    #[error("File designates string variable {name} (index {index}) as weight variable, but weight variables must be numeric.")]
    InvalidWeightVar { name: Identifier, index: u32 },

    #[error(
        "File weight variable index {index} is invalid because it exceeds maximum variable index {max_index}."
    )]
    WeightIndexOutOfRange { index: u32, max_index: usize },

    #[error(
        "File weight variable index {index} is invalid because it refers to long string continuation for variable {name}."
    )]
    WeightIndexStringContinuation { index: u32, name: Identifier },

    #[error("{0}")]
    InvalidRole(InvalidRole),

    #[error("File header claims {expected} variable positions but {actual} were read from file.")]
    WrongVariablePositions { actual: usize, expected: usize },

    #[error("Unknown variable name \"{name}\" in long string missing value record.")]
    LongStringMissingValueUnknownVariable { name: Identifier },

    #[error("Invalid long string missing value for {} variable {name}.", width.display_adjective())]
    LongStringMissingValueBadWdith { name: Identifier, width: VarWidth },

    #[error("Long string missing values record says variable {name} has {count} missing values, but only 1 to 3 missing values are allowed.")]
    LongStringMissingValueInvalidCount { name: Identifier, count: usize },

    #[error("Unknown extension record with subtype {subtype} at offset {offset:#x}, consisting of {count} {size}-byte units.  Please feel free to report this as a bug.")]
    UnknownExtensionRecord {
        offset: u64,
        subtype: u32,
        size: u32,
        count: u32,
    },

    #[error(
        "Floating-point representation indicated by system file ({0}) differs from expected (1)."
    )]
    UnexpectedFloatFormat(i32),

    #[error(
        "Integer format indicated by system file ({actual}) differs from expected ({expected})."
    )]
    UnexpectedEndianess { actual: i32, expected: i32 },

    #[error(
        "System file specifies value {actual:?} ({}) as {name} but {expected:?} ({}) was expected.",
        HexFloat(*actual),
        HexFloat(*expected)
    )]
    UnexpectedFloatValue {
        actual: f64,
        expected: f64,
        name: &'static str,
    },

    #[error("Variable set \"{variable_set}\" includes unknown variable {variable}.")]
    UnknownVariableSetVariable {
        variable_set: String,
        variable: Identifier,
    },

    #[error("Details TBD (cooked)")]
    TBD,
}

#[derive(Clone, Debug)]
pub struct Headers {
    pub header: HeaderRecord<String>,
    pub variable: Vec<VariableRecord<String>>,
    pub value_label: Vec<ValueLabelRecord<RawStrArray<8>, String>>,
    pub document: Vec<DocumentRecord<String>>,
    pub integer_info: Option<IntegerInfoRecord>,
    pub float_info: Option<FloatInfoRecord>,
    pub var_display: Option<VarDisplayRecord>,
    pub multiple_response: Vec<MultipleResponseRecord<Identifier, String>>,
    pub long_string_value_labels: Vec<LongStringValueLabelRecord<Identifier, String>>,
    pub long_string_missing_values: Vec<LongStringMissingValueRecord<Identifier>>,
    pub encoding: Option<EncodingRecord>,
    pub number_of_cases: Option<NumberOfCasesRecord>,
    pub variable_sets: Vec<VariableSetRecord>,
    pub product_info: Option<ProductInfoRecord>,
    pub long_names: Vec<LongNamesRecord>,
    pub very_long_strings: Vec<VeryLongStringsRecord>,
    pub file_attributes: Vec<FileAttributesRecord>,
    pub variable_attributes: Vec<VariableAttributesRecord>,
    pub other_extension: Vec<Extension>,
    pub end_of_headers: Option<u32>,
    pub z_header: Option<ZHeader>,
    pub z_trailer: Option<ZTrailer>,
}

fn take_first<T>(
    mut vec: Vec<T>,
    record_name: &'static str,
    warn: &mut impl FnMut(Error),
) -> Option<T> {
    if vec.len() > 1 {
        warn(Error::MoreThanOne(record_name));
    }
    vec.drain(..).next()
}

impl Headers {
    pub fn new(
        headers: Vec<raw::DecodedRecord>,
        warn: &mut impl FnMut(Error),
    ) -> Result<Headers, Error> {
        let mut file_header = Vec::new();
        let mut variable = Vec::new();
        let mut value_label = Vec::new();
        let mut document = Vec::new();
        let mut integer_info = Vec::new();
        let mut float_info = Vec::new();
        let mut var_display = Vec::new();
        let mut multiple_response = Vec::new();
        let mut long_string_value_labels = Vec::new();
        let mut long_string_missing_values = Vec::new();
        let mut encoding = Vec::new();
        let mut number_of_cases = Vec::new();
        let mut variable_sets = Vec::new();
        let mut product_info = Vec::new();
        let mut long_names = Vec::new();
        let mut very_long_strings = Vec::new();
        let mut file_attributes = Vec::new();
        let mut variable_attributes = Vec::new();
        let mut other_extension = Vec::new();
        let mut end_of_headers = Vec::new();
        let mut z_header = Vec::new();
        let mut z_trailer = Vec::new();

        for header in headers {
            match header {
                DecodedRecord::Header(record) => {
                    file_header.push(record);
                }
                DecodedRecord::Variable(record) => {
                    variable.push(record);
                }
                DecodedRecord::ValueLabel(record) => {
                    value_label.push(record);
                }
                DecodedRecord::Document(record) => {
                    document.push(record);
                }
                DecodedRecord::IntegerInfo(record) => {
                    integer_info.push(record);
                }
                DecodedRecord::FloatInfo(record) => {
                    float_info.push(record);
                }
                DecodedRecord::VariableSets(record) => {
                    variable_sets.push(record);
                }
                DecodedRecord::VarDisplay(record) => {
                    var_display.push(record);
                }
                DecodedRecord::MultipleResponse(record) => {
                    multiple_response.push(record);
                }
                DecodedRecord::LongStringValueLabels(record) => {
                    long_string_value_labels.push(record)
                }
                DecodedRecord::LongStringMissingValues(record) => {
                    long_string_missing_values.push(record);
                }
                DecodedRecord::Encoding(record) => {
                    encoding.push(record);
                }
                DecodedRecord::NumberOfCases(record) => {
                    number_of_cases.push(record);
                }
                DecodedRecord::ProductInfo(record) => {
                    product_info.push(record);
                }
                DecodedRecord::LongNames(record) => {
                    long_names.push(record);
                }
                DecodedRecord::VeryLongStrings(record) => {
                    very_long_strings.push(record);
                }
                DecodedRecord::FileAttributes(record) => {
                    file_attributes.push(record);
                }
                DecodedRecord::VariableAttributes(record) => {
                    variable_attributes.push(record);
                }
                DecodedRecord::OtherExtension(record) => {
                    other_extension.push(record);
                }
                DecodedRecord::EndOfHeaders(record) => {
                    end_of_headers.push(record);
                }
                DecodedRecord::ZHeader(record) => {
                    z_header.push(record);
                }
                DecodedRecord::ZTrailer(record) => {
                    z_trailer.push(record);
                }
            }
        }

        let Some(file_header) = take_first(file_header, "file header", warn) else {
            return Err(Error::MissingHeaderRecord);
        };

        Ok(Headers {
            header: file_header,
            variable,
            value_label,
            document,
            integer_info: take_first(integer_info, "integer info", warn),
            float_info: take_first(float_info, "float info", warn),
            var_display: take_first(var_display, "variable display", warn),
            multiple_response,
            long_string_value_labels,
            long_string_missing_values,
            encoding: take_first(encoding, "encoding", warn),
            number_of_cases: take_first(number_of_cases, "number of cases", warn),
            variable_sets,
            product_info: take_first(product_info, "product info", warn),
            long_names,
            very_long_strings,
            file_attributes,
            variable_attributes,
            other_extension,
            end_of_headers: take_first(end_of_headers, "end of headers", warn),
            z_header: take_first(z_header, "z_header", warn),
            z_trailer: take_first(z_trailer, "z_trailer", warn),
        })
    }

    pub fn decode(
        mut self,
        mut cases: Cases,
        encoding: &'static Encoding,
        mut warn: impl FnMut(Error),
    ) -> Result<(Dictionary, Metadata, Cases), Error> {
        let mut dictionary = Dictionary::new(encoding);

        let file_label = fix_line_ends(self.header.file_label.trim_end_matches(' '));
        if !file_label.is_empty() {
            dictionary.file_label = Some(file_label);
        }

        for mut attributes in self.file_attributes.drain(..) {
            dictionary.attributes.append(&mut attributes.0)
        }

        // Concatenate all the document records (really there should only be one)
        // and trim off the trailing spaces that pad them to 80 bytes.
        dictionary.documents = self
            .document
            .drain(..)
            .flat_map(|record| record.lines)
            .map(trim_end_spaces)
            .collect();

        if let Some(integer_info) = &self.integer_info {
            let floating_point_rep = integer_info.floating_point_rep;
            if floating_point_rep != 1 {
                warn(Error::UnexpectedFloatFormat(floating_point_rep))
            }

            let expected = match self.header.endian {
                Endian::Big => 1,
                Endian::Little => 2,
            };
            let actual = integer_info.endianness;
            if actual != expected {
                warn(Error::UnexpectedEndianess { actual, expected });
            }
        };

        if let Some(float_info) = &self.float_info {
            for (expected, expected2, actual, name) in [
                (f64::MIN, None, float_info.sysmis, "SYSMIS"),
                (f64::MAX, None, float_info.highest, "HIGHEST"),
                (
                    f64::MIN,
                    Some(f64::MIN.next_up()),
                    float_info.lowest,
                    "LOWEST",
                ),
            ] {
                if actual != expected && expected2.is_none_or(|expected2| expected2 != actual) {
                    warn(Error::UnexpectedFloatValue {
                        expected,
                        actual,
                        name,
                    });
                }
            }
        }

        if let Some(nominal_case_size) = self.header.nominal_case_size {
            let n_vars = self.variable.len();
            if n_vars != nominal_case_size as usize
                && self
                    .integer_info
                    .as_ref()
                    .is_none_or(|info| info.version.0 != 13)
            {
                warn(Error::WrongVariablePositions {
                    actual: n_vars,
                    expected: nominal_case_size as usize,
                });
            }
        }

        let mut decoder = Decoder {
            encoding,
            n_generated_names: 0,
        };

        let mut var_index_map = BTreeMap::new();
        let mut value_index = 0;
        for (index, input) in self
            .variable
            .iter()
            .enumerate()
            .filter(|(_index, record)| record.width != RawWidth::Continuation)
        {
            let name = trim_end_spaces(input.name.to_string());
            let name = match Identifier::from_encoding(name, encoding)
                .and_then(Identifier::must_be_ordinary)
            {
                Ok(name) => {
                    if !dictionary.variables.contains(&name.0) {
                        name
                    } else {
                        let new_name = decoder.generate_name(&dictionary);
                        warn(Error::DuplicateVariableName {
                            duplicate_name: name.clone(),
                            new_name: new_name.clone(),
                        });
                        new_name
                    }
                }
                Err(id_error) => {
                    let new_name = decoder.generate_name(&dictionary);
                    warn(Error::InvalidVariableName {
                        id_error,
                        new_name: new_name.clone(),
                    });
                    new_name
                }
            };
            let mut variable = Variable::new(
                name.clone(),
                VarWidth::try_from(input.width).unwrap(),
                encoding,
            );

            // Set the short name the same as the long name (even if we renamed it).
            variable.short_names = vec![name];

            variable.label = input.label.clone();

            variable.missing_values = input.missing_values.clone();

            variable.print_format = decode_format(
                input.print_format,
                variable.width,
                |new_spec, format_error| {
                    warn(Error::InvalidPrintFormat {
                        new_spec,
                        variable: variable.name.clone(),
                        format_error,
                    })
                },
            );
            variable.write_format = decode_format(
                input.write_format,
                variable.width,
                |new_spec, format_error| {
                    warn(Error::InvalidWriteFormat {
                        new_spec,
                        variable: variable.name.clone(),
                        format_error,
                    })
                },
            );

            // Check for long string continuation records.
            let n_values = input.width.n_values().unwrap();
            for offset in 1..n_values {
                if self
                    .variable
                    .get(index + offset)
                    .is_none_or(|record| record.width != RawWidth::Continuation)
                {
                    warn(Error::MissingLongStringContinuation {
                        width: input.width,
                        start_index: index,
                        end_index: index + n_values - 1,
                        error_index: index + offset,
                    });
                    break;
                }
            }

            let dict_index = dictionary.add_var(variable).unwrap();
            assert_eq!(var_index_map.insert(value_index, dict_index), None);
            value_index += n_values;
        }

        if let Some(weight_index) = self.header.weight_index {
            let index = weight_index as usize - 1;
            if index >= value_index {
                warn(Error::WeightIndexOutOfRange {
                    index: weight_index,
                    max_index: var_index_map.len(),
                });
            } else {
                let (var_index, dict_index) = var_index_map.range(..=&index).last().unwrap();
                let variable = &dictionary.variables[*dict_index];
                if *var_index == index {
                    if variable.is_numeric() {
                        dictionary.weight = Some(*dict_index);
                    } else {
                        warn(Error::InvalidWeightVar {
                            index: weight_index,
                            name: variable.name.clone(),
                        });
                    }
                } else {
                    warn(Error::WeightIndexStringContinuation {
                        index: weight_index,
                        name: variable.name.clone(),
                    });
                }
            }
        }

        for record in self.value_label.drain(..) {
            let mut dict_indexes = Vec::with_capacity(record.dict_indexes.len());
            let mut long_string_variables = Vec::new();
            for value_index in record.dict_indexes.iter() {
                let Some(dict_index) = var_index_map.get(&(*value_index as usize - 1)) else {
                    unreachable!()
                };
                let variable = &dictionary.variables[*dict_index];
                if variable.width.is_long_string() {
                    long_string_variables.push(variable.name.clone());
                } else {
                    dict_indexes.push(*dict_index);
                }
            }
            if !long_string_variables.is_empty() {
                warn(Error::InvalidLongStringValueLabels {
                    offsets: record.offsets.clone(),
                    variables: long_string_variables,
                });
            }

            let written_by_readstat = self.header.eye_catcher.contains("ReadStat");
            for dict_index in dict_indexes {
                let variable = dictionary.variables.get_index_mut2(dict_index).unwrap();
                let mut duplicates = Vec::new();
                for ValueLabel {
                    datum: value,
                    label,
                } in record.labels.iter().cloned()
                {
                    let datum = value.decode(variable.width);
                    if variable.value_labels.insert(datum, label).is_some() {
                        duplicates.push(value);
                    }
                }
                if written_by_readstat {
                    // Ignore any possible duplicates.  ReadStat is buggy and emits
                    // value labels whose values are longer than string variables'
                    // widths, that are identical in the actual width of the
                    // variable, e.g. both values "ABC123" and "ABC456" for a string
                    // variable with width 3.
                } else if !duplicates.is_empty() {
                    warn(Error::DuplicateValueLabels {
                        variable: variable.name.clone(),
                        values: duplicates
                            .iter()
                            .map(|value| {
                                value
                                    .decode(variable.width)
                                    .display(variable.print_format, variable.encoding)
                                    .with_trimming()
                                    .with_quoted_string()
                                    .to_string()
                            })
                            .collect(),
                    });
                }
            }
        }

        if let Some(display) = &self.var_display {
            for (index, display) in display.0.iter().enumerate() {
                if let Some(variable) = dictionary.variables.get_index_mut2(index) {
                    if let Some(width) = display.width {
                        variable.display_width = width;
                    }
                    if let Some(alignment) = display.alignment {
                        variable.alignment = alignment;
                    }
                    if let Some(measure) = display.measure {
                        variable.measure = Some(measure);
                    }
                } else {
                    warn(dbg!(Error::TBD));
                }
            }
        }

        for record in self
            .multiple_response
            .iter()
            .flat_map(|record| record.0.iter())
        {
            match MultipleResponseSet::decode(&dictionary, record, &mut warn) {
                Ok(mrset) => {
                    dictionary.mrsets.insert(ByIdentifier::new(mrset));
                }
                Err(error) => warn(error),
            }
        }

        if !self.very_long_strings.is_empty() {
            'outer: for record in self
                .very_long_strings
                .drain(..)
                .flat_map(|record| record.0.into_iter())
            {
                let Some(index) = dictionary.variables.get_index_of(&record.short_name.0) else {
                    warn(dbg!(Error::TBD));
                    continue;
                };
                let width = VarWidth::String(record.length);
                let n_segments = width.n_segments();
                if n_segments == 1 {
                    warn(dbg!(Error::ShortVeryLongString {
                        short_name: record.short_name.clone(),
                        width: record.length
                    }));
                    continue;
                }
                if index + n_segments > dictionary.variables.len() {
                    warn(dbg!(Error::VeryLongStringOverflow {
                        short_name: record.short_name.clone(),
                        width: record.length,
                        index,
                        n_segments,
                        len: dictionary.variables.len()
                    }));
                    continue;
                }
                let mut short_names = Vec::with_capacity(n_segments);
                for i in 0..n_segments {
                    let alloc_width = width.segment_alloc_width(i);
                    let segment = &dictionary.variables[index + i];
                    short_names.push(segment.short_names[0].clone());
                    let segment_width = segment.width.as_string_width().unwrap_or(0);
                    if segment_width.next_multiple_of(8) != alloc_width.next_multiple_of(8) {
                        warn(Error::VeryLongStringInvalidSegmentWidth {
                            short_name: record.short_name.clone(),
                            width: record.length,
                            index: i,
                            actual: segment_width,
                            expected: alloc_width,
                        });
                        continue 'outer;
                    }
                }
                dictionary.delete_vars(index + 1..index + n_segments);
                let variable = dictionary.variables.get_index_mut2(index).unwrap();
                variable.short_names = short_names;
                variable.resize(width);
            }
            cases = cases.with_widths(dictionary.variables.iter().map(|var| var.width));
        }

        if self.long_names.is_empty() {
            // There are no long variable names.  Use the short variable names,
            // converted to lowercase, as the long variable names.
            for index in 0..dictionary.variables.len() {
                let lower = dictionary.variables[index].name.0.as_ref().to_lowercase();
                if let Ok(new_name) = Identifier::from_encoding(lower, dictionary.encoding) {
                    let _ = dictionary.try_rename_var(index, new_name);
                }
            }
        } else {
            // Rename each of the variables, one by one.  (In a correctly
            // constructed system file, this cannot create any intermediate
            // duplicate variable names, because all of the new variable names are
            // longer than any of the old variable names and thus there cannot be
            // any overlaps.)
            for renaming in self
                .long_names
                .iter()
                .flat_map(|record| record.0.iter().cloned())
            {
                let LongName {
                    short_name,
                    long_name,
                } = renaming;
                if let Some(index) = dictionary.variables.get_index_of(&short_name.0) {
                    if let Err(long_name) = dictionary.try_rename_var(index, long_name) {
                        warn(Error::DuplicateLongName(long_name));
                    }
                    dictionary
                        .variables
                        .get_index_mut2(index)
                        .unwrap()
                        .short_names = vec![short_name];
                } else {
                    warn(dbg!(Error::TBD));
                }
            }
        }

        for mut attr_set in self
            .variable_attributes
            .drain(..)
            .flat_map(|record| record.0.into_iter())
        {
            if let Some((_, variable)) = dictionary
                .variables
                .get_full_mut2(&attr_set.long_var_name.0)
            {
                variable.attributes.append(&mut attr_set.attributes);
            } else {
                warn(dbg!(Error::TBD));
            }
        }

        // Assign variable roles.
        for index in 0..dictionary.variables.len() {
            let variable = dictionary.variables.get_index_mut2(index).unwrap();
            match variable.attributes.role() {
                Ok(Some(role)) => variable.role = role,
                Ok(None) => (),
                Err(error) => warn(Error::InvalidRole(error)),
            }
        }

        // Long string value labels.
        for record in self
            .long_string_value_labels
            .drain(..)
            .flat_map(|record| record.0.into_iter())
        {
            let Some((_, variable)) = dictionary.variables.get_full_mut2(&record.var_name.0) else {
                warn(Error::UnknownLongStringValueLabelVariable(
                    record.var_name.clone(),
                ));
                continue;
            };
            let Some(width) = variable.width.as_string_width() else {
                warn(Error::LongStringValueLabelNumericVariable(
                    record.var_name.clone(),
                ));
                continue;
            };
            for (mut value, label) in record.labels.into_iter() {
                // XXX warn about too-long value?
                value.0.resize(width, b' ');
                // XXX warn abouat duplicate value labels?
                variable.value_labels.insert(Datum::String(value), label);
            }
        }

        for mut record in self
            .long_string_missing_values
            .drain(..)
            .flat_map(|record| record.0.into_iter())
        {
            let Some((_, variable)) = dictionary.variables.get_full_mut2(&record.var_name.0) else {
                warn(Error::LongStringMissingValueUnknownVariable {
                    name: record.var_name.clone(),
                });
                continue;
            };
            if !variable.width.is_long_string() {
                warn(Error::LongStringMissingValueBadWdith {
                    name: record.var_name.clone(),
                    width: variable.width,
                });
                continue;
            }
            if record.missing_values.len() > 3 {
                warn(Error::LongStringMissingValueInvalidCount {
                    name: record.var_name.clone(),
                    count: record.missing_values.len(),
                });
                record.missing_values.truncate(3);
            }
            let values = record
                .missing_values
                .into_iter()
                .map(|v| {
                    let mut value = RawString::from(v.0.as_slice());
                    value.resize(variable.width.as_string_width().unwrap());
                    Datum::String(value)
                })
                .collect::<Vec<_>>();
            match MissingValues::new(values, None) {
                Ok(missing_values) => variable.missing_values = missing_values,
                Err(MissingValuesError::TooWide) => warn(dbg!(Error::TBD)),
                Err(MissingValuesError::TooMany) | Err(MissingValuesError::MixedTypes) => {
                    unreachable!()
                }
            }
        }

        for record in self
            .variable_sets
            .drain(..)
            .flat_map(|record| record.sets.into_iter())
        {
            let mut variables = Vec::with_capacity(record.variable_names.len());
            for variable_name in record.variable_names {
                let Some((dict_index, _)) = dictionary.variables.get_full_mut2(&variable_name.0)
                else {
                    warn(Error::UnknownVariableSetVariable {
                        variable_set: record.name.clone(),
                        variable: variable_name.clone(),
                    });
                    continue;
                };
                variables.push(dict_index);
            }
            let variable_set = VariableSet {
                name: record.name,
                variables,
            };
            dictionary.variable_sets.push(variable_set);
        }

        for record in self.other_extension.drain(..) {
            warn(Error::UnknownExtensionRecord {
                offset: record.offsets.start,
                subtype: record.subtype,
                size: record.size,
                count: record.count,
            });
        }

        let metadata = Metadata::decode(&self, warn);
        if let Some(n_cases) = metadata.n_cases {
            cases = cases.with_expected_cases(n_cases);
        }
        Ok((dictionary, metadata, cases))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Metadata {
    pub creation: NaiveDateTime,
    pub endian: Endian,
    pub compression: Option<Compression>,
    pub n_cases: Option<u64>,
    pub product: String,
    pub product_ext: Option<String>,
    pub version: Option<(i32, i32, i32)>,
}

impl Metadata {
    pub fn to_pivot_rows(&self) -> (Group, Vec<Value>) {
        let mut group = Group::new("File Information");
        let mut values = Vec::new();

        group.push("Created");
        values.push(Value::new_number_with_format(
            Some(date_time_to_pspp(self.creation)),
            Format::DATETIME40_0,
        ));

        let mut product = Group::new("Writer");
        product.push("Product");
        values.push(Value::new_user_text(
            self.product
                .trim_start_matches("$(#) SPSS DATA FILE")
                .trim_start(),
        ));
        product.push("Product 2");
        values.push(
            self.product_ext
                .as_ref()
                .map(|product_ext| Value::new_user_text(product_ext))
                .unwrap_or_default(),
        );
        product.push("Version");
        values.push(
            self.version
                .map(|version| {
                    Value::new_text(format!("{}.{}.{}", version.0, version.1, version.2))
                })
                .unwrap_or_default(),
        );
        group.push(product);

        group.push("Compression");
        values.push(Value::new_text(match self.compression {
            Some(Compression::Simple) => "SAV",
            Some(Compression::ZLib) => "ZSAV",
            None => "None",
        }));

        group.push("Number of Cases");
        values.push(match self.n_cases {
            Some(n_cases) => Value::new_integer(Some(n_cases as f64)),
            None => Value::new_text("Unknown"),
        });

        (group, values)
    }

    fn decode(headers: &Headers, mut warn: impl FnMut(Error)) -> Self {
        let header = &headers.header;
        let creation_date = NaiveDate::parse_from_str(&header.creation_date, "%e %b %y")
            .unwrap_or_else(|_| {
                warn(Error::InvalidCreationDate {
                    creation_date: header.creation_date.to_string(),
                });
                Default::default()
            });
        let creation_time = NaiveTime::parse_from_str(&header.creation_time, "%H:%M:%S")
            .unwrap_or_else(|_| {
                warn(Error::InvalidCreationTime {
                    creation_time: header.creation_time.to_string(),
                });
                Default::default()
            });
        let creation = NaiveDateTime::new(creation_date, creation_time);

        let product = header
            .eye_catcher
            .trim_start_matches("@(#) SPSS DATA FILE")
            .trim_end()
            .to_string();

        Self {
            creation,
            endian: header.endian,
            compression: header.compression,
            n_cases: headers
                .number_of_cases
                .as_ref()
                .map(|record| record.n_cases)
                .or_else(|| header.n_cases.map(|n| n as u64)),
            product,
            product_ext: headers.product_info.as_ref().map(|pe| fix_line_ends(&pe.0)),
            version: headers.integer_info.as_ref().map(|ii| ii.version),
        }
    }
}

struct Decoder {
    pub encoding: &'static Encoding,
    n_generated_names: usize,
}

impl Decoder {
    fn generate_name(&mut self, dictionary: &Dictionary) -> Identifier {
        loop {
            self.n_generated_names += 1;
            let name = Identifier::from_encoding(
                format!("VAR{:03}", self.n_generated_names),
                self.encoding,
            )
            .unwrap();
            if !dictionary.variables.contains(&name.0) {
                return name;
            }
            assert!(self.n_generated_names < usize::MAX);
        }
    }
}

impl MultipleResponseSet {
    fn decode(
        dictionary: &Dictionary,
        input: &raw::MultipleResponseSet<Identifier, String>,
        warn: &mut impl FnMut(Error),
    ) -> Result<Self, Error> {
        let mr_set_name = input.name.clone();
        if !mr_set_name.0.starts_with("$") {
            return Err(Error::InvalidMrSetName(IdError::MissingAt(
                mr_set_name.clone(),
            )));
        }

        let mut variables = Vec::with_capacity(input.short_names.len());
        for short_name in input.short_names.iter() {
            let Some(dict_index) = dictionary.variables.get_index_of(&short_name.0) else {
                warn(Error::UnknownMrSetVariable {
                    mr_set: mr_set_name.clone(),
                    short_name: short_name.clone(),
                });
                continue;
            };
            if variables.contains(&dict_index) {
                warn(Error::DuplicateMrSetVariable {
                    mr_set: mr_set_name.clone(),
                    variable: dictionary.variables[dict_index].name.clone(),
                });
                continue;
            }
            variables.push(dict_index);
        }

        match variables.len() {
            0 => return Err(Error::EmptyMrSet(mr_set_name)),
            1 => return Err(Error::OneVarMrSet(mr_set_name)),
            _ => (),
        }

        let Some((Some(min_width), Some(max_width))) = variables
            .iter()
            .copied()
            .map(|dict_index| dictionary.variables[dict_index].width)
            .map(|w| (Some(w), Some(w)))
            .reduce(|(na, wa), (nb, wb)| (VarWidth::narrower(na, nb), VarWidth::wider(wa, wb)))
        else {
            return Err(Error::MixedMrSet(mr_set_name));
        };

        let mr_type = MultipleResponseType::decode(&mr_set_name, &input.mr_type, min_width)?;

        Ok(MultipleResponseSet {
            name: mr_set_name,
            width: min_width..=max_width,
            label: input.label.to_string(),
            mr_type,
            variables,
        })
    }
}

fn trim_end_spaces(mut s: String) -> String {
    s.truncate(s.trim_end_matches(' ').len());
    s
}

/// Returns a copy of `s` in which all lone CR and CR LF pairs have been
/// replaced by LF.
///
/// (A product that identifies itself as VOXCO INTERVIEWER 4.3 produces system
/// files that use CR-only line ends in the file label and extra product info.)
fn fix_line_ends(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut s = s.chars().peekable();
    while let Some(c) = s.next() {
        match c {
            '\r' => {
                s.next_if_eq(&'\n');
                out.push('\n')
            }
            c => out.push(c),
        }
    }
    out
}

fn decode_format(
    raw: raw::Spec,
    width: VarWidth,
    mut warn: impl FnMut(Format, FormatError),
) -> Format {
    UncheckedFormat::try_from(raw)
        .and_then(Format::try_from)
        .and_then(|x| x.check_width_compatibility(width))
        .unwrap_or_else(|error| {
            let new_format = Format::default_for_width(width);
            warn(new_format, error);
            new_format
        })
}

impl MultipleResponseType {
    fn decode(
        mr_set: &Identifier,
        input: &raw::MultipleResponseType,
        min_width: VarWidth,
    ) -> Result<Self, Error> {
        match input {
            raw::MultipleResponseType::MultipleDichotomy { value, labels } => {
                let value = match min_width {
                    VarWidth::Numeric => {
                        let string = String::from_utf8_lossy(&value.0);
                        let number: f64 = string.trim().parse().map_err(|_| {
                            Error::InvalidMDGroupCountedValue {
                                mr_set: mr_set.clone(),
                                number: string.into(),
                            }
                        })?;
                        Datum::Number(Some(number))
                    }
                    VarWidth::String(max_width) => {
                        let mut value = value.0.as_slice();
                        while value.ends_with(b" ") {
                            value = &value[..value.len() - 1];
                        }
                        let width = value.len();
                        if width > max_width as usize {
                            return Err(Error::TooWideMDGroupCountedValue {
                                mr_set: mr_set.clone(),
                                value: String::from_utf8_lossy(value).into(),
                                width,
                                max_width,
                            });
                        };
                        Datum::String(value.into())
                    }
                };
                Ok(MultipleResponseType::MultipleDichotomy {
                    datum: value,
                    labels: *labels,
                })
            }
            raw::MultipleResponseType::MultipleCategory => {
                Ok(MultipleResponseType::MultipleCategory)
            }
        }
    }
}

/*
trait Quoted {
    fn quoted(self) -> WithQuotes<Self>
    where
        Self: Display + Sized;
}

impl<T> Quoted for T
where
    T: Display,
{
    fn quoted(self) -> WithQuotes<Self> {
        WithQuotes(self)
    }
}

struct WithQuotes<T>(T)
where
    T: Display;

impl<T> Display for WithQuotes<T>
where
    T: Display,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "\"{}\"", &self.0)
    }
}
*/
