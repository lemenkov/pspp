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
    borrow::Borrow,
    cmp::Ordering,
    fmt::{Debug, Display, Formatter, Result as FmtResult},
    hash::{Hash, Hasher},
    ops::{Deref, DerefMut},
};

use encoding_rs::{CoderResult, Encoder, EncoderResult, Encoding, UTF_8};
use serde::Serialize;
use thiserror::Error as ThisError;
use unicase::UniCase;
use unicode_properties::UnicodeGeneralCategory;
use unicode_segmentation::UnicodeSegmentation;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Class {
    /// No distinguishing prefix.
    Ordinary,

    /// Starting with `$`.
    System,

    /// Starting with `#`.
    Scratch,

    /// Starting with `!`.
    Macro,
}

impl Class {
    pub fn must_leave(self) -> bool {
        self == Self::Scratch
    }
}

impl From<&Identifier> for Class {
    fn from(id: &Identifier) -> Self {
        if id.0.starts_with('$') {
            Self::System
        } else if id.0.starts_with('#') {
            Self::Scratch
        } else if id.0.starts_with('!') {
            Self::Macro
        } else {
            Self::Ordinary
        }
    }
}

pub trait IdentifierChar {
    /// Returns true if `self` is an ASCII character that may be the first
    /// character in an identifier.
    fn ascii_may_start_id(self) -> bool;

    /// Returns true if `self` may be the first character in an identifier.
    fn may_start_id(self) -> bool;

    /// Returns true if `self` is an ASCII character that may be a second or
    /// subsequent character in an identifier.
    fn ascii_may_continue_id(self) -> bool;

    /// Returns true if `self` may be a second or subsequent character in an
    /// identifier.
    fn may_continue_id(self) -> bool;
}

impl IdentifierChar for char {
    fn ascii_may_start_id(self) -> bool {
        matches!(self, 'a'..='z' | 'A'..='Z' | '@' | '#' | '$' | '!')
    }

    fn may_start_id(self) -> bool {
        if self < '\u{0080}' {
            self.ascii_may_start_id()
        } else {
            use unicode_properties::GeneralCategoryGroup::*;

            matches!(self.general_category_group(), Letter | Mark | Symbol)
                && self != char::REPLACEMENT_CHARACTER
        }
    }

    fn ascii_may_continue_id(self) -> bool {
        matches!(self, 'a'..='z' | 'A'..='Z' | '0'..='9' | '@' | '#' | '$' | '.' | '_')
    }

    fn may_continue_id(self) -> bool {
        if self < '\u{0080}' {
            self.ascii_may_continue_id()
        } else {
            use unicode_properties::GeneralCategoryGroup::*;

            matches!(
                self.general_category_group(),
                Letter | Mark | Symbol | Number
            ) && self != char::REPLACEMENT_CHARACTER
        }
    }
}

#[derive(Clone, Debug, ThisError, PartialEq, Eq)]
pub enum Error {
    #[error("Identifier cannot be empty string.")]
    Empty,

    #[error("\"{0}\" may not be used as an identifier because it is a reserved word.")]
    Reserved(String),

    #[error("\"!\" is not a valid identifier.")]
    Bang,

    #[error("{string:?} may not be used as an identifier because it begins with disallowed character {c:?}.")]
    BadFirstCharacter { string: String, c: char },

    #[error(
        "{string:?} may not be used as an identifier because it contains disallowed character {c:?}."
    )]
    BadLaterCharacter { string: String, c: char },

    #[error("Identifier \"{id}\" is {length} bytes in the encoding in use ({encoding}), which exceeds the {max}-byte limit.")]
    TooLong {
        id: String,
        length: usize,
        encoding: &'static str,
        max: usize,
    },

    #[error("\"{id}\" may not be used as an identifier because the encoding in use ({encoding}) cannot represent \"{c}\".")]
    NotEncodable {
        id: String,
        encoding: &'static str,
        c: char,
    },

    #[error("Multiple response set name \"{0}\" does not begin with required \"$\".")]
    MissingAt(Identifier),
}

pub enum ReservedWord {
    And,
    Or,
    Not,
    Eq,
    Ge,
    Gt,
    Le,
    Lt,
    Ne,
    All,
    By,
    To,
    With,
}

impl TryFrom<&str> for ReservedWord {
    type Error = ();

