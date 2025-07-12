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
    fs::File,
    io::{Cursor, Read, Seek},
    path::Path,
    sync::Arc,
};

use crate::{
    crypto::EncryptedFile,
    endian::Endian,
    output::{
        pivot::{test::assert_lines_eq, Axis3, Dimension, Group, PivotTable, Value},
        Details, Item, Text,
    },
    sys::{
        cooked::ReaderOptions,
        raw::{self, ErrorDetails},
        sack::sack,
    },
};

#[test]
fn variable_labels_and_missing_values() {
    test_sack_sysfile("variable_labels_and_missing_values");
}

#[test]
fn unspecified_number_of_variable_positions() {
    test_sack_sysfile("unspecified_number_of_variable_positions");
}

#[test]
fn wrong_variable_positions_but_v13() {
    test_sack_sysfile("wrong_variable_positions_but_v13");
}

#[test]
fn value_labels() {
    test_sack_sysfile("value_labels");
}

#[test]
fn documents() {
    test_sack_sysfile("documents");
}

#[test]
fn empty_document_record() {
    test_sack_sysfile("empty_document_record");
}

#[test]
fn variable_sets() {
    test_sack_sysfile("variable_sets");
}

#[test]
fn multiple_response_sets() {
    test_sack_sysfile("multiple_response_sets");
}

#[test]
fn extra_product_info() {
    // Also checks for handling of CR-only line ends in file label and extra
    // product info.
    test_sack_sysfile("extra_product_info");
}

#[test]
fn variable_display_without_width() {
    test_sack_sysfile("variable_display_without_width");
}

#[test]
fn variable_display_with_width() {
    test_sack_sysfile("variable_display_with_width");
}

#[test]
fn long_variable_names() {
    test_sack_sysfile("long_variable_names");
}

#[test]
fn very_long_strings() {
    test_sack_sysfile("very_long_strings");
}

#[test]
fn attributes() {
    test_sack_sysfile("attributes");
}

#[test]
fn variable_roles() {
    test_sack_sysfile("variable_roles");
}

#[test]
fn compressed_data() {
    test_sack_sysfile("compressed_data");
}

#[test]
fn compressed_data_zero_bias() {
    test_sack_sysfile("compressed_data_zero_bias");
}

#[test]
fn compressed_data_other_bias() {
    test_sack_sysfile("compressed_data_other_bias");
}

#[test]
fn zcompressed_data() {
    test_sack_sysfile("zcompressed_data");
}

#[test]
fn no_variables() {
    test_sack_sysfile("no_variables");
}

#[test]
fn unknown_encoding() {
    test_sack_sysfile("unknown_encoding");
}

#[test]
fn misplaced_type_4_record() {
    test_sack_sysfile("misplaced_type_4_record");
}

#[test]
fn bad_record_type() {
    test_sack_sysfile("bad_record_type");
}

#[test]
fn wrong_variable_positions() {
    test_sack_sysfile("wrong_variable_positions");
}

#[test]
fn invalid_variable_name() {
    test_sack_sysfile("invalid_variable_name");
}

#[test]
fn invalid_label_indicator() {
    test_sack_sysfile("invalid_label_indicator");
}

#[test]
fn invalid_missing_indicator() {
    test_sack_sysfile("invalid_missing_indicator");
}

#[test]
fn invalid_missing_indicator2() {
    test_sack_sysfile("invalid_missing_indicator2");
}

#[test]
fn missing_string_continuation() {
    test_sack_sysfile("missing_string_continuation");
}

#[test]
fn invalid_variable_format() {
    test_sack_sysfile("invalid_variable_format");
}

#[test]
fn invalid_long_string_missing_values() {
    test_sack_sysfile("invalid_long_string_missing_values");
}

#[test]
fn weight_must_be_numeric() {
    test_sack_sysfile("weight_must_be_numeric");
}

#[test]
fn weight_variable_bad_index() {
    test_sack_sysfile("weight_variable_bad_index");
}

