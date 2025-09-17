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

use anyhow::Result;
use clap::{Parser, Subcommand};
use encoding_rs::Encoding;
use thiserror::Error as ThisError;

use crate::{convert::Convert, decrypt::Decrypt, show::Show, show_por::ShowPor};

mod convert;
mod decrypt;
mod show;
mod show_por;

/// PSPP, a program for statistical analysis of sampled data.
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand, Clone, Debug)]
enum Command {
    Convert(Convert),
    Decrypt(Decrypt),
    Show(Show),
    ShowPor(ShowPor),
}

impl Command {
    fn run(self) -> Result<()> {
        match self {
            Command::Convert(convert) => convert.run(),
            Command::Decrypt(decrypt) => decrypt.run(),
            Command::Show(show) => show.run(),
            Command::ShowPor(show_por) => show_por.run(),
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
