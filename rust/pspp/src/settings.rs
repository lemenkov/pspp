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

use std::sync::{Arc, OnceLock};

use binrw::Endian;
use enum_map::EnumMap;
use serde::Serialize;

use crate::{
    format::{Format, Settings as FormatSettings},
    message::Severity,
    output::pivot::Look,
};

/// Whether to show variable or value labels or the underlying value or variable
/// name.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Show {
    /// Value (or variable name) only.
    Value,

    /// Label only.
    ///
    /// The value will be shown if no label is available.
    #[default]
    Label,

    /// Value (or variable name) and label.
    ///
    /// Just the value will be shown, if no label is available.
    Both,
}

impl Show {
    pub fn show_value(&self) -> bool {
        *self != Self::Label
    }

    pub fn show_label(&self) -> bool {
        *self != Self::Value
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
pub struct EndianSettings {
    /// Endianness for reading IB, PIB, and RB formats.
    pub input: Endian,

    /// Endianness for writing IB, PIB, and RB formats.
    pub output: Endian,
}

impl Default for EndianSettings {
    fn default() -> Self {
        Self {
            input: Endian::NATIVE,
            output: Endian::NATIVE,
        }
    }
}

impl EndianSettings {
    pub const fn new(endian: Endian) -> Self {
        Self {
            input: endian,
            output: endian,
        }
    }
}

pub struct Settings {
    pub look: Arc<Look>,

    /// `MDISPLAY`: how to display matrices in `MATRIX`...`END MATRIX`.
    pub matrix_display: MatrixDisplay,

    pub view_length: usize,
    pub view_width: usize,
    pub safer: bool,
    pub include: bool,
    pub route_errors_to_terminal: bool,
    pub route_errors_to_listing: bool,
    pub scompress: bool,
    pub undefined: bool,
    pub blanks: Option<f64>,
    pub max_messages: EnumMap<Severity, usize>,
    pub printback: bool,
    pub macros: MacroSettings,
    pub max_loops: usize,
    pub workspace: usize,
    pub default_format: Format,
    pub testing: bool,
    pub fuzz_bits: usize,
    pub scale_min: usize,
    pub commands: Compatibility,
    pub global: Compatibility,
    pub syntax: Compatibility,
    pub formats: FormatSettings,
    pub endian: EndianSettings,
    pub small: f64,
    pub show_values: Show,
    pub show_variables: Show,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            look: Arc::new(Look::default()),
            matrix_display: MatrixDisplay::default(),
            view_length: 24,
            view_width: 79,
            safer: false,
            include: true,
            route_errors_to_terminal: true,
            route_errors_to_listing: true,
            scompress: true,
            undefined: true,
            blanks: None,
            max_messages: EnumMap::from_fn(|_| 100),
            printback: true,
            macros: MacroSettings::default(),
            max_loops: 40,
            workspace: 64 * 1024 * 1024,
            default_format: Format::F8_2,
            testing: false,
            fuzz_bits: 6,
            scale_min: 24,
            commands: Compatibility::default(),
            global: Compatibility::default(),
            syntax: Compatibility::default(),
            formats: FormatSettings::default(),
            endian: EndianSettings::default(),
            small: 0.0001,
            show_values: Show::default(),
            show_variables: Show::default(),
        }
    }
}

impl Settings {
    pub fn global() -> &'static Settings {
        static GLOBAL: OnceLock<Settings> = OnceLock::new();
        GLOBAL.get_or_init(Settings::default)
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Default)]
pub enum Compatibility {
    /// Use improved PSPP behavior.
    #[default]
    Enhanced,

    /// Be as compatible as possible.
    Compatible,
}

pub struct MacroSettings {
    /// Expand macros?
    pub expand: bool,

    /// Print macro expansions?
    pub print_expansions: bool,

    /// Maximum iterations of `!FOR`.
    pub max_iterations: usize,

    /// Maximum nested macro expansion levels.
    pub max_nest: usize,
}

impl Default for MacroSettings {
    fn default() -> Self {
        Self {
            expand: true,
            print_expansions: false,
            max_iterations: 1000,
            max_nest: 50,
        }
    }
}

/// How to display matrices in `MATRIX`...`END MATRIX`.
#[derive(Default)]
pub enum MatrixDisplay {
    /// Output matrices as text.
    #[default]
    Text,

    /// Output matrices as pivot tables.
    Tables,
}

pub enum OutputType {
    /// Errors and warnings.
    Error,

    /// Notes.
    Notes,

    /// Syntax printback.
    Syntax,

    /// Everything else.
    Other,
}
