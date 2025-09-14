//! # Decryption for SPSS encrypted files
//!
//! SPSS supports encryption using a password for data, viewer, and syntax
//! files.  The encryption mechanism is poorly designed, so this module provides
//! support for decrypting, but not encrypting, the SPSS format.
//! Use [EncryptedFile] as the starting point for reading an encrypted file.
//!
//! SPSS also supports what calls "encrypted passwords".  Use [EncodedPassword]
//! to encode and decode these passwords.

// Warn about missing docs, but not for items declared with `#[cfg(test)]`.
#![cfg_attr(not(test), warn(missing_docs))]

use aes::{
    cipher::{generic_array::GenericArray, BlockDecrypt, KeyInit},
    Aes256, Aes256Dec,
};
use cmac::{Cmac, Mac};
use smallvec::SmallVec;
use std::{
    fmt::Debug,
    io::{BufRead, Error as IoError, ErrorKind, Read, Seek, SeekFrom},
};
use thiserror::Error as ThisError;

use binrw::{io::NoSeek, BinRead};

/// Error reading an encrypted file.
#[derive(Clone, Debug, ThisError)]
pub enum Error {
    /// I/O error.
    #[error("I/O error reading encrypted file wrapper ({0})")]
    IoError(ErrorKind),

    /// Invalid padding in final encrypted data block.
    #[error("Invalid padding in final encrypted data block")]
    InvalidPadding,

    /// Not an encrypted file.
    #[error("Not an encrypted file")]
    NotEncrypted,

    /// Encrypted file has invalid length.
    #[error("Encrypted file has invalid length {0} (expected 4 more than a multiple of 16).")]
    InvalidLength(u64),

    /// Unknown file type.
    #[error("Unknown file type {0:?}.")]
    UnknownFileType(String),
}

impl From<std::io::Error> for Error {
    fn from(value: std::io::Error) -> Self {
        Self::IoError(value.kind())
    }
}

#[derive(BinRead)]
struct EncryptedHeader {
    /// Fixed as `1c 00 00 00 00 00 00 00` in practice.
    _ignore: [u8; 8],

    /// File type.
    #[br(magic = b"ENCRYPTED")]
    file_type: [u8; 3],

    /// Fixed as `15 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` in practice.
    _ignore2: [u8; 16],
}

/// An encrypted file.
pub struct EncryptedFile<R> {
    reader: R,
    file_type: FileType,

    /// Length of the ciphertext (excluding the 36-byte header).
    length: u64,

    /// First block of ciphertext, for verifying that any password the user
    /// tries is correct.
    first_block: [u8; 16],

    /// Last block of ciphertext, for checking padding and determining the
    /// plaintext length.
    last_block: [u8; 16],
}

/// Type of encrypted file.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum FileType {
    /// A `.sps` syntax file.
    Syntax,

    /// A `.spv` viewer file.
    Viewer,

    /// A `.sav` data file.
    Data,
}

