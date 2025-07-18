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

use std::{borrow::Cow, path::Path, sync::Arc};

use clap::ValueEnum;
use serde::{Deserialize, Serialize};

use crate::output::{
    cairo::{CairoConfig, CairoDriver},
    csv::{CsvConfig, CsvDriver},
    html::{HtmlConfig, HtmlDriver},
    json::{JsonConfig, JsonDriver},
    spv::{SpvConfig, SpvDriver},
    text::{TextConfig, TextDriver},
};

use super::{page::PageSetup, Item};

// An output driver.
pub trait Driver {
    fn name(&self) -> Cow<'static, str>;

    fn write(&mut self, item: &Arc<Item>);

    /// Returns false if the driver doesn't support page setup.
    fn setup(&mut self, page_setup: &PageSetup) -> bool {
        let _ = page_setup;
        false
    }

    /// Ensures that anything written with [Self::write] has been displayed.
    ///
    /// This is called from the text-based UI before showing the command prompt,
    /// to ensure that the user has actually been shown any preceding output If
    /// it doesn't make sense for this driver to be used this way, then this
    /// function need not do anything.
    fn flush(&mut self) {}

    /// Ordinarily, the core driver code will skip passing hidden output items
    /// to [Self::write].  If this returns true, the core driver hands them to
    /// the driver to let it handle them itself.
    fn handles_show(&self) -> bool {
        false
    }

    /// Ordinarily, the core driver code will flatten groups of output items
    /// before passing them to [Self::write].  If this returns true, the core
    /// driver code leaves them in place for the driver to handle.
    fn handles_groups(&self) -> bool {
        false
    }
}

impl Driver for Box<dyn Driver> {
    fn name(&self) -> Cow<'static, str> {
        (&**self).name()
    }

    fn write(&mut self, item: &Arc<Item>) {
        (&mut **self).write(item);
    }

    fn setup(&mut self, page_setup: &PageSetup) -> bool {
        (&mut **self).setup(page_setup)
    }

    fn flush(&mut self) {
        (&mut **self).flush();
    }

    fn handles_show(&self) -> bool {
        (&**self).handles_show()
    }

    fn handles_groups(&self) -> bool {
        (&**self).handles_groups()
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "driver", rename_all = "snake_case")]
pub enum Config {
    Text(TextConfig),
    Pdf(CairoConfig),
    Html(HtmlConfig),
    Json(JsonConfig),
    Csv(CsvConfig),
    Spv(SpvConfig),
}

#[derive(Copy, Clone, Debug, Serialize, Deserialize, PartialEq, ValueEnum)]
#[serde(rename_all = "snake_case")]
pub enum DriverType {
    Text,
    Pdf,
    Html,
    Csv,
    Json,
    Spv,
}

impl dyn Driver {
    pub fn new(config: &Config) -> anyhow::Result<Box<Self>> {
        match config {
            Config::Text(text_config) => Ok(Box::new(TextDriver::new(text_config)?)),
            Config::Pdf(cairo_config) => Ok(Box::new(CairoDriver::new(cairo_config)?)),
            Config::Html(html_config) => Ok(Box::new(HtmlDriver::new(html_config)?)),
            Config::Csv(csv_config) => Ok(Box::new(CsvDriver::new(csv_config)?)),
            Config::Json(json_config) => Ok(Box::new(JsonDriver::new(json_config)?)),
            Config::Spv(spv_config) => Ok(Box::new(SpvDriver::new(spv_config)?)),
        }
    }

    pub fn driver_type_from_filename(file: impl AsRef<Path>) -> Option<&'static str> {
        match file.as_ref().extension()?.to_str()? {
            "txt" | "text" => Some("text"),
            "pdf" => Some("pdf"),
            "htm" | "html" => Some("html"),
            "csv" => Some("csv"),
            "json" => Some("json"),
            "spv" => Some("spv"),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use serde::Serialize;

    use crate::output::driver::Config;

    #[test]
    fn toml() {
        let config = r#"driver = "text"
file = "filename.text"
"#;
        let toml: Config = toml::from_str(config).unwrap();
        println!("{}", toml::to_string_pretty(&toml).unwrap());

        #[derive(Serialize)]
        struct Map<'a> {
            file: &'a str,
        }
        println!(
            "{}",
            toml::to_string_pretty(&Map { file: "filename" }).unwrap()
        );
    }
}
