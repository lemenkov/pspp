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

use crate::prompt::PromptStyle;

use super::{Segment, Segmenter, Syntax};

fn push_segment(
    segmenter: &mut Segmenter,
    input: &str,
    one_byte: bool,
) -> Option<(usize, Segment)> {
    if one_byte {
        for len in input.char_indices().map(|(pos, _c)| pos) {
            if let Ok(result) = segmenter.push(&input[..len], false) {
                return result;
            }
        }
    }
    segmenter.push(input, true).unwrap()
}

fn _check_segmentation(
    mut input: &str,
    mode: Syntax,
    expect_segments: &[(Segment, &str)],
    expect_prompts: &[PromptStyle],
    one_byte: bool,
) {
    let mut segments = Vec::with_capacity(expect_segments.len());
    let mut prompts = Vec::new();
    let mut segmenter = Segmenter::new(mode, false);
    while let Some((seg_len, seg_type)) = push_segment(&mut segmenter, input, one_byte) {
        let (token, rest) = input.split_at(seg_len);
        segments.push((seg_type, token));
        if let Segment::Newline = seg_type {
            prompts.push(segmenter.prompt());
        }
        input = rest;
    }

    if segments != expect_segments {
        eprintln!("segments differ from expected:");
        let difference = diff::slice(expect_segments, &segments);
        for result in difference {
            match result {
                diff::Result::Left(left) => eprintln!("-{left:?}"),
                diff::Result::Both(left, _right) => eprintln!(" {left:?}"),
                diff::Result::Right(right) => eprintln!("+{right:?}"),
            }
        }
        panic!();
    }

    if prompts != expect_prompts {
        eprintln!("prompts differ from expected:");
        let difference = diff::slice(expect_prompts, &prompts);
        for result in difference {
            match result {
                diff::Result::Left(left) => eprintln!("-{left:?}"),
                diff::Result::Both(left, _right) => eprintln!(" {left:?}"),
                diff::Result::Right(right) => eprintln!("+{right:?}"),
            }
        }
        panic!();
    }
}

fn check_segmentation(
    input: &str,
    mode: Syntax,
    expect_segments: &[(Segment, &str)],
    expect_prompts: &[PromptStyle],
) {
    for (one_byte, one_byte_name) in [(false, "full-string"), (true, "byte-by-byte")] {
        println!("running {one_byte_name} segmentation test with LF newlines...");
        _check_segmentation(input, mode, expect_segments, expect_prompts, one_byte);

        println!("running {one_byte_name} segmentation test with CRLF newlines...");
        _check_segmentation(
            &input.replace('\n', "\r\n"),
            mode,
            &expect_segments
                .iter()
                .map(|(segment, s)| match *segment {
                    Segment::Newline => (Segment::Newline, "\r\n"),
                    _ => (*segment, *s),
                })
                .collect::<Vec<_>>(),
            expect_prompts,
            one_byte,
        );

        if let Some(input) = input.strip_suffix('\n') {
            println!("running {one_byte_name} segmentation test without final newline...");
            let mut expect_segments: Vec<_> = expect_segments.to_vec();
            assert_eq!(expect_segments.pop(), Some((Segment::Newline, "\n")));
            while let Some((Segment::SeparateCommands | Segment::EndCommand, "")) =
                expect_segments.last()
            {
                expect_segments.pop();
            }
            _check_segmentation(
                input,
                mode,
                &expect_segments,
                &expect_prompts[..expect_prompts.len() - 1],
                one_byte,
            );
        }
    }
}

#[allow(dead_code)]
fn print_segmentation(mut input: &str) {
    let mut segmenter = Segmenter::new(Syntax::Interactive, false);
    while let Some((seg_len, seg_type)) = segmenter.push(input, true).unwrap() {
        let (token, rest) = input.split_at(seg_len);
        print!("{seg_type:?} {token:?}");
        if let Segment::Newline = seg_type {
            print!(" ({:?})", segmenter.prompt())
        }
        println!();
        input = rest;
    }
}

