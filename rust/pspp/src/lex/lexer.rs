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

//! High-level lexical analysis.

use std::{
    borrow::{Borrow, Cow},
    collections::VecDeque,
    fmt::{Debug, Formatter, Result as FmtResult, Write},
    fs,
    io::Result as IoResult,
    iter::once,
    mem::take,
    ops::{Range, RangeInclusive},
    path::Path,
    rc::Rc,
    sync::Arc,
};

use chardetng::EncodingDetector;
use encoding_rs::{Encoding, UTF_8};
use unicode_width::{UnicodeWidthChar, UnicodeWidthStr};

use crate::{
    lex::scan::merge_tokens,
    macros::{macro_tokens_to_syntax, MacroSet, ParseStatus, Parser},
    message::{Category, Diagnostic, Location, Point, Severity},
    settings::Settings,
};

use super::{
    scan::{MergeAction, ScanError, StringScanner},
    segment::{Segmenter, Syntax},
    token::Token,
};

/// Error handling for a syntax reader.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub enum ErrorHandling {
    /// Discard input line and continue reading.
    Terminal,

    /// Continue to next command, except for cascading failures.
    #[default]
    Continue,

    /// Continue, even for cascading failures.
    Ignore,

    /// Stop processing,
    Stop,
}

/// A syntax file and its contents.
///
/// This holds the entire contents of a syntax file, which are always read into
/// memory in their entirety, recoded into UTF-8 if necessary.  It includes the
/// file name (if any), and an index to make finding lines by line number more
/// efficient.
pub struct SyntaxFile {
    /// `None` if this reader is not associated with a file.
    file_name: Option<Arc<String>>,

    /// Original encoding.
    #[allow(dead_code)]
    encoding: &'static Encoding,

    /// Source file contents.
    contents: String,

    /// Byte offsets into `buffer` of starts of lines.  The first element is 0.
    lines: Vec<usize>,
}

impl SyntaxFile {
    /// Returns a `SyntaxFile` by reading `path` and recoding it from
    /// `encoding`.
    pub fn for_file<P>(path: P, encoding: Option<&'static Encoding>) -> IoResult<Self>
    where
        P: AsRef<Path>,
    {
        let bytes = fs::read(path.as_ref())?;
        let encoding = encoding.unwrap_or_else(|| {
            let mut encoding_detector = EncodingDetector::new();
            encoding_detector.feed(&bytes, true);
            encoding_detector.guess(None, true)
        });
        let (contents, _malformed) = encoding.decode_with_bom_removal(&bytes);
        Ok(Self::new(
            contents.to_string(),
            Some(path.as_ref().to_string_lossy().to_string()),
            encoding,
        ))
    }

    /// Creates a new `SyntaxFile` for `contents`, recording that `contents` was
    /// originally encoded in `encoding` and that it was read from `file_name`.
    pub fn new(contents: String, file_name: Option<String>, encoding: &'static Encoding) -> Self {
        let lines = once(0)
            .chain(contents.match_indices('\n').map(|(index, _s)| index + 1))
            .filter(|index| *index < contents.len())
            .collect::<Vec<_>>();
        Self {
            file_name: file_name.map(Arc::new),
            encoding,
            contents,
            lines,
        }
    }

    /// Returns a `SyntaxFile` for `contents`.
    pub fn for_string(contents: String) -> Self {
        Self::new(contents, None, UTF_8)
    }

    fn offset_to_point(&self, offset: usize) -> Point {
        let line = self
            .lines
            .partition_point(|&line_start| line_start <= offset);
        Point {
            line: line as i32,
            column: Some(
                self.contents
                    .get(self.lines[line - 1]..offset)
                    .unwrap_or_default()
                    .width() as i32
                    + 1,
            ),
        }
    }

    /// Returns the syntax for 1-based line-number `line_number`.
    fn get_line(&self, line_number: i32) -> &str {
        if (1..=self.lines.len() as i32).contains(&line_number) {
            let line_number = line_number as usize;
            let start = self.lines[line_number - 1];
            let end = self.lines.get(line_number).copied().unwrap_or(
                self.contents[start..]
                    .find('\n')
                    .map(|ofs| ofs + start)
                    .unwrap_or(self.contents.len()),
            );
            self.contents[start..end].strip_newline()
        } else {
            ""
        }
    }
    fn token_location(&self, range: RangeInclusive<&LexToken>) -> Location {
        Location {
            file_name: self.file_name.clone(),
            span: Some(
                self.offset_to_point(range.start().pos.start)
                    ..self.offset_to_point(range.end().pos.end),
            ),
            omit_underlines: false,
        }
    }
}

impl Default for SyntaxFile {
    fn default() -> Self {
        Self::new(String::new(), None, UTF_8)
    }
}

trait StripNewline {
    fn strip_newline(&self) -> &str;
}

impl StripNewline for str {
    fn strip_newline(&self) -> &str {
        self.strip_suffix("\r\n")
            .unwrap_or(self.strip_suffix('\n').unwrap_or(self))
    }
}

fn ellipsize(s: &str) -> Cow<'_, str> {
    if s.width() > 64 {
        let mut out = String::new();
        let mut width = 0;
        for c in s.chars() {
            out.push(c);
            width += c.width().unwrap_or(0);
            if width > 64 {
                break;
            }
        }
        out.push_str("...");
        Cow::from(out)
    } else {
        Cow::from(s)
    }
}

