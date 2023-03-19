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

use pango::SCALE;

use crate::output::pivot::HorzAlign;

mod driver;
pub mod fsm;
pub mod pager;

pub use driver::CairoDriver;

/// Conversion from 1/96" units ("pixels") to Cairo/Pango units.
fn px_to_xr(x: usize) -> usize {
    x * 3 * (SCALE as usize * 72 / 96) / 3
}

fn xr_to_pt(x: usize) -> f64 {
    x as f64 / SCALE as f64
}

fn horz_align_to_pango(horz_align: HorzAlign) -> pango::Alignment {
    match horz_align {
        HorzAlign::Right | HorzAlign::Decimal { .. } => pango::Alignment::Right,
        HorzAlign::Left => pango::Alignment::Left,
        HorzAlign::Center => pango::Alignment::Center,
    }
}

#[cfg(test)]
mod test {
    use crate::output::cairo::CairoDriver;

    #[test]
    fn create() {
        CairoDriver::new("test.pdf");
    }
}
