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

use anyhow::{Result, anyhow};
use clap::Args;
use pspp::crypto::EncryptedFile;
use std::{fs::File, path::PathBuf};
use zeroize::Zeroizing;

/// Decrypts an encrypted SPSS data, output, or syntax file.
#[derive(Args, Clone, Debug)]
pub struct Decrypt {
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
    pub fn run(self) -> Result<()> {
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
