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
    collections::BTreeMap,
    fs::File,
    io::{Read, Seek},
    ops::Range,
    path::Path,
};

use crate::{
    calendar::date_time_to_pspp,
    crypto::EncryptedFile,
    data::{Datum, RawString},
    dictionary::{
        Dictionary, InvalidRole, MissingValues, MissingValuesError, MultipleResponseSet,
        MultipleResponseType, VarWidth, Variable, VariableSet,
    },
    endian::Endian,
    format::{Error as FormatError, Format, UncheckedFormat},
    hexfloat::HexFloat,
    identifier::{ByIdentifier, Error as IdError, Identifier},
    output::pivot::{Group, Value},
    sys::raw::{
        self, infer_encoding,
        records::{
            Compression, DocumentRecord, EncodingRecord, Extension, FileAttributesRecord,
            FileHeader, FloatInfoRecord, IntegerInfoRecord, LongName, LongNamesRecord,
            LongStringMissingValueRecord, LongStringValueLabelRecord, MultipleResponseRecord,
            NumberOfCasesRecord, ProductInfoRecord, RawFormat, ValueLabel, ValueLabelRecord,
            VarDisplayRecord, VariableAttributesRecord, VariableRecord, VariableSetRecord,
            VeryLongStringsRecord,
        },
        Cases, DecodedRecord, RawDatum, RawWidth, Reader,
    },
};
use anyhow::{anyhow, Error as AnyError};
use binrw::io::BufReader;
use chrono::{NaiveDate, NaiveDateTime, NaiveTime};
use encoding_rs::Encoding;
use indexmap::set::MutableValues;
use itertools::Itertools;
use thiserror::Error as ThisError;

/// A warning for decoding [Records] into a [SystemFile].
#[derive(ThisError, Clone, Debug)]
pub enum Error {
    /// File creation date is not in the expected format format.
    #[error("File creation date {0} is not in the expected format \"DD MMM YY\" format.  Using 01 Jan 1970.")]
    InvalidCreationDate(
        /// Date.
        String,
    ),

    /// File creation time is not in the expected format.
    #[error("File creation time {0} is not in the expected format \"HH:MM:SS\" format.  Using midnight.")]
    InvalidCreationTime(
        /// Time.
        String,
    ),

    /// Invalid variable name.
    #[error("{id_error}  Renaming variable to {new_name}.")]
    InvalidVariableName {
        /// Identifier error.
        id_error: IdError,
        /// New name.
        new_name: Identifier,
    },

