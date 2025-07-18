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

//! Converting big- and little-endian `[u8]` arrays to and from primitive types.

use binrw::Endian;

/// Converts a primitive type into a big- or little-endian `[u8]` array.
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

/// Parses a `[u8]` array as a big- or little-endian primitive type.
pub trait FromBytes<T, const N: usize> {
    /// Given 'bytes', returns `T`.
    fn parse(self, bytes: [u8; N]) -> T;
}
impl FromBytes<u64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> u64 {
        match self {
            Endian::Big => u64::from_be_bytes(bytes),
            Endian::Little => u64::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<u32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> u32 {
        match self {
            Endian::Big => u32::from_be_bytes(bytes),
            Endian::Little => u32::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<u16, 2> for Endian {
    fn parse(self, bytes: [u8; 2]) -> u16 {
        match self {
            Endian::Big => u16::from_be_bytes(bytes),
            Endian::Little => u16::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<u8, 1> for Endian {
    fn parse(self, bytes: [u8; 1]) -> u8 {
        match self {
            Endian::Big => u8::from_be_bytes(bytes),
            Endian::Little => u8::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<i64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> i64 {
        match self {
            Endian::Big => i64::from_be_bytes(bytes),
            Endian::Little => i64::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<i32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> i32 {
        match self {
            Endian::Big => i32::from_be_bytes(bytes),
            Endian::Little => i32::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<i16, 2> for Endian {
    fn parse(self, bytes: [u8; 2]) -> i16 {
        match self {
            Endian::Big => i16::from_be_bytes(bytes),
            Endian::Little => i16::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<i8, 1> for Endian {
    fn parse(self, bytes: [u8; 1]) -> i8 {
        match self {
            Endian::Big => i8::from_be_bytes(bytes),
            Endian::Little => i8::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<f64, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> f64 {
        match self {
            Endian::Big => f64::from_be_bytes(bytes),
            Endian::Little => f64::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<Option<f64>, 8> for Endian {
    fn parse(self, bytes: [u8; 8]) -> Option<f64> {
        let number: f64 = self.parse(bytes);
        (number != -f64::MAX).then_some(number)
    }
}
impl FromBytes<f32, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> f32 {
        match self {
            Endian::Big => f32::from_be_bytes(bytes),
            Endian::Little => f32::from_le_bytes(bytes),
        }
    }
}
impl FromBytes<Option<f32>, 4> for Endian {
    fn parse(self, bytes: [u8; 4]) -> Option<f32> {
        let number: f32 = self.parse(bytes);
        (number != -f32::MAX).then_some(number)
    }
}
