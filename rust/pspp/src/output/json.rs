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

use std::{
    borrow::Cow,
    fs::File,
    io::{BufWriter, Write},
    path::PathBuf,
    sync::Arc,
};

use serde::{Deserialize, Serialize};

use super::{driver::Driver, Item};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct JsonConfig {
    file: PathBuf,
}

pub struct JsonDriver {
    file: BufWriter<File>,
}

impl JsonDriver {
    pub fn new(config: &JsonConfig) -> std::io::Result<Self> {
        Ok(Self {
            file: BufWriter::new(File::create(&config.file)?),
        })
    }
}

impl Driver for JsonDriver {
    fn name(&self) -> Cow<'static, str> {
        Cow::from("json")
    }

    fn write(&mut self, item: &Arc<Item>) {
        serde_json::to_writer_pretty(&mut self.file, item).unwrap(); // XXX handle errors
    }

    fn flush(&mut self) {
        let _ = self.file.flush();
    }
}