    fn try_from(source: &str) -> Result<Self, Self::Error> {
        if !(2..=4).contains(&source.len()) {
            Err(())
        } else {
            let b = source.as_bytes();
            let c0 = b[0].to_ascii_uppercase();
            let c1 = b[1].to_ascii_uppercase();
            match (source.len(), c0, c1) {
                (2, b'B', b'Y') => Ok(Self::By),
                (2, b'E', b'Q') => Ok(Self::Eq),
                (2, b'G', b'T') => Ok(Self::Gt),
                (2, b'G', b'E') => Ok(Self::Ge),
                (2, b'L', b'T') => Ok(Self::Lt),
                (2, b'L', b'E') => Ok(Self::Le),
                (2, b'N', b'E') => Ok(Self::Ne),
                (3, b'N', b'O') if b[2].eq_ignore_ascii_case(&b'T') => Ok(Self::Not),
                (2, b'O', b'R') => Ok(Self::Or),
                (2, b'T', b'O') => Ok(Self::To),
                (3, b'A', b'L') if b[2].eq_ignore_ascii_case(&b'L') => Ok(Self::All),
                (3, b'A', b'N') if b[2].eq_ignore_ascii_case(&b'D') => Ok(Self::And),
                (4, b'W', b'I') if b[2..4].eq_ignore_ascii_case(b"TH") => Ok(Self::With),
                _ => Err(()),
            }
        }
    }
}