    /// Invalid print format.
    #[error(
        "Substituting {new_format} for invalid print format on variable {variable}.  {format_error}"
    )]
    InvalidPrintFormat {
        /// New format.
        new_format: Format,
        /// Variable.
        variable: Identifier,
        /// Underlying error.
        format_error: FormatError,
    },

    /// Invalid write format.
    #[error(
        "Substituting {new_format} for invalid write format on variable {variable}.  {format_error}"
    )]
    InvalidWriteFormat {
        /// New format.
        new_format: Format,
        /// Variable.
        variable: Identifier,
        /// Underlying error.
        format_error: FormatError,
    },

    /// Renaming variable with duplicate name {duplicate_name} to {new_name}..
    #[error("Renaming variable with duplicate name {duplicate_name} to {new_name}.")]
    DuplicateVariableName {
        /// Duplicate name.
        duplicate_name: Identifier,
        /// New name.
        new_name: Identifier,
    },

    /// Variable index {start_index} is a {width} that should be followed by
    /// long string continuation records through index {end_index} (inclusive),
    /// but index {error_index} is not a continuation.
    #[error("Variable index {start_index} is a {width} that should be followed by long string continuation records through index {end_index} (inclusive), but index {error_index} is not a continuation")]
    MissingLongStringContinuation {
        /// Width of variable.
        width: RawWidth,
        /// First variable index.
        start_index: usize,
        /// Last variable index.
        end_index: usize,
        /// Index of error.
        error_index: usize,
    },

    /// Invalid long string value labels.
    #[error(
        "At offsets {:#x}...{:#x}, record types 3 and 4 may not add value labels to one or more long string variables: {}", .offsets.start, .offsets.end, variables.iter().join(", ")
    )]
    InvalidLongStringValueLabels {
        /// Range of file offsets.
        offsets: Range<u64>,
        /// Variables.
        variables: Vec<Identifier>,
    },

    /// Variable has duplicate value labels.
    #[error("{variable} has duplicate value labels for the following value(s): {}", values.iter().join(", "))]
    DuplicateValueLabels {
        /// Variable.
        variable: Identifier,
        /// Duplicate values.
        values: Vec<String>,
    },

    /// Invalid multiple response set name.
    #[error("Invalid multiple response set name.  {0}")]
    InvalidMrSetName(
        /// Identifier error.
        IdError,
    ),

    /// Multiple response set includes unknown variable.
    #[error("Multiple response set {mr_set} includes unknown variable {short_name}.")]
    UnknownMrSetVariable {
        /// Multiple response set name.
        mr_set: Identifier,
        /// Short name of variable.
        short_name: Identifier,
    },

    /// Multiple response set {mr_set} includes variable {variable} more than once.
    #[error("Multiple response set {mr_set} includes variable {variable} more than once.")]
    DuplicateMrSetVariable {
        /// Multiple response set name.
        mr_set: Identifier,
        /// Duplicated variable.
        variable: Identifier,
    },

    /// Multiple response set {0} has no variables.
    #[error("Multiple response set {0} has no variables.")]
    EmptyMrSet(
        /// Multiple response set name.
        Identifier,
    ),

    /// Multiple response set {0} has only one variable.
    #[error("Multiple response set {0} has only one variable.")]
    OneVarMrSet(
        /// Multiple response set name.
        Identifier,
    ),

    /// Multiple response set {0} contains both string and numeric variables.
    #[error("Multiple response set {0} contains both string and numeric variables.")]
    MixedMrSet(
        /// Multiple response set name.
        Identifier,
    ),

    /// Invalid numeric format for counted value {number} in multiple response set {mr_set}.
    #[error(
        "Invalid numeric format for counted value {number} in multiple response set {mr_set}."
    )]
    InvalidMDGroupCountedValue {
        /// Multiple response set name.
        mr_set: Identifier,
        /// Value that should be numeric.
        number: String,
    },

    /// Counted value {value} has width {width}, but it must be no wider than
    /// {max_width}, the width of the narrowest variable in multiple response
    /// set {mr_set}.
    #[error("Counted value {value} has width {width}, but it must be no wider than {max_width}, the width of the narrowest variable in multiple response set {mr_set}.")]
    TooWideMDGroupCountedValue {
        /// Multiple response set name.
        mr_set: Identifier,
        /// Counted value.
        value: String,
        /// Width of counted value.
        width: usize,
        /// Maximum allowed width of counted value.
        max_width: u16,
    },

    /// Ignoring long string value label for unknown variable {0}.
    #[error("Ignoring long string value label for unknown variable {0}.")]
    UnknownLongStringValueLabelVariable(
        /// Variable name.
        Identifier,
    ),

    /// Ignoring long string value label for numeric variable {0}.
    #[error("Ignoring long string value label for numeric variable {0}.")]
    LongStringValueLabelNumericVariable(
        /// Variable name.
        Identifier,
    ),

    /// Invalid variable name {0} in variable attribute record.
    #[error("Invalid variable name {0} in variable attribute record.")]
    UnknownAttributeVariableName(
        /// Variable name.
        Identifier,
    ),

    /// Unknown short name {0} in long variable name record.
    #[error("Unknown short name {0} in long variable name record.")]
    UnknownShortName(
        /// Short variable name.
        Identifier,
    ),

    /// Duplicate long variable name {0}.
    #[error("Duplicate long variable name {0}.")]
    DuplicateLongName(
        /// Long variable name.
        Identifier,
    ),

    /// Very long string entry for unknown variable {0}.
    #[error("Very long string entry for unknown variable {0}.")]
    UnknownVeryLongString(
        /// Variable name.
        Identifier,
    ),

    /// Variable with short name {short_name} listed in very long string record
    /// with width {width}, which requires only one segment.
    #[error("Variable with short name {short_name} listed in very long string record with width {width}, which requires only one segment.")]
    ShortVeryLongString {
        /// Short variable name.
        short_name: Identifier,
        /// Invalid width.
        width: u16,
    },

    /// Variable with short name {short_name} listed in very long string record
    /// with width {width} requires string segments for {n_segments} dictionary
    /// indexes starting at index {index}, but the dictionary only contains
    /// {len} indexes.
    #[error("Variable with short name {short_name} listed in very long string record with width {width} requires string segments for {n_segments} dictionary indexes starting at index {index}, but the dictionary only contains {len} indexes.")]
    VeryLongStringOverflow {
        /// Short variable name.
        short_name: Identifier,
        /// Width.
        width: u16,
        /// Starting index.
        index: usize,
        /// Expected number of segments.
        n_segments: usize,
        /// Number of indexes in dictionary.
        len: usize,
    },

    /// Variable with short name {short_name} listed in very long string record
    /// with width {width} has segment {index} of width {actual} (expected
    /// {expected}).
    #[error("Variable with short name {short_name} listed in very long string record with width {width} has segment {index} of width {actual} (expected {expected}).")]
    VeryLongStringInvalidSegmentWidth {
        /// Variable short name.
        short_name: Identifier,
        /// Variable width.
        width: u16,
        /// Variable index.
        index: usize,
        /// Actual width.
        actual: usize,
        /// Expected width.
        expected: usize,
    },

    /// File contains multiple {0:?} records.
    #[error("File contains multiple {0:?} records.")]
    MoreThanOne(
        /// Record name.
        &'static str,
    ),

    /// File designates string variable {name} (index {index}) as weight
    /// variable, but weight variables must be numeric.
    #[error("File designates string variable {name} (index {index}) as weight variable, but weight variables must be numeric.")]
    InvalidWeightVar {
        /// Variable name.
        name: Identifier,
        /// Variable index.
        index: u32,
    },

    /// File weight variable index {index} is invalid because it exceeds maximum
    /// variable index {max_index}.
    #[error(
        "File weight variable index {index} is invalid because it exceeds maximum variable index {max_index}."
    )]
    WeightIndexOutOfRange {
        /// Variable index.
        index: u32,
        /// Maximum variable index.
        max_index: usize,
    },

    /// File weight variable index {index} is invalid because it refers to long
    /// string continuation for variable {name}.
    #[error(
        "File weight variable index {index} is invalid because it refers to long string continuation for variable {name}."
    )]
    WeightIndexStringContinuation {
        /// Variable index.
        index: u32,
        /// Variable name.
        name: Identifier,
    },

    /// Invalid role.
    #[error(transparent)]
    InvalidRole(
        /// Role error.
        InvalidRole,
    ),

    /// File header claims {expected} variable positions but {actual} were read
    /// from file.
    #[error("File header claims {expected} variable positions but {actual} were read from file.")]
    WrongVariablePositions {
        /// Actual number of variable positions.
        actual: usize,
        /// Number of variable positions claimed by file header.
        expected: usize,
    },

    /// Unknown variable name \"{name}\" in long string missing value record.
    #[error("Unknown variable name \"{name}\" in long string missing value record.")]
    LongStringMissingValueUnknownVariable {
        /// Variable name.
        name: Identifier,
    },

    /// Invalid long string missing value for {} variable {name}.
    #[error("Invalid long string missing value for {} variable {name}.", width.display_adjective())]
    LongStringMissingValueBadWdith {
        /// Variable name.
        name: Identifier,
        /// Variable width.
        width: VarWidth,
    },

    /// Long string missing values record says variable {name} has {count}
    /// missing values, but only 1 to 3 missing values are allowed.
    #[error("Long string missing values record says variable {name} has {count} missing values, but only 1 to 3 missing values are allowed.")]
    LongStringMissingValueInvalidCount {
        /// Variable name.
        name: Identifier,
        /// Claimed number of missing values.
        count: usize,
    },

    /// Long string missing values for variable {0} are too wide.
    #[error("Long string missing values for variable {0} are too wide.")]
    MissingValuesTooWide(
        /// Variable name.
        Identifier,
    ),

    /// Unknown extension record with subtype {subtype} at offset {offset:#x},
    /// consisting of {count} {size}-byte units.  Please feel free to report
    /// this as a bug.
    #[error("Unknown extension record with subtype {subtype} at offset {offset:#x}, consisting of {count} {size}-byte units.  Please feel free to report this as a bug.")]
    UnknownExtensionRecord {
        /// Extension record file starting offset.
        offset: u64,
        /// Extension record subtype.
        subtype: u32,
        /// Extension record per-element size.
        size: u32,
        /// Number of elements in extension record.
        count: u32,
    },

    /// Floating-point representation indicated by system file ({0}) differs from expected (1).
    #[error(
        "Floating-point representation indicated by system file ({0}) differs from expected (1)."
    )]
    UnexpectedFloatFormat(
        /// Floating-point format.
        i32,
    ),

    /// Integer format indicated by system file ({actual}) differs from
    /// expected ({expected})..
    #[error(
        "Integer format indicated by system file ({actual}) differs from expected ({expected})."
    )]
    UnexpectedEndianess {
        /// Endianness declared by system file.
        actual: i32,
        /// Actual endianness used in system file.
        expected: i32,
    },

    /// System file specifies value {actual:?} ({}) as {name} but {expected:?} ({}) was expected..
    #[error(
        "System file specifies value {actual:?} ({}) as {name} but {expected:?} ({}) was expected.",
        HexFloat(*actual),
        HexFloat(*expected)
    )]
    UnexpectedFloatValue {
        /// Actual floating-point value in system file.
        actual: f64,
        /// Expected floating-point value in system file.
        expected: f64,
        /// Name for this special floating-point value.
        name: &'static str,
    },

    /// Variable set \"{variable_set}\" includes unknown variable {variable}.
    #[error("Variable set \"{variable_set}\" includes unknown variable {variable}.")]
    UnknownVariableSetVariable {
        /// Name of variable set.
        variable_set: String,
        /// Variable name.
        variable: Identifier,
    },

    /// Dictionary has {expected} variables but {actual} variable display
    /// entries are present.
    #[error(
        "Dictionary has {expected} variables but {actual} variable display entries are present."
    )]
    WrongNumberOfVarDisplay {
        /// Expected number of variable-display entries.
        expected: usize,
        /// Number of variable-display entries actually present.
        actual: usize,
    },
}

