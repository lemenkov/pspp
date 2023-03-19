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

use std::sync::LazyLock;

use crate::locale_charset::locale_charset;
use encoding_rs::{Encoding, UTF_8};

include!(concat!(env!("OUT_DIR"), "/encodings.rs"));

pub fn codepage_from_encoding(encoding: &str) -> Option<u32> {
    CODEPAGE_NAME_TO_NUMBER
        .get(encoding.to_ascii_lowercase().as_str())
        .copied()
}

use thiserror::Error as ThisError;

#[derive(Clone, ThisError, Debug, PartialEq, Eq)]
pub enum Error {
    #[error("This system file does not indicate its own character encoding.  For best results, specify an encoding explicitly.  Use SYSFILE INFO with ENCODING=\"DETECT\" to analyze the possible encodings.")]
    NoEncoding,

    #[error("This system file encodes text strings with unknown code page {0}.")]
    UnknownCodepage(i32),

    #[error("This system file encodes text strings with unknown encoding {0}.")]
    UnknownEncoding(String),

    #[error("This system file is encoded in EBCDIC, which is not supported.")]
    Ebcdic,
}

pub fn default_encoding() -> &'static Encoding {
    static DEFAULT_ENCODING: LazyLock<&'static Encoding> =
        LazyLock::new(|| Encoding::for_label(locale_charset().as_bytes()).unwrap_or(UTF_8));
    &DEFAULT_ENCODING
}

pub fn get_encoding(
    encoding: Option<&str>,
    character_code: Option<i32>,
) -> Result<&'static Encoding, Error> {
    let label = if let Some(encoding) = encoding {
        encoding
    } else if let Some(codepage) = character_code {
        match codepage {
            1 => return Err(Error::Ebcdic),
            2 | 3 => {
                // These ostensibly mean "7-bit ASCII" and "8-bit ASCII"[sic]
                // respectively.  However, many files have character code 2 but
                // data which are clearly not ASCII.  Therefore, ignore these
                // values.
                return Err(Error::NoEncoding);
            }
            4 => "MS_KANJI",
            _ => CODEPAGE_NUMBER_TO_NAME
                .get(&codepage)
                .copied()
                .ok_or(Error::UnknownCodepage(codepage))?,
        }
    } else {
        return Err(Error::NoEncoding);
    };

    Encoding::for_label(label.as_bytes()).ok_or(Error::UnknownEncoding(label.into()))
}

/*
#[cfg(test)]
mod tests {
    use std::thread::spawn;

    use encoding_rs::{EUC_JP, UTF_8, WINDOWS_1252};

    #[test]
    fn round_trip() {
        let mut threads = Vec::new();
        for thread in 0..128 {
            let start: u32 = thread << 25;
            let end = start + ((1 << 25) - 1);
            threads.push(spawn(move || {
                for i in start..=end {
                    let s = i.to_le_bytes();
                    let (utf8, replacement) = EUC_JP.decode_without_bom_handling(&s);
                    if !replacement {
                        let s2 = UTF_8.encode(&utf8).0;
                        assert_eq!(s.as_slice(), &*s2);
                    }
                }
            }));
        }
        for thread in threads {
            thread.join().unwrap();
        }
    }
}
*/