/// A token in a [`Source`].
///
/// This relates a token back to where it was read, which allows for better
/// error reporting.
pub struct LexToken {
    /// The token.
    pub token: Token,

    /// The source file that the token was read from.
    pub file: Arc<SyntaxFile>,

    /// For a token obtained through the lexer in an ordinary way, this is the
    /// location of the token in the [`Source`]'s buffer.
    ///
    /// For a token produced through macro expansion, this is the entire macro
    /// call.
    pos: Range<usize>,

    /// For a token obtained through macro expansion, the part of the macro
    /// expansion that represents this token.
    ///
    /// For a token obtained through the lexer in an ordinary way, this is
    /// `None`.
    macro_rep: Option<MacroRepresentation>,
}

impl Debug for LexToken {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        self.token.fmt(f)
    }
}

#[allow(dead_code)]
struct LexError {
    error: ScanError,
    pos: Range<usize>,
}

impl Borrow<Token> for LexToken {
    fn borrow(&self) -> &Token {
        &self.token
    }
}

impl LexToken {
    fn representation(&self) -> &str {
        &self.file.contents[self.pos.clone()]
    }
}

struct MacroRepresentation {
    /// An entire macro expansion.
    expansion: Arc<String>,

    /// The substring of `expansion` that represents a single token.
    pos: RangeInclusive<usize>,
}

/// A sequence of tokens.
pub struct Tokens {
    tokens: Vec<LexToken>,
}

impl Tokens {
    fn new(tokens: Vec<LexToken>) -> Self {
        assert!(matches!(tokens.last().unwrap().token, Token::End));
        Self { tokens }
    }
}

impl Debug for Tokens {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "Tokens {{ ")?;
        for (index, token) in self.tokens.iter().enumerate() {
            if index > 0 {
                write!(f, ", ")?;
            }
            write!(f, "{:?}", token.representation())?;
        }
        write!(f, " }}")
    }
}

/// An iterator for [TokenSlice].
pub struct TokenSliceIter<'a> {
    slice: &'a TokenSlice,
    rest: Range<usize>,
}

impl<'a> TokenSliceIter<'a> {
    /// Creates a new iterator for `slice`.
    pub fn new(slice: &'a TokenSlice) -> Self {
        Self {
            slice,
            rest: slice.range.clone(),
        }
    }

    /// Returns the tokens not yet visited by the iterator.
    pub fn remainder(&self) -> TokenSlice {
        TokenSlice {
            backing: self.slice.backing.clone(),
            range: self.rest.clone(),
        }
    }
}

impl<'a> Iterator for TokenSliceIter<'a> {
    type Item = &'a LexToken;

    fn next(&mut self) -> Option<Self::Item> {
        if self.rest.is_empty() {
            None
        } else {
            self.rest.start += 1;
            Some(&self.slice.backing.tokens[self.rest.start - 1])
        }
    }
}

/// A subrange of tokens inside [Tokens].
#[derive(Clone)]
pub struct TokenSlice {
    backing: Rc<Tokens>,
    range: Range<usize>,
}

