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

use enum_map::{EnumMap, enum_map};
use serde::{Deserialize, Serialize};

use super::pivot::{Axis2, HorzAlign};

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Orientation {
    #[default]
    Portrait,
    Landscape,
}

/// Chart size.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChartSize {
    /// Size specified in the chart itself.
    #[default]
    AsIs,

    /// Full page.
    FullHeight,

    /// Half-page.
    HalfHeight,

    /// Quarter-page.
    QuarterHeight,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
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

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Heading(pub Vec<Paragraph>);

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(default)]
pub struct PageSetup {
    /// Page number of first page.
    pub initial_page_number: i32,

    /// Paper size in inches.
    pub paper: EnumMap<Axis2, f64>,

    /// Margin width in inches.
    pub margins: EnumMap<Axis2, [f64; 2]>,

    /// Portrait or landscape.
    pub orientation: Orientation,

    /// Space between objects, in inches.
    pub object_spacing: f64,

    /// Size of charts.
    pub chart_size: ChartSize,

    /// Header and footer.
    pub headings: [Heading; 2],
}

impl Default for PageSetup {
    fn default() -> Self {
        Self {
            initial_page_number: 1,
            paper: enum_map! { Axis2::X => 8.5, Axis2::Y => 11.0 },
            margins: enum_map! { Axis2::X => [0.5, 0.5], Axis2::Y => [0.5, 0.5] },
            orientation: Default::default(),
            object_spacing: 12.0 / 72.0,
            chart_size: Default::default(),
            headings: Default::default(),
        }
    }
}

impl PageSetup {
    pub fn printable_size(&self) -> EnumMap<Axis2, f64> {
        EnumMap::from_fn(|axis| self.paper[axis] - self.margins[axis][0] - self.margins[axis][1])
    }
}