impl<R> EncryptedFile<R>
where
    R: Read + Seek,
{
    /// Opens `reader` as an encrypted file.
    ///
    /// This reads enough of the file to verify that it is in the expected
    /// format and returns an error if it cannot be read or is not the expected
    /// format.
    ///
    /// `reader` doesn't need to be [BufRead], and probably should not be.  The
    /// [EncryptedReader] returned by [unlock] or [unlock_literal] will be
    /// [BufRead].
    ///
    /// [unlock]: Self::unlock
    /// [unlock_literal]: Self::unlock_literal
    pub fn new(mut reader: R) -> Result<Self, Error> {
        let header =
            EncryptedHeader::read_le(&mut NoSeek::new(&mut reader)).map_err(
                |error| match error {
                    binrw::Error::BadMagic { .. } => Error::NotEncrypted,
                    binrw::Error::Io(error) => Error::IoError(error.kind()),
                    _ => unreachable!(),
                },
            )?;
        let file_type = match &header.file_type {
            b"SAV" => FileType::Data,
            b"SPV" => FileType::Viewer,
            b"SPS" => FileType::Syntax,
            _ => {
                return Err(Error::UnknownFileType(
                    header.file_type.iter().map(|b| *b as char).collect(),
                ))
            }
        };
        let mut first_block = [0; 16];
        reader.read_exact(&mut first_block)?;
        let length = reader.seek(SeekFrom::End(-16))? + 16;
        if length < 36 + 16 || (length - 36) % 16 != 0 {
            return Err(Error::InvalidLength(length + 36));
        }
        let mut last_block = [0; 16];
        reader.read_exact(&mut last_block)?;
        reader.seek(SeekFrom::Start(36))?;
        Ok(Self {
            reader,
            file_type,
            length,
            first_block,
            last_block,
        })
    }

    /// Tries to unlock the encrypted file using both `password` and with
    /// `password` decoded with [EncodedPassword::decode].  If successful,
    /// returns an [EncryptedReader] for the file; on failure, returns the
    /// [EncryptedFile] again for another try.
    pub fn unlock(self, password: &[u8]) -> Result<EncryptedReader<R>, Self> {
        self.unlock_literal(password).or_else(|this| {
            match EncodedPassword::from_encoded(password) {
                Some(encoded) => this.unlock_literal(&encoded.decode()),
                None => Err(this),
            }
        })
    }

    /// Tries to unlock the encrypted file using just `password`.  If
    /// successful, returns an [EncryptedReader] for the file; on failure,
    /// returns the [EncryptedFile] again for another try.
    ///
    /// If the password itself might be encoded ("encrypted"), instead use
    /// [Self::unlock] to try it both ways.
    pub fn unlock_literal(self, password: &[u8]) -> Result<EncryptedReader<R>, Self> {
        // NIST SP 800-108 fixed data.
        #[rustfmt::skip]
        static  FIXED: &[u8] = &[
            // i
            0x00, 0x00, 0x00, 0x01,

            // label
            0x35, 0x27, 0x13, 0xcc, 0x53, 0xa7, 0x78, 0x89,
            0x87, 0x53, 0x22, 0x11, 0xd6, 0x5b, 0x31, 0x58,
            0xdc, 0xfe, 0x2e, 0x7e, 0x94, 0xda, 0x2f, 0x00,
            0xcc, 0x15, 0x71, 0x80, 0x0a, 0x6c, 0x63, 0x53,

            // delimiter
            0x00,

            // context
            0x38, 0xc3, 0x38, 0xac, 0x22, 0xf3, 0x63, 0x62,
            0x0e, 0xce, 0x85, 0x3f, 0xb8, 0x07, 0x4c, 0x4e,
            0x2b, 0x77, 0xc7, 0x21, 0xf5, 0x1a, 0x80, 0x1d,
            0x67, 0xfb, 0xe1, 0xe1, 0x83, 0x07, 0xd8, 0x0d,

            // L
            0x00, 0x00, 0x01, 0x00,
        ];

        // Truncate password to at most 10 bytes.
        let password = password.get(..10).unwrap_or(password);
        let n = password.len();

        //  padded_password = password padded with zeros to 32 bytes.
        let mut padded_password = [0; 32];
        padded_password[..n].copy_from_slice(password);

        // cmac = CMAC(padded_password, fixed).
        let mut cmac = <Cmac<Aes256> as Mac>::new_from_slice(&padded_password).unwrap();
        cmac.update(FIXED);
        let cmac = cmac.finalize().into_bytes();

        // The key is the cmac repeated twice.
        let mut key = [0; 32];
        key[..16].copy_from_slice(cmac.as_slice());
        key[16..].copy_from_slice(cmac.as_slice());

        // Use key to initialize AES.
        let aes = <Aes256Dec as KeyInit>::new_from_slice(&key).unwrap();

        // Decrypt first block to verify password.
        let mut out = [0; 16];
        aes.decrypt_block_b2b(
            GenericArray::from_slice(&self.first_block),
            GenericArray::from_mut_slice(&mut out),
        );
        static MAGIC: &[&[u8]] = &[
            b"$FL2@(#)",
            b"$FL3@(#)",
            b"* Encoding",
            b"PK\x03\x04\x14\0\x08",
        ];
        if !MAGIC.iter().any(|magic| out.starts_with(magic)) {
            return Err(self);
        }

        // Decrypt last block to check padding and get final length.
        aes.decrypt_block_b2b(
            GenericArray::from_slice(&self.last_block),
            GenericArray::from_mut_slice(&mut out),
        );
        let Some(padding_length) = parse_padding(&out) else {
            return Err(self);
        };

        Ok(EncryptedReader::new(
            self.reader,
            aes,
            self.file_type,
            self.length - 36 - padding_length as u64,
        ))
    }

    /// Returns the type of encrypted file.
    pub fn file_type(&self) -> FileType {
        self.file_type
    }
}

