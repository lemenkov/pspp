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

use std::{fmt::Write, fs::File, io::BufRead, path::Path};

use binrw::{io::BufReader, Endian};
use encoding_rs::UTF_8;
use itertools::Itertools;
use smallstr::SmallString;
use smallvec::SmallVec;

use crate::{
    data::{ByteString, Datum, WithEncoding},
    format::{AbstractFormat, Epoch, Format, Settings, Type, UncheckedFormat, CC},
    lex::{scan::StringScanner, segment::Syntax, Punct, Token},
    settings::EndianSettings,
};

fn test(name: &str) {
    let filename = Path::new("src/format/testdata/display").join(name);
    let input = BufReader::new(File::open(&filename).unwrap());
    let settings = Settings::default()
        .with_cc(CC::A, ",,,".parse().unwrap())
        .with_cc(CC::B, "-,[[[,]]],-".parse().unwrap())
        .with_cc(CC::C, "((,[,],))".parse().unwrap())
        .with_cc(CC::D, ",XXX,,-".parse().unwrap())
        .with_cc(CC::E, ",,YYY,-".parse().unwrap());
    let endian = EndianSettings::new(Endian::Big);
    let mut value = Some(0.0);
    let mut value_name = String::new();
    for (line, line_number) in input.lines().map(|r| r.unwrap()).zip(1..) {
        let line = line.trim();
        let tokens = StringScanner::new(line, Syntax::Interactive, true)
            .unwrapped()
            .collect::<Vec<_>>();
        match &tokens[0] {
            Token::Number(number) => {
                value = if let Some(Token::Punct(Punct::Exp)) = tokens.get(1) {
                    assert_eq!(tokens.len(), 3);
                    let exponent = tokens[2].as_number().unwrap();
                    Some(number.powf(exponent))
                } else {
                    assert_eq!(tokens.len(), 1);
                    Some(*number)
                };
                value_name = String::from(line);
            }
            Token::End => {
                value = None;
                value_name = String::from(line);
            }
            Token::Id(id) => {
                let format: UncheckedFormat =
                    id.0.as_str()
                        .parse::<AbstractFormat>()
                        .unwrap()
                        .try_into()
                        .unwrap();
                let format: Format = format.try_into().unwrap();
                assert_eq!(tokens.get(1), Some(&Token::Punct(Punct::Colon)));
                let expected = tokens[2].as_string().unwrap();
                let actual = Datum::<WithEncoding<ByteString>>::Number(value)
                    .display(format)
                    .with_settings(&settings)
                    .with_endian(endian)
                    .to_string();
                assert_eq!(
                    expected,
                    &actual,
                    "{}:{line_number}: Error formatting {value_name} as {format}",
                    filename.display()
                );
            }
            _ => panic!(),
        }
    }
}

#[test]
fn comma() {
    test("comma.txt");
}

#[test]
fn dot() {
    test("dot.txt");
}

#[test]
fn dollar() {
    test("dollar.txt");
}

#[test]
fn pct() {
    test("pct.txt");
}

#[test]
fn e() {
    test("e.txt");
}

#[test]
fn f() {
    test("f.txt");
}

#[test]
fn n() {
    test("n.txt");
}

#[test]
fn z() {
    test("z.txt");
}

#[test]
fn cca() {
    test("cca.txt");
}

#[test]
fn ccb() {
    test("ccb.txt");
}

#[test]
fn ccc() {
    test("ccc.txt");
}

#[test]
fn ccd() {
    test("ccd.txt");
}

#[test]
fn cce() {
    test("cce.txt");
}

#[test]
fn pibhex() {
    test("pibhex.txt");
}

#[test]
fn rbhex() {
    test("rbhex.txt");
}

#[test]
fn leading_zeros() {
    struct Test {
        with_leading_zero: Settings,
        without_leading_zero: Settings,
    }

    impl Test {
        fn new() -> Self {
            Self {
                without_leading_zero: Settings::default(),
                with_leading_zero: Settings::default().with_leading_zero(true),
            }
        }

        fn test_with_settings(value: f64, expected: [&str; 2], settings: &Settings) {
            let value = Datum::<WithEncoding<ByteString>>::from(value);
            for (expected, d) in expected.into_iter().zip([2, 1].into_iter()) {
                assert_eq!(
                    &value
                        .display(Format::new(Type::F, 5, d).unwrap())
                        .with_settings(settings)
                        .to_string(),
                    expected
                );
            }
        }
        fn test(&self, value: f64, without: [&str; 2], with: [&str; 2]) {
            Self::test_with_settings(value, without, &self.without_leading_zero);
            Self::test_with_settings(value, with, &self.with_leading_zero);
        }
    }
    let test = Test::new();
    test.test(0.5, ["  .50", "   .5"], [" 0.50", "  0.5"]);
    test.test(0.99, ["  .99", "  1.0"], [" 0.99", "  1.0"]);
    test.test(0.01, ["  .01", "   .0"], [" 0.01", "  0.0"]);
    test.test(0.0, ["  .00", "   .0"], [" 0.00", "  0.0"]);
    test.test(-0.0, ["  .00", "   .0"], [" 0.00", "  0.0"]);
    test.test(-0.5, [" -.50", "  -.5"], ["-0.50", " -0.5"]);
    test.test(-0.99, [" -.99", " -1.0"], ["-0.99", " -1.0"]);
    test.test(-0.01, [" -.01", "   .0"], ["-0.01", "  0.0"]);
}

#[test]
fn non_ascii_cc() {
    fn test(settings: &Settings, value: f64, expected: &str) {
        assert_eq!(
            &Datum::<WithEncoding<ByteString>>::from(value)
                .display(Format::new(Type::CC(CC::A), 10, 2).unwrap())
                .with_settings(settings)
                .to_string(),
            expected
        );
    }

    let settings = Settings::default().with_cc(CC::A, "«,¥,€,»".parse().unwrap());
    test(&settings, 1.0, "   ¥1.00€ ");
    test(&settings, -1.0, "  «¥1.00€»");
    test(&settings, 1.5, "   ¥1.50€ ");
    test(&settings, -1.5, "  «¥1.50€»");
    test(&settings, 0.75, "    ¥.75€ ");
    test(&settings, 1.5e10, " ¥2E+010€ ");
    test(&settings, -1.5e10, "«¥2E+010€»");
}

