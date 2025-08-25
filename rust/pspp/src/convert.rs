/* PSPP - a program for statistical analysis.
 * Copyright (C) 2023 Free Software Foundation, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>. */

use std::{
    fs::File,
    io::{stdout, Write},
    path::PathBuf,
};

use anyhow::{bail, Result};
use chrono::{Datelike, NaiveTime, Timelike};
use clap::Args;
use csv::Writer;
use encoding_rs::Encoding;
use pspp::{
    calendar::calendar_offset_to_gregorian,
    data::{ByteString, Datum, WithEncoding},
    format::{DisplayPlain, Type},
    sys::{raw::records::Compression, ReadOptions, WriteOptions},
    util::ToSmallString,
    variable::Variable,
};

use crate::{parse_encoding, OutputFormat};

/// Convert SPSS data files into other formats.
#[derive(Args, Clone, Debug)]
pub struct Convert {
    /// Input file name.
    input: PathBuf,

    /// Output file name (if omitted, output is written to stdout).
    output: Option<PathBuf>,

    /// Format for output file (if omitted, the intended format is inferred
    /// based on file extension).
    #[arg(short = 'O')]
    output_format: Option<OutputFormat>,

    /// The encoding to use for reading the input file.
    #[arg(short = 'e', long, value_parser = parse_encoding)]
    encoding: Option<&'static Encoding>,

    /// Password for decryption, with or without what SPSS calls "password encryption".
    ///
    /// Specify only for an encrypted system file.
    #[clap(short, long)]
    password: Option<String>,

    /// Maximum number of cases to print.
    #[arg(short = 'c', long = "cases")]
    max_cases: Option<usize>,

    #[command(flatten, next_help_heading = "Options for CSV output")]
    csv_options: CsvOptions,

    #[command(flatten, next_help_heading = "Options for system file output")]
    sys_options: SysOptions,
}

#[derive(Args, Clone, Debug)]
struct CsvOptions {
    /// Omit writing variable names as the first line of output.
    #[arg(long)]
    no_var_names: bool,

    /// Writes user-missing values like system-missing values.  Otherwise,
    /// user-missing values are written the same way as non-missing values.
    #[arg(long)]
    recode: bool,

    /// Write value labels instead of values.
    #[arg(long)]
    labels: bool,

    /// Use print formats for numeric variables.
    #[arg(long)]
    print_formats: bool,

    /// Decimal point.
    #[arg(long, default_value_t = '.')]
    decimal: char,

    /// Delimiter.
    ///
    /// The default is `,` unless that would be the same as the decimal point,
    /// in which case `;` is the default.
    #[arg(long)]
    delimiter: Option<char>,

    /// Character used to quote the delimiter.
    #[arg(long, default_value_t = '"')]
    qualifier: char,
}

