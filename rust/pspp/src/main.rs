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

use anyhow::{anyhow, bail, Error as AnyError, Result};
use chrono::{Datelike, NaiveTime, Timelike};
use clap::{Args, Parser, Subcommand, ValueEnum};
use csv::Writer;
use encoding_rs::Encoding;
use pspp::{
    calendar::calendar_offset_to_gregorian,
    crypto::EncryptedFile,
    data::{ByteString, Datum, WithEncoding},
    format::{DisplayPlain, Type},
    output::{
        driver::{Config, Driver},
        pivot::PivotTable,
        Details, Item, Text,
    },
    sys::{
        self,
        raw::{
            infer_encoding, records::Compression, Decoder, EncodingReport, Magic, Reader, Record,
        },
        ReadOptions, Records,
    },
    util::ToSmallString,
    variable::Variable,
};
use serde::Serialize;
use std::{
    cell::RefCell,
    ffi::OsStr,
    fmt::{Display, Write as _},
    fs::File,
    io::{stdout, BufReader, Write},
    path::{Path, PathBuf},
    rc::Rc,
    sync::Arc,
};
use thiserror::Error as ThisError;
use zeroize::Zeroizing;

/// PSPP, a program for statistical analysis of sampled data.
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

/// Output file format.
#[derive(Copy, Clone, Debug, PartialEq, Eq, ValueEnum)]
enum OutputFormat {
    /// Comma-separated values using each variable's print format (variable
    /// names are written as the first line)
    Csv,

    /// System file
    Sys,
}

impl TryFrom<&Path> for OutputFormat {
    type Error = AnyError;

    fn try_from(value: &Path) -> std::result::Result<Self, Self::Error> {
        let extension = value.extension().unwrap_or_default();
        if extension.eq_ignore_ascii_case("csv") || extension.eq_ignore_ascii_case("txt") {
            Ok(OutputFormat::Csv)
        } else if extension.eq_ignore_ascii_case("sav") || extension.eq_ignore_ascii_case("sys") {
            Ok(OutputFormat::Sys)
        } else {
            Err(anyhow!(
                "Unknown output file extension '{}'",
                extension.display()
            ))
        }
    }
}

/// Convert SPSS data files into other formats.
#[derive(Args, Clone, Debug)]
struct Convert {
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
    fn run(self) -> Result<()> {
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
                let mut output = sys::WriteOptions::new()
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

/// Decrypts an encrypted SPSS data, output, or syntax file.
#[derive(Args, Clone, Debug)]
struct Decrypt {
    /// Input file name.
    input: PathBuf,

    /// Output file name.
    output: PathBuf,

    /// Password for decryption, with or without what SPSS calls "password encryption".
    ///
    /// If omitted, PSPP will prompt interactively for the password.
    #[clap(short, long)]
    password: Option<String>,
}

impl Decrypt {
    fn run(self) -> Result<()> {
        let input = EncryptedFile::new(File::open(&self.input)?)?;
        let password = match self.password {
            Some(password) => Zeroizing::new(password),
            None => {
                eprintln!("Please enter the password for {}:", self.input.display());
                readpass::from_tty().unwrap()
            }
        };
        let mut reader = match input.unlock(password.as_bytes()) {
            Ok(reader) => reader,
            Err(_) => return Err(anyhow!("Incorrect password.")),
        };
        let mut writer = File::create(self.output)?;
        std::io::copy(&mut reader, &mut writer)?;
        Ok(())
    }
}

/// Show information about SPSS system files.
#[derive(Args, Clone, Debug)]
struct Show {
    /// What to show.
    #[arg(value_enum)]
    mode: Mode,

    /// File to show.
    #[arg(required = true)]
    input_file: PathBuf,

    /// Output file name.  If omitted, output is written to stdout.
    output_file: Option<PathBuf>,

    /// Output driver configuration options.
    #[arg(short = 'o')]
    output_options: Vec<String>,

    /// Maximum number of cases to read.
    ///
    /// If specified without an argument, all cases will be read.
    #[arg(
        long = "data",
        num_args = 0..=1,
        default_missing_value = "18446744073709551615",
        default_value_t = 0
    )]
    max_cases: u64,

    /// Output format.
    #[arg(long, short = 'f')]
    format: Option<ShowFormat>,

    /// The encoding to use.
    #[arg(long, value_parser = parse_encoding)]
    encoding: Option<&'static Encoding>,
}

enum Output {
    Driver {
        driver: Rc<RefCell<Box<dyn Driver>>>,
        mode: Mode,
    },
    Json {
        writer: Rc<RefCell<Box<dyn Write>>>,
        pretty: bool,
    },
    Discard,
}