fn test_binhex(name: &str) {
    let filename = Path::new("src/format/testdata/display").join(name);
    let input = BufReader::new(File::open(&filename).unwrap());
    let mut value = None;
    let mut value_name = String::new();

    let endian = EndianSettings::new(Endian::Big);
    for (line, line_number) in input.lines().map(|r| r.unwrap()).zip(1..) {
        let line = line.trim();
        let tokens = StringScanner::new(line, Syntax::Interactive, true)
            .unwrapped()
            .collect::<Vec<_>>();
        match &tokens[0] {
            Token::Number(number) => {
                value = Some(*number);
                value_name = String::from(line);
            }
            Token::End => {
                value = None;
                value_name = String::from(line);
            }
            Token::Id(id) => {
                let format: UncheckedFormat =
                    id.0.as_str()
                        .parse::<AbstractFormat>()
                        .unwrap()
                        .try_into()
                        .unwrap();
                let format: Format = format.try_into().unwrap();
                assert_eq!(tokens.get(1), Some(&Token::Punct(Punct::Colon)));
                let expected = tokens[2].as_string().unwrap();
                let mut actual = SmallVec::<[u8; 16]>::new();
                Datum::<WithEncoding<ByteString>>::Number(value)
                    .display(format)
                    .with_endian(endian)
                    .write(&mut actual, UTF_8)
                    .unwrap();
                let mut actual_s = SmallString::<[u8; 32]>::new();
                for b in actual {
                    write!(&mut actual_s, "{:02x}", b).unwrap();
                }
                assert_eq!(
                    expected,
                    &*actual_s,
                    "{}:{line_number}: Error formatting {value_name} as {format}",
                    filename.display()
                );
            }
            _ => panic!(),
        }
    }
}

#[test]
fn p() {
    test_binhex("p.txt");
}

#[test]
fn pk() {
    test_binhex("pk.txt");
}

#[test]
fn ib() {
    test_binhex("ib.txt");
}

#[test]
fn pib() {
    test_binhex("pib.txt");
}

#[test]
fn rb() {
    test_binhex("rb.txt");
}

fn test_dates(format: Format, expect: &[&str]) {
    let settings = Settings::default().with_epoch(Epoch(1930));
    let parser = Type::DateTime.parser(UTF_8).with_settings(&settings);
    static INPUTS: &[&str; 20] = &[
        "10-6-1648 0:0:0",
        "30-6-1680 4:50:38.12301",
        "24-7-1716 12:31:35.23453",
        "19-6-1768 12:47:53.34505",
        "2-8-1819 1:26:0.45615",
        "27-3-1839 20:58:11.56677",
        "19-4-1903 7:36:5.18964",
        "25-8-1929 15:43:49.83132",
        "29-9-1941 4:25:9.01293",
        "19-4-1943 6:49:27.52375",
        "7-10-1943 2:57:52.01565",
        "17-3-1992 16:45:44.86529",
        "25-2-1996 21:30:57.82047",
        "29-9-41 4:25:9.15395",
        "19-4-43 6:49:27.10533",
        "7-10-43 2:57:52.48229",
        "17-3-92 16:45:44.65827",
        "25-2-96 21:30:57.58219",
        "10-11-2038 22:30:4.18347",
        "18-7-2094 1:56:51.59319",
    ];
    assert_eq!(expect.len(), INPUTS.len());
    for (input, expect) in INPUTS.iter().copied().zip_eq(expect.iter().copied()) {
        let value = parser.parse(input).unwrap().with_encoding(UTF_8);
        let formatted = value.display(format).with_settings(&settings).to_string();
        assert_eq!(&formatted, expect);
    }
}

#[test]
fn date9() {
    test_dates(
        Format::new(Type::Date, 9, 0).unwrap(),
        &[
            "*********",
            "*********",
            "*********",
            "*********",
            "*********",
            "*********",
            "*********",
            "*********",
            "29-SEP-41",
            "19-APR-43",
            "07-OCT-43",
            "17-MAR-92",
            "25-FEB-96",
            "29-SEP-41",
            "19-APR-43",
            "07-OCT-43",
            "17-MAR-92",
            "25-FEB-96",
            "*********",
            "*********",
        ],
    );
}

#[test]
fn date11() {
    test_dates(
        Format::new(Type::Date, 11, 0).unwrap(),
        &[
            "10-JUN-1648",
            "30-JUN-1680",
            "24-JUL-1716",
            "19-JUN-1768",
            "02-AUG-1819",
            "27-MAR-1839",
            "19-APR-1903",
            "25-AUG-1929",
            "29-SEP-1941",
            "19-APR-1943",
            "07-OCT-1943",
            "17-MAR-1992",
            "25-FEB-1996",
            "29-SEP-1941",
            "19-APR-1943",
            "07-OCT-1943",
            "17-MAR-1992",
            "25-FEB-1996",
            "10-NOV-2038",
            "18-JUL-2094",
        ],
    );
}

#[test]
fn adate8() {
    test_dates(
        Format::new(Type::ADate, 8, 0).unwrap(),
        &[
            "********", "********", "********", "********", "********", "********", "********",
            "********", "09/29/41", "04/19/43", "10/07/43", "03/17/92", "02/25/96", "09/29/41",
            "04/19/43", "10/07/43", "03/17/92", "02/25/96", "********", "********",
        ],
    );
}

#[test]
fn adate10() {
    test_dates(
        Format::new(Type::ADate, 10, 0).unwrap(),
        &[
            "06/10/1648",
            "06/30/1680",
            "07/24/1716",
            "06/19/1768",
            "08/02/1819",
            "03/27/1839",
            "04/19/1903",
            "08/25/1929",
            "09/29/1941",
            "04/19/1943",
            "10/07/1943",
            "03/17/1992",
            "02/25/1996",
            "09/29/1941",
            "04/19/1943",
            "10/07/1943",
            "03/17/1992",
            "02/25/1996",
            "11/10/2038",
            "07/18/2094",
        ],
    );
}

#[test]
fn edate8() {
    test_dates(
        Format::new(Type::EDate, 8, 0).unwrap(),
        &[
            "********", "********", "********", "********", "********", "********", "********",
            "********", "29.09.41", "19.04.43", "07.10.43", "17.03.92", "25.02.96", "29.09.41",
            "19.04.43", "07.10.43", "17.03.92", "25.02.96", "********", "********",
        ],
    );
}

#[test]
fn edate10() {
    test_dates(
        Format::new(Type::EDate, 10, 0).unwrap(),
        &[
            "10.06.1648",
            "30.06.1680",
            "24.07.1716",
            "19.06.1768",
            "02.08.1819",
            "27.03.1839",
            "19.04.1903",
            "25.08.1929",
            "29.09.1941",
            "19.04.1943",
            "07.10.1943",
            "17.03.1992",
            "25.02.1996",
            "29.09.1941",
            "19.04.1943",
            "07.10.1943",
            "17.03.1992",
            "25.02.1996",
            "10.11.2038",
            "18.07.2094",
        ],
    );
}

#[test]
fn jdate5() {
    test_dates(
        Format::new(Type::JDate, 5, 0).unwrap(),
        &[
            "*****", "*****", "*****", "*****", "*****", "*****", "*****", "*****", "41272",
            "43109", "43280", "92077", "96056", "41272", "43109", "43280", "92077", "96056",
            "*****", "*****",
        ],
    );
}

