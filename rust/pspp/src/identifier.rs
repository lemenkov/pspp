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

use encoding_rs::{EncoderResult, Encoding, UTF_8};
use finl_unicode::categories::{CharacterCategories, MajorCategory};
use thiserror::Error as ThisError;
use unicase::UniCase;

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
            use MajorCategory::*;

            [L, M, S].contains(&self.get_major_category()) && self != char::REPLACEMENT_CHARACTER
        }
    }

    fn ascii_may_continue_id(self) -> bool {
        matches!(self, 'a'..='z' | 'A'..='Z' | '0'..='9' | '@' | '#' | '$' | '.' | '_')
    }

    fn may_continue_id(self) -> bool {
        if self < '\u{0080}' {
            self.ascii_may_continue_id()
        } else {
            use MajorCategory::*;

            [L, M, S, N].contains(&self.get_major_category()) && self != char::REPLACEMENT_CHARACTER
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

    pub fn new(s: impl Into<UniCase<String>>) -> Result<Self, Error> {
        Self::from_encoding(s, UTF_8)
    }

    pub fn from_encoding(
        s: impl Into<UniCase<String>>,
        encoding: &'static Encoding,
    ) -> Result<Identifier, Error> {
        let s: UniCase<String> = s.into();
        Self::is_plausible(&s)?;
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
    pub fn is_plausible(s: &str) -> Result<(), Error> {
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