pub fn is_reserved_word(s: &str) -> bool {
    ReservedWord::try_from(s).is_ok()
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Identifier(pub UniCase<String>);

impl Identifier {
    /// Maximum length of an identifier, in bytes.  The limit applies in the
    /// encoding used by the dictionary, not in UTF-8.
    pub const MAX_LEN: usize = 64;

    fn new_unchecked(s: impl Into<UniCase<String>>) -> Self {
        let s: UniCase<String> = s.into();
        debug_assert!(Self::check_plausible(&s).is_ok());
        Identifier(s)
    }

    pub fn new(s: impl Into<UniCase<String>>) -> Result<Self, Error> {
        Self::from_encoding(s, UTF_8)
    }

    /// Converts this identifier to UTF-8.  This is generally a no-op, because
    /// our internal encoding is UTF-8, but some identifiers are longer in UTF-8
    /// than in their code page, which means that to satisfy the 64-byte limit
    /// this function sometimes has to remove trailing grapheme clusters.
    pub fn codepage_to_unicode(&mut self) {
        while self.len() > Self::MAX_LEN {
            let (new_len, _) = self.as_str().grapheme_indices(true).next_back().unwrap();
            self.0.truncate(new_len);
            if self.0.is_empty() {
                // We had a grapheme cluster longer than 64 bytes!
                *self = Identifier::new("VAR").unwrap();
                return;
            }
        }
    }

    pub fn from_encoding(
        s: impl Into<UniCase<String>>,
        encoding: &'static Encoding,
    ) -> Result<Identifier, Error> {
        let s: UniCase<String> = s.into();
        Self::check_plausible(&s)?;
        let identifier = Identifier(s);
        identifier.check_encoding(encoding)?;
        Ok(identifier)
    }

    /// Checks whether this is a valid identifier in the given `encoding`.  An
    /// identifier that is valid in one encoding might be invalid in another
    /// because some characters are unencodable or because it is too long.
    pub fn check_encoding(&self, encoding: &'static Encoding) -> Result<(), Error> {
        let s = self.0.as_str();
        let (_encoded, _, unencodable) = encoding.encode(s);
        if unencodable {
            let mut encoder = encoding.new_encoder();
            let mut buf = Vec::with_capacity(
                encoder
                    .max_buffer_length_from_utf8_without_replacement(s.len())
                    .unwrap(),
            );
            let EncoderResult::Unmappable(c) = encoder
                .encode_from_utf8_to_vec_without_replacement(s, &mut buf, true)
                .0
            else {
                unreachable!();
            };
            return Err(Error::NotEncodable {
                id: s.into(),
                encoding: encoding.name(),
                c,
            });
        }
        /*
        if encoded.len() > Self::MAX_LEN {
            return Err(Error::TooLong {
                id: s.into(),
                length: encoded.len(),
                encoding: encoding.name(),
                max: Self::MAX_LEN,
            });
        }*/
        Ok(())
    }
    pub fn check_plausible(s: &str) -> Result<(), Error> {
        if s.is_empty() {
            return Err(Error::Empty);
        }
        if is_reserved_word(s) {
            return Err(Error::Reserved(s.into()));
        }
        if s == "!" {
            return Err(Error::Bang);
        }

        let mut i = s.chars();
        let first = i.next().unwrap();
        if !first.may_start_id() {
            return Err(Error::BadFirstCharacter {
                string: s.into(),
                c: first,
            });
        }
        for c in i {
            if !c.may_continue_id() {
                return Err(Error::BadLaterCharacter {
                    string: s.into(),
                    c,
                });
            }
        }
        Ok(())
    }

    /// Returns true if `token` is a case-insensitive match for `keyword`.
    ///
    /// Keywords match `keyword` and `token` are identical, or `token` is at
    /// least 3 characters long and those characters are identical to `keyword`
    /// or differ only in case.
    ///
    /// `keyword` must be ASCII.
    pub fn matches_keyword(&self, keyword: &str) -> bool {
        id_match_n_nonstatic(keyword, self.0.as_str(), 3)
    }

    /// Returns true if `token` is a case-insensitive match for at least the
    /// first `n` characters of `keyword`.
    ///
    /// `keyword` must be ASCII.
    pub fn matches_keyword_n(&self, keyword: &str, n: usize) -> bool {
        id_match_n_nonstatic(keyword, self.0.as_str(), n)
    }

    pub fn must_be_ordinary(self) -> Result<Self, Error> {
        match Class::from(&self) {
            Class::Ordinary => Ok(self),
            _ => {
                let s = self.0.into_inner();
                let first = s.chars().next().unwrap();
                Err(Error::BadFirstCharacter {
                    string: s,
                    c: first,
                })
            }
        }
    }

    pub fn class(&self) -> Class {
        self.into()
    }

    pub fn as_str(&self) -> &str {
        self.0.as_ref()
    }

    /// Returns this this identifier truncated to at most 8 bytes in `encoding`.
    pub fn shortened(&self, encoding: &'static Encoding) -> Self {
        let new_len = shortened_len(self, "", encoding, 8);
        Self::new_unchecked(self.0[..new_len].to_string())
    }

    /// Returns a prefix of this identifier concatenated with all of `suffix`,
    /// including as many grapheme clusters from the beginning of this
    /// identifier as would fit within `max_len` bytes if the resulting string
    /// were to be re-encoded in `encoding`.
    ///
    /// `max_len` would ordinarily be 64, since that's the maximum length of an
    /// identifier, but a value of 8 is appropriate for short variable names.
    ///
    /// This function fails if adding or using `suffix` produces an invalid
    /// [Identifier], for example if `max_len` is short enough that none of the
    /// identifier can be included and `suffix` begins with `'_'` or another
    /// character that may not appear at the beginning of an identifier.
    ///
    /// # Examples
    ///
    /// Simple examples for UTF-8 `encoding` with `max_len` of 6:
    ///
    /// ```text
    /// identifier="abc",  suffix="xyz"     => "abcxyz"
    /// identifier="abcd", suffix="xyz"     => "abcxyz"
    /// identifier="abc",  suffix="uvwxyz"  => "uvwxyz"
    /// identifier="abc",  suffix="tuvwxyz" => "tuvwxyz"
    /// ```
    ///
    /// Examples for windows-1252 `encoding` with `max_len` of 6:
    ///
    /// ```text
    /// identifier="éèä",  suffix="xyz"    => "éèäxyz"
    /// ```
    ///
    /// (each letter in the identifier is only 1 byte in windows-1252 even
    /// though they each take 2 bytes in UTF-8)
    pub fn with_suffix(
        &self,
        suffix: &str,
        encoding: &'static Encoding,
        max_len: usize,
    ) -> Result<Self, Error> {
        let prefix_len = shortened_len(self, suffix, encoding, max_len);
        if prefix_len == 0 {
            Self::new(suffix)
        } else {
            Self::new(format!("{}{suffix}", &self[..prefix_len]))
        }
    }
}

impl Serialize for Identifier {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.0.as_str().serialize(serializer)
    }
}

fn encode_fully(encoder: &mut Encoder, mut src: &str, dst: &mut Vec<u8>, last: bool) {
    while let (CoderResult::OutputFull, read, _) = encoder.encode_from_utf8_to_vec(src, dst, last) {
        src = &src[read..];
        dst.reserve((dst.capacity() * 2) - dst.len());
    }
}