#[test]
fn jdate7() {
    test_dates(
        Format::new(Type::JDate, 7, 0).unwrap(),
        &[
            "1648162", "1680182", "1716206", "1768171", "1819214", "1839086", "1903109", "1929237",
            "1941272", "1943109", "1943280", "1992077", "1996056", "1941272", "1943109", "1943280",
            "1992077", "1996056", "2038314", "2094199",
        ],
    );
}

#[test]
fn sdate8() {
    test_dates(
        Format::new(Type::SDate, 8, 0).unwrap(),
        &[
            "********", "********", "********", "********", "********", "********", "********",
            "********", "41/09/29", "43/04/19", "43/10/07", "92/03/17", "96/02/25", "41/09/29",
            "43/04/19", "43/10/07", "92/03/17", "96/02/25", "********", "********",
        ],
    );
}

#[test]
fn sdate10() {
    test_dates(
        Format::new(Type::SDate, 10, 0).unwrap(),
        &[
            "1648/06/10",
            "1680/06/30",
            "1716/07/24",
            "1768/06/19",
            "1819/08/02",
            "1839/03/27",
            "1903/04/19",
            "1929/08/25",
            "1941/09/29",
            "1943/04/19",
            "1943/10/07",
            "1992/03/17",
            "1996/02/25",
            "1941/09/29",
            "1943/04/19",
            "1943/10/07",
            "1992/03/17",
            "1996/02/25",
            "2038/11/10",
            "2094/07/18",
        ],
    );
}

#[test]
fn qyr6() {
    test_dates(
        Format::new(Type::QYr, 6, 0).unwrap(),
        &[
            "******", "******", "******", "******", "******", "******", "******", "******",
            "3 Q 41", "2 Q 43", "4 Q 43", "1 Q 92", "1 Q 96", "3 Q 41", "2 Q 43", "4 Q 43",
            "1 Q 92", "1 Q 96", "******", "******",
        ],
    );
}

#[test]
fn qyr8() {
    test_dates(
        Format::new(Type::QYr, 8, 0).unwrap(),
        &[
            "2 Q 1648", "2 Q 1680", "3 Q 1716", "2 Q 1768", "3 Q 1819", "1 Q 1839", "2 Q 1903",
            "3 Q 1929", "3 Q 1941", "2 Q 1943", "4 Q 1943", "1 Q 1992", "1 Q 1996", "3 Q 1941",
            "2 Q 1943", "4 Q 1943", "1 Q 1992", "1 Q 1996", "4 Q 2038", "3 Q 2094",
        ],
    );
}

#[test]
fn moyr6() {
    test_dates(
        Format::new(Type::MoYr, 6, 0).unwrap(),
        &[
            "******", "******", "******", "******", "******", "******", "******", "******",
            "SEP 41", "APR 43", "OCT 43", "MAR 92", "FEB 96", "SEP 41", "APR 43", "OCT 43",
            "MAR 92", "FEB 96", "******", "******",
        ],
    );
}

#[test]
fn moyr8() {
    test_dates(
        Format::new(Type::MoYr, 8, 0).unwrap(),
        &[
            "JUN 1648", "JUN 1680", "JUL 1716", "JUN 1768", "AUG 1819", "MAR 1839", "APR 1903",
            "AUG 1929", "SEP 1941", "APR 1943", "OCT 1943", "MAR 1992", "FEB 1996", "SEP 1941",
            "APR 1943", "OCT 1943", "MAR 1992", "FEB 1996", "NOV 2038", "JUL 2094",
        ],
    );
}

#[test]
fn wkyr8() {
    test_dates(
        Format::new(Type::WkYr, 8, 0).unwrap(),
        &[
            "********", "********", "********", "********", "********", "********", "********",
            "********", "39 WK 41", "16 WK 43", "40 WK 43", "11 WK 92", " 8 WK 96", "39 WK 41",
            "16 WK 43", "40 WK 43", "11 WK 92", " 8 WK 96", "********", "********",
        ],
    );
}

#[test]
fn wkyr10() {
    test_dates(
        Format::new(Type::WkYr, 10, 0).unwrap(),
        &[
            "24 WK 1648",
            "26 WK 1680",
            "30 WK 1716",
            "25 WK 1768",
            "31 WK 1819",
            "13 WK 1839",
            "16 WK 1903",
            "34 WK 1929",
            "39 WK 1941",
            "16 WK 1943",
            "40 WK 1943",
            "11 WK 1992",
            " 8 WK 1996",
            "39 WK 1941",
            "16 WK 1943",
            "40 WK 1943",
            "11 WK 1992",
            " 8 WK 1996",
            "45 WK 2038",
            "29 WK 2094",
        ],
    );
}

#[test]
fn datetime17() {
    test_dates(
        Format::new(Type::DateTime, 17, 0).unwrap(),
        &[
            "10-JUN-1648 00:00",
            "30-JUN-1680 04:50",
            "24-JUL-1716 12:31",
            "19-JUN-1768 12:47",
            "02-AUG-1819 01:26",
            "27-MAR-1839 20:58",
            "19-APR-1903 07:36",
            "25-AUG-1929 15:43",
            "29-SEP-1941 04:25",
            "19-APR-1943 06:49",
            "07-OCT-1943 02:57",
            "17-MAR-1992 16:45",
            "25-FEB-1996 21:30",
            "29-SEP-1941 04:25",
            "19-APR-1943 06:49",
            "07-OCT-1943 02:57",
            "17-MAR-1992 16:45",
            "25-FEB-1996 21:30",
            "10-NOV-2038 22:30",
            "18-JUL-2094 01:56",
        ],
    );
}

#[test]
fn datetime18() {
    test_dates(
        Format::new(Type::DateTime, 18, 0).unwrap(),
        &[
            " 10-JUN-1648 00:00",
            " 30-JUN-1680 04:50",
            " 24-JUL-1716 12:31",
            " 19-JUN-1768 12:47",
            " 02-AUG-1819 01:26",
            " 27-MAR-1839 20:58",
            " 19-APR-1903 07:36",
            " 25-AUG-1929 15:43",
            " 29-SEP-1941 04:25",
            " 19-APR-1943 06:49",
            " 07-OCT-1943 02:57",
            " 17-MAR-1992 16:45",
            " 25-FEB-1996 21:30",
            " 29-SEP-1941 04:25",
            " 19-APR-1943 06:49",
            " 07-OCT-1943 02:57",
            " 17-MAR-1992 16:45",
            " 25-FEB-1996 21:30",
            " 10-NOV-2038 22:30",
            " 18-JUL-2094 01:56",
        ],
    );
}

