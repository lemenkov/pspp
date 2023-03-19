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

use enum_iterator::Sequence;
use smallvec::SmallVec;

/// The endianness for integer and floating-point numbers in SPSS system files.
///
/// SPSS system files can declare IBM 370 and DEC VAX floating-point
/// representations, but no file that uses either of these has ever been found
/// in the wild, so this code does not handle them.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Sequence)]
pub enum Endian {
    /// Big-endian: MSB at lowest address.
    #[cfg_attr(target_endian = "big", default)]
    Big,

    /// Little-endian: LSB at lowest address.
    #[cfg_attr(target_endian = "little", default)]
    Little,
}

impl Endian {
    pub fn identify_u32(expected_value: u32, bytes: [u8; 4]) -> Option<Self> {
        let as_big: u32 = Endian::Big.parse(bytes);
        let as_little: u32 = Endian::Little.parse(bytes);
        match (as_big == expected_value, as_little == expected_value) {
            (true, false) => Some(Endian::Big),
            (false, true) => Some(Endian::Little),
            _ => None,
        }
    }

    pub fn identify_f64(expected_value: f64, bytes: [u8; 8]) -> Option<Self> {
        let as_big: f64 = Endian::Big.parse(bytes);
        let as_little: f64 = Endian::Little.parse(bytes);
        match (as_big == expected_value, as_little == expected_value) {
            (true, false) => Some(Endian::Big),
            (false, true) => Some(Endian::Little),
            _ => None,
        }
    }

    pub fn to_smallvec<const N: usize>(self, mut value: u64, n: usize) -> SmallVec<[u8; N]> {
        debug_assert!(n <= 8);
        let mut vec = SmallVec::new();
        value <<= 8 * (8 - n);
        for _ in 0..n {
            vec.push((value >> 56) as u8);
            value <<= 8;
        }
        if self == Endian::Little {
            vec.reverse();
        }
        vec
    }
}

pub trait ToBytes<T, const N: usize> {
    fn to_bytes(self, value: T) -> [u8; N];
}
impl ToBytes<u64, 8> for Endian {
    fn to_bytes(self, value: u64) -> [u8; 8] {
        match self {
            Endian::Big => u64::to_be_bytes(value),
            Endian::Little => u64::to_le_bytes(value),
        }
    }
}
impl ToBytes<i64, 8> for Endian {
    fn to_bytes(self, value: i64) -> [u8; 8] {
        match self {
            Endian::Big => i64::to_be_bytes(value),
            Endian::Little => i64::to_le_bytes(value),
        }
    }
}
impl ToBytes<u32, 4> for Endian {
    fn to_bytes(self, value: u32) -> [u8; 4] {
        match self {
            Endian::Big => u32::to_be_bytes(value),
            Endian::Little => u32::to_le_bytes(value),
        }
    }
}
impl ToBytes<i32, 4> for Endian {
    fn to_bytes(self, value: i32) -> [u8; 4] {
        match self {
            Endian::Big => i32::to_be_bytes(value),
            Endian::Little => i32::to_le_bytes(value),
        }
    }
}
impl ToBytes<u16, 2> for Endian {
    fn to_bytes(self, value: u16) -> [u8; 2] {
        match self {
            Endian::Big => u16::to_be_bytes(value),
            Endian::Little => u16::to_le_bytes(value),
        }
    }
}
impl ToBytes<u8, 1> for Endian {
    fn to_bytes(self, value: u8) -> [u8; 1] {
        [value]
    }
}
impl ToBytes<f64, 8> for Endian {
    fn to_bytes(self, value: f64) -> [u8; 8] {
        match self {
            Endian::Big => f64::to_be_bytes(value),
            Endian::Little => f64::to_le_bytes(value),
        }
    }
}

/// Parses an `N`-byte array in one of the supported formats into native format
/// as type `T`.
pub trait Parse<T, const N: usize> {
    /// Given 'bytes', returns `T`.
    fn parse(self, bytes: [u8; N]) -> T;
}
impl Parse<u64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> u64 {
        match self {
            Endian::Big => u64::from_be_bytes(bytes),
            Endian::Little => u64::from_le_bytes(bytes),
        }
    }
}
impl Parse<u32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> u32 {
        match self {
            Endian::Big => u32::from_be_bytes(bytes),
            Endian::Little => u32::from_le_bytes(bytes),
        }
    }
}
impl Parse<u16, 2> for Endian {
    fn parse(self, bytes: [u8; 2]) -> u16 {
        match self {
            Endian::Big => u16::from_be_bytes(bytes),
            Endian::Little => u16::from_le_bytes(bytes),
        }
    }
}
impl Parse<u8, 1> for Endian {
    fn parse(self, bytes: [u8; 1]) -> u8 {
        match self {
            Endian::Big => u8::from_be_bytes(bytes),
            Endian::Little => u8::from_le_bytes(bytes),
        }
    }
}
impl Parse<i64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> i64 {
        match self {
            Endian::Big => i64::from_be_bytes(bytes),
            Endian::Little => i64::from_le_bytes(bytes),
        }
    }
}
impl Parse<i32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> i32 {
        match self {
            Endian::Big => i32::from_be_bytes(bytes),
            Endian::Little => i32::from_le_bytes(bytes),
        }
    }
}
impl Parse<i16, 2> for Endian {
    fn parse(self, bytes: [u8; 2]) -> i16 {
        match self {
            Endian::Big => i16::from_be_bytes(bytes),
            Endian::Little => i16::from_le_bytes(bytes),
        }
    }
}
impl Parse<i8, 1> for Endian {
    fn parse(self, bytes: [u8; 1]) -> i8 {
        match self {
            Endian::Big => i8::from_be_bytes(bytes),
            Endian::Little => i8::from_le_bytes(bytes),
        }
    }
}
impl Parse<f64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> f64 {
        match self {
            Endian::Big => f64::from_be_bytes(bytes),
            Endian::Little => f64::from_le_bytes(bytes),
        }
    }
}
impl Parse<Option<f64>, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> Option<f64> {
        let number: f64 = self.parse(bytes);
        (number != -f64::MAX).then_some(number)
    }
}
impl Parse<f32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> f32 {
        match self {
            Endian::Big => f32::from_be_bytes(bytes),
            Endian::Little => f32::from_le_bytes(bytes),
        }
    }
}
impl Parse<Option<f32>, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> Option<f32> {
        let number: f32 = self.parse(bytes);
        (number != -f32::MAX).then_some(number)
    }
}
