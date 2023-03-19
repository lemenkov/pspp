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

use std::path::PathBuf;

use enum_map::{enum_map, EnumMap};

use super::pivot::{Axis2, HorzAlign};

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum Orientation {
    #[default]
    Portrait,
    Landscape,
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum ChartSize {
    #[default]
    AsIs,
    FullHeight,
    HalfHeight,
    QuarterHeight,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Paragraph {
    pub markup: String,
    pub horz_align: HorzAlign,
}

impl Default for Paragraph {
    fn default() -> Self {
        Self {
            markup: Default::default(),
            horz_align: HorzAlign::Left,
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Heading(pub Vec<Paragraph>);

pub struct Setup {
    pub initial_page_number: i32,

    /// Paper size in inches.
    pub paper: EnumMap<Axis2, f64>,

    /// Margin width in inches.
    pub margins: EnumMap<Axis2, [f64; 2]>,

    pub orientation: Orientation,

    /// Space between objects, in inches.
    pub object_spacing: f64,

    pub chart_size: ChartSize,

    /// Header and footer.
    pub headings: [Heading; 2],

    file_name: Option<PathBuf>,
}

impl Default for Setup {
    fn default() -> Self {
        Self {
            initial_page_number: 1,
            paper: enum_map! { Axis2::X => 8.5, Axis2::Y => 11.0 },
            margins: enum_map! { Axis2::X => [0.5, 0.5], Axis2::Y => [0.5, 0.5] },
            orientation: Default::default(),
            object_spacing: 12.0 / 72.0,
            chart_size: Default::default(),
            headings: Default::default(),
            file_name: None,
        }
    }
}

impl Setup {
    pub fn printable_size(&self) -> EnumMap<Axis2, f64> {
        EnumMap::from_fn(|axis| self.paper[axis] - self.margins[axis][0] - self.margins[axis][1])
    }
}