#[test]
fn datetime19() {
    test_dates(
        Format::new(Type::DateTime, 19, 0).unwrap(),
        &[
            "  10-JUN-1648 00:00",
            "  30-JUN-1680 04:50",
            "  24-JUL-1716 12:31",
            "  19-JUN-1768 12:47",
            "  02-AUG-1819 01:26",
            "  27-MAR-1839 20:58",
            "  19-APR-1903 07:36",
            "  25-AUG-1929 15:43",
            "  29-SEP-1941 04:25",
            "  19-APR-1943 06:49",
            "  07-OCT-1943 02:57",
            "  17-MAR-1992 16:45",
            "  25-FEB-1996 21:30",
            "  29-SEP-1941 04:25",
            "  19-APR-1943 06:49",
            "  07-OCT-1943 02:57",
            "  17-MAR-1992 16:45",
            "  25-FEB-1996 21:30",
            "  10-NOV-2038 22:30",
            "  18-JUL-2094 01:56",
        ],
    );
}

#[test]
fn datetime20() {
    test_dates(
        Format::new(Type::DateTime, 20, 0).unwrap(),
        &[
            "10-JUN-1648 00:00:00",
            "30-JUN-1680 04:50:38",
            "24-JUL-1716 12:31:35",
            "19-JUN-1768 12:47:53",
            "02-AUG-1819 01:26:00",
            "27-MAR-1839 20:58:11",
            "19-APR-1903 07:36:05",
            "25-AUG-1929 15:43:49",
            "29-SEP-1941 04:25:09",
            "19-APR-1943 06:49:27",
            "07-OCT-1943 02:57:52",
            "17-MAR-1992 16:45:44",
            "25-FEB-1996 21:30:57",
            "29-SEP-1941 04:25:09",
            "19-APR-1943 06:49:27",
            "07-OCT-1943 02:57:52",
            "17-MAR-1992 16:45:44",
            "25-FEB-1996 21:30:57",
            "10-NOV-2038 22:30:04",
            "18-JUL-2094 01:56:51",
        ],
    );
}

#[test]
fn datetime21() {
    test_dates(
        Format::new(Type::DateTime, 21, 0).unwrap(),
        &[
            " 10-JUN-1648 00:00:00",
            " 30-JUN-1680 04:50:38",
            " 24-JUL-1716 12:31:35",
            " 19-JUN-1768 12:47:53",
            " 02-AUG-1819 01:26:00",
            " 27-MAR-1839 20:58:11",
            " 19-APR-1903 07:36:05",
            " 25-AUG-1929 15:43:49",
            " 29-SEP-1941 04:25:09",
            " 19-APR-1943 06:49:27",
            " 07-OCT-1943 02:57:52",
            " 17-MAR-1992 16:45:44",
            " 25-FEB-1996 21:30:57",
            " 29-SEP-1941 04:25:09",
            " 19-APR-1943 06:49:27",
            " 07-OCT-1943 02:57:52",
            " 17-MAR-1992 16:45:44",
            " 25-FEB-1996 21:30:57",
            " 10-NOV-2038 22:30:04",
            " 18-JUL-2094 01:56:51",
        ],
    );
}

#[test]
fn datetime22() {
    test_dates(
        Format::new(Type::DateTime, 22, 0).unwrap(),
        &[
            "  10-JUN-1648 00:00:00",
            "  30-JUN-1680 04:50:38",
            "  24-JUL-1716 12:31:35",
            "  19-JUN-1768 12:47:53",
            "  02-AUG-1819 01:26:00",
            "  27-MAR-1839 20:58:11",
            "  19-APR-1903 07:36:05",
            "  25-AUG-1929 15:43:49",
            "  29-SEP-1941 04:25:09",
            "  19-APR-1943 06:49:27",
            "  07-OCT-1943 02:57:52",
            "  17-MAR-1992 16:45:44",
            "  25-FEB-1996 21:30:57",
            "  29-SEP-1941 04:25:09",
            "  19-APR-1943 06:49:27",
            "  07-OCT-1943 02:57:52",
            "  17-MAR-1992 16:45:44",
            "  25-FEB-1996 21:30:57",
            "  10-NOV-2038 22:30:04",
            "  18-JUL-2094 01:56:51",
        ],
    );
}

#[test]
fn datetime22_1() {
    test_dates(
        Format::new(Type::DateTime, 22, 1).unwrap(),
        &[
            "10-JUN-1648 00:00:00.0",
            "30-JUN-1680 04:50:38.1",
            "24-JUL-1716 12:31:35.2",
            "19-JUN-1768 12:47:53.3",
            "02-AUG-1819 01:26:00.5",
            "27-MAR-1839 20:58:11.6",
            "19-APR-1903 07:36:05.2",
            "25-AUG-1929 15:43:49.8",
            "29-SEP-1941 04:25:09.0",
            "19-APR-1943 06:49:27.5",
            "07-OCT-1943 02:57:52.0",
            "17-MAR-1992 16:45:44.9",
            "25-FEB-1996 21:30:57.8",
            "29-SEP-1941 04:25:09.2",
            "19-APR-1943 06:49:27.1",
            "07-OCT-1943 02:57:52.5",
            "17-MAR-1992 16:45:44.7",
            "25-FEB-1996 21:30:57.6",
            "10-NOV-2038 22:30:04.2",
            "18-JUL-2094 01:56:51.6",
        ],
    );
}

#[test]
fn datetime23_2() {
    test_dates(
        Format::new(Type::DateTime, 23, 2).unwrap(),
        &[
            "10-JUN-1648 00:00:00.00",
            "30-JUN-1680 04:50:38.12",
            "24-JUL-1716 12:31:35.23",
            "19-JUN-1768 12:47:53.35",
            "02-AUG-1819 01:26:00.46",
            "27-MAR-1839 20:58:11.57",
            "19-APR-1903 07:36:05.19",
            "25-AUG-1929 15:43:49.83",
            "29-SEP-1941 04:25:09.01",
            "19-APR-1943 06:49:27.52",
            "07-OCT-1943 02:57:52.02",
            "17-MAR-1992 16:45:44.87",
            "25-FEB-1996 21:30:57.82",
            "29-SEP-1941 04:25:09.15",
            "19-APR-1943 06:49:27.11",
            "07-OCT-1943 02:57:52.48",
            "17-MAR-1992 16:45:44.66",
            "25-FEB-1996 21:30:57.58",
            "10-NOV-2038 22:30:04.18",
            "18-JUL-2094 01:56:51.59",
        ],
    );
}

#[test]
fn datetime24_3() {
    test_dates(
        Format::new(Type::DateTime, 24, 3).unwrap(),
        &[
            "10-JUN-1648 00:00:00.000",
            "30-JUN-1680 04:50:38.123",
            "24-JUL-1716 12:31:35.235",
            "19-JUN-1768 12:47:53.345",
            "02-AUG-1819 01:26:00.456",
            "27-MAR-1839 20:58:11.567",
            "19-APR-1903 07:36:05.190",
            "25-AUG-1929 15:43:49.831",
            "29-SEP-1941 04:25:09.013",
            "19-APR-1943 06:49:27.524",
            "07-OCT-1943 02:57:52.016",
            "17-MAR-1992 16:45:44.865",
            "25-FEB-1996 21:30:57.820",
            "29-SEP-1941 04:25:09.154",
            "19-APR-1943 06:49:27.105",
            "07-OCT-1943 02:57:52.482",
            "17-MAR-1992 16:45:44.658",
            "25-FEB-1996 21:30:57.582",
            "10-NOV-2038 22:30:04.183",
            "18-JUL-2094 01:56:51.593",
        ],
    );
}

