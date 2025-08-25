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

use anyhow::{anyhow, Error as AnyError, Result};
use clap::{Parser, Subcommand, ValueEnum};
use encoding_rs::Encoding;
use std::path::Path;
use thiserror::Error as ThisError;

use crate::{convert::Convert, decrypt::Decrypt, show::Show};

mod convert;
mod decrypt;
mod show;

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

fn main() -> Result<()> {
    Cli::parse().command.run()
}
