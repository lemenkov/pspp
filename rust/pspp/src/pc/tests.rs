use std::path::Path;

use itertools::Itertools;

use crate::{
    data::cases_to_output,
    output::{
        Details, Item, Text,
        pivot::{PivotTable, tests::assert_lines_eq},
    },
    pc::PcFile,
};

fn test_pcfile(name: &str) {
    let base_filename = Path::new("src/pc/testdata").join(name);
    let input_filename = base_filename.with_extension("sys");
    let expected_filename = base_filename.with_extension("expected");

    let mut warnings = Vec::new();
    let output = match PcFile::open_file(input_filename, |warning| warnings.push(warning)) {
        Ok(pc_file) => {
            let (dictionary, metadata, cases) = pc_file.into_parts();

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
fn pcfile_test1() {
    test_pcfile("test1");
}

#[test]
fn pcfile_test2() {
    test_pcfile("test2");
}