impl Debug for TokenSlice {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "TokenSlice {{ ")?;
        for (index, token) in self.tokens().iter().enumerate() {
            if index > 0 {
                write!(f, ", ")?;
            }
            write!(f, "{:?}", token.representation())?;
        }
        write!(f, " }}")
    }
}

#[allow(missing_docs)]
impl TokenSlice {
    /// Create a new slice that initially contains all of `backing`.
    pub fn new(backing: Rc<Tokens>) -> Self {
        let range = 0..backing.tokens.len() - 1;
        Self { backing, range }
    }

    fn tokens(&self) -> &[LexToken] {
        &self.backing.tokens[self.range.clone()]
    }
    /// Returns the token with the given `index`, or `None` if `index` is out of
    /// range.
    pub fn get_token(&self, index: usize) -> Option<&Token> {
        self.get(index).map(|token| &token.token)
    }

    /// Returns the [LexToken] with the given `index`, or `None` if `index` is
    /// out of range.
    pub fn get(&self, index: usize) -> Option<&LexToken> {
        self.tokens().get(index)
    }

    /// Returns an error with the given `text`, citing these tokens.
    pub fn error<S>(&self, text: S) -> Diagnostic
    where
        S: ToString,
    {
        self.diagnostic(Severity::Error, text.to_string())
    }

    /// Returns a warning with the given `text`, citing these tokens.
    pub fn warning<S>(&self, text: S) -> Diagnostic
    where
        S: ToString,
    {
        self.diagnostic(Severity::Warning, text.to_string())
    }

    pub fn subslice(&self, range: Range<usize>) -> Self {
        debug_assert!(range.start <= range.end);
        debug_assert!(range.end <= self.len());
        let start = self.range.start + range.start;
        let end = start + range.len();
        Self {
            backing: self.backing.clone(),
            range: start..end,
        }
    }

    pub fn first(&self) -> &LexToken {
        &self.backing.tokens[self.range.start]
    }
    fn last(&self) -> &LexToken {
        &self.backing.tokens[self.range.end - 1]
    }
    pub fn end(&self) -> Self {
        self.subslice(self.len()..self.len())
    }

    fn file(&self) -> Option<&Arc<SyntaxFile>> {
        let first = self.first();
        let last = self.last();
        if Arc::ptr_eq(&first.file, &last.file) {
            Some(&first.file)
        } else {
            None
        }
    }

    pub fn len(&self) -> usize {
        self.tokens().len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn iter(&self) -> TokenSliceIter<'_> {
        TokenSliceIter::new(self)
    }

    /// If the tokens contains a macro call, this returns the raw
    /// syntax for the macro call (not for the expansion) and for any other
    /// tokens included in that range.  The syntax is encoded in UTF-8 and in
    /// the original form supplied to the lexer so that, for example, it may
    /// include comments, spaces, and new-lines if it spans multiple tokens.
    ///
    /// Returns `None` if the token range doesn't include a macro call.
    fn get_macro_call(&self) -> Option<&str> {
        if self.iter().any(|token| token.macro_rep.is_some()) {
            let token0 = self.first();
            let token1 = self.last();
            if let Some(file) = self.file() {
                let start = token0.pos.start;
                let end = token1.pos.end;
                if start < end {
                    return Some(&file.contents[start..end]);
                }
            }
        }
        None
    }

    fn location(&self) -> Location {
        if let Some(file) = self.file() {
            file.token_location(
                &self.backing.tokens[self.range.start]..=&self.backing.tokens[self.range.end],
            )
        } else {
            // XXX support non-contiguous locations?
            let first = self.first();
            first.file.token_location(self.first()..=self.first())
        }
    }

    pub fn skip_to(&self, token: &Token) -> Self {
        self.skip_until(|t| t == token)
    }

    pub fn skip_until<F>(&self, f: F) -> Self
    where
        F: Fn(&Token) -> bool,
    {
        for (index, token) in self.iter().enumerate() {
            if f(&token.token) {
                return self.subslice(index..self.len());
            }
        }
        self.end()
    }

    pub fn skip(&self, token: &Token) -> Option<Self> {
        self.skip_if(|t| t == token)
    }

    pub fn skip_if<F>(&self, f: F) -> Option<Self>
    where
        F: Fn(&Token) -> bool,
    {
        let mut iter = self.iter();
        if iter.next().is_some_and(|token| f(&token.token)) {
            Some(iter.remainder())
        } else {
            None
        }
    }