#[test]
fn datetime25_4() {
    test_dates(
        Format::new(Type::DateTime, 25, 4).unwrap(),
        &[
            "10-JUN-1648 00:00:00.0000",
            "30-JUN-1680 04:50:38.1230",
            "24-JUL-1716 12:31:35.2345",
            "19-JUN-1768 12:47:53.3450",
            "02-AUG-1819 01:26:00.4562",
            "27-MAR-1839 20:58:11.5668",
            "19-APR-1903 07:36:05.1896",
            "25-AUG-1929 15:43:49.8313",
            "29-SEP-1941 04:25:09.0129",
            "19-APR-1943 06:49:27.5238",
            "07-OCT-1943 02:57:52.0156",
            "17-MAR-1992 16:45:44.8653",
            "25-FEB-1996 21:30:57.8205",
            "29-SEP-1941 04:25:09.1539",
            "19-APR-1943 06:49:27.1053",
            "07-OCT-1943 02:57:52.4823",
            "17-MAR-1992 16:45:44.6583",
            "25-FEB-1996 21:30:57.5822",
            "10-NOV-2038 22:30:04.1835",
            "18-JUL-2094 01:56:51.5932",
        ],
    );
}

#[test]
fn datetime26_5() {
    test_dates(
        Format::new(Type::DateTime, 26, 5).unwrap(),
        &[
            "10-JUN-1648 00:00:00.00000",
            "30-JUN-1680 04:50:38.12301",
            "24-JUL-1716 12:31:35.23453",
            "19-JUN-1768 12:47:53.34505",
            "02-AUG-1819 01:26:00.45615",
            "27-MAR-1839 20:58:11.56677",
            "19-APR-1903 07:36:05.18964",
            "25-AUG-1929 15:43:49.83132",
            "29-SEP-1941 04:25:09.01293",
            "19-APR-1943 06:49:27.52375",
            "07-OCT-1943 02:57:52.01565",
            "17-MAR-1992 16:45:44.86529",
            "25-FEB-1996 21:30:57.82047",
            "29-SEP-1941 04:25:09.15395",
            "19-APR-1943 06:49:27.10533",
            "07-OCT-1943 02:57:52.48229",
            "17-MAR-1992 16:45:44.65827",
            "25-FEB-1996 21:30:57.58219",
            "10-NOV-2038 22:30:04.18347",
            "18-JUL-2094 01:56:51.59319",
        ],
    );
}

#[test]
fn ymdhms16() {
    test_dates(
        Format::new(Type::YmdHms, 16, 0).unwrap(),
        &[
            "1648-06-10 00:00",
            "1680-06-30 04:50",
            "1716-07-24 12:31",
            "1768-06-19 12:47",
            "1819-08-02 01:26",
            "1839-03-27 20:58",
            "1903-04-19 07:36",
            "1929-08-25 15:43",
            "1941-09-29 04:25",
            "1943-04-19 06:49",
            "1943-10-07 02:57",
            "1992-03-17 16:45",
            "1996-02-25 21:30",
            "1941-09-29 04:25",
            "1943-04-19 06:49",
            "1943-10-07 02:57",
            "1992-03-17 16:45",
            "1996-02-25 21:30",
            "2038-11-10 22:30",
            "2094-07-18 01:56",
        ],
    );
}

#[test]
fn ymdhms17() {
    test_dates(
        Format::new(Type::YmdHms, 17, 0).unwrap(),
        &[
            " 1648-06-10 00:00",
            " 1680-06-30 04:50",
            " 1716-07-24 12:31",
            " 1768-06-19 12:47",
            " 1819-08-02 01:26",
            " 1839-03-27 20:58",
            " 1903-04-19 07:36",
            " 1929-08-25 15:43",
            " 1941-09-29 04:25",
            " 1943-04-19 06:49",
            " 1943-10-07 02:57",
            " 1992-03-17 16:45",
            " 1996-02-25 21:30",
            " 1941-09-29 04:25",
            " 1943-04-19 06:49",
            " 1943-10-07 02:57",
            " 1992-03-17 16:45",
            " 1996-02-25 21:30",
            " 2038-11-10 22:30",
            " 2094-07-18 01:56",
        ],
    );
}

#[test]
fn ymdhms18() {
    test_dates(
        Format::new(Type::YmdHms, 18, 0).unwrap(),
        &[
            "  1648-06-10 00:00",
            "  1680-06-30 04:50",
            "  1716-07-24 12:31",
            "  1768-06-19 12:47",
            "  1819-08-02 01:26",
            "  1839-03-27 20:58",
            "  1903-04-19 07:36",
            "  1929-08-25 15:43",
            "  1941-09-29 04:25",
            "  1943-04-19 06:49",
            "  1943-10-07 02:57",
            "  1992-03-17 16:45",
            "  1996-02-25 21:30",
            "  1941-09-29 04:25",
            "  1943-04-19 06:49",
            "  1943-10-07 02:57",
            "  1992-03-17 16:45",
            "  1996-02-25 21:30",
            "  2038-11-10 22:30",
            "  2094-07-18 01:56",
        ],
    );
}

#[test]
fn ymdhms19() {
    test_dates(
        Format::new(Type::YmdHms, 19, 0).unwrap(),
        &[
            "1648-06-10 00:00:00",
            "1680-06-30 04:50:38",
            "1716-07-24 12:31:35",
            "1768-06-19 12:47:53",
            "1819-08-02 01:26:00",
            "1839-03-27 20:58:11",
            "1903-04-19 07:36:05",
            "1929-08-25 15:43:49",
            "1941-09-29 04:25:09",
            "1943-04-19 06:49:27",
            "1943-10-07 02:57:52",
            "1992-03-17 16:45:44",
            "1996-02-25 21:30:57",
            "1941-09-29 04:25:09",
            "1943-04-19 06:49:27",
            "1943-10-07 02:57:52",
            "1992-03-17 16:45:44",
            "1996-02-25 21:30:57",
            "2038-11-10 22:30:04",
            "2094-07-18 01:56:51",
        ],
    );
}

