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

use std::{
    borrow::Cow,
    cmp::Ordering,
    fmt::{Debug, Display},
    hash::Hash,
};

use encoding_rs::{Encoding, UTF_8};
use serde::Serialize;

use crate::{
    data::{ByteCow, ByteStr, ByteString, MutRawString, Quoted, RawString, ResizeError},
    variable::VarWidth,
};

pub trait Encoded {
    fn encoding(&self) -> &'static Encoding;
}

impl Encoded for &'_ str {
    fn encoding(&self) -> &'static Encoding {
        UTF_8
    }
}

impl Encoded for String {
    fn encoding(&self) -> &'static Encoding {
        UTF_8
    }
}

impl Encoded for &'_ String {
    fn encoding(&self) -> &'static Encoding {
        UTF_8
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WithEncoding<T> {
    pub encoding: &'static Encoding,
    pub inner: T,
}

impl<T> WithEncoding<T> {
    pub fn new(inner: T, encoding: &'static Encoding) -> Self {
        Self { inner, encoding }
    }

    pub fn into_inner(self) -> T {
        self.inner
    }
}

impl<'a> WithEncoding<ByteCow<'a>> {
    pub fn into_owned(self) -> WithEncoding<ByteString> {
        WithEncoding::new(self.inner.into_owned(), self.encoding)
    }
}

impl<T> PartialOrd for WithEncoding<T>
where
    T: PartialOrd,
{
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        self.inner.partial_cmp(&other.inner)
    }
}

impl<T> Ord for WithEncoding<T>
where
    T: Ord,
{
    fn cmp(&self, other: &Self) -> Ordering {
        self.inner.cmp(&other.inner)
    }
}

impl<T> Serialize for WithEncoding<T>
where
    WithEncoding<T>: EncodedString,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.as_str().serialize(serializer)
    }
}

pub trait EncodedString: Encoded + RawString + Display + Debug {
    fn as_str(&self) -> Cow<'_, str>;
    fn into_string(self) -> String
    where
        Self: Sized,
    {
        self.as_str().into_owned()
    }
    fn to_encoding(&self, encoding: &'static Encoding) -> WithEncoding<ByteCow<'_>>;
    fn as_encoded_byte_str(&self) -> WithEncoding<ByteStr<'_>> {
        WithEncoding::new(ByteStr(self.raw_string_bytes()), self.encoding())
    }
    fn cloned(&self) -> WithEncoding<ByteString> {
        WithEncoding::new(ByteString::from(self.raw_string_bytes()), self.encoding())
    }
    fn quoted(&self) -> Quoted<&Self>
    where
        Self: Sized,
    {
        Quoted(self)
    }
}

impl<'a> EncodedString for &'a str {
    fn as_str(&self) -> Cow<'_, str> {
        Cow::from(*self)
    }

    fn to_encoding(&self, encoding: &'static Encoding) -> WithEncoding<ByteCow<'_>> {
        WithEncoding::new(ByteCow(encoding.encode(self).0), encoding)
    }
}

impl EncodedString for String {
    fn as_str(&self) -> Cow<'_, str> {
        Cow::from(String::as_str(&self))
    }

    fn to_encoding(&self, encoding: &'static Encoding) -> WithEncoding<ByteCow<'_>> {
        WithEncoding::new(ByteCow(encoding.encode(&self).0), encoding)
    }
}

impl EncodedString for &'_ String {
    fn as_str(&self) -> Cow<'_, str> {
        Cow::from(String::as_str(&self))
    }

    fn to_encoding(&self, encoding: &'static Encoding) -> WithEncoding<ByteCow<'_>> {
        WithEncoding::new(ByteCow(encoding.encode(String::as_str(&self)).0), encoding)
    }
}

impl<T> RawString for WithEncoding<T>
where
    T: RawString,
{
    fn raw_string_bytes(&self) -> &[u8] {
        self.inner.raw_string_bytes()
    }
}