fn parse_padding(block: &[u8; 16]) -> Option<usize> {
    let pad = block[15] as usize;
    if (1..=16).contains(&pad) && block[16 - pad..].iter().all(|b| *b == pad as u8) {
        Some(pad)
    } else {
        None
    }
}

impl<R> Debug for EncryptedFile<R>
where
    R: Read,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "EncryptedFile({:?})", &self.file_type)
    }
}

/// Encrypted file reader.
///
/// This implements [Read] and [Seek] for SPSS encrypted files.  To construct an
/// [EncryptedReader], call [EncryptedFile::new], then [EncryptedFile::unlock].
pub struct EncryptedReader<R> {
    /// Underlying reader.
    reader: R,

    /// AES-256 decryption key.
    aes: Aes256Dec,

    /// Type of file.
    file_type: FileType,

    /// Plaintext file length (not including the file header or padding).
    length: u64,

    /// Plaintext data buffer.
    buffer: Box<[u8; 4096]>,

    /// Plaintext offset of the byte in `buffer[0]`.  A multiple of 16 less than
    /// or equal to `length`.
    start: u64,

    /// Number of bytes in buffer (`0 <= head <= 4096`).
    head: usize,

    /// Offset in buffer of the next byte to read (`head <= tail`).
    tail: usize,
}

impl<R> EncryptedReader<R> {
    fn new(reader: R, aes: Aes256Dec, file_type: FileType, length: u64) -> Self {
        Self {
            reader,
            aes,
            file_type,
            length,
            buffer: Box::new([0; 4096]),
            start: 0,
            head: 0,
            tail: 0,
        }
    }

    fn read_buffer(&mut self, buf: &mut [u8]) -> Result<usize, IoError> {
        let n = buf.len().min(self.head - self.tail);
        buf[..n].copy_from_slice(&self.buffer[self.tail..n + self.tail]);
        self.tail += n;
        Ok(n)
    }

    /// Returns the type of encrypted file.
    pub fn file_type(&self) -> FileType {
        self.file_type
    }
}

impl<R> EncryptedReader<R>
where
    R: Read,
{
    fn fill_buffer(&mut self, offset: u64) -> Result<(), IoError> {
        self.start = offset / 16 * 16;
        self.head = 0;
        self.tail = (offset % 16) as usize;
        let n = self.buffer.len().min((self.length - self.start) as usize);
        self.reader
            .read_exact(&mut self.buffer[..n.next_multiple_of(16)])?;
        for offset in (0..n).step_by(16) {
            self.aes.decrypt_block(GenericArray::from_mut_slice(
                &mut self.buffer[offset..offset + 16],
            ));
        }
        self.head = n;
        Ok(())
    }
}

impl<R> Read for EncryptedReader<R>
where
    R: Read,
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, IoError> {
        if self.tail < self.head {
            self.read_buffer(buf)
        } else {
            let offset = self.start + self.head as u64;
            if offset < self.length {
                self.fill_buffer(offset)?;
                self.read_buffer(buf)
            } else {
                Ok(0)
            }
        }
    }
}

impl<R> Seek for EncryptedReader<R>
where
    R: Read + Seek,
{
    fn seek(&mut self, pos: SeekFrom) -> Result<u64, IoError> {
        let offset = match pos {
            SeekFrom::Start(offset) => Some(offset),
            SeekFrom::End(relative) => self.length.checked_add_signed(relative),
            SeekFrom::Current(relative) => {
                (self.start + self.tail as u64).checked_add_signed(relative)
            }
        }
        .filter(|offset| *offset < u64::MAX - 36)
        .ok_or(IoError::from(ErrorKind::InvalidInput))?;
        if offset != self.start + self.tail as u64 {
            self.reader.seek(SeekFrom::Start(offset / 16 * 16 + 36))?;
            self.fill_buffer(offset)?;
        }
        Ok(offset)
    }
}

impl<R> BufRead for EncryptedReader<R>
where
    R: Read + Seek,
{
    fn fill_buf(&mut self) -> std::io::Result<&[u8]> {
        if self.tail >= self.head {
            let offset = self.start + self.head as u64;
            if offset < self.length {
                self.fill_buffer(offset)?;
            }
        }
        Ok(&self.buffer[self.tail..self.head])
    }

    fn consume(&mut self, amount: usize) {
        self.tail += amount;
        debug_assert!(self.tail <= self.head);
    }
}

const fn b(x: i32) -> u16 {
    1 << x
}