#[test]
fn ymdhms20() {
    test_dates(
        Format::new(Type::YmdHms, 20, 0).unwrap(),
        &[
            " 1648-06-10 00:00:00",
            " 1680-06-30 04:50:38",
            " 1716-07-24 12:31:35",
            " 1768-06-19 12:47:53",
            " 1819-08-02 01:26:00",
            " 1839-03-27 20:58:11",
            " 1903-04-19 07:36:05",
            " 1929-08-25 15:43:49",
            " 1941-09-29 04:25:09",
            " 1943-04-19 06:49:27",
            " 1943-10-07 02:57:52",
            " 1992-03-17 16:45:44",
            " 1996-02-25 21:30:57",
            " 1941-09-29 04:25:09",
            " 1943-04-19 06:49:27",
            " 1943-10-07 02:57:52",
            " 1992-03-17 16:45:44",
            " 1996-02-25 21:30:57",
            " 2038-11-10 22:30:04",
            " 2094-07-18 01:56:51",
        ],
    );
}

#[test]
fn ymdhms21() {
    test_dates(
        Format::new(Type::YmdHms, 21, 0).unwrap(),
        &[
            "  1648-06-10 00:00:00",
            "  1680-06-30 04:50:38",
            "  1716-07-24 12:31:35",
            "  1768-06-19 12:47:53",
            "  1819-08-02 01:26:00",
            "  1839-03-27 20:58:11",
            "  1903-04-19 07:36:05",
            "  1929-08-25 15:43:49",
            "  1941-09-29 04:25:09",
            "  1943-04-19 06:49:27",
            "  1943-10-07 02:57:52",
            "  1992-03-17 16:45:44",
            "  1996-02-25 21:30:57",
            "  1941-09-29 04:25:09",
            "  1943-04-19 06:49:27",
            "  1943-10-07 02:57:52",
            "  1992-03-17 16:45:44",
            "  1996-02-25 21:30:57",
            "  2038-11-10 22:30:04",
            "  2094-07-18 01:56:51",
        ],
    );
}

#[test]
fn ymdhms21_1() {
    test_dates(
        Format::new(Type::YmdHms, 21, 1).unwrap(),
        &[
            "1648-06-10 00:00:00.0",
            "1680-06-30 04:50:38.1",
            "1716-07-24 12:31:35.2",
            "1768-06-19 12:47:53.3",
            "1819-08-02 01:26:00.5",
            "1839-03-27 20:58:11.6",
            "1903-04-19 07:36:05.2",
            "1929-08-25 15:43:49.8",
            "1941-09-29 04:25:09.0",
            "1943-04-19 06:49:27.5",
            "1943-10-07 02:57:52.0",
            "1992-03-17 16:45:44.9",
            "1996-02-25 21:30:57.8",
            "1941-09-29 04:25:09.2",
            "1943-04-19 06:49:27.1",
            "1943-10-07 02:57:52.5",
            "1992-03-17 16:45:44.7",
            "1996-02-25 21:30:57.6",
            "2038-11-10 22:30:04.2",
            "2094-07-18 01:56:51.6",
        ],
    );
}

#[test]
fn ymdhms22_2() {
    test_dates(
        Format::new(Type::YmdHms, 22, 2).unwrap(),
        &[
            "1648-06-10 00:00:00.00",
            "1680-06-30 04:50:38.12",
            "1716-07-24 12:31:35.23",
            "1768-06-19 12:47:53.35",
            "1819-08-02 01:26:00.46",
            "1839-03-27 20:58:11.57",
            "1903-04-19 07:36:05.19",
            "1929-08-25 15:43:49.83",
            "1941-09-29 04:25:09.01",
            "1943-04-19 06:49:27.52",
            "1943-10-07 02:57:52.02",
            "1992-03-17 16:45:44.87",
            "1996-02-25 21:30:57.82",
            "1941-09-29 04:25:09.15",
            "1943-04-19 06:49:27.11",
            "1943-10-07 02:57:52.48",
            "1992-03-17 16:45:44.66",
            "1996-02-25 21:30:57.58",
            "2038-11-10 22:30:04.18",
            "2094-07-18 01:56:51.59",
        ],
    );
}

#[test]
fn ymdhms23_3() {
    test_dates(
        Format::new(Type::YmdHms, 23, 3).unwrap(),
        &[
            "1648-06-10 00:00:00.000",
            "1680-06-30 04:50:38.123",
            "1716-07-24 12:31:35.235",
            "1768-06-19 12:47:53.345",
            "1819-08-02 01:26:00.456",
            "1839-03-27 20:58:11.567",
            "1903-04-19 07:36:05.190",
            "1929-08-25 15:43:49.831",
            "1941-09-29 04:25:09.013",
            "1943-04-19 06:49:27.524",
            "1943-10-07 02:57:52.016",
            "1992-03-17 16:45:44.865",
            "1996-02-25 21:30:57.820",
            "1941-09-29 04:25:09.154",
            "1943-04-19 06:49:27.105",
            "1943-10-07 02:57:52.482",
            "1992-03-17 16:45:44.658",
            "1996-02-25 21:30:57.582",
            "2038-11-10 22:30:04.183",
            "2094-07-18 01:56:51.593",
        ],
    );
}

#[test]
fn ymdhms24_4() {
    test_dates(
        Format::new(Type::YmdHms, 24, 4).unwrap(),
        &[
            "1648-06-10 00:00:00.0000",
            "1680-06-30 04:50:38.1230",
            "1716-07-24 12:31:35.2345",
            "1768-06-19 12:47:53.3450",
            "1819-08-02 01:26:00.4562",
            "1839-03-27 20:58:11.5668",
            "1903-04-19 07:36:05.1896",
            "1929-08-25 15:43:49.8313",
            "1941-09-29 04:25:09.0129",
            "1943-04-19 06:49:27.5238",
            "1943-10-07 02:57:52.0156",
            "1992-03-17 16:45:44.8653",
            "1996-02-25 21:30:57.8205",
            "1941-09-29 04:25:09.1539",
            "1943-04-19 06:49:27.1053",
            "1943-10-07 02:57:52.4823",
            "1992-03-17 16:45:44.6583",
            "1996-02-25 21:30:57.5822",
            "2038-11-10 22:30:04.1835",
            "2094-07-18 01:56:51.5932",
        ],
    );
}

#[test]
fn ymdhms25_5() {
    test_dates(
        Format::new(Type::YmdHms, 25, 5).unwrap(),
        &[
            "1648-06-10 00:00:00.00000",
            "1680-06-30 04:50:38.12301",
            "1716-07-24 12:31:35.23453",
            "1768-06-19 12:47:53.34505",
            "1819-08-02 01:26:00.45615",
            "1839-03-27 20:58:11.56677",
            "1903-04-19 07:36:05.18964",
            "1929-08-25 15:43:49.83132",
            "1941-09-29 04:25:09.01293",
            "1943-04-19 06:49:27.52375",
            "1943-10-07 02:57:52.01565",
            "1992-03-17 16:45:44.86529",
            "1996-02-25 21:30:57.82047",
            "1941-09-29 04:25:09.15395",
            "1943-04-19 06:49:27.10533",
            "1943-10-07 02:57:52.48229",
            "1992-03-17 16:45:44.65827",
            "1996-02-25 21:30:57.58219",
            "2038-11-10 22:30:04.18347",
            "2094-07-18 01:56:51.59319",
        ],
    );
}