#[test]
fn weight_variable_continuation() {
    test_sack_sysfile("weight_variable_continuation");
}

#[test]
fn multiple_documents_records() {
    test_sack_sysfile("multiple_documents_records");
}

#[test]
fn unknown_extension_record() {
    test_sack_sysfile("unknown_extension_record");
}

#[test]
fn extension_too_large() {
    test_sack_sysfile("extension_too_large");
}

#[test]
fn bad_machine_integer_info_count() {
    test_sack_sysfile("bad_machine_integer_info_count");
}

#[test]
fn bad_machine_integer_info_float_format() {
    test_sack_sysfile("bad_machine_integer_info_float_format");
}

#[test]
fn bad_machine_integer_info_endianness() {
    test_sack_sysfile("bad_machine_integer_info_endianness");
}

#[test]
fn bad_machine_float_info_size() {
    test_sack_sysfile("bad_machine_float_info_size");
}

#[test]
fn wrong_special_floats() {
    test_sack_sysfile("wrong_special_floats");
}

#[test]
fn variable_sets_unknown_variable() {
    test_sack_sysfile("variable_sets_unknown_variable");
}

#[test]
fn multiple_response_sets_bad_name() {
    test_sack_sysfile("multiple_response_sets_bad_name");
}

#[test]
fn multiple_response_sets_missing_space_after_c() {
    test_sack_sysfile("multiple_response_sets_missing_space_after_c");
}

#[test]
fn multiple_response_sets_missing_space_after_e() {
    test_sack_sysfile("multiple_response_sets_missing_space_after_e");
}

#[test]
fn multiple_response_sets_missing_label_source() {
    test_sack_sysfile("multiple_response_sets_missing_label_source");
}

#[test]
fn multiple_response_sets_unexpected_label_source() {
    test_sack_sysfile("multiple_response_sets_unexpected_label_source");
}

#[test]
fn multiple_response_sets_bad_counted_string() {
    test_sack_sysfile("multiple_response_sets_bad_counted_string");
}

#[test]
fn multiple_response_sets_counted_string_missing_space() {
    test_sack_sysfile("multiple_response_sets_counted_string_missing_space");
}

#[test]
fn multiple_response_sets_counted_string_bad_length() {
    test_sack_sysfile("multiple_response_sets_counted_string_bad_length");
}

#[test]
fn multiple_response_sets_missing_space_after_counted_string() {
    test_sack_sysfile("multiple_response_sets_missing_space_after_counted_string");
}

#[test]
fn multiple_response_sets_missing_newline_after_variable_name() {
    test_sack_sysfile("multiple_response_sets_missing_newline_after_variable_name");
}

#[test]
fn multiple_response_sets_duplicate_variable_name() {
    test_sack_sysfile("multiple_response_sets_duplicate_variable_name");
}

#[test]
fn mixed_variable_types_in_mrsets() {
    test_sack_sysfile("mixed_variable_types_in_mrsets");
}

#[test]
fn missing_newline_after_variable_name_in_mrsets() {
    test_sack_sysfile("missing_newline_after_variable_name_in_mrsets");
}

#[test]
fn zero_or_one_variable_in_mrset() {
    test_sack_sysfile("zero_or_one_variable_in_mrset");
}

#[test]
fn wrong_display_parameter_size() {
    test_sack_sysfile("wrong_display_parameter_size");
}

#[test]
fn wrong_display_parameter_count() {
    test_sack_sysfile("wrong_display_parameter_count");
}

#[test]
fn wrong_display_measurement_level() {
    test_sack_sysfile("wrong_display_measurement_level");
}

#[test]
fn wrong_display_alignment() {
    test_sack_sysfile("wrong_display_alignment");
}

#[test]
fn bad_variable_name_in_variable_value_pair() {
    test_sack_sysfile("bad_variable_name_in_variable_value_pair");
}