/// Options for reading a system file.
#[derive(Default, Clone, Debug)]
pub struct ReaderOptions {
    /// Character encoding for text in the system file.
    ///
    /// If not set, the character encoding will be determined from reading the
    /// file, or a default encoding will be used.
    pub encoding: Option<&'static Encoding>,

    /// Password to use to unlock an encrypted system file.
    ///
    /// For an encrypted system file, this must be set to the (encoded or
    /// unencoded) password.
    ///
    /// For a plaintext system file, this must be `None`.
    pub password: Option<String>,
}

impl ReaderOptions {
    /// Construct a new `ReaderOptions` that initially does not specify an
    /// encoding or password.
    pub fn new() -> Self {
        Self::default()
    }

    /// Causes the file to be read using the specified `encoding`, or with a
    /// default if `encoding` is None.
    pub fn with_encoding(self, encoding: Option<&'static Encoding>) -> Self {
        Self { encoding, ..self }
    }

    /// Causes the file to be read by decrypting it with the given `password` or
    /// without decrypting if `encoding` is None.
    pub fn with_password(self, password: Option<String>) -> Self {
        Self { password, ..self }
    }

    /// Opens the file at `path`, reporting warnings using `warn`.
    pub fn open_file<P, F>(self, path: P, warn: F) -> Result<SystemFile, AnyError>
    where
        P: AsRef<Path>,
        F: FnMut(AnyError),
    {
        let file = File::open(path)?;
        if self.password.is_some() {
            // Don't create `BufReader`, because [EncryptedReader] will buffer.
            self.open_reader(file, warn)
        } else {
            self.open_reader(BufReader::new(file), warn)
        }
    }