    pub fn skip_keyword(&self, keyword: &str) -> Option<Self> {
        self.skip_if(|token| token.matches_keyword(keyword))
    }

    pub fn skip_syntax(&self, syntax: &str) -> Option<Self> {
        let mut input = self.clone();
        for token in StringScanner::new(syntax, Syntax::Interactive, true).unwrapped() {
            input = input.skip(&token)?;
        }
        Some(input)
    }

    pub fn diagnostic(&self, severity: Severity, text: String) -> Diagnostic {
        let mut s = String::new();
        if let Some(call) = self.get_macro_call() {
            write!(&mut s, "In syntax expanded from `{}`: ", ellipsize(call)).unwrap();
        }

        if !text.is_empty() {
            s.push_str(&text);
        } else {
            s.push_str("Syntax error.");
        }

        if !s.ends_with('.') {
            s.push('.');
        }

        let location = self.location();
        let mut source = Vec::new();
        if let Some(Range {
            start: Point { line: l0, .. },
            end: Point { line: l1, .. },
        }) = location.span
        {
            if let Some(file) = self.file() {
                let lines = if l1 - l0 > 3 {
                    vec![l0, l0 + 1, l1]
                } else {
                    (l0..=l1).collect()
                };
                for line_number in lines {
                    source.push((line_number, file.get_line(line_number).to_string()));
                }
            }
        }

        Diagnostic {
            category: Category::Syntax,
            severity,
            location,
            source,
            stack: Vec::new(),
            command_name: None, // XXX
            text: s,
        }
    }

    pub fn split<F>(&self, predicate: F) -> impl Iterator<Item = Self> + use<'_, F>
    where
        F: Fn(&LexToken) -> bool,
    {
        self.tokens().split(predicate).map(move |slice| {
            // SAFETY: `slice` is inside `self.tokens`.
            let start_ofs = unsafe { slice.as_ptr().offset_from(self.tokens().as_ptr()) } as usize;
            self.subslice(start_ofs..start_ofs + slice.len())
        })
    }
}

/// A source of tokens read from a [SyntaxFile].
pub struct Source {
    file: Arc<SyntaxFile>,
    segmenter: Segmenter,
    seg_pos: usize,
    lookahead: VecDeque<LexToken>,
}

impl Source {
    /// Creates a new `Source` reading from `file`, using the default [Syntax].
    pub fn new_default(file: &Arc<SyntaxFile>) -> Self {
        Self::new(file, Syntax::default())
    }

    /// Creates a new `Source` reading from `file` using `syntax`.
    pub fn new(file: &Arc<SyntaxFile>, syntax: Syntax) -> Self {
        Self {
            file: file.clone(),
            segmenter: Segmenter::new(syntax, false),
            seg_pos: 0,
            lookahead: VecDeque::new(),
        }
    }

    /// Reads and returns a whole command from this source, expanding the given
    /// `macros` as it reads.
    pub fn read_command(&mut self, macros: &MacroSet) -> Option<Tokens> {
        loop {
            if let Some(end) = self
                .lookahead
                .iter()
                .position(|token| token.token == Token::End)
            {
                return Some(Tokens::new(self.lookahead.drain(..=end).collect()));
            }
            if !self.read_lookahead(macros) {
                if self.lookahead.is_empty() {
                    return None;
                }
                let len = self.file.contents.len();
                self.lookahead.push_back(LexToken {
                    token: Token::End,
                    file: self.file.clone(),
                    pos: len..len,
                    macro_rep: None,
                });
            }
        }
    }

