use std::fmt::{Display, Write};

use smallstr::SmallString;

pub trait ToSmallString {
    fn to_small_string<const N: usize>(&self) -> SmallString<[u8; N]>;
}

impl<T> ToSmallString for T
where
    T: Display,
{
    fn to_small_string<const N: usize>(&self) -> SmallString<[u8; N]> {
        let mut s = SmallString::new();
        write!(&mut s, "{}", self).unwrap();
        s
    }
}