    /// Opens the file read from `reader`, reporting warnings using `warn`.
    pub fn open_reader<R, F>(self, reader: R, warn: F) -> Result<SystemFile, AnyError>
    where
        R: Read + Seek + 'static,
        F: FnMut(AnyError),
    {
        if let Some(password) = &self.password {
            Self::open_reader_inner(
                EncryptedFile::new(reader)?
                    .unlock(password.as_bytes())
                    .map_err(|_| anyhow!("Incorrect password."))?,
                self.encoding,
                warn,
            )
        } else {
            Self::open_reader_inner(reader, self.encoding, warn)
        }
    }

    fn open_reader_inner<R, F>(
        reader: R,
        encoding: Option<&'static Encoding>,
        mut warn: F,
    ) -> Result<SystemFile, AnyError>
    where
        R: Read + Seek + 'static,
        F: FnMut(AnyError),
    {
        let mut reader = Reader::new(reader, |warning| warn(warning.into()))?;
        let records = reader.records().collect::<Result<Vec<_>, _>>()?;
        let header = reader.header().clone();
        let cases = reader.cases();
        let encoding = if let Some(encoding) = encoding {
            encoding
        } else {
            infer_encoding(&records, |warning| warn(warning.into()))?
        };
        let mut decoder = raw::Decoder::new(encoding, |warning| warn(warning.into()));
        let header = header.decode(&mut decoder);
        let records = records
            .into_iter()
            .map(|record| record.decode(&mut decoder))
            .collect::<Records>();
        let encoding = decoder.into_encoding();

        Ok(records.decode(header, cases, encoding, |e| warn(e.into())))
    }
}