fn shortened_len(prefix: &str, suffix: &str, encoding: &'static Encoding, max_len: usize) -> usize {
    assert!(max_len <= 64);
    if encoding == UTF_8 {
        if prefix.len() + suffix.len() <= max_len {
            prefix.len()
        } else if suffix.len() >= max_len {
            0
        } else {
            let mut copy_len = 0;
            for (cluster_start, cluster) in prefix.grapheme_indices(true) {
                let cluster_end = cluster_start + cluster.len();
                if cluster_end > max_len - suffix.len() {
                    break;
                }
                copy_len = cluster_end;
            }
            copy_len
        }
    } else {
        let mut copy_len = 0;
        let mut tmp = Vec::with_capacity(max_len);
        for (cluster_start, cluster) in prefix.grapheme_indices(true) {
            let cluster_end = cluster_start + cluster.len();
            let mut encoder = encoding.new_encoder();
            tmp.clear();
            encode_fully(&mut encoder, &prefix[..cluster_end], &mut tmp, false);
            if tmp.len() <= max_len {
                encode_fully(&mut encoder, suffix, &mut tmp, true);
            }
            if tmp.len() > max_len {
                break;
            }
            copy_len = cluster_end;
        }
        copy_len
    }
}

impl Deref for Identifier {
    type Target = UniCase<String>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl PartialEq<str> for Identifier {
    fn eq(&self, other: &str) -> bool {
        self.0.eq(&UniCase::new(other))
    }
}

/// Returns true if `token` is a case-insensitive match for `keyword`.
///
/// Keywords match `keyword` and `token` are identical, or `token` is at least 3
/// characters long and those characters are identical to `keyword` or differ
/// only in case.
///
/// `keyword` must be ASCII.  It's normally a constant string, so it's declared
/// as `&'static str` to make it harder to reverse the argument order. But
/// there's no reason that a non-static string won't work, so use
/// [`id_match_n_nonstatic`] instead if you need it.
pub fn id_match(keyword: &'static str, token: &str) -> bool {
    id_match_n(keyword, token, 3)
}

/// Returns true if `token` is a case-insensitive match for at least the first
/// `n` characters of `keyword`.
///
/// `keyword` must be ASCII.  It's normally a constant string, so it's declared
/// as `&'static str` to make it harder to reverse the argument order. But
/// there's no reason that a non-static string won't work, so use
/// [`id_match_n_nonstatic`] instead if you need it.
pub fn id_match_n(keyword: &'static str, token: &str, n: usize) -> bool {
    id_match_n_nonstatic(keyword, token, n)
}

/// Returns true if `token` is a case-insensitive match for at least the first
/// `n` characters of `keyword`.
///
/// `keyword` must be ASCII.
pub fn id_match_n_nonstatic(keyword: &str, token: &str, n: usize) -> bool {
    debug_assert!(keyword.is_ascii());
    let keyword_prefix = if (n..keyword.len()).contains(&token.len()) {
        &keyword[..token.len()]
    } else {
        keyword
    };
    keyword_prefix.eq_ignore_ascii_case(token)
}

impl Display for Identifier {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        write!(f, "{}", self.0)
    }
}

impl Debug for Identifier {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{:?}", self.0)
    }
}

pub trait HasIdentifier {
    fn identifier(&self) -> &UniCase<String>;
}

pub struct ByIdentifier<T>(pub T)
where
    T: HasIdentifier;

impl<T> ByIdentifier<T>
where
    T: HasIdentifier,
{
    pub fn new(inner: T) -> Self {
        Self(inner)
    }
}

impl<T> PartialEq for ByIdentifier<T>
where
    T: HasIdentifier,
{
    fn eq(&self, other: &Self) -> bool {
        self.0.identifier().eq(other.0.identifier())
    }
}

impl<T> Eq for ByIdentifier<T> where T: HasIdentifier {}

impl<T> PartialOrd for ByIdentifier<T>
where
    T: HasIdentifier,
{
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl<T> Ord for ByIdentifier<T>
where
    T: HasIdentifier,
{
    fn cmp(&self, other: &Self) -> Ordering {
        self.0.identifier().cmp(other.0.identifier())
    }
}

impl<T> Hash for ByIdentifier<T>
where
    T: HasIdentifier,
{
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.identifier().hash(state)
    }
}

impl<T> Borrow<UniCase<String>> for ByIdentifier<T>
where
    T: HasIdentifier,
{
    fn borrow(&self) -> &UniCase<String> {
        self.0.identifier()
    }
}

impl<T> Debug for ByIdentifier<T>
where
    T: HasIdentifier + Debug,
{
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        self.0.fmt(f)
    }
}