fn test_times(format: Format, name: &str) {
    let directory = Path::new("src/format/testdata/display");
    let input_filename = directory.join("time-input.txt");
    let input = BufReader::new(File::open(&input_filename).unwrap());

    let output_filename = directory.join(name);
    let output = BufReader::new(File::open(&output_filename).unwrap());

    let parser = Type::DTime.parser(UTF_8);
    for ((input, expect), line_number) in input
        .lines()
        .map(|r| r.unwrap())
        .zip_eq(output.lines().map(|r| r.unwrap()))
        .zip(1..)
    {
        let formatted = parser
            .parse(input)
            .unwrap()
            .with_encoding(UTF_8)
            .display(format)
            .to_string();
        assert!(
                formatted == expect,
                "formatting {}:{line_number} as {format}:\n  actual: {formatted:?}\nexpected: {expect:?}",
                input_filename.display()
            );
    }
}

#[test]
fn time5() {
    test_times(Format::new(Type::Time, 5, 0).unwrap(), "time5.txt");
}

#[test]
fn time6() {
    test_times(Format::new(Type::Time, 6, 0).unwrap(), "time6.txt");
}

#[test]
fn time7() {
    test_times(Format::new(Type::Time, 7, 0).unwrap(), "time7.txt");
}

#[test]
fn time8() {
    test_times(Format::new(Type::Time, 8, 0).unwrap(), "time8.txt");
}

#[test]
fn time9() {
    test_times(Format::new(Type::Time, 9, 0).unwrap(), "time9.txt");
}

#[test]
fn time10() {
    test_times(Format::new(Type::Time, 10, 0).unwrap(), "time10.txt");
}

#[test]
fn time10_1() {
    test_times(Format::new(Type::Time, 10, 1).unwrap(), "time10.1.txt");
}

#[test]
fn time11() {
    test_times(Format::new(Type::Time, 11, 0).unwrap(), "time11.txt");
}

#[test]
fn time11_1() {
    test_times(Format::new(Type::Time, 11, 1).unwrap(), "time11.1.txt");
}

#[test]
fn time11_2() {
    test_times(Format::new(Type::Time, 11, 2).unwrap(), "time11.2.txt");
}

#[test]
fn time12() {
    test_times(Format::new(Type::Time, 12, 0).unwrap(), "time12.txt");
}

#[test]
fn time12_1() {
    test_times(Format::new(Type::Time, 12, 1).unwrap(), "time12.1.txt");
}

#[test]
fn time12_2() {
    test_times(Format::new(Type::Time, 12, 2).unwrap(), "time12.2.txt");
}

#[test]
fn time12_3() {
    test_times(Format::new(Type::Time, 12, 3).unwrap(), "time12.3.txt");
}

#[test]
fn time13() {
    test_times(Format::new(Type::Time, 13, 0).unwrap(), "time13.txt");
}

#[test]
fn time13_1() {
    test_times(Format::new(Type::Time, 13, 1).unwrap(), "time13.1.txt");
}

#[test]
fn time13_2() {
    test_times(Format::new(Type::Time, 13, 2).unwrap(), "time13.2.txt");
}

#[test]
fn time13_3() {
    test_times(Format::new(Type::Time, 13, 3).unwrap(), "time13.3.txt");
}

#[test]
fn time13_4() {
    test_times(Format::new(Type::Time, 13, 4).unwrap(), "time13.4.txt");
}

#[test]
fn time14() {
    test_times(Format::new(Type::Time, 14, 0).unwrap(), "time14.txt");
}

#[test]
fn time14_1() {
    test_times(Format::new(Type::Time, 14, 1).unwrap(), "time14.1.txt");
}

#[test]
fn time14_2() {
    test_times(Format::new(Type::Time, 14, 2).unwrap(), "time14.2.txt");
}

#[test]
fn time14_3() {
    test_times(Format::new(Type::Time, 14, 3).unwrap(), "time14.3.txt");
}

#[test]
fn time14_4() {
    test_times(Format::new(Type::Time, 14, 4).unwrap(), "time14.4.txt");
}

#[test]
fn time14_5() {
    test_times(Format::new(Type::Time, 14, 5).unwrap(), "time14.5.txt");
}

#[test]
fn time15() {
    test_times(Format::new(Type::Time, 15, 0).unwrap(), "time15.txt");
}

#[test]
fn time15_1() {
    test_times(Format::new(Type::Time, 15, 1).unwrap(), "time15.1.txt");
}

#[test]
fn time15_2() {
    test_times(Format::new(Type::Time, 15, 2).unwrap(), "time15.2.txt");
}

#[test]
fn time15_3() {
    test_times(Format::new(Type::Time, 15, 3).unwrap(), "time15.3.txt");
}

#[test]
fn time15_4() {
    test_times(Format::new(Type::Time, 15, 4).unwrap(), "time15.4.txt");
}

#[test]
fn time15_5() {
    test_times(Format::new(Type::Time, 15, 5).unwrap(), "time15.5.txt");
}

#[test]
fn time15_6() {
    test_times(Format::new(Type::Time, 15, 6).unwrap(), "time15.6.txt");
}

#[test]
fn mtime5() {
    test_times(Format::new(Type::MTime, 5, 0).unwrap(), "mtime5.txt");
}

#[test]
fn mtime6() {
    test_times(Format::new(Type::MTime, 6, 0).unwrap(), "mtime6.txt");
}

#[test]
fn mtime7() {
    test_times(Format::new(Type::MTime, 7, 0).unwrap(), "mtime7.txt");
}

#[test]
fn mtime7_1() {
    test_times(Format::new(Type::MTime, 7, 1).unwrap(), "mtime7.1.txt");
}

#[test]
fn mtime8() {
    test_times(Format::new(Type::MTime, 8, 0).unwrap(), "mtime8.txt");
}

#[test]
fn mtime8_1() {
    test_times(Format::new(Type::MTime, 8, 1).unwrap(), "mtime8.1.txt");
}

#[test]
fn mtime8_2() {
    test_times(Format::new(Type::MTime, 8, 2).unwrap(), "mtime8.2.txt");
}

#[test]
fn mtime9() {
    test_times(Format::new(Type::MTime, 9, 0).unwrap(), "mtime9.txt");
}

#[test]
fn mtime9_1() {
    test_times(Format::new(Type::MTime, 9, 1).unwrap(), "mtime9.1.txt");
}

#[test]
fn mtime9_2() {
    test_times(Format::new(Type::MTime, 9, 2).unwrap(), "mtime9.2.txt");
}

#[test]
fn mtime9_3() {
    test_times(Format::new(Type::MTime, 9, 3).unwrap(), "mtime9.3.txt");
}