static AH: [[u16; 2]; 4] = [
    [b(2), b(2) | b(3) | b(6) | b(7)],
    [b(3), b(0) | b(1) | b(4) | b(5)],
    [b(4) | b(7), b(8) | b(9) | b(12) | b(13)],
    [b(5) | b(6), b(10) | b(11) | b(14) | b(15)],
];

static AL: [[u16; 2]; 4] = [
    [b(0) | b(3) | b(12) | b(15), b(0) | b(1) | b(4) | b(5)],
    [b(1) | b(2) | b(13) | b(14), b(2) | b(3) | b(6) | b(7)],
    [b(4) | b(7) | b(8) | b(11), b(8) | b(9) | b(12) | b(13)],
    [b(5) | b(6) | b(9) | b(10), b(10) | b(11) | b(14) | b(15)],
];

static BH: [[u16; 2]; 4] = [
    [b(2), b(1) | b(3) | b(9) | b(11)],
    [b(3), b(0) | b(2) | b(8) | b(10)],
    [b(4) | b(7), b(4) | b(6) | b(12) | b(14)],
    [b(5) | b(6), b(5) | b(7) | b(13) | b(15)],
];

static BL: [[u16; 2]; 4] = [
    [b(0) | b(3) | b(12) | b(15), b(0) | b(2) | b(8) | b(10)],
    [b(1) | b(2) | b(13) | b(14), b(1) | b(3) | b(9) | b(11)],
    [b(4) | b(7) | b(8) | b(11), b(4) | b(6) | b(12) | b(14)],
    [b(5) | b(6) | b(9) | b(10), b(5) | b(7) | b(13) | b(15)],
];

fn decode_nibble(table: &[[u16; 2]; 4], nibble: u8) -> u16 {
    for section in table.iter() {
        if section[0] & (1 << nibble) != 0 {
            return section[1];
        }
    }
    0
}

fn find_1bit(x: u16) -> Option<u8> {
    x.is_power_of_two().then(|| x.trailing_zeros() as u8)
}

fn decode_pair(a: u8, b: u8) -> Option<u8> {
    let x = find_1bit(decode_nibble(&AH, a >> 4) & decode_nibble(&BH, b >> 4))?;
    let y = find_1bit(decode_nibble(&AL, a & 15) & decode_nibble(&BL, b & 15))?;
    Some((x << 4) | y)
}

fn encode_nibble(table: &[[u16; 2]; 4], nibble: u8) -> Vec<u8> {
    for section in table.iter() {
        if section[1] & (1 << nibble) != 0 {
            let mut outputs = Vec::with_capacity(4);
            let mut bits = section[0];
            while bits != 0 {
                outputs.push(bits.trailing_zeros() as u8);
                bits &= bits - 1;
            }
            return outputs;
        }
    }
    unreachable!()
}

fn encode_byte(hi_table: &[[u16; 2]; 4], lo_table: &[[u16; 2]; 4], byte: u8) -> Vec<char> {
    let hi_variants = encode_nibble(hi_table, byte >> 4);
    let lo_variants = encode_nibble(lo_table, byte & 15);
    let mut variants = Vec::with_capacity(hi_variants.len() * lo_variants.len());
    for hi in hi_variants.iter().copied() {
        for lo in lo_variants.iter().copied() {
            let byte = (hi << 4) | lo;
            if byte != 127 {
                variants.push(byte as char);
            }
        }
    }
    variants
}

/// An encoded password.
///
/// SPSS calls these "encrypted passwords", but they are not encrypted.  They
/// are encoded with a simple scheme, analogous to base64 encoding but
/// one-to-many: any plaintext password maps to many possible encoded passwords.
///
/// The encoding scheme maps each plaintext password byte to 2 ASCII characters,
/// using only at most the first 10 bytes of the plaintext password.  Thus, an
/// encoded password is always a multiple of 2 characters long, and never longer
/// than 20 characters.  The characters in an encoded password are always in the
/// graphic ASCII range 33 through 126.  Each successive pair of characters in
/// the password encodes a single byte in the plaintext password.
///
/// This struct supports both encoding and decoding passwords.
#[derive(Clone, Debug)]
pub struct EncodedPassword(Vec<Vec<char>>);

impl EncodedPassword {
    /// Creates an [EncodedPassword] from an already-encoded password `encoded`.
    /// Returns `None` if `encoded` is not a valid encoded password.
    pub fn from_encoded(encoded: &[u8]) -> Option<Self> {
        if encoded.len() > 20
            || encoded.len() % 2 != 0
            || !encoded.iter().all(|byte| (32..=127).contains(byte))
        {
            return None;
        }

        Some(EncodedPassword(
            encoded.iter().map(|byte| vec![*byte as char]).collect(),
        ))
    }