impl Output {
    /*
    fn show_metadata(&self, metadata: MetadataEntry) -> Result<()> {
        match self {
            Self::Driver { driver, .. } => {
                driver
                    .borrow_mut()
                    .write(&Arc::new(Item::new(metadata.into_pivot_table())));
                Ok(())
            }
            Self::Json { .. } => self.show_json(&metadata),
            Self::Discard => Ok(()),
        }
    }*/

    fn show<T>(&self, value: &T) -> Result<()>
    where
        T: Serialize,
        for<'a> &'a T: Into<Details>,
    {
        match self {
            Self::Driver { driver, .. } => {
                driver
                    .borrow_mut()
                    .write(&Arc::new(Item::new(value.into())));
                Ok(())
            }
            Self::Json { .. } => self.show_json(value),
            Self::Discard => Ok(()),
        }
    }

    fn show_json<T>(&self, value: &T) -> Result<()>
    where
        T: Serialize,
    {
        match self {
            Self::Driver { mode, driver: _ } => {
                Err(anyhow!("Mode '{mode}' only supports output as JSON."))
            }
            Self::Json { writer, pretty } => {
                let mut writer = writer.borrow_mut();
                match pretty {
                    true => serde_json::to_writer_pretty(&mut *writer, value)?,
                    false => serde_json::to_writer(&mut *writer, value)?,
                };
                writeln!(writer)?;
                Ok(())
            }
            Self::Discard => Ok(()),
        }
    }

    fn warn(&self, warning: &impl Display) {
        match self {
            Output::Driver { driver, .. } => {
                driver
                    .borrow_mut()
                    .write(&Arc::new(Item::from(Text::new_log(warning.to_string()))));
            }
            Output::Json { .. } => {
                #[derive(Serialize)]
                struct Warning {
                    warning: String,
                }
                let warning = Warning {
                    warning: warning.to_string(),
                };
                let _ = self.show_json(&warning);
            }
            Self::Discard => (),
        }
    }
}

impl Show {
    fn run(self) -> Result<()> {
        let format = if let Some(format) = self.format {
            format
        } else if let Some(output_file) = &self.output_file {
            match output_file
                .extension()
                .unwrap_or(OsStr::new(""))
                .to_str()
                .unwrap_or("")
            {
                "json" => ShowFormat::Json,
                "ndjson" => ShowFormat::Ndjson,
                _ => ShowFormat::Output,
            }
        } else {
            ShowFormat::Json
        };

        let output = match format {
            ShowFormat::Output => {
                let mut config = String::new();

                if let Some(file) = &self.output_file {
                    #[derive(Serialize)]
                    struct File<'a> {
                        file: &'a Path,
                    }
                    let file = File {
                        file: file.as_path(),
                    };
                    let toml_file = toml::to_string_pretty(&file).unwrap();
                    config.push_str(&toml_file);
                }
                for option in &self.output_options {
                    writeln!(&mut config, "{option}").unwrap();
                }

                let table: toml::Table = toml::from_str(&config)?;
                if !table.contains_key("driver") {
                    let driver = if let Some(file) = &self.output_file {
                        <dyn Driver>::driver_type_from_filename(file).ok_or_else(|| {
                            anyhow!("{}: no default output format for file name", file.display())
                        })?
                    } else {
                        "text"
                    };

                    #[derive(Serialize)]
                    struct DriverConfig {
                        driver: &'static str,
                    }
                    config.insert_str(
                        0,
                        &toml::to_string_pretty(&DriverConfig { driver }).unwrap(),
                    );
                }

                let config: Config = toml::from_str(&config)?;
                Output::Driver {
                    mode: self.mode,
                    driver: Rc::new(RefCell::new(Box::new(<dyn Driver>::new(&config)?))),
                }
            }
            ShowFormat::Json | ShowFormat::Ndjson => Output::Json {
                pretty: format == ShowFormat::Json,
                writer: if let Some(output_file) = &self.output_file {
                    Rc::new(RefCell::new(Box::new(File::create(output_file)?)))
                } else {
                    Rc::new(RefCell::new(Box::new(stdout())))
                },
            },
            ShowFormat::Discard => Output::Discard,
        };

        let reader = File::open(&self.input_file)?;
        let reader = BufReader::new(reader);
        let mut reader = Reader::new(reader, Box::new(|warning| output.warn(&warning)))?;

