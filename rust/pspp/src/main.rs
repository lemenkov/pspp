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

use anyhow::{anyhow, Result};
use clap::{Args, Parser, Subcommand, ValueEnum};
use encoding_rs::Encoding;
use pspp::crypto::EncryptedFile;
use pspp::sys::cooked::{Error, Headers};
use pspp::sys::raw::{encoding_from_headers, Decoder, Magic, Reader, Record, Warning};
use std::fs::File;
use std::io::{stdout, BufReader, Write};
use std::path::{Path, PathBuf};
use std::str;
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

    /// The encoding to use.
    #[arg(short = 'e', long, value_parser = parse_encoding)]
    encoding: Option<&'static Encoding>,

    /// Maximum number of cases to print.
    #[arg(short = 'c', long = "cases")]
    max_cases: Option<u64>,

    #[command(flatten, next_help_heading = "Options for CSV output")]
    csv_options: CsvOptions,
}

#[derive(Args, Clone, Debug)]
struct CsvOptions {
    /// Omit writing variable names as the first line of output.
    #[arg(long)]
    no_var_names: bool,
}

impl Convert {
    fn warn(warning: Warning) {
        eprintln!("warning: {warning}");
    }

    fn err(error: Error) {
        eprintln!("error: {error}");
    }

    fn run(self) -> Result<()> {
        let mut reader = Reader::new(BufReader::new(File::open(&self.input)?), Self::warn)?;
        let headers = reader.headers().collect::<Result<Vec<_>, _>>()?;
        let encoding = encoding_from_headers(&headers, &mut |w| Self::warn(w))?;
        let mut decoder = Decoder::new(encoding, |w| Self::warn(w));
        let mut decoded_records = Vec::new();
        for header in headers {
            decoded_records.push(header.decode(&mut decoder)?);
        }
        let headers = Headers::new(decoded_records, &mut |e| Self::err(e))?;
        let (dictionary, _metadata, cases) =
            headers.decode(reader.cases(), encoding, |e| Self::err(e))?;
        let writer = match self.output {
            Some(path) => Box::new(File::create(path)?) as Box<dyn Write>,
            None => Box::new(stdout()),
        };
        let mut output = csv::WriterBuilder::new().from_writer(writer);
        if !self.csv_options.no_var_names {
            output.write_record(dictionary.variables.iter().map(|var| var.name.as_str()))?;
        }

        for (_case_number, case) in (0..self.max_cases.unwrap_or(u64::MAX)).zip(cases) {
            output.write_record(case?.into_iter().zip(dictionary.variables.iter()).map(
                |(datum, variable)| {
                    datum
                        .display(variable.print_format, variable.encoding)
                        .to_string()
                },
            ))?;
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

/// Dissects SPSS system files.
#[derive(Args, Clone, Debug)]
struct Dissect {
    /// Maximum number of cases to print.
    #[arg(long = "data", default_value_t = 0)]
    max_cases: u64,

    /// Files to dissect.
    #[arg(required = true)]
    files: Vec<PathBuf>,

    /// How to dissect the file.
    #[arg(short, long, value_enum, default_value_t)]
    mode: Mode,

    /// The encoding to use.
    #[arg(long, value_parser = parse_encoding)]
    encoding: Option<&'static Encoding>,
}

impl Dissect {
    fn run(self) -> Result<()> {
        for file in self.files {
            dissect(&file, self.max_cases, self.mode, self.encoding)?;
        }
        Ok(())
    }
}

#[derive(Subcommand, Clone, Debug)]
enum Command {
    Convert(Convert),
    Decrypt(Decrypt),
    Dissect(Dissect),
}

impl Command {
    fn run(self) -> Result<()> {
        match self {
            Command::Convert(convert) => convert.run(),
            Command::Decrypt(decrypt) => decrypt.run(),
            Command::Dissect(dissect) => dissect.run(),
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

#[derive(Clone, Copy, Debug, Default, ValueEnum)]
enum Mode {
    Identify,
    Raw,
    Decoded,
    #[default]
    Cooked,
}

fn main() -> Result<()> {
    Cli::parse().command.run()
}

fn dissect(
    file_name: &Path,
    max_cases: u64,
    mode: Mode,
    encoding: Option<&'static Encoding>,
) -> Result<()> {
    let reader = File::open(file_name)?;
    let reader = BufReader::new(reader);
    let mut reader = Reader::new(reader, Box::new(|warning| println!("{warning}")))?;

    match mode {
        Mode::Identify => {
            let Record::Header(header) = reader.headers().next().unwrap()? else {
                unreachable!()
            };
            match header.magic {
                Magic::Sav => println!("SPSS System File"),
                Magic::Zsav => println!("SPSS System File with Zlib compression"),
                Magic::Ebcdic => println!("EBCDIC-encoded SPSS System File"),
            }
            return Ok(());
        }
        Mode::Raw => {
            for header in reader.headers() {
                let header = header?;
                println!("{:?}", header);
            }
            for (_index, case) in (0..max_cases).zip(reader.cases()) {
                println!("{:?}", case?);
            }
        }
        Mode::Decoded => {
            let headers: Vec<Record> = reader.headers().collect::<Result<Vec<_>, _>>()?;
            let encoding = match encoding {
                Some(encoding) => encoding,
                None => encoding_from_headers(&headers, &mut |e| eprintln!("{e}"))?,
            };
            let mut decoder = Decoder::new(encoding, |e| eprintln!("{e}"));
            for header in headers {
                let header = header.decode(&mut decoder);
                println!("{:?}", header);
                /*
                                if let Record::Cases(cases) = header {
                                    let mut cases = cases.borrow_mut();
                                    for _ in 0..max_cases {
                                        let Some(Ok(record)) = cases.next() else {
                                            break;
                                        };
                                        println!("{:?}", record);
                                    }
                                }
                */
            }
        }
        Mode::Cooked => {
            let headers: Vec<Record> = reader.headers().collect::<Result<Vec<_>, _>>()?;
            let encoding = match encoding {
                Some(encoding) => encoding,
                None => encoding_from_headers(&headers, &mut |e| eprintln!("{e}"))?,
            };
            let mut decoder = Decoder::new(encoding, |e| eprintln!("{e}"));
            let mut decoded_records = Vec::new();
            for header in headers {
                decoded_records.push(header.decode(&mut decoder)?);
            }
            let headers = Headers::new(decoded_records, &mut |e| eprintln!("{e}"))?;
            let (dictionary, metadata, _cases) =
                headers.decode(reader.cases(), encoding, |e| eprintln!("{e}"))?;
            println!("{dictionary:#?}");
            println!("{metadata:#?}");
        }
    }

    Ok(())
}