/// The content of an SPSS system file.
#[derive(Debug)]
pub struct SystemFile {
    /// The system file dictionary.
    pub dictionary: Dictionary,

    /// System file metadata that is not part of the dictionary.
    pub metadata: Metadata,

    /// Data in the system file.
    pub cases: Cases,
}

impl SystemFile {
    /// Returns the individual parts of the [SystemFile].
    pub fn into_parts(self) -> (Dictionary, Metadata, Cases) {
        (self.dictionary, self.metadata, self.cases)
    }
}

/// Decoded records in a system file, arranged by type.
///
/// The `Vec` fields are all in order read from the file.
#[derive(Clone, Debug, Default)]
pub struct Records {
    /// Variable records.
    pub variable: Vec<VariableRecord<String>>,

    /// Value label records.
    pub value_label: Vec<ValueLabelRecord<RawDatum, String>>,

    /// Document records.
    pub document: Vec<DocumentRecord<String>>,

    /// Integer info record.
    pub integer_info: Vec<IntegerInfoRecord>,

    /// Float info record.
    pub float_info: Vec<FloatInfoRecord>,

    /// Variable display record.
    pub var_display: Vec<VarDisplayRecord>,

    /// Multiple response set records.
    pub multiple_response: Vec<MultipleResponseRecord<Identifier, String>>,

    /// Long string value label records.
    pub long_string_value_labels: Vec<LongStringValueLabelRecord<Identifier, String>>,

    /// Long string missing value records.
    pub long_string_missing_values: Vec<LongStringMissingValueRecord<Identifier>>,

    /// Encoding record.
    pub encoding: Vec<EncodingRecord>,

    /// Number of cases record.
    pub number_of_cases: Vec<NumberOfCasesRecord>,

    /// Variable sets records.
    pub variable_sets: Vec<VariableSetRecord>,

    /// Product info record.
    pub product_info: Vec<ProductInfoRecord>,

    /// Long variable naems records.
    pub long_names: Vec<LongNamesRecord>,

    /// Very long string variable records.
    pub very_long_strings: Vec<VeryLongStringsRecord>,

    /// File attribute records.
    pub file_attributes: Vec<FileAttributesRecord>,

    /// Variable attribute records.
    pub variable_attributes: Vec<VariableAttributesRecord>,

    /// Other extension records.
    pub other_extension: Vec<Extension>,
}