#[test]
fn test_identifiers() {
    check_segmentation(
        r#"a ab abc abcd !abcd
A AB ABC ABCD !ABCD
aB aBC aBcD !aBcD
$x $y $z !$z
grève Ângstrom poté
#a #b #c ## #d !#d
@efg @ @@. @#@ !@ 
## # #12345 #.#
f@#_.#6
GhIjK
.x 1y _z
!abc abc!
"#,
        Syntax::Auto,
        &[
            (Segment::Identifier, "a"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ab"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "abc"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "abcd"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!abcd"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "A"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "AB"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ABC"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ABCD"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!ABCD"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "aB"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "aBC"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "aBcD"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!aBcD"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "$x"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "$y"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "$z"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!$z"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "grève"),
            (Segment::Spaces, "\u{00a0}"),
            (Segment::Identifier, "Ângstrom"),
            (Segment::Spaces, "\u{00a0}"),
            (Segment::Identifier, "poté"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "#a"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#b"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#c"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "##"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#d"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!#d"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "@efg"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "@"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "@@."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "@#@"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "!@"),
            (Segment::Spaces, " "),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "##"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#12345"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#.#"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "f@#_.#6"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "GhIjK"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Identifier, "x"),
            (Segment::Spaces, " "),
            (Segment::Number, "1"),
            (Segment::Identifier, "y"),
            (Segment::Spaces, " "),
            (Segment::Punct, "_"),
            (Segment::Identifier, "z"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "!abc"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "abc"),
            (Segment::Punct, "!"),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
        ],
    );
}

#[test]
fn test_identifiers_ending_in_dot() {
    check_segmentation(
        r#"abcd. abcd.
ABCD. ABCD.
aBcD. aBcD. 
$y. $z. あいうえお.
#c. #d..
@@. @@....
#.#.
#abcd.
.
. 
LMNOP. 
QRSTUV./* end of line comment */
qrstuv. /* end of line comment */
QrStUv./* end of line comment */ 
wxyz./* unterminated end of line comment
WXYZ. /* unterminated end of line comment
WxYz./* unterminated end of line comment 
"#,
        Syntax::Auto,
        &[
            (Segment::Identifier, "abcd."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "abcd"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "ABCD."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ABCD"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "aBcD."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "aBcD"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "$y."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "$z."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "あいうえお"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "#c."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#d."),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "@@."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "@@..."),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "#.#"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "#abcd"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "LMNOP"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "QRSTUV"),
            (Segment::EndCommand, "."),
            (Segment::Comment, "/* end of line comment */"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "qrstuv"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* end of line comment */"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "QrStUv"),
            (Segment::EndCommand, "."),
            (Segment::Comment, "/* end of line comment */"),
            (Segment::Spaces, " "),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "wxyz"),
            (Segment::EndCommand, "."),
            (Segment::Comment, "/* unterminated end of line comment"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "WXYZ"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* unterminated end of line comment"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "WxYz"),
            (Segment::EndCommand, "."),
            (Segment::Comment, "/* unterminated end of line comment "),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_reserved_words() {
    check_segmentation(
        r#"and or not eq ge gt le lt ne all by to with
AND OR NOT EQ GE GT LE LT NE ALL BY TO WITH
andx orx notx eqx gex gtx lex ltx nex allx byx tox withx
and. with.
"#,
        Syntax::Auto,
        &[
            (Segment::Identifier, "and"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "or"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "not"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "eq"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ge"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "gt"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "le"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "lt"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ne"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "all"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "by"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "to"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "with"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "AND"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "OR"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "NOT"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "EQ"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "GE"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "GT"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "LE"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "LT"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "NE"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ALL"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "BY"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "TO"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "WITH"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "andx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "orx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "notx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "eqx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "gex"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "gtx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "lex"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ltx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "nex"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "allx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "byx"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "tox"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "withx"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "and."),
            (Segment::Spaces, " "),
            (Segment::Identifier, "with"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_punctuation() {
    check_segmentation(
        r#"~ & | = >= > <= < ~= <> ( ) , - + * / [ ] **
~&|=>=><=<~=<>(),-+*/[]**!*
% : ; ? _ ` { } ~ !*
"#,
        Syntax::Auto,
        &[
            (Segment::Punct, "~"),
            (Segment::Spaces, " "),
            (Segment::Punct, "&"),
            (Segment::Spaces, " "),
            (Segment::Punct, "|"),
            (Segment::Spaces, " "),
            (Segment::Punct, "="),
            (Segment::Spaces, " "),
            (Segment::Punct, ">="),
            (Segment::Spaces, " "),
            (Segment::Punct, ">"),
            (Segment::Spaces, " "),
            (Segment::Punct, "<="),
            (Segment::Spaces, " "),
            (Segment::Punct, "<"),
            (Segment::Spaces, " "),
            (Segment::Punct, "~="),
            (Segment::Spaces, " "),
            (Segment::Punct, "<>"),
            (Segment::Spaces, " "),
            (Segment::Punct, "("),
            (Segment::Spaces, " "),
            (Segment::Punct, ")"),
            (Segment::Spaces, " "),
            (Segment::Punct, ","),
            (Segment::Spaces, " "),
            (Segment::Punct, "-"),
            (Segment::Spaces, " "),
            (Segment::Punct, "+"),
            (Segment::Spaces, " "),
            (Segment::Punct, "*"),
            (Segment::Spaces, " "),
            (Segment::Punct, "/"),
            (Segment::Spaces, " "),
            (Segment::Punct, "["),
            (Segment::Spaces, " "),
            (Segment::Punct, "]"),
            (Segment::Spaces, " "),
            (Segment::Punct, "**"),
            (Segment::Newline, "\n"),
            (Segment::Punct, "~"),
            (Segment::Punct, "&"),
            (Segment::Punct, "|"),
            (Segment::Punct, "="),
            (Segment::Punct, ">="),
            (Segment::Punct, ">"),
            (Segment::Punct, "<="),
            (Segment::Punct, "<"),
            (Segment::Punct, "~="),
            (Segment::Punct, "<>"),
            (Segment::Punct, "("),
            (Segment::Punct, ")"),
            (Segment::Punct, ","),
            (Segment::Punct, "-"),
            (Segment::Punct, "+"),
            (Segment::Punct, "*"),
            (Segment::Punct, "/"),
            (Segment::Punct, "["),
            (Segment::Punct, "]"),
            (Segment::Punct, "**"),
            (Segment::Punct, "!*"),
            (Segment::Newline, "\n"),
            (Segment::Punct, "%"),
            (Segment::Spaces, " "),
            (Segment::Punct, ":"),
            (Segment::Spaces, " "),
            (Segment::Punct, ";"),
            (Segment::Spaces, " "),
            (Segment::Punct, "?"),
            (Segment::Spaces, " "),
            (Segment::Punct, "_"),
            (Segment::Spaces, " "),
            (Segment::Punct, "`"),
            (Segment::Spaces, " "),
            (Segment::Punct, "{"),
            (Segment::Spaces, " "),
            (Segment::Punct, "}"),
            (Segment::Spaces, " "),
            (Segment::Punct, "~"),
            (Segment::Spaces, " "),
            (Segment::Punct, "!*"),
            (Segment::Newline, "\n"),
        ],
        &[PromptStyle::Later, PromptStyle::Later, PromptStyle::Later],
    );
}

#[test]
fn test_positive_numbers() {
    check_segmentation(
        r#"0 1 01 001. 1.
123. /* comment 1 */ /* comment 2 */
.1 0.1 00.1 00.10
5e1 6E-1 7e+1 6E+01 6e-03
.3E1 .4e-1 .5E+1 .6e+01 .7E-03
1.23e1 45.6E-1 78.9e+1 99.9E+01 11.2e-03
. 1e e1 1e+ 1e- 1.
"#,
        Syntax::Auto,
        &[
            (Segment::Number, "0"),
            (Segment::Spaces, " "),
            (Segment::Number, "1"),
            (Segment::Spaces, " "),
            (Segment::Number, "01"),
            (Segment::Spaces, " "),
            (Segment::Number, "001."),
            (Segment::Spaces, " "),
            (Segment::Number, "1"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Number, "123"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* comment 1 */"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* comment 2 */"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Number, "1"),
            (Segment::Spaces, " "),
            (Segment::Number, "0.1"),
            (Segment::Spaces, " "),
            (Segment::Number, "00.1"),
            (Segment::Spaces, " "),
            (Segment::Number, "00.10"),
            (Segment::Newline, "\n"),
            (Segment::Number, "5e1"),
            (Segment::Spaces, " "),
            (Segment::Number, "6E-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "7e+1"),
            (Segment::Spaces, " "),
            (Segment::Number, "6E+01"),
            (Segment::Spaces, " "),
            (Segment::Number, "6e-03"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Number, "3E1"),
            (Segment::Spaces, " "),
            (Segment::Number, ".4e-1"),
            (Segment::Spaces, " "),
            (Segment::Number, ".5E+1"),
            (Segment::Spaces, " "),
            (Segment::Number, ".6e+01"),
            (Segment::Spaces, " "),
            (Segment::Number, ".7E-03"),
            (Segment::Newline, "\n"),
            (Segment::Number, "1.23e1"),
            (Segment::Spaces, " "),
            (Segment::Number, "45.6E-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "78.9e+1"),
            (Segment::Spaces, " "),
            (Segment::Number, "99.9E+01"),
            (Segment::Spaces, " "),
            (Segment::Number, "11.2e-03"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "1e"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "e1"),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "1e+"),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "1e-"),
            (Segment::Spaces, " "),
            (Segment::Number, "1"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_negative_numbers() {
    check_segmentation(
        r#" -0 -1 -01 -001. -1.
 -123. /* comment 1 */ /* comment 2 */
 -.1 -0.1 -00.1 -00.10
 -5e1 -6E-1 -7e+1 -6E+01 -6e-03
 -.3E1 -.4e-1 -.5E+1 -.6e+01 -.7E-03
 -1.23e1 -45.6E-1 -78.9e+1 -99.9E+01 -11.2e-03
 -/**/1
 -. -1e -e1 -1e+ -1e- -1.
"#,
        Syntax::Auto,
        &[
            (Segment::Spaces, " "),
            (Segment::Number, "-0"),
            (Segment::Spaces, " "),
            (Segment::Number, "-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-01"),
            (Segment::Spaces, " "),
            (Segment::Number, "-001."),
            (Segment::Spaces, " "),
            (Segment::Number, "-1"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Number, "-123"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* comment 1 */"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* comment 2 */"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-0.1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-00.1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-00.10"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Number, "-5e1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-6E-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-7e+1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-6E+01"),
            (Segment::Spaces, " "),
            (Segment::Number, "-6e-03"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.3E1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.4e-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.5E+1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.6e+01"),
            (Segment::Spaces, " "),
            (Segment::Number, "-.7E-03"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Number, "-1.23e1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-45.6E-1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-78.9e+1"),
            (Segment::Spaces, " "),
            (Segment::Number, "-99.9E+01"),
            (Segment::Spaces, " "),
            (Segment::Number, "-11.2e-03"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Punct, "-"),
            (Segment::Comment, "/**/"),
            (Segment::Number, "1"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Punct, "-"),
            (Segment::Punct, "."),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "-1e"),
            (Segment::Spaces, " "),
            (Segment::Punct, "-"),
            (Segment::Identifier, "e1"),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "-1e+"),
            (Segment::Spaces, " "),
            (Segment::ExpectedExponent, "-1e-"),
            (Segment::Spaces, " "),
            (Segment::Number, "-1"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_strings() {
    check_segmentation(
        r#"'x' "y" 'abc'
'Don''t' "Can't" 'Won''t'
"""quoted""" '"quoted"'
'' ""
'missing end quote
"missing double quote
x"4142" X'5152'
u'fffd' U"041"
+ new command
+ /* comment */ 'string continuation'
+ /* also a punctuator on blank line
- 'new command'
"#,
        Syntax::Auto,
        &[
            (Segment::QuotedString, "'x'"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "\"y\""),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'abc'"),
            (Segment::Newline, "\n"),
            (Segment::QuotedString, "'Don''t'"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "\"Can't\""),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'Won''t'"),
            (Segment::Newline, "\n"),
            (Segment::QuotedString, "\"\"\"quoted\"\"\""),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'\"quoted\"'"),
            (Segment::Newline, "\n"),
            (Segment::QuotedString, "''"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "\"\""),
            (Segment::Newline, "\n"),
            (Segment::ExpectedQuote, "'missing end quote"),
            (Segment::Newline, "\n"),
            (Segment::ExpectedQuote, "\"missing double quote"),
            (Segment::Newline, "\n"),
            (Segment::HexString, "x\"4142\""),
            (Segment::Spaces, " "),
            (Segment::HexString, "X'5152'"),
            (Segment::Newline, "\n"),
            (Segment::UnicodeString, "u'fffd'"),
            (Segment::Spaces, " "),
            (Segment::UnicodeString, "U\"041\""),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "+"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "new"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::Punct, "+"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* comment */"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'string continuation'"),
            (Segment::Newline, "\n"),
            (Segment::Punct, "+"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/* also a punctuator on blank line"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "-"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'new command'"),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
        ],
    );
}

#[test]
fn test_shbang() {
    check_segmentation(
        r#"#! /usr/bin/pspp
title my title.
#! /usr/bin/pspp
"#,
        Syntax::Interactive,
        &[
            (Segment::Shbang, "#! /usr/bin/pspp"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "title"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "my"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "title"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "#"),
            (Segment::Punct, "!"),
            (Segment::Spaces, " "),
            (Segment::Punct, "/"),
            (Segment::Identifier, "usr"),
            (Segment::Punct, "/"),
            (Segment::Identifier, "bin"),
            (Segment::Punct, "/"),
            (Segment::Identifier, "pspp"),
            (Segment::Newline, "\n"),
        ],
        &[PromptStyle::First, PromptStyle::First, PromptStyle::Later],
    );
}

#[test]
fn test_comment_command() {
    check_segmentation(
        r#"* Comment commands "don't
have to contain valid tokens.

** Check ambiguity with ** token.
****************.

comment keyword works too.
COMM also.
com is ambiguous with COMPUTE.

   * Comment need not start at left margin.

* Comment ends with blank line

next command.

"#,
        Syntax::Interactive,
        &[
            (Segment::CommentCommand, "* Comment commands \"don't"),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "have to contain valid tokens"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "** Check ambiguity with ** token"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "****************"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "comment keyword works too"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "COMM also"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "com"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "is"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "ambiguous"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "with"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "COMPUTE"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "   "),
            (
                Segment::CommentCommand,
                "* Comment need not start at left margin",
            ),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::CommentCommand, "* Comment ends with blank line"),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "next"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Comment,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Comment,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_document_command() {
    check_segmentation(
        r#"DOCUMENT one line.
DOC more
    than
        one
            line.
docu
first.paragraph
isn't parsed as tokens

second paragraph.
"#,
        Syntax::Interactive,
        &[
            (Segment::StartDocument, ""),
            (Segment::Document, "DOCUMENT one line."),
            (Segment::EndCommand, ""),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::StartDocument, ""),
            (Segment::Document, "DOC more"),
            (Segment::Newline, "\n"),
            (Segment::Document, "    than"),
            (Segment::Newline, "\n"),
            (Segment::Document, "        one"),
            (Segment::Newline, "\n"),
            (Segment::Document, "            line."),
            (Segment::EndCommand, ""),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::StartDocument, ""),
            (Segment::Document, "docu"),
            (Segment::Newline, "\n"),
            (Segment::Document, "first.paragraph"),
            (Segment::Newline, "\n"),
            (Segment::Document, "isn't parsed as tokens"),
            (Segment::Newline, "\n"),
            (Segment::Document, ""),
            (Segment::Newline, "\n"),
            (Segment::Document, "second paragraph."),
            (Segment::EndCommand, ""),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::First,
            PromptStyle::Document,
            PromptStyle::Document,
            PromptStyle::Document,
            PromptStyle::First,
            PromptStyle::Document,
            PromptStyle::Document,
            PromptStyle::Document,
            PromptStyle::Document,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_file_label_command() {
    check_segmentation(
        r#"FIL label isn't quoted.
FILE
  lab 'is quoted'.
FILE /*
/**/  lab not quoted here either

"#,
        Syntax::Interactive,
        &[
            (Segment::Identifier, "FIL"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "label"),
            (Segment::Spaces, " "),
            (Segment::UnquotedString, "isn't quoted"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "FILE"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "lab"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "'is quoted'"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "FILE"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/*"),
            (Segment::Newline, "\n"),
            (Segment::Comment, "/**/"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "lab"),
            (Segment::Spaces, " "),
            (Segment::UnquotedString, "not quoted here either"),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_begin_data() {
    check_segmentation(
        r#"begin data.
end data.

begin data. /*
123
xxx
end data.

BEG /**/ DAT /*
5 6 7 /* x

end  data
end data
.

begin
 data.
data
end data.

begin data "xxx".
begin data 123.
not data
"#,
        Syntax::Interactive,
        &[
            (Segment::Identifier, "begin"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "begin"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, "/*"),
            (Segment::Newline, "\n"),
            (Segment::InlineData, "123"),
            (Segment::Newline, "\n"),
            (Segment::InlineData, "xxx"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "BEG"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/**/"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "DAT"),
            (Segment::Spaces, " "),
            (Segment::Comment, "/*"),
            (Segment::Newline, "\n"),
            (Segment::InlineData, "5 6 7 /* x"),
            (Segment::Newline, "\n"),
            (Segment::InlineData, ""),
            (Segment::Newline, "\n"),
            (Segment::InlineData, "end  data"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "begin"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::InlineData, "data"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "begin"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::Spaces, " "),
            (Segment::QuotedString, "\"xxx\""),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "begin"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::Spaces, " "),
            (Segment::Number, "123"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "not"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "data"),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Data,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::Data,
            PromptStyle::Data,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Later,
        ],
    );
}

#[test]
fn test_do_repeat() {
    check_segmentation(
        r#"do repeat x=a b c
          y=d e f.
  do repeat a=1 thru 5.
another command.
second command
+ third command.
end /* x */ /* y */ repeat print.
end
 repeat.
do
  repeat #a=1.
  inner command.
end repeat.
"#,
        Syntax::Interactive,
        &[
            (Segment::Identifier, "do"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "x"),
            (Segment::Punct, "="),
            (Segment::Identifier, "a"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "b"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "c"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "          "),
            (Segment::Identifier, "y"),
            (Segment::Punct, "="),
            (Segment::Identifier, "d"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "e"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "f"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "  do repeat a=1 thru 5."),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "another command."),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "second command"),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "+ third command."),
            (Segment::Newline, "\n"),
            (
                Segment::DoRepeatCommand,
                "end /* x */ /* y */ repeat print.",
            ),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "do"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "repeat"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#a"),
            (Segment::Punct, "="),
            (Segment::Number, "1"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "  inner command."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_do_repeat_overflow() {
    const N: usize = 257;
    let do_repeat: Vec<String> = (0..N)
        .map(|i| format!("do repeat v{i}={i} thru {}.\n", i + 5))
        .collect();
    let end_repeat: Vec<String> = (0..N)
        .rev()
        .map(|i| format!("end repeat. /* {i}\n"))
        .collect();

    let s: String = do_repeat
        .iter()
        .chain(end_repeat.iter())
        .map(|s| s.as_str())
        .collect();
    let mut expect_output = vec![
        (Segment::Identifier, "do"),
        (Segment::Spaces, " "),
        (Segment::Identifier, "repeat"),
        (Segment::Spaces, " "),
        (Segment::Identifier, "v0"),
        (Segment::Punct, "="),
        (Segment::Number, "0"),
        (Segment::Spaces, " "),
        (Segment::Identifier, "thru"),
        (Segment::Spaces, " "),
        (Segment::Number, "5"),
        (Segment::EndCommand, "."),
        (Segment::Newline, "\n"),
    ];
    for (i, line) in do_repeat.iter().enumerate().take(N).skip(1) {
        expect_output.push((Segment::DoRepeatCommand, line.trim_end()));
        if i >= 255 {
            expect_output.push((Segment::DoRepeatOverflow, ""));
        }
        expect_output.push((Segment::Newline, "\n"));
    }
    for line in &end_repeat[..254] {
        expect_output.push((Segment::DoRepeatCommand, line.trim_end()));
        expect_output.push((Segment::Newline, "\n"));
    }
    let comments: Vec<String> = (0..(N - 254)).rev().map(|i| format!("/* {i}")).collect();
    for comment in &comments {
        expect_output.extend([
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::EndCommand, "."),
            (Segment::Spaces, " "),
            (Segment::Comment, comment),
            (Segment::Newline, "\n"),
        ]);
    }

    let expect_prompts: Vec<_> = (0..N * 2 - 3)
        .map(|_| PromptStyle::DoRepeat)
        .chain([PromptStyle::First, PromptStyle::First, PromptStyle::First])
        .collect();
    check_segmentation(&s, Syntax::Interactive, &expect_output, &expect_prompts);
}

#[test]
fn test_do_repeat_batch() {
    check_segmentation(
        r#"do repeat x=a b c
          y=d e f
do repeat a=1 thru 5
another command
second command
+ third command
end /* x */ /* y */ repeat print
end
 repeat
do
  repeat #a=1

  inner command
end repeat
"#,
        Syntax::Batch,
        &[
            (Segment::Identifier, "do"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "x"),
            (Segment::Punct, "="),
            (Segment::Identifier, "a"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "b"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "c"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "          "),
            (Segment::Identifier, "y"),
            (Segment::Punct, "="),
            (Segment::Identifier, "d"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "e"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "f"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::DoRepeatCommand, "do repeat a=1 thru 5"),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "another command"),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "second command"),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "+ third command"),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "end /* x */ /* y */ repeat print"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::Identifier, "do"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "repeat"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "#a"),
            (Segment::Punct, "="),
            (Segment::Number, "1"),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::DoRepeatCommand, "  inner command"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "end"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "repeat"),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::DoRepeat,
            PromptStyle::DoRepeat,
            PromptStyle::Later,
        ],
    );
}

mod define {
    use crate::{
        lex::segment::{Segment, Syntax},
        prompt::PromptStyle,
    };

    use super::check_segmentation;

    #[test]
    fn test_simple() {
        check_segmentation(
            r#"define !macro1()
var1 var2 var3 "!enddefine"
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "var1 var2 var3 \"!enddefine\""),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define, PromptStyle::Define, PromptStyle::First],
        );
    }

    #[test]
    fn test_no_newline_after_parentheses() {
        check_segmentation(
            r#"define !macro1() var1 var2 var3 /* !enddefine
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::MacroBody, " var1 var2 var3 /* !enddefine"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define, PromptStyle::First],
        );
    }

    #[test]
    fn test_no_newline_before_enddefine() {
        check_segmentation(
            r#"define !macro1()
var1 var2 var3!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "var1 var2 var3"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define, PromptStyle::First],
        );
    }

    #[test]
    fn test_all_on_one_line() {
        check_segmentation(
            r#"define !macro1()var1 var2 var3!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::MacroBody, "var1 var2 var3"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::First],
        );
    }

    #[test]
    fn test_empty() {
        check_segmentation(
            r#"define !macro1()
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define, PromptStyle::First],
        );
    }

    #[test]
    fn test_blank_lines() {
        check_segmentation(
            r#"define !macro1()


!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, ""),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, ""),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[
                PromptStyle::Define,
                PromptStyle::Define,
                PromptStyle::Define,
                PromptStyle::First,
            ],
        );
    }

    #[test]
    fn test_arguments() {
        check_segmentation(
            r#"define !macro1(a(), b(), c())
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Identifier, "a"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Punct, ","),
                (Segment::Spaces, " "),
                (Segment::Identifier, "b"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Punct, ","),
                (Segment::Spaces, " "),
                (Segment::Identifier, "c"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define, PromptStyle::First],
        );
    }

    #[test]
    fn test_multiline_arguments() {
        check_segmentation(
            r#"define !macro1(
  a(), b(
  ),
  c()
)
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Newline, "\n"),
                (Segment::Spaces, "  "),
                (Segment::Identifier, "a"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Punct, ","),
                (Segment::Spaces, " "),
                (Segment::Identifier, "b"),
                (Segment::Punct, "("),
                (Segment::Newline, "\n"),
                (Segment::Spaces, "  "),
                (Segment::Punct, ")"),
                (Segment::Punct, ","),
                (Segment::Newline, "\n"),
                (Segment::Spaces, "  "),
                (Segment::Identifier, "c"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[
                PromptStyle::Later,
                PromptStyle::Later,
                PromptStyle::Later,
                PromptStyle::Later,
                PromptStyle::Define,
                PromptStyle::First,
            ],
        );
    }

    #[test]
    fn test_arguments_start_on_second_line() {
        check_segmentation(
            r#"define !macro1
(x,y,z
)
content 1
content 2
!enddefine.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Newline, "\n"),
                (Segment::Punct, "("),
                (Segment::Identifier, "x"),
                (Segment::Punct, ","),
                (Segment::Identifier, "y"),
                (Segment::Punct, ","),
                (Segment::Identifier, "z"),
                (Segment::Newline, "\n"),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "content 1"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "content 2"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "!enddefine"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[
                PromptStyle::Later,
                PromptStyle::Later,
                PromptStyle::Define,
                PromptStyle::Define,
                PromptStyle::Define,
                PromptStyle::First,
            ],
        );
    }

    #[test]
    fn test_early_end_of_command_1() {
        check_segmentation(
            r#"define !macro1.
data list /x 1.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "data"),
                (Segment::Spaces, " "),
                (Segment::Identifier, "list"),
                (Segment::Spaces, " "),
                (Segment::Punct, "/"),
                (Segment::Identifier, "x"),
                (Segment::Spaces, " "),
                (Segment::Number, "1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::First, PromptStyle::First],
        );
    }

    #[test]
    fn test_early_end_of_command_2() {
        check_segmentation(
            r#"define !macro1
x.
data list /x 1.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "x"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "data"),
                (Segment::Spaces, " "),
                (Segment::Identifier, "list"),
                (Segment::Spaces, " "),
                (Segment::Punct, "/"),
                (Segment::Identifier, "x"),
                (Segment::Spaces, " "),
                (Segment::Number, "1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Later, PromptStyle::First, PromptStyle::First],
        );
    }

    #[test]
    fn test_early_end_of_command_3() {
        check_segmentation(
            r#"define !macro1(.
x.
data list /x 1.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "x"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "data"),
                (Segment::Spaces, " "),
                (Segment::Identifier, "list"),
                (Segment::Spaces, " "),
                (Segment::Punct, "/"),
                (Segment::Identifier, "x"),
                (Segment::Spaces, " "),
                (Segment::Number, "1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::First, PromptStyle::First, PromptStyle::First],
        );
    }

    #[test]
    fn test_early_end_of_command_4() {
        // Notice the command terminator at the end of the `DEFINE` command,
        // which should not be there and ends it early.
        check_segmentation(
            r#"define !macro1.
data list /x 1.
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
                (Segment::Identifier, "data"),
                (Segment::Spaces, " "),
                (Segment::Identifier, "list"),
                (Segment::Spaces, " "),
                (Segment::Punct, "/"),
                (Segment::Identifier, "x"),
                (Segment::Spaces, " "),
                (Segment::Number, "1"),
                (Segment::EndCommand, "."),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::First, PromptStyle::First],
        );
    }

    #[test]
    fn test_missing_enddefine() {
        check_segmentation(
            r#"define !macro1()
content line 1
content line 2
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "content line 1"),
                (Segment::Newline, "\n"),
                (Segment::MacroBody, "content line 2"),
                (Segment::Newline, "\n"),
            ],
            &[
                PromptStyle::Define,
                PromptStyle::Define,
                PromptStyle::Define,
            ],
        );
    }

    #[test]
    fn test_missing_enddefine_2() {
        check_segmentation(
            r#"define !macro1()
"#,
            Syntax::Interactive,
            &[
                (Segment::Identifier, "define"),
                (Segment::Spaces, " "),
                (Segment::MacroName, "!macro1"),
                (Segment::Punct, "("),
                (Segment::Punct, ")"),
                (Segment::Newline, "\n"),
            ],
            &[PromptStyle::Define],
        );
    }
}