    fn read_lookahead(&mut self, macros: &MacroSet) -> bool {
        let mut errors = Vec::new();
        let mut pp = VecDeque::new();
        while let Some((seg_len, seg_type)) = self
            .segmenter
            .push(&self.file.contents[self.seg_pos..], true)
            .unwrap()
        {
            let pos = self.seg_pos..self.seg_pos + seg_len;
            self.seg_pos += seg_len;

            match seg_type.to_token(&self.file.contents[pos.clone()]) {
                None => (),
                Some(Ok(token)) => {
                    let end = token == Token::End;
                    pp.push_back(LexToken {
                        file: self.file.clone(),
                        token,
                        pos,
                        macro_rep: None,
                    });
                    if end {
                        break;
                    }
                }
                Some(Err(error)) => errors.push(LexError { error, pos }),
            }
        }
        // XXX report errors
        if pp.is_empty() {
            return false;
        }

        let mut merge = if !Settings::global().macros.expand || macros.is_empty() {
            take(&mut pp)
        } else {
            let mut merge = VecDeque::new();
            while !pp.is_empty() {
                self.expand_macro(macros, &mut pp, &mut merge);
            }
            merge
        };

        while let Ok(Some(result)) =
            merge_tokens(|index| Ok(merge.get(index).map(|token| &token.token)))
        {
            match result {
                MergeAction::Copy => self.lookahead.push_back(merge.pop_front().unwrap()),
                MergeAction::Expand { n, token } => {
                    let first = &merge[0];
                    let last = &merge[n - 1];
                    self.lookahead.push_back(LexToken {
                        file: self.file.clone(),
                        token,
                        pos: first.pos.start..last.pos.end,
                        macro_rep: match (&first.macro_rep, &last.macro_rep) {
                            (Some(a), Some(b)) if Arc::ptr_eq(&a.expansion, &b.expansion) => {
                                Some(MacroRepresentation {
                                    expansion: a.expansion.clone(),
                                    pos: *a.pos.start()..=*b.pos.end(),
                                })
                            }
                            _ => None,
                        },
                    });
                    merge.drain(..n);
                }
            }
        }
        true
    }

    fn expand_macro(
        &self,
        macros: &MacroSet,
        src: &mut VecDeque<LexToken>,
        dst: &mut VecDeque<LexToken>,
    ) {
        // Now pass tokens one-by-one to the macro expander.
        let Some(mut parser) = Parser::new(macros, &src[0].token) else {
            // Common case where there is no macro to expand.
            dst.push_back(src.pop_front().unwrap());
            return;
        };
        for token in src.range(1..) {
            if parser.push(&token.token, &self.file.contents[token.pos.clone()], &|e| {
                println!("{e:?}")
            }) == ParseStatus::Complete
            {
                break;
            }
        }
        let call = parser.finish();
        if call.is_empty() {
            // False alarm: no macro to expand after all.
            dst.push_back(src.pop_front().unwrap());
            return;
        }

        // Expand the tokens.
        let c0 = &src[0];
        let c1 = &src[call.len() - 1];
        let mut expansion = Vec::new();
        call.expand(
            self.segmenter.syntax(),
            self.file.token_location(c0..=c1),
            &mut expansion,
            |e| println!("{e:?}"),
        );

        if Settings::global().macros.print_expansions {
            // XXX
        }

        // Append the macro expansion tokens to the lookahead.
        let mut macro_rep = String::new();
        let mut pos = Vec::with_capacity(expansion.len());
        for [prefix, token] in macro_tokens_to_syntax(expansion.as_slice()) {
            macro_rep.push_str(prefix);
            let len = macro_rep.len();
            pos.push(len..=len + token.len() - 1);
        }
        let macro_rep = Arc::new(macro_rep);
        for (index, token) in expansion.into_iter().enumerate() {
            let lt = LexToken {
                file: self.file.clone(),
                token: token.token,
                pos: c0.pos.start..c1.pos.end,
                macro_rep: Some(MacroRepresentation {
                    expansion: Arc::clone(&macro_rep),
                    pos: pos[index].clone(),
                }),
            };
            dst.push_back(lt);
        }
        src.drain(..call.len());
    }
}

#[cfg(test)]
mod new_lexer_tests {
    use std::sync::Arc;

    use encoding_rs::UTF_8;

    use crate::macros::MacroSet;

    use super::{Source, SyntaxFile};

    #[test]
    fn test() {
        let code = r#"DATA LIST LIST /A * B * X * Y * .
BEGIN DATA.
2 3 4 5
END DATA.

CROSSTABS VARIABLES X (1,7) Y (1,7) /TABLES X BY Y.
"#;
        let file = Arc::new(SyntaxFile::new(
            String::from(code),
            Some(String::from("crosstabs.sps")),
            UTF_8,
        ));
        let mut source = Source::new_default(&file);
        while let Some(tokens) = source.read_command(&MacroSet::new()) {
            println!("{tokens:?}");
        }
    }
}