impl Extend<raw::DecodedRecord> for Records {
    fn extend<T>(&mut self, iter: T)
    where
        T: IntoIterator<Item = raw::DecodedRecord>,
    {
        for record in iter {
            match record {
                DecodedRecord::Variable(record) => {
                    self.variable.push(record);
                }
                DecodedRecord::ValueLabel(record) => {
                    self.value_label.push(record);
                }
                DecodedRecord::Document(record) => {
                    self.document.push(record);
                }
                DecodedRecord::IntegerInfo(record) => {
                    self.integer_info.push(record);
                }
                DecodedRecord::FloatInfo(record) => {
                    self.float_info.push(record);
                }
                DecodedRecord::VariableSets(record) => {
                    self.variable_sets.push(record);
                }
                DecodedRecord::VarDisplay(record) => {
                    self.var_display.push(record);
                }
                DecodedRecord::MultipleResponse(record) => {
                    self.multiple_response.push(record);
                }
                DecodedRecord::LongStringValueLabels(record) => {
                    self.long_string_value_labels.push(record)
                }
                DecodedRecord::LongStringMissingValues(record) => {
                    self.long_string_missing_values.push(record);
                }
                DecodedRecord::Encoding(record) => {
                    self.encoding.push(record);
                }
                DecodedRecord::NumberOfCases(record) => {
                    self.number_of_cases.push(record);
                }
                DecodedRecord::ProductInfo(record) => {
                    self.product_info.push(record);
                }
                DecodedRecord::LongNames(record) => {
                    self.long_names.push(record);
                }
                DecodedRecord::VeryLongStrings(record) => {
                    self.very_long_strings.push(record);
                }
                DecodedRecord::FileAttributes(record) => {
                    self.file_attributes.push(record);
                }
                DecodedRecord::VariableAttributes(record) => {
                    self.variable_attributes.push(record);
                }
                DecodedRecord::OtherExtension(record) => {
                    self.other_extension.push(record);
                }
                DecodedRecord::EndOfHeaders(_)
                | DecodedRecord::ZHeader(_)
                | DecodedRecord::ZTrailer(_) => (),
            }
        }
    }
}

impl FromIterator<DecodedRecord> for Records {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = DecodedRecord>,
    {
        let mut records = Records::default();
        records.extend(iter);
        records
    }
}

impl Records {
    /// Constructs `Records` from the raw records in `iter`, decoding them with
    /// `decoder`.
    pub fn from_raw<T>(iter: T, decoder: &mut raw::Decoder) -> Self
    where
        T: IntoIterator<Item = raw::Record>,
    {
        iter.into_iter()
            .map(|record| record.decode(decoder))
            .collect()
    }