impl<T> Clone for ByIdentifier<T>
where
    T: HasIdentifier + Clone,
{
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

impl<T> Deref for ByIdentifier<T>
where
    T: HasIdentifier + Clone,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T> DerefMut for ByIdentifier<T>
where
    T: HasIdentifier + Clone,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl<T> Serialize for ByIdentifier<T>
where
    T: HasIdentifier + Clone + Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        self.0.serialize(serializer)
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;

    use encoding_rs::{Encoding, UTF_8, WINDOWS_1252};
    use unicase::UniCase;

    use crate::identifier::Identifier;

    use super::{ByIdentifier, HasIdentifier};

    #[derive(PartialEq, Eq, Debug, Clone)]
    struct SimpleVar {
        name: Identifier,
        value: i32,
    }

    impl HasIdentifier for SimpleVar {
        fn identifier(&self) -> &UniCase<String> {
            &self.name.0
        }
    }

    #[test]
    fn identifier() {
        // Variables should not be the same if their values differ.
        let abcd = Identifier::new("abcd").unwrap();
        let abcd1 = SimpleVar {
            name: abcd.clone(),
            value: 1,
        };
        let abcd2 = SimpleVar {
            name: abcd,
            value: 2,
        };
        assert_ne!(abcd1, abcd2);

        // But [ByIdentifier]` should treat them the same.
        let abcd1_by_name = ByIdentifier::new(abcd1);
        let abcd2_by_name = ByIdentifier::new(abcd2);
        assert_eq!(abcd1_by_name, abcd2_by_name);

        // And a [HashSet] of [ByIdentifier] should also treat them the same.
        let mut vars: HashSet<ByIdentifier<SimpleVar>> = HashSet::new();
        assert!(vars.insert(ByIdentifier::new(abcd1_by_name.0.clone())));
        assert!(!vars.insert(ByIdentifier::new(abcd2_by_name.0.clone())));
        assert_eq!(
            vars.get(&UniCase::new(String::from("abcd")))
                .unwrap()
                .0
                .value,
            1
        );
    }

    #[test]
    fn with_suffix() {
        for (head, suffix, encoding, max_len, expected) in [
            ("abc", "xyz", UTF_8, 6, "abcxyz"),
            ("abcd", "xyz", UTF_8, 6, "abcxyz"),
            ("abcd", "uvwxyz", UTF_8, 6, "uvwxyz"),
            ("abc", "tuvwxyz", UTF_8, 6, "tuvwxyz"),
            ("éèä", "xyz", UTF_8, 6, "éxyz"),
            ("éèä", "xyz", WINDOWS_1252, 6, "éèäxyz"),
        ] {
            let head = Identifier::new(head).unwrap();
            let suffix = Identifier::new(suffix).unwrap();
            let actual = head.with_suffix(&suffix, encoding, max_len).unwrap();
            assert_eq!(&actual, expected);
        }
    }

    #[test]
    fn shortened() {
        for (long, expected_short, encoding) in [
            ("abc", "abc", UTF_8),
            ("éèäîVarNameA", "éèäî", UTF_8),
            ("éèäîVarNameA", "éèäîVarN", WINDOWS_1252),
        ] {
            let long = Identifier::new(long).unwrap();
            let short = long.shortened(encoding);
            assert_eq!(&short, expected_short);
        }
    }

    #[test]
    fn codepage_to_unicode() {
        fn check_unicode(identifier: &str, encoding: &'static Encoding, expected: &str) {
            let identifier = Identifier::from_encoding(String::from(identifier), encoding).unwrap();
            let mut actual = identifier.clone();
            actual.codepage_to_unicode();
            assert_eq!(actual.as_str(), expected);
        }

        check_unicode("abc", UTF_8, "abc");
        check_unicode("éèäî", UTF_8, "éèäî");

        // 32 bytes in windows-1252, 64 bytes in UTF-8, no truncation.
        check_unicode(
            "éèäîéèäîéèäîéèäîéèäîéèäîéèäîéèäî",
            WINDOWS_1252,
            "éèäîéèäîéèäîéèäîéèäîéèäîéèäîéèäî",
        );

        // 33 or 34 bytes in windows-1252, 65 or 66 bytes in UTF-8, truncate
        // last (2-byte) character.
        check_unicode(
            "xéèäîéèäîéèäîéèäîéèäîéèäîéèäîéèäî",
            WINDOWS_1252,
            "xéèäîéèäîéèäîéèäîéèäîéèäîéèäîéèä",
        );
        check_unicode(
            "xyéèäîéèäîéèäîéèäîéèäîéèäîéèäîéèäî",
            WINDOWS_1252,
            "xyéèäîéèäîéèäîéèäîéèäîéèäîéèäîéèä",
        );
    }
}