    /// Returns an [EncodedPassword] as an encoded version of the given
    /// `plaintext` password.  Only the first 10 bytes, at most, of the
    /// plaintext password is used.
    pub fn from_plaintext(plaintext: &[u8]) -> EncodedPassword {
        let input = plaintext.get(..10).unwrap_or(plaintext);
        EncodedPassword(
            input
                .iter()
                .copied()
                .flat_map(|byte| [encode_byte(&AH, &AL, byte), encode_byte(&BH, &BL, byte)])
                .collect(),
        )
    }

    /// Returns the number of variations of this encoded password.
    ///
    /// An [EncodedPassword] created by [EncodedPassword::from_plaintext] has
    /// many variations: between `16**n` and `32**n` for an `n`-byte plaintext
    /// password, so up to `32**10` (about 1e15) for the 10-byte longest
    /// plaintext passwords.
    ///
    /// An [EncodedPassword] created by [EncodedPassword::from_encoded] has only
    /// a single variation, the one passed in by that function.
    pub fn n_variants(&self) -> u64 {
        self.0
            .iter()
            .map(|variants| variants.len() as u64)
            .product()
    }

    /// Returns one variation of this encoded password, numbered `index`.  All
    /// variations decode the same way.
    pub fn variant(&self, mut index: u64) -> String {
        let mut output = String::with_capacity(20);
        for variants in &self.0 {
            let n = variants.len() as u64;
            output.push(variants[(index % n) as usize]);
            index /= n;
        }
        output
    }

    /// Returns the decoded version of this encoded password.
    pub fn decode(&self) -> SmallVec<[u8; 10]> {
        let mut output = SmallVec::new();
        for [a, b] in self.0.as_chunks::<2>().0 {
            output.push(decode_pair(a[0] as u8, b[0] as u8).unwrap());
        }
        output
    }
}

#[cfg(test)]
mod tests {
    use std::{io::Cursor, path::Path};

    use crate::crypto::{EncodedPassword, EncryptedFile, FileType};

    fn test_decrypt(input_name: &Path, expected_name: &Path, password: &str, file_type: FileType) {
        let input_filename = Path::new("src/crypto/testdata").join(input_name);
        let input = std::fs::read(&input_filename).unwrap();
        let mut cursor = Cursor::new(&input);
        let file = EncryptedFile::new(&mut cursor).unwrap();
        assert_eq!(file.file_type(), file_type);
        let mut reader = file.unlock_literal(password.as_bytes()).unwrap();
        assert_eq!(reader.file_type(), file_type);
        let mut actual = Vec::new();
        std::io::copy(&mut reader, &mut actual).unwrap();

        let expected_filename = Path::new("src/crypto/testdata").join(expected_name);
        let expected = std::fs::read(&expected_filename).unwrap();
        if actual != expected {
            panic!();
        }
    }

    #[test]
    fn sys_file() {
        test_decrypt(
            Path::new("test-encrypted.sav"),
            Path::new("test.sav"),
            "pspp",
            FileType::Data,
        );
    }

    #[test]
    fn syntax_file() {
        test_decrypt(
            Path::new("test-encrypted.sps"),
            Path::new("test.sps"),
            "password",
            FileType::Syntax,
        );
    }

    #[test]
    fn spv_file() {
        test_decrypt(
            Path::new("test-encrypted.spv"),
            Path::new("test.spv"),
            "Password1",
            FileType::Viewer,
        );
    }

    #[test]
    fn password_encoding() {
        // Decode a few specific passwords.
        assert_eq!(
            EncodedPassword::from_encoded(b"-|")
                .unwrap()
                .decode()
                .as_slice(),
            b"b"
        );
        assert_eq!(
            EncodedPassword::from_encoded(b" A")
                .unwrap()
                .decode()
                .as_slice(),
            b"a"
        );

        // Check that the encoding and decoding algorithms are inverses
        // for individual characters at least.
        for plaintext in 0..=255 {
            let encoded = EncodedPassword::from_plaintext(&[plaintext]);
            for variant in 0..encoded.n_variants() {
                let encoded_variant = encoded.variant(variant);
                let decoded = EncodedPassword::from_encoded(encoded_variant.as_bytes())
                    .unwrap()
                    .decode();
                assert_eq!(&[plaintext], decoded.as_slice());
            }
        }
    }
}