impl CsvOptions {
    fn write_field<W>(
        &self,
        datum: &Datum<WithEncoding<ByteString>>,
        variable: &Variable,
        writer: &mut Writer<W>,
    ) -> csv::Result<()>
    where
        W: Write,
    {
        if self.labels
            && let Some(label) = variable.value_labels.get(&datum)
        {
            writer.write_field(label)
        } else if datum.is_sysmis() {
            writer.write_field(" ")
        } else if self.print_formats || datum.is_string() {
            writer.write_field(
                &datum
                    .display(variable.print_format)
                    .with_trimming()
                    .to_small_string::<64>(),
            )
        } else {
            let number = datum.as_number().unwrap().unwrap();
            match variable.print_format.type_() {
                Type::F
                | Type::Comma
                | Type::Dot
                | Type::Dollar
                | Type::Pct
                | Type::E
                | Type::CC(_)
                | Type::N
                | Type::Z
                | Type::P
                | Type::PK
                | Type::IB
                | Type::PIB
                | Type::PIBHex
                | Type::RB
                | Type::RBHex
                | Type::WkDay
                | Type::Month => writer.write_field(
                    &number
                        .display_plain()
                        .with_decimal(self.decimal)
                        .to_small_string::<64>(),
                ),

                Type::Date
                | Type::ADate
                | Type::EDate
                | Type::JDate
                | Type::SDate
                | Type::QYr
                | Type::MoYr
                | Type::WkYr => {
                    if number >= 0.0
                        && let Some(date) =
                            calendar_offset_to_gregorian(number / 60.0 / 60.0 / 24.0)
                    {
                        writer.write_field(
                            &format_args!(
                                "{:02}/{:02}/{:04}",
                                date.month(),
                                date.day(),
                                date.year()
                            )
                            .to_small_string::<64>(),
                        )
                    } else {
                        writer.write_field(" ")
                    }
                }

                Type::DateTime | Type::YmdHms => {
                    if number >= 0.0
                        && let Some(date) =
                            calendar_offset_to_gregorian(number / 60.0 / 60.0 / 24.0)
                        && let Some(time) = NaiveTime::from_num_seconds_from_midnight_opt(
                            (number % (60.0 * 60.0 * 24.0)) as u32,
                            0,
                        )
                    {
                        writer.write_field(
                            &format_args!(
                                "{:02}/{:02}/{:04} {:02}:{:02}:{:02}",
                                date.month(),
                                date.day(),
                                date.year(),
                                time.hour(),
                                time.minute(),
                                time.second()
                            )
                            .to_small_string::<64>(),
                        )
                    } else {
                        writer.write_field(" ")
                    }
                }

                Type::MTime | Type::Time | Type::DTime => {
                    if let Some(time) =
                        NaiveTime::from_num_seconds_from_midnight_opt(number.abs() as u32, 0)
                    {
                        writer.write_field(
                            format_args!(
                                "{}{:02}:{:02}:{:02}",
                                if number.is_sign_negative() { "-" } else { "" },
                                time.hour(),
                                time.minute(),
                                time.second()
                            )
                            .to_small_string::<64>(),
                        )
                    } else {
                        writer.write_field(" ")
                    }
                }

                Type::A | Type::AHex => unreachable!(),
            }
        }
    }
}

#[derive(Args, Clone, Debug)]
struct SysOptions {
    /// Write the output file with Unicode (UTF-8) encoding.
    ///
    /// If the input was not already encoded in Unicode, this triples the width
    /// of string variables.
    #[arg(long = "unicode")]
    to_unicode: bool,

    /// How to compress data in the system file.
    #[arg(long, default_value = "simple")]
    compression: Option<Compression>,
}

impl Convert {
    pub fn run(self) -> Result<()> {
        fn warn(warning: anyhow::Error) {
            eprintln!("warning: {warning}");
        }

        let output_format = match self.output_format {
            Some(format) => format,
            None => match &self.output {
                Some(output) => output.as_path().try_into()?,
                _ => OutputFormat::Csv,
            },
        };

        let mut system_file = ReadOptions::new(warn)
            .with_encoding(self.encoding)
            .with_password(self.password.clone())
            .open_file(&self.input)?;
        if output_format == OutputFormat::Sys && self.sys_options.to_unicode {
            system_file = system_file.into_unicode();
        }
        let (dictionary, _, cases) = system_file.into_parts();

        // Take only the first `self.max_cases` cases.
        let cases = cases.take(self.max_cases.unwrap_or(usize::MAX));

        match output_format {
            OutputFormat::Csv => {
                let writer = match self.output {
                    Some(path) => Box::new(File::create(path)?) as Box<dyn Write>,
                    None => Box::new(stdout()),
                };
                let decimal: u8 = self.csv_options.decimal.try_into()?;
                let delimiter: u8 = match self.csv_options.delimiter {
                    Some(delimiter) => delimiter.try_into()?,
                    None if decimal != b',' => b',',
                    None => b';',
                };
                let qualifier: u8 = self.csv_options.qualifier.try_into()?;
                let mut output = csv::WriterBuilder::new()
                    .delimiter(delimiter)
                    .quote(qualifier)
                    .from_writer(writer);
                if !self.csv_options.no_var_names {
                    output
                        .write_record(dictionary.variables.iter().map(|var| var.name.as_str()))?;
                }

                for case in cases {
                    for (datum, variable) in case?.into_iter().zip(dictionary.variables.iter()) {
                        self.csv_options
                            .write_field(&datum, variable, &mut output)?;
                    }
                    output.write_record(None::<&[u8]>)?;
                }
            }
            OutputFormat::Sys => {
                let Some(output) = &self.output else {
                    bail!("output file name must be specified for output to a system file")
                };
                let mut output = WriteOptions::new()
                    .with_compression(self.sys_options.compression)
                    .write_file(&dictionary, output)?;
                for case in cases {
                    output.write_case(case?)?;
                }
            }
        }
        Ok(())
    }
}
