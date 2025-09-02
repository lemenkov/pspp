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

use crate::parse_encoding;
use anyhow::{anyhow, Result};
use clap::{Args, ValueEnum};
use encoding_rs::Encoding;
use pspp::{
    output::{
        driver::{Config, Driver},
        pivot::PivotTable,
        Details, Item, Text,
    },
    sys::{
        raw::{infer_encoding, Decoder, EncodingReport, Magic, Reader, Record},
        Records,
    },
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

/// Show information about SPSS system files.
#[derive(Args, Clone, Debug)]
pub struct Show {
    /// What to show.
    #[arg(value_enum)]
    mode: Mode,

    /// File to show.
    #[arg(required = true)]
    input: PathBuf,

    /// Output file name.  If omitted, output is written to stdout.
    output: Option<PathBuf>,

    /// The encoding to use.
    #[arg(long, value_parser = parse_encoding, help_heading = "Input file options")]
    encoding: Option<&'static Encoding>,

    /// Maximum number of cases to read.
    ///
    /// If specified without an argument, all cases will be read.
    #[arg(
        long = "data",
        num_args = 0..=1,
        default_missing_value = "18446744073709551615",
        default_value_t = 0,
        help_heading = "Input file options"
    )]
    max_cases: u64,

    /// Output driver configuration options.
    #[arg(short = 'o', help_heading = "Output options")]
    output_options: Vec<String>,

    /// Output format.
    #[arg(long, short = 'f', help_heading = "Output options")]
    format: Option<ShowFormat>,
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
    pub fn run(self) -> Result<()> {
        let format = if let Some(format) = self.format {
            format
        } else if let Some(output_file) = &self.output {
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

                if let Some(file) = &self.output {
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
                    let driver = if let Some(file) = &self.output {
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
                writer: if let Some(output_file) = &self.output {
                    Rc::new(RefCell::new(Box::new(File::create(output_file)?)))
                } else {
                    Rc::new(RefCell::new(Box::new(stdout())))
                },
            },
            ShowFormat::Discard => Output::Discard,
        };

        let reader = File::open(&self.input)?;
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

/// What to show in a system file.
#[derive(Clone, Copy, Debug, Default, PartialEq, ValueEnum)]
enum Mode {
    /// The kind of file.
    Identity,

    /// File dictionary, with variables, value labels, attributes, ...
    #[default]
    #[value(alias = "dict")]
    Dictionary,

    /// Possible encodings of text in file dictionary and (with `--data`) cases.
    Encodings,

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