impl<T> MutRawString for WithEncoding<T>
where
    T: MutRawString,
{
    fn resize(&mut self, new_len: usize) -> Result<(), ResizeError> {
        self.inner.resize(new_len)
    }

    fn trim_end(&mut self) {
        self.inner.trim_end();
    }
}

impl<T> EncodedString for WithEncoding<T>
where
    T: RawString,
{
    /// Returns this string recoded in UTF-8.  Invalid characters will be
    /// replaced by [REPLACEMENT_CHARACTER].
    ///
    /// [REPLACEMENT_CHARACTER]: std::char::REPLACEMENT_CHARACTER
    fn as_str(&self) -> Cow<'_, str> {
        self.encoding
            .decode_without_bom_handling(self.raw_string_bytes())
            .0
    }

    /// Returns this string recoded in `encoding`.  Invalid characters will be
    /// replaced by [REPLACEMENT_CHARACTER].
    ///
    /// [REPLACEMENT_CHARACTER]: std::char::REPLACEMENT_CHARACTER
    fn to_encoding(&self, encoding: &'static Encoding) -> WithEncoding<ByteCow<'_>> {
        let utf8 = self.as_str();
        let inner = match encoding.encode(&utf8).0 {
            Cow::Borrowed(_) => {
                // Recoding into UTF-8 and then back did not change anything.
                Cow::from(self.raw_string_bytes())
            }
            Cow::Owned(owned) => Cow::Owned(owned),
        };
        WithEncoding {
            encoding,
            inner: ByteCow(inner),
        }
    }
}

impl WithEncoding<ByteString> {
    pub fn codepage_to_unicode(&mut self) {
        if self.encoding() != UTF_8 {
            let new_len = (self.inner.len() * 3).min(VarWidth::MAX_STRING as usize);
            if let Cow::Owned(string) = self
                .encoding()
                .decode_without_bom_handling(self.raw_string_bytes())
                .0
            {
                self.inner = ByteString::from(string);
            }

            // Use `self.inner.0.resize` (instead of `self.inner.resize()`)
            // because this is a forced resize that can trim off non-spaces.
            self.inner.0.resize(new_len, b' ');

            self.encoding = UTF_8;
        }
    }
}

impl<T> Encoded for WithEncoding<T> {
    fn encoding(&self) -> &'static Encoding {
        self.encoding
    }
}

impl<T> Display for WithEncoding<T>
where
    T: RawString,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.as_str())
    }
}

impl<T> Hash for WithEncoding<T>
where
    T: Hash,
{
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.inner.hash(state);
    }
}

#[cfg(test)]
mod tests {
    use std::{char::REPLACEMENT_CHARACTER, iter::repeat_n};

    use encoding_rs::{Encoding, UTF_8, WINDOWS_1252};

    use crate::data::{ByteString, EncodedString, RawString};

    #[test]
    fn codepage_to_unicode() {
        fn check_unicode(original: &str, encoding: &'static Encoding, expected: &str) {
            let original = ByteString::from(encoding.encode(original).0).with_encoding(encoding);
            let mut actual = original.clone();
            actual.codepage_to_unicode();
            assert_eq!(actual.as_str().len(), expected.len());
            assert_eq!(actual.as_str(), expected);
        }

        check_unicode("abc", UTF_8, "abc");
        check_unicode("abc", WINDOWS_1252, "abc      ");
        check_unicode("éèäî", WINDOWS_1252, "éèäî    ");
        check_unicode(
            &repeat_n('é', 15000).collect::<String>(),
            WINDOWS_1252,
            &repeat_n('é', 15000)
                .chain(repeat_n(' ', 2767))
                .collect::<String>(),
        );
        check_unicode(
            &repeat_n('é', 20000).collect::<String>(),
            WINDOWS_1252,
            &repeat_n('é', 16383)
                .chain(std::iter::once(REPLACEMENT_CHARACTER))
                .collect::<String>(),
        );
    }
}