        match self.mode {
            Mode::Identity => {
                match reader.header().magic {
                    Magic::Sav => println!("SPSS System File"),
                    Magic::Zsav => println!("SPSS System File with Zlib compression"),
                    Magic::Ebcdic => println!("EBCDIC-encoded SPSS System File"),
                }
                return Ok(());
            }
            Mode::Raw => {
                output.show_json(reader.header())?;
                for record in reader.records() {
                    output.show_json(&record?)?;
                }
                for (_index, case) in (0..self.max_cases).zip(reader.cases()) {
                    output.show_json(&case?)?;
                }
            }
            Mode::Decoded => {
                let records: Vec<Record> = reader.records().collect::<Result<Vec<_>, _>>()?;
                let encoding = match self.encoding {
                    Some(encoding) => encoding,
                    None => infer_encoding(&records, &mut |e| output.warn(&e))?,
                };
                let mut decoder = Decoder::new(encoding, |e| output.warn(&e));
                for record in records {
                    output.show_json(&record.decode(&mut decoder))?;
                }
            }
            Mode::Dictionary => {
                let records: Vec<Record> = reader.records().collect::<Result<Vec<_>, _>>()?;
                let encoding = match self.encoding {
                    Some(encoding) => encoding,
                    None => infer_encoding(&records, &mut |e| output.warn(&e))?,
                };
                let mut decoder = Decoder::new(encoding, |e| output.warn(&e));
                let records = Records::from_raw(records, &mut decoder);
                let (dictionary, metadata, cases) = records
                    .decode(
                        reader.header().clone().decode(&mut decoder),
                        reader.cases(),
                        encoding,
                        |e| output.warn(&e),
                    )
                    .into_parts();
                match &output {
                    Output::Driver { driver, mode: _ } => {
                        driver
                            .borrow_mut()
                            .write(&Arc::new(Item::new(PivotTable::from(&metadata))));
                        driver
                            .borrow_mut()
                            .write(&Arc::new(Item::new(Details::Group(
                                dictionary
                                    .all_pivot_tables()
                                    .into_iter()
                                    .map(|pivot_table| Arc::new(Item::new(pivot_table)))
                                    .collect(),
                            ))));
                    }
                    Output::Json { .. } => {
                        output.show_json(&dictionary)?;
                        output.show_json(&metadata)?;
                        for (_index, case) in (0..self.max_cases).zip(cases) {
                            output.show_json(&case?)?;
                        }
                    }
                    Output::Discard => (),
                }
            }
            Mode::Encodings => {
                let encoding_report = EncodingReport::new(reader, self.max_cases)?;
                output.show(&encoding_report)?;
            }
        }

        Ok(())
    }
}

#[derive(Subcommand, Clone, Debug)]
enum Command {
    Convert(Convert),
    Decrypt(Decrypt),
    Show(Show),
}

impl Command {
    fn run(self) -> Result<()> {
        match self {
            Command::Convert(convert) => convert.run(),
            Command::Decrypt(decrypt) => decrypt.run(),
            Command::Show(show) => show.run(),
        }
    }
}

#[derive(ThisError, Debug)]
#[error("{0}: unknown encoding")]
struct UnknownEncodingError(String);

fn parse_encoding(arg: &str) -> Result<&'static Encoding, UnknownEncodingError> {
    match Encoding::for_label_no_replacement(arg.as_bytes()) {
        Some(encoding) => Ok(encoding),
        None => Err(UnknownEncodingError(arg.to_string())),
    }
}

/// What to show in a system file.
#[derive(Clone, Copy, Debug, Default, PartialEq, ValueEnum)]
enum Mode {
    /// The file dictionary, including variables, value labels, attributes, and so on.
    #[default]
    #[value(alias = "dict")]
    Dictionary,

    /// Possible encodings of text in the file dictionary and (with `--data`) cases.
    Encodings,

    /// The kind of file.
    Identity,

    /// Raw file records, without assuming a particular character encoding.
    Raw,

    /// Raw file records decoded with a particular character encoding.
    Decoded,
}

impl Mode {
    fn as_str(&self) -> &'static str {
        match self {
            Mode::Dictionary => "dictionary",
            Mode::Identity => "identity",
            Mode::Raw => "raw",
            Mode::Decoded => "decoded",
            Mode::Encodings => "encodings",
        }
    }
}

impl Display for Mode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Serialize, ValueEnum)]
#[serde(rename_all = "snake_case")]
enum ShowFormat {
    /// Pretty-printed JSON.
    #[default]
    Json,
    /// Newline-delimited JSON.
    Ndjson,
    /// Pivot tables.
    Output,
    /// No output.
    Discard,
}

fn main() -> Result<()> {
    Cli::parse().command.run()
}