#[test]
fn mtime10() {
    test_times(Format::new(Type::MTime, 10, 0).unwrap(), "mtime10.txt");
}

#[test]
fn mtime10_1() {
    test_times(Format::new(Type::MTime, 10, 1).unwrap(), "mtime10.1.txt");
}

#[test]
fn mtime10_2() {
    test_times(Format::new(Type::MTime, 10, 2).unwrap(), "mtime10.2.txt");
}

#[test]
fn mtime10_3() {
    test_times(Format::new(Type::MTime, 10, 3).unwrap(), "mtime10.3.txt");
}

#[test]
fn mtime10_4() {
    test_times(Format::new(Type::MTime, 10, 4).unwrap(), "mtime10.4.txt");
}

#[test]
fn mtime11() {
    test_times(Format::new(Type::MTime, 11, 0).unwrap(), "mtime11.txt");
}

#[test]
fn mtime11_1() {
    test_times(Format::new(Type::MTime, 11, 1).unwrap(), "mtime11.1.txt");
}

#[test]
fn mtime11_2() {
    test_times(Format::new(Type::MTime, 11, 2).unwrap(), "mtime11.2.txt");
}

#[test]
fn mtime11_3() {
    test_times(Format::new(Type::MTime, 11, 3).unwrap(), "mtime11.3.txt");
}

#[test]
fn mtime11_4() {
    test_times(Format::new(Type::MTime, 11, 4).unwrap(), "mtime11.4.txt");
}

#[test]
fn mtime11_5() {
    test_times(Format::new(Type::MTime, 11, 5).unwrap(), "mtime11.5.txt");
}

#[test]
fn mtime12_5() {
    test_times(Format::new(Type::MTime, 12, 5).unwrap(), "mtime12.5.txt");
}

#[test]
fn mtime13_5() {
    test_times(Format::new(Type::MTime, 13, 5).unwrap(), "mtime13.5.txt");
}

#[test]
fn mtime14_5() {
    test_times(Format::new(Type::MTime, 14, 5).unwrap(), "mtime14.5.txt");
}

#[test]
fn mtime15_5() {
    test_times(Format::new(Type::MTime, 15, 5).unwrap(), "mtime15.5.txt");
}

#[test]
fn mtime16_5() {
    test_times(Format::new(Type::MTime, 16, 5).unwrap(), "mtime16.5.txt");
}

#[test]
fn dtime8() {
    test_times(Format::new(Type::DTime, 8, 0).unwrap(), "dtime8.txt");
}

#[test]
fn dtime9() {
    test_times(Format::new(Type::DTime, 9, 0).unwrap(), "dtime9.txt");
}

#[test]
fn dtime10() {
    test_times(Format::new(Type::DTime, 10, 0).unwrap(), "dtime10.txt");
}

#[test]
fn dtime11() {
    test_times(Format::new(Type::DTime, 11, 0).unwrap(), "dtime11.txt");
}

#[test]
fn dtime12() {
    test_times(Format::new(Type::DTime, 12, 0).unwrap(), "dtime12.txt");
}

#[test]
fn dtime13() {
    test_times(Format::new(Type::DTime, 13, 0).unwrap(), "dtime13.txt");
}

#[test]
fn dtime13_1() {
    test_times(Format::new(Type::DTime, 13, 1).unwrap(), "dtime13.1.txt");
}

#[test]
fn dtime14() {
    test_times(Format::new(Type::DTime, 14, 0).unwrap(), "dtime14.txt");
}

#[test]
fn dtime14_1() {
    test_times(Format::new(Type::DTime, 14, 1).unwrap(), "dtime14.1.txt");
}

#[test]
fn dtime14_2() {
    test_times(Format::new(Type::DTime, 14, 2).unwrap(), "dtime14.2.txt");
}

#[test]
fn dtime15() {
    test_times(Format::new(Type::DTime, 15, 0).unwrap(), "dtime15.txt");
}

#[test]
fn dtime15_1() {
    test_times(Format::new(Type::DTime, 15, 1).unwrap(), "dtime15.1.txt");
}

#[test]
fn dtime15_2() {
    test_times(Format::new(Type::DTime, 15, 2).unwrap(), "dtime15.2.txt");
}

#[test]
fn dtime15_3() {
    test_times(Format::new(Type::DTime, 15, 3).unwrap(), "dtime15.3.txt");
}

#[test]
fn dtime16() {
    test_times(Format::new(Type::DTime, 16, 0).unwrap(), "dtime16.txt");
}

#[test]
fn dtime16_1() {
    test_times(Format::new(Type::DTime, 16, 1).unwrap(), "dtime16.1.txt");
}

#[test]
fn dtime16_2() {
    test_times(Format::new(Type::DTime, 16, 2).unwrap(), "dtime16.2.txt");
}

#[test]
fn dtime16_3() {
    test_times(Format::new(Type::DTime, 16, 3).unwrap(), "dtime16.3.txt");
}

#[test]
fn dtime16_4() {
    test_times(Format::new(Type::DTime, 16, 4).unwrap(), "dtime16.4.txt");
}

#[test]
fn dtime17() {
    test_times(Format::new(Type::DTime, 17, 0).unwrap(), "dtime17.txt");
}

#[test]
fn dtime17_1() {
    test_times(Format::new(Type::DTime, 17, 1).unwrap(), "dtime17.1.txt");
}

#[test]
fn dtime17_2() {
    test_times(Format::new(Type::DTime, 17, 2).unwrap(), "dtime17.2.txt");
}

#[test]
fn dtime17_3() {
    test_times(Format::new(Type::DTime, 17, 3).unwrap(), "dtime17.3.txt");
}

#[test]
fn dtime17_4() {
    test_times(Format::new(Type::DTime, 17, 4).unwrap(), "dtime17.4.txt");
}

#[test]
fn dtime17_5() {
    test_times(Format::new(Type::DTime, 17, 5).unwrap(), "dtime17.5.txt");
}

#[test]
fn dtime18() {
    test_times(Format::new(Type::DTime, 18, 0).unwrap(), "dtime18.txt");
}

#[test]
fn dtime18_1() {
    test_times(Format::new(Type::DTime, 18, 1).unwrap(), "dtime18.1.txt");
}

#[test]
fn dtime18_2() {
    test_times(Format::new(Type::DTime, 18, 2).unwrap(), "dtime18.2.txt");
}

#[test]
fn dtime18_3() {
    test_times(Format::new(Type::DTime, 18, 3).unwrap(), "dtime18.3.txt");
}

#[test]
fn dtime18_4() {
    test_times(Format::new(Type::DTime, 18, 4).unwrap(), "dtime18.4.txt");
}

#[test]
fn dtime18_5() {
    test_times(Format::new(Type::DTime, 18, 5).unwrap(), "dtime18.5.txt");
}

#[test]
fn dtime18_6() {
    test_times(Format::new(Type::DTime, 18, 6).unwrap(), "dtime18.6.txt");
}