#[test]
fn duplicate_long_variable_name() {
    test_sack_sysfile("duplicate_long_variable_name");
}

#[test]
fn bad_very_long_string_length() {
    test_sack_sysfile("bad_very_long_string_length");
}

#[test]
fn bad_very_long_string_segment_width() {
    test_sack_sysfile("bad_very_long_string_segment_width");
}

#[test]
fn too_many_value_labels() {
    test_sack_sysfile("too_many_value_labels");
}

#[test]
fn missing_type_4_record() {
    test_sack_sysfile("missing_type_4_record");
}

#[test]
fn value_label_with_no_associated_variables() {
    test_sack_sysfile("value_label_with_no_associated_variables");
}

#[test]
fn type_4_record_names_long_string_variable() {
    test_sack_sysfile("type_4_record_names_long_string_variable");
}

#[test]
fn value_label_variable_indexes_must_be_in_correct_range() {
    test_sack_sysfile("value_label_variable_indexes_must_be_in_correct_range");
}

#[test]
fn value_label_variable_indexes_must_not_be_long_string_continuation() {
    test_sack_sysfile("value_label_variable_indexes_must_not_be_long_string_continuation");
}

#[test]
fn variables_for_value_label_must_all_be_same_type() {
    test_sack_sysfile("variables_for_value_label_must_all_be_same_type");
}

#[test]
fn duplicate_value_labels_type() {
    test_sack_sysfile("duplicate_value_labels_type");
}

#[test]
fn missing_attribute_value() {
    test_sack_sysfile("missing_attribute_value");
}

#[test]
fn unquoted_attribute_value() {
    test_sack_sysfile("unquoted_attribute_value");
}

#[test]
fn duplicate_attribute_name() {
    test_sack_sysfile("duplicate_attribute_name");
}

#[test]
fn bad_variable_name_in_long_string_value_label() {
    test_sack_sysfile("bad_variable_name_in_long_string_value_label");
}

#[test]
fn fewer_data_records_than_indicated_by_file_header() {
    test_sack_sysfile("fewer_data_records_than_indicated_by_file_header");
}

#[test]
fn more_data_records_than_indicated_by_file_header() {
    test_sack_sysfile("more_data_records_than_indicated_by_file_header");
}

#[test]
fn partial_data_record_between_variables() {
    test_sack_sysfile("partial_data_record_between_variables");
}

#[test]
fn partial_data_record_within_long_string() {
    test_sack_sysfile("partial_data_record_within_long_string");
}

#[test]
fn partial_compressed_data_record() {
    test_sack_sysfile("partial_compressed_data_record");
}

#[test]
fn zcompressed_data_bad_zheader_ofs() {
    test_sack_sysfile("zcompressed_data_bad_zheader_ofs");
}

#[test]
fn zcompressed_data_bad_ztrailer_ofs() {
    test_sack_sysfile("zcompressed_data_bad_ztrailer_ofs");
}

#[test]
fn zcompressed_data_invalid_ztrailer_len() {
    test_sack_sysfile("zcompressed_data_invalid_ztrailer_len");
}

#[test]
fn zcompressed_data_wrong_ztrailer_len() {
    test_sack_sysfile("zcompressed_data_wrong_ztrailer_len");
}

#[test]
fn zcompressed_data_wrong_ztrailer_bias() {
    test_sack_sysfile("zcompressed_data_wrong_ztrailer_bias");
}

#[test]
fn zcompressed_data_wrong_ztrailer_zero() {
    test_sack_sysfile("zcompressed_data_wrong_ztrailer_zero");
}

#[test]
fn zcompressed_data_wrong_block_size() {
    test_sack_sysfile("zcompressed_data_wrong_block_size");
}

#[test]
fn zcompressed_data_wrong_n_blocks() {
    test_sack_sysfile("zcompressed_data_wrong_n_blocks");
}