#[test]
fn test_batch_mode() {
    check_segmentation(
        r#"first command
     another line of first command
+  second command
third command

fourth command.
   fifth command.
"#,
        Syntax::Batch,
        &[
            (Segment::Identifier, "first"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "     "),
            (Segment::Identifier, "another"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "line"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "of"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "first"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "+"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "second"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::Identifier, "third"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "fourth"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "   "),
            (Segment::Identifier, "fifth"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
        ],
    );
}

#[test]
fn test_auto_mode() {
    check_segmentation(
        r#"command
     another line of command
2sls
+  another command
another line of second command
data list /x 1
aggregate.
print eject.
twostep cluster


fourth command.
   fifth command.
"#,
        Syntax::Auto,
        &[
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "     "),
            (Segment::Identifier, "another"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "line"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "of"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::Number, "2"),
            (Segment::Identifier, "sls"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, "+"),
            (Segment::Spaces, "  "),
            (Segment::Identifier, "another"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "another"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "line"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "of"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "second"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::Identifier, "data"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "list"),
            (Segment::Spaces, " "),
            (Segment::Punct, "/"),
            (Segment::Identifier, "x"),
            (Segment::Spaces, " "),
            (Segment::Number, "1"),
            (Segment::Newline, "\n"),
            (Segment::StartCommand, ""),
            (Segment::Identifier, "aggregate"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "print"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "eject"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "twostep"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "cluster"),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::SeparateCommands, ""),
            (Segment::Newline, "\n"),
            (Segment::Identifier, "fourth"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
            (Segment::Spaces, "   "),
            (Segment::Identifier, "fifth"),
            (Segment::Spaces, " "),
            (Segment::Identifier, "command"),
            (Segment::EndCommand, "."),
            (Segment::Newline, "\n"),
        ],
        &[
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::Later,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
            PromptStyle::First,
        ],
    );
}
