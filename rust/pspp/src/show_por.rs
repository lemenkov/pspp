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

use anyhow::{Result, anyhow};
use clap::{Args, ValueEnum};
use pspp::{
    data::cases_to_output,
    output::{
        Details, Item, Text,
        driver::{Config, Driver},
        pivot::PivotTable,
    },
    por::PortableFile,
};
use serde::Serialize;
use std::{
    cell::RefCell,
    ffi::OsStr,
    fmt::{Display, Write as _},
    fs::File,
    io::{BufReader, Write, stdout},
    path::{Path, PathBuf},
    rc::Rc,
    sync::Arc,
};

/// Show information about SPSS portable files.
#[derive(Args, Clone, Debug)]
pub struct ShowPor {
    /// What to show.
    #[arg(value_enum)]
    mode: Mode,

    /// File to show.
    #[arg(required = true)]
    input: PathBuf,

    /// Output file name.  If omitted, output is written to stdout.
    output: Option<PathBuf>,

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
    max_cases: usize,

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

impl ShowPor {
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

        let reader = BufReader::new(File::open(&self.input)?);
        match self.mode {
            Mode::Dictionary => {
                let PortableFile {
                    dictionary,
                    metadata: _,
                    cases,
                } = PortableFile::open(reader, |warning| output.warn(&warning))?;
                let cases = cases.take(self.max_cases);

                match &output {
                    Output::Driver { driver, mode: _ } => {
                        let mut output = Vec::new();
                        output.extend(
                            dictionary
                                .all_pivot_tables()
                                .into_iter()
                                .map(|pivot_table| Item::new(pivot_table)),
                        );
                        output.extend(cases_to_output(&dictionary, cases));
                        driver
                            .borrow_mut()
                            .write(&Arc::new(Item::new(Details::Group(
                                output.into_iter().map(Arc::new).collect(),
                            ))));
                    }
                    Output::Json { .. } => {
                        output.show_json(&dictionary)?;
                        for (_index, case) in (0..self.max_cases).zip(cases) {
                            output.show_json(&case?)?;
                        }
                    }
                    Output::Discard => (),
                }
            }
            Mode::Metadata => {
                let metadata =
                    PortableFile::open(reader, |warning| output.warn(&warning))?.metadata;

                match &output {
                    Output::Driver { driver, mode: _ } => {
                        driver
                            .borrow_mut()
                            .write(&Arc::new(Item::new(PivotTable::from(&metadata))));
                    }
                    Output::Json { .. } => {
                        output.show_json(&metadata)?;
                    }
                    Output::Discard => (),
                }
            }
            Mode::Histogram => {
                let (histogram, translations) = PortableFile::read_histogram(reader)?;
                let h = histogram
                    .into_iter()
                    .enumerate()
                    .filter_map(|(index, count)| {
                        if count > 0
                            && index != translations[index as u8] as usize
                            && translations[index as u8] != 0
                        {
                            Some((
                                format!("{index:02x}"),
                                translations[index as u8] as char,
                                count,
                            ))
                        } else {
                            None
                        }
                    })
                    .collect::<Vec<_>>();
                output.show_json(&h)?;
            }
        }
        Ok(())
    }
}

/// What to show in a system file.
#[derive(Clone, Copy, Debug, Default, PartialEq, ValueEnum)]
enum Mode {
    /// File dictionary, with variables, value labels, ...
    #[default]
    #[value(alias = "dict")]
    Dictionary,

    /// File metadata not included in the dictionary.
    Metadata,

    /// Histogram of character incidence in the file.
    Histogram,
}

impl Mode {
    fn as_str(&self) -> &'static str {
        match self {
            Mode::Dictionary => "dictionary",
            Mode::Metadata => "metadata",
            Mode::Histogram => "histogram",
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
