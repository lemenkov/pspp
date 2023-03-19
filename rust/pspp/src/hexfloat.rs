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

use num::Float;
use std::{
    fmt::{Display, Formatter, Result},
    num::FpCategory,
};

pub struct HexFloat<T: Float>(pub T);

impl<T: Float> Display for HexFloat<T> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        let sign = if self.0.is_sign_negative() { "-" } else { "" };
        match self.0.classify() {
            FpCategory::Nan => return write!(f, "NaN"),
            FpCategory::Infinite => return write!(f, "{sign}Infinity"),
            FpCategory::Zero => return write!(f, "{sign}0.0"),
            _ => (),
        };
        let (significand, mut exponent, _) = self.0.integer_decode();
        let mut hex_sig = format!("{:x}", significand);
        while hex_sig.ends_with('0') {
            hex_sig.pop();
            exponent += 4;
        }
        match hex_sig.len() {
            0 => write!(f, "{sign}0.0"),
            1 => write!(f, "{sign}0x{hex_sig}.0p{exponent}"),
            len => write!(
                f,
                "{sign}0x{}.{}p{}",
                hex_sig.chars().next().unwrap(),
                &hex_sig[1..],
                exponent + 4 * (len as i16 - 1)
            ),
        }
    }
}

#[cfg(test)]
mod hex_float_tests {
    use crate::hexfloat::HexFloat;
    use num::Float;

    #[test]
    fn test() {
        assert_eq!(format!("{}", HexFloat(1.0)), "0x1.0p0");
        assert_eq!(format!("{}", HexFloat(123.0)), "0x1.ecp6");
        assert_eq!(format!("{}", HexFloat(1.0 / 16.0)), "0x1.0p-4");
        assert_eq!(format!("{}", HexFloat(f64::infinity())), "Infinity");
        assert_eq!(format!("{}", HexFloat(f64::neg_infinity())), "-Infinity");
        assert_eq!(format!("{}", HexFloat(f64::nan())), "NaN");
        assert_eq!(format!("{}", HexFloat(0.0)), "0.0");
        assert_eq!(format!("{}", HexFloat(f64::neg_zero())), "-0.0");
    }
}
