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

pub trait ToInteger {
    fn to_exact_integer<T>(&self) -> Option<T>
    where
        T: FromFloat;
    fn to_exact_usize(&self) -> Option<usize> {
        self.to_exact_integer()
    }
    fn to_exact_u8(&self) -> Option<u8> {
        self.to_exact_integer()
    }
    fn to_exact_u16(&self) -> Option<u16> {
        self.to_exact_integer()
    }
    fn to_exact_u32(&self) -> Option<u32> {
        self.to_exact_integer()
    }
    fn to_exact_u64(&self) -> Option<u64> {
        self.to_exact_integer()
    }
    fn to_exact_u128(&self) -> Option<u128> {
        self.to_exact_integer()
    }
    fn to_exact_isize(&self) -> Option<usize> {
        self.to_exact_integer()
    }
    fn to_exact_i8(&self) -> Option<i8> {
        self.to_exact_integer()
    }
    fn to_exact_i16(&self) -> Option<i16> {
        self.to_exact_integer()
    }
    fn to_exact_i32(&self) -> Option<i32> {
        self.to_exact_integer()
    }
    fn to_exact_i64(&self) -> Option<i64> {
        self.to_exact_integer()
    }
    fn to_exact_i128(&self) -> Option<i128> {
        self.to_exact_integer()
    }
}

impl ToInteger for f64 {
    fn to_exact_integer<T>(&self) -> Option<T>
    where
        T: FromFloat,
    {
        T::from_float(*self)
    }
}

pub trait FromFloat {
    fn from_float(x: f64) -> Option<Self>
    where
        Self: Sized;
}

macro_rules! impl_from_float {
    ($T:ident) => {
        impl FromFloat for $T {
            fn from_float(x: f64) -> Option<Self>
            where
                Self: Sized,
            {
                if x.trunc() == x && x >= $T::MIN as f64 && x <= $T::MAX as f64 {
                    Some(x as Self)
                } else {
                    None
                }
            }
        }
    };
}

impl_from_float!(usize);
impl_from_float!(u8);
impl_from_float!(u16);
impl_from_float!(u32);
impl_from_float!(u64);
impl_from_float!(u128);
impl_from_float!(isize);
impl_from_float!(i8);
impl_from_float!(i16);
impl_from_float!(i32);
impl_from_float!(i64);
impl_from_float!(i128);