#[test]
fn zcompressed_data_wrong_uncompressed_ofs() {
    test_sack_sysfile("zcompressed_data_wrong_uncompressed_ofs");
}

#[test]
fn zcompressed_data_wrong_compressed_ofs() {
    test_sack_sysfile("zcompressed_data_wrong_compressed_ofs");
}

#[test]
fn zcompressed_data_compressed_sizes_dont_add_up() {
    test_sack_sysfile("zcompressed_data_compressed_sizes_dont_add_up");
}

#[test]
fn zcompressed_data_uncompressed_size_block_size() {
    test_sack_sysfile("zcompressed_data_uncompressed_size_block_size");
}

#[test]
fn zcompressed_data_compression_expands_data_too_much() {
    test_sack_sysfile("zcompressed_data_compression_expands_data_too_much");
}

#[test]
fn zcompressed_data_compressed_sizes_don_t_add_up() {
    test_sack_sysfile("zcompressed_data_compressed_sizes_don_t_add_up");
}

/// CVE-2017-10791.
/// See also https://bugzilla.redhat.com/show_bug.cgi?id=1467004.
/// See also https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=866890.
/// See also https://security-tracker.debian.org/tracker/CVE-2017-10791.
/// Found by team OWL337, using the collAFL fuzzer.
#[test]
fn integer_overflows_in_long_string_missing_values() {
    test_raw_sysfile("integer_overflows_in_long_string_missing_values");
}

/// CVE-2017-10792.
/// See also https://bugzilla.redhat.com/show_bug.cgi?id=1467005.
/// See also https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=866890.
/// See also https://security-tracker.debian.org/tracker/CVE-2017-10792.
/// Reported by team OWL337, with fuzzer collAFL.
#[test]
fn null_dereference_skipping_bad_extension_record_18() {
    test_raw_sysfile("null_dereference_skipping_bad_extension_record_18");
}

/// Duplicate variable name handling negative test.
///
/// SPSS-generated system file can contain duplicate variable names (see bug
/// #41475).
#[test]
fn duplicate_variable_name() {
    test_sack_sysfile("duplicate_variable_name");
}

#[test]
fn encrypted_file() {
    test_encrypted_sysfile("test-encrypted.sav", "pspp");
}

#[test]
fn encrypted_file_without_password() {
    let error = ReaderOptions::new()
        .open_file("src/crypto/testdata/test-encrypted.sav", |_| {
            panic!();
        })
        .unwrap_err();
    assert!(matches!(
        error.downcast::<raw::Error>().unwrap().details,
        ErrorDetails::Encrypted
    ));
}

fn test_raw_sysfile(name: &str) {
    let input_filename = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("src/sys/testdata")
        .join(name)
        .with_extension("sav");
    let sysfile = File::open(&input_filename).unwrap();
    let expected_filename = input_filename.with_extension("expected");
    let expected = String::from_utf8(std::fs::read(&expected_filename).unwrap()).unwrap();
    test_sysfile(sysfile, &expected, &expected_filename);
}

fn test_encrypted_sysfile(name: &str, password: &str) {
    let input_filename = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("src/sys/testdata")
        .join(name)
        .with_extension("sav");
    let sysfile = EncryptedFile::new(File::open(&input_filename).unwrap())
        .unwrap()
        .unlock(password.as_bytes())
        .unwrap();
    let expected_filename = input_filename.with_extension("expected");
    let expected = String::from_utf8(std::fs::read(&expected_filename).unwrap()).unwrap();
    test_sysfile(sysfile, &expected, &expected_filename);
}

fn test_sack_sysfile(name: &str) {
    let input_filename = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("src/sys/testdata")
        .join(name)
        .with_extension("sack");
    let input = String::from_utf8(std::fs::read(&input_filename).unwrap()).unwrap();
    let expected_filename = input_filename.with_extension("expected");
    let expected = String::from_utf8(std::fs::read(&expected_filename).unwrap()).unwrap();
    for endian in [Endian::Big, Endian::Little] {
        let expected = expected.replace(
            "{endian}",
            match endian {
                Endian::Big => "1",
                Endian::Little => "2",
            },
        );
        let sysfile = sack(&input, Some(&input_filename), endian).unwrap();
        test_sysfile(Cursor::new(sysfile), &expected, &expected_filename);
    }
}