    /// Decodes this [Records] along with `header` and `cases` into a
    /// [SystemFile].  `encoding` is the encoding that was used to decode these
    /// records.  Uses `warn` to report warnings.
    pub fn decode(
        mut self,
        header: FileHeader<String>,
        mut cases: Cases,
        encoding: &'static Encoding,
        mut warn: impl FnMut(Error),
    ) -> SystemFile {
        for (count, record_name) in [
            (self.integer_info.len(), "integer info"),
            (self.float_info.len(), "float info"),
            (self.var_display.len(), "variable display"),
            (self.encoding.len(), "encoding"),
            (self.number_of_cases.len(), "number of cases"),
            (self.product_info.len(), "product info"),
        ] {
            if count > 1 {
                warn(Error::MoreThanOne(record_name));
            }
        }

        let mut dictionary = Dictionary::new(encoding);

        let file_label = fix_line_ends(header.file_label.trim_end_matches(' '));
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

        if let Some(integer_info) = self.integer_info.first() {
            let floating_point_rep = integer_info.floating_point_rep;
            if floating_point_rep != 1 {
                warn(Error::UnexpectedFloatFormat(floating_point_rep))
            }

            let expected = match header.endian {
                Endian::Big => 1,
                Endian::Little => 2,
            };
            let actual = integer_info.endianness;
            if actual != expected {
                warn(Error::UnexpectedEndianess { actual, expected });
            }
        };

        if let Some(float_info) = self.float_info.get(0) {
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

        if let Some(nominal_case_size) = header.nominal_case_size {
            let n_vars = self.variable.len();
            if n_vars != nominal_case_size as usize
                && self
                    .integer_info
                    .get(0)
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
                        new_format: new_spec,
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
                        new_format: new_spec,
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

        if let Some(weight_index) = header.weight_index {
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

            let written_by_readstat = header.eye_catcher.contains("ReadStat");
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

        if let Some(display) = self.var_display.first() {
            if display.0.len() != dictionary.variables.len() {
                warn(Error::WrongNumberOfVarDisplay {
                    expected: dictionary.variables.len(),
                    actual: display.0.len(),
                });
            }
            for (display, index) in display.0.iter().zip(0..dictionary.variables.len()) {
                let variable = dictionary.variables.get_index_mut2(index).unwrap();
                if let Some(width) = display.width {
                    variable.display_width = width;
                }
                if let Some(alignment) = display.alignment {
                    variable.alignment = alignment;
                }
                if let Some(measure) = display.measure {
                    variable.measure = Some(measure);
                }
            }
        }

        for record in self
            .multiple_response
            .iter()
            .flat_map(|record| record.sets.iter())
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
                    warn(Error::UnknownVeryLongString(record.short_name.clone()));
                    continue;
                };
                let width = VarWidth::String(record.length);
                let n_segments = width.n_segments();
                if n_segments == 1 {
                    warn(Error::ShortVeryLongString {
                        short_name: record.short_name.clone(),
                        width: record.length,
                    });
                    continue;
                }
                if index + n_segments > dictionary.variables.len() {
                    warn(Error::VeryLongStringOverflow {
                        short_name: record.short_name.clone(),
                        width: record.length,
                        index,
                        n_segments,
                        len: dictionary.variables.len(),
                    });
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
                    warn(Error::UnknownShortName(short_name.clone()));
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
                warn(Error::UnknownAttributeVariableName(
                    attr_set.long_var_name.clone(),
                ));
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
            .flat_map(|record| record.labels.into_iter())
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
            .flat_map(|record| record.values.into_iter())
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
                Err(MissingValuesError::TooWide) => {
                    warn(Error::MissingValuesTooWide(record.var_name.clone()))
                }
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

        let metadata = Metadata::decode(&header, &self, warn);
        if let Some(n_cases) = metadata.n_cases {
            cases = cases.with_expected_cases(n_cases);
        }
        SystemFile {
            dictionary,
            metadata,
            cases,
        }
    }
}

/// System file metadata that is not part of [Dictionary].
///
/// [Dictionary]: crate::dictionary::Dictionary
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Metadata {
    /// Creation date and time.
    ///
    /// This comes from the file header, not from the file system.
    pub creation: NaiveDateTime,

    /// Endianness of integers and floating-point numbers in the file.
    pub endian: Endian,

    /// Compression type (if any).
    pub compression: Option<Compression>,

    /// Number of cases in the file, if it says.
    ///
    /// This is not trustworthy: there can be more or fewer.
    pub n_cases: Option<u64>,

    /// Name of the product that wrote the file.
    pub product: String,

    /// Extended name of the product that wrote the file.
    pub product_ext: Option<String>,

    /// Version number of the product that wrote the file.
    ///
    /// For example, `(1,2,3)` is version 1.2.3.
    pub version: Option<(i32, i32, i32)>,
}

impl Metadata {
    /// Returns a pivot table [Group] and associated [Value]s that describe this
    /// `Metadata` if they are put into a [PivotTable].
    ///
    /// [PivotTable]: crate::output::pivot::PivotTable
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

    fn decode(header: &FileHeader<String>, headers: &Records, mut warn: impl FnMut(Error)) -> Self {
        let header = &header;
        let creation_date = NaiveDate::parse_from_str(&header.creation_date, "%e %b %y")
            .unwrap_or_else(|_| {
                warn(Error::InvalidCreationDate(header.creation_date.to_string()));
                Default::default()
            });
        let creation_time = NaiveTime::parse_from_str(&header.creation_time, "%H:%M:%S")
            .unwrap_or_else(|_| {
                warn(Error::InvalidCreationTime(header.creation_time.to_string()));
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
                .first()
                .map(|record| record.n_cases)
                .or_else(|| header.n_cases.map(|n| n as u64)),
            product,
            product_ext: headers.product_info.first().map(|pe| fix_line_ends(&pe.0)),
            version: headers.integer_info.first().map(|ii| ii.version),
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
        input: &raw::records::MultipleResponseSet<Identifier, String>,
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
    raw: RawFormat,
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
        input: &raw::records::MultipleResponseType,
        min_width: VarWidth,
    ) -> Result<Self, Error> {
        match input {
            raw::records::MultipleResponseType::MultipleDichotomy { value, labels } => {
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
            raw::records::MultipleResponseType::MultipleCategory => {
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
