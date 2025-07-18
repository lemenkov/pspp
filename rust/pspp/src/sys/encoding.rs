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

//! Character encodings in system files.
//!
//! These are useful for reading and writing system files at a low level.

use std::sync::LazyLock;

use crate::locale_charset::locale_charset;
use encoding_rs::{Encoding, UTF_8};
use serde::Serialize;
use thiserror::Error as ThisError;

include!(concat!(env!("OUT_DIR"), "/encodings.rs"));

/// Returns the code page number corresponding to `encoding`, or `None` if
/// unknown.
pub fn codepage_from_encoding_name(encoding: &str) -> Option<u32> {
    CODEPAGE_NAME_TO_NUMBER
        .get(encoding.to_ascii_lowercase().as_str())
        .copied()
}

/// Returns the code page number for `encoding`.
pub fn codepage_from_encoding(encoding: &'static Encoding) -> u32 {
    // This `unwrap()` is tested against all the actual [Encoding]s in a
    // #[test].
    codepage_from_encoding_name(encoding.name()).unwrap()
}

/// An error or warning related to encodings.
#[derive(Clone, ThisError, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Error {
    /// Warning that the system file doesn't indicate its own encoding.
    #[error("This system file does not indicate its own character encoding.  For best results, specify an encoding explicitly.  Use SYSFILE INFO with ENCODING=\"DETECT\" to analyze the possible encodings.")]
    NoEncoding,

    /// Unknown code page.
    #[error("This system file encodes text strings with unknown code page {0}.")]
    UnknownCodepage(
        /// The code page number.
        i32,
    ),

    /// Unknown encoding.
    #[error("This system file encodes text strings with unknown encoding {0}.")]
    UnknownEncoding(
        /// The encoding name.
        String,
    ),

    /// EBCDIC not supported.
    #[error("This system file is encoded in EBCDIC, which is not supported.")]
    Ebcdic,
}

/// Returns the default encoding to use.
///
/// The default encoding is taken from the system or user's configured locale.
pub fn default_encoding() -> &'static Encoding {
    static DEFAULT_ENCODING: LazyLock<&'static Encoding> =
        LazyLock::new(|| Encoding::for_label(locale_charset().as_bytes()).unwrap_or(UTF_8));
    &DEFAULT_ENCODING
}

/// Returns the character encoding to use for a system file.
///
/// `encoding`, if any, should come from [EncodingRecord], and `character_code`,
/// if any, should from [IntegerInfoRecord].  Returns an error if the encoding
/// to use is unclear or unspecified, or if (for EBCDIC) it is unsupported.
///
/// [EncodingRecord]: crate::sys::raw::records::EncodingRecord
/// [IntegerInfoRecord]: crate::sys::raw::records::IntegerInfoRecord
pub fn get_encoding(
    encoding: Option<&str>,
    character_code: Option<i32>,
) -> Result<&'static Encoding, Error> {
    fn inner(label: &str) -> Result<&'static Encoding, Error> {
        Encoding::for_label(label.as_bytes()).ok_or(Error::UnknownEncoding(label.into()))
    }

    match (encoding, character_code) {
        (Some(encoding), _) => inner(encoding),
        (None, Some(1)) => Err(Error::Ebcdic),
        (None, Some(2 | 3)) => {
            // These ostensibly mean "7-bit ASCII" and "8-bit ASCII"[sic]
            // respectively.  However, many files have character code 2 but
            // data which are clearly not ASCII.  Therefore, ignore these
            // values.
            Err(Error::NoEncoding)
        }
        (None, Some(4)) => inner("MS_KANJI"),
        (None, Some(codepage)) => inner(
            CODEPAGE_NUMBER_TO_NAME
                .get(&codepage)
                .copied()
                .ok_or(Error::UnknownCodepage(codepage))?,
        ),
        (None, None) => Err(Error::NoEncoding),
    }
}

#[cfg(test)]
mod tests {
    use crate::sys::encoding::codepage_from_encoding;

    /// Test that every `Encoding` has a codepage.
    #[test]
    fn codepages() {
        codepage_from_encoding(&encoding_rs::BIG5);
        codepage_from_encoding(&encoding_rs::EUC_JP);
        codepage_from_encoding(&encoding_rs::EUC_KR);
        codepage_from_encoding(&encoding_rs::GB18030);
        codepage_from_encoding(&encoding_rs::GBK);
        codepage_from_encoding(&encoding_rs::IBM866);
        codepage_from_encoding(&encoding_rs::ISO_2022_JP);
        codepage_from_encoding(&encoding_rs::ISO_8859_2);
        codepage_from_encoding(&encoding_rs::ISO_8859_3);
        codepage_from_encoding(&encoding_rs::ISO_8859_4);
        codepage_from_encoding(&encoding_rs::ISO_8859_5);
        codepage_from_encoding(&encoding_rs::ISO_8859_6);
        codepage_from_encoding(&encoding_rs::ISO_8859_7);
        codepage_from_encoding(&encoding_rs::ISO_8859_8);
        codepage_from_encoding(&encoding_rs::ISO_8859_8_I);
        codepage_from_encoding(&encoding_rs::ISO_8859_10);
        codepage_from_encoding(&encoding_rs::ISO_8859_13);
        codepage_from_encoding(&encoding_rs::ISO_8859_14);
        codepage_from_encoding(&encoding_rs::ISO_8859_15);
        codepage_from_encoding(&encoding_rs::ISO_8859_16);
        codepage_from_encoding(&encoding_rs::KOI8_R);
        codepage_from_encoding(&encoding_rs::KOI8_U);
        codepage_from_encoding(&encoding_rs::MACINTOSH);
        codepage_from_encoding(&encoding_rs::REPLACEMENT);
        codepage_from_encoding(&encoding_rs::SHIFT_JIS);
        codepage_from_encoding(&encoding_rs::UTF_8);
        codepage_from_encoding(&encoding_rs::UTF_16BE);
        codepage_from_encoding(&encoding_rs::UTF_16LE);
        codepage_from_encoding(&encoding_rs::WINDOWS_874);
        codepage_from_encoding(&encoding_rs::WINDOWS_1250);
        codepage_from_encoding(&encoding_rs::WINDOWS_1251);
        codepage_from_encoding(&encoding_rs::WINDOWS_1252);
        codepage_from_encoding(&encoding_rs::WINDOWS_1253);
        codepage_from_encoding(&encoding_rs::WINDOWS_1254);
        codepage_from_encoding(&encoding_rs::WINDOWS_1255);
        codepage_from_encoding(&encoding_rs::WINDOWS_1256);
        codepage_from_encoding(&encoding_rs::WINDOWS_1257);
        codepage_from_encoding(&encoding_rs::WINDOWS_1258);
        codepage_from_encoding(&encoding_rs::X_MAC_CYRILLIC);
        codepage_from_encoding(&encoding_rs::X_USER_DEFINED);
    }
}