fn test_sysfile<R>(sysfile: R, expected: &str, expected_filename: &Path)
where
    R: Read + Seek + 'static,
{
    let mut warnings = Vec::new();
    let output = match ReaderOptions::new().open_reader(sysfile, |warning| warnings.push(warning)) {
        Ok(system_file) => {
            let (dictionary, metadata, cases) = system_file.into_parts();
            let (group, data) = metadata.to_pivot_rows();
            let metadata_table = PivotTable::new([(Axis3::Y, Dimension::new(group))]).with_data(
                data.into_iter()
                    .enumerate()
                    .filter(|(_row, value)| !value.is_empty())
                    .map(|(row, value)| ([row], value)),
            );
            let (group, data) = dictionary.to_pivot_rows();
            let dictionary_table = PivotTable::new([(Axis3::Y, Dimension::new(group))]).with_data(
                data.into_iter()
                    .enumerate()
                    .filter(|(_row, value)| !value.is_empty())
                    .map(|(row, value)| ([row], value)),
            );
            let mut output = Vec::new();
            output.extend(
                warnings
                    .into_iter()
                    .map(|warning| Arc::new(Item::from(Text::new_log(warning.to_string())))),
            );
            output.push(Arc::new(metadata_table.into()));
            output.push(Arc::new(dictionary_table.into()));
            output.push(Arc::new(
                dictionary.output_variables().to_pivot_table().into(),
            ));
            if let Some(pt) = dictionary.output_value_labels().to_pivot_table() {
                output.push(Arc::new(pt.into()));
            }
            if let Some(pt) = dictionary.output_mrsets().to_pivot_table() {
                output.push(Arc::new(pt.into()));
            }
            if let Some(pt) = dictionary.output_attributes().to_pivot_table() {
                output.push(Arc::new(pt.into()));
            }
            if let Some(pt) = dictionary.output_variable_sets().to_pivot_table() {
                output.push(Arc::new(pt.into()));
            }
            let variables =
                Group::new("Variable").with_multiple(dictionary.variables.iter().map(|var| &**var));
            let mut case_numbers = Group::new("Case").with_label_shown();
            let mut data = Vec::new();
            for case in cases {
                match case {
                    Ok(case) => {
                        case_numbers
                            .push(Value::new_integer(Some((case_numbers.len() + 1) as f64)));
                        data.push(
                            case.0
                                .into_iter()
                                .map(|datum| Value::new_datum(&datum, dictionary.encoding))
                                .collect::<Vec<_>>(),
                        );
                    }
                    Err(error) => {
                        output.push(Arc::new(Item::from(Text::new_log(error.to_string()))));
                    }
                }
            }
            if !data.is_empty() {
                let mut pt = PivotTable::new([
                    (Axis3::X, Dimension::new(variables)),
                    (Axis3::Y, Dimension::new(case_numbers)),
                ]);
                for (row_number, row) in data.into_iter().enumerate() {
                    for (column_number, datum) in row.into_iter().enumerate() {
                        pt.insert(&[column_number, row_number], datum);
                    }
                }
                output.push(Arc::new(pt.into()));
            }
            Item::new(Details::Group(output))
        }
        Err(error) => Item::new(Details::Text(Box::new(Text::new_log(error.to_string())))),
    };

    let actual = output.to_string();
    if expected != actual && std::env::var("PSPP_REFRESH_EXPECTED").is_ok() {
        std::fs::write(expected_filename, actual).unwrap();
        panic!("{}: refreshed output", expected_filename.display());
    }
    assert_lines_eq(&expected, expected_filename.display(), &actual, "actual");
}
