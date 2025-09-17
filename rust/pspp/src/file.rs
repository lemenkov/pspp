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

//! Basic infrastructure for files understood by PSPP.

#![cfg_attr(not(test), warn(missing_docs))]
use std::{
    fs::File,
    io::{Error, Read, Seek},
    path::Path,
};

use zip::ZipArchive;

use crate::sys::raw::Magic;

/// Type of a file understood by PSPP.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum FileType {
    /// A [system file](crate::sys).
    System {
        /// Whether the file is encrypted.
        encrypted: bool,
    },

    /// A [portable file](crate::por).
    Portable,

    /// An SPSS PC+ data file.
    Pc,

    /// An [SPSS Viewer file](crate::output::spv).
    Viewer {
        /// Whether the file is encrypted.
        encrypted: bool,
    },

    /// A file that may be an SPSS syntax file.
    Syntax {
        /// True if there's confidence that this is a syntax file, which would
        /// be either because it has an indicated encoding or because it is
        /// encrypted.
        confident: bool,

        /// Whether the file is encrypted.
        encrypted: bool,
    },
}

impl FileType {
    /// Returns true if we're confident about the file's type.
    ///
    /// (We can't always confidently identify syntax files because they look
    /// mostly like any kind of text file.)
    pub fn is_confident(&self) -> bool {
        match self {
            Self::Syntax { confident, .. } => *confident,
            _ => true,
        }
    }

    /// Returns true if the file is encrypted.
    pub fn is_encrypted(&self) -> bool {
        match self {
            FileType::System { encrypted } => *encrypted,
            FileType::Viewer { encrypted } => *encrypted,
            FileType::Syntax {
                confident: _,
                encrypted,
            } => *encrypted,
            _ => false,
        }
    }

    /// Attempts to identify the type of file at `path`.  Returns:
    ///
    /// * `Err(error)`: I/O error.
    ///
    /// * `Ok(Some(type))`: Identified file type.
    ///
    /// * `Ok(None)`: Unknown file type.
    pub fn from_file<P>(path: P) -> Result<Option<Self>, Error>
    where
        P: AsRef<Path>,
    {
        Self::from_reader(File::open(path)?)
    }

    /// Like [from_file](Self::from_file) for an arbitrary `reader`.
    pub fn from_reader<R>(mut reader: R) -> Result<Option<Self>, Error>
    where
        R: Read + Seek,
    {
        let mut buf = vec![0; 512];
        let mut n = 0;
        while n < buf.capacity() {
            let count = reader.read(&mut buf[n..])?;
            n += count;
            if count == 0 {
                break;
            }
        }
        buf.truncate(n);

        if let Some(magic) = buf.get(0..4) {
            let magic: [u8; 4] = magic.try_into().unwrap();
            if Magic::try_from(magic).is_ok() {
                return Ok(Some(Self::System { encrypted: false }));
            }
        }

        match buf.get(8..20) {
            Some(b"ENCRYPTEDSAV") => {
                return Ok(Some(Self::System { encrypted: true }));
            }
            Some(b"ENCRYPTEDSPV") => {
                return Ok(Some(Self::Viewer { encrypted: true }));
            }
            Some(b"ENCRYPTEDSPS") => {
                return Ok(Some(Self::Syntax {
                    confident: true,
                    encrypted: true,
                }));
            }
            _ => (),
        }

        if buf
            .get(200 + 256..)
            .unwrap_or_default()
            .windows(8)
            .any(|w| w == b"SPSSPORT")
        {
            return Ok(Some(Self::Portable));
        }

        if buf.get(0x104..0x108) == Some(b"SPSS") {
            return Ok(Some(Self::Pc));
        }

        let mut string = String::new();
        if buf.get(..7) == Some(&[0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08])
            && let Ok(mut archive) = ZipArchive::new(reader)
            && let Ok(mut file) = archive.by_name("META-INF/MANIFEST.MF")
            && let Ok(_) = file.read_to_string(&mut string)
            && string.trim() == "allowPivoting=true"
        {
            return Ok(Some(Self::Viewer { encrypted: false }));
        }

        if !buf.is_empty() && !buf.contains(&0) {
            return Ok(Some(Self::Syntax {
                confident: buf.starts_with(b"* Encoding:"),
                encrypted: false,
            }));
        }

        Ok(None)
    }
}

#[cfg(test)]
mod tests {
    use crate::file::FileType;

    #[test]
    fn file_type() {
        assert_eq!(
            FileType::from_file("src/file/testdata/test.sav").unwrap(),
            Some(FileType::System { encrypted: false })
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test-encrypted.sav").unwrap(),
            Some(FileType::System { encrypted: true })
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test.por").unwrap(),
            Some(FileType::Portable)
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test-encrypted.spv").unwrap(),
            Some(FileType::Viewer { encrypted: true })
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test.spv").unwrap(),
            Some(FileType::Viewer { encrypted: false })
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test.sps").unwrap(),
            Some(FileType::Syntax {
                confident: false,
                encrypted: false
            })
        );
        assert_eq!(
            FileType::from_file("src/file/testdata/test-encoding.sps").unwrap(),
            Some(FileType::Syntax {
                confident: true,
                encrypted: false
            })
        );
    }
}
