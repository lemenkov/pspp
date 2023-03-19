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

use crate::{
    identifier::Identifier,
    lex::{
        segment::Syntax,
        token::{Punct, Token},
    },
};

use super::{ScanError, ScanToken, StringScanner};

fn print_token(token: &Token) {
    match token {
        Token::Id(s) => print!("Token::Id(String::from({s:?}))"),
        Token::Number(number) => print!("Token::Number({number:?})"),
        Token::String(s) => print!("Token::String(String::from({s:?}))"),
        Token::End => print!("Token::EndCommand"),
        Token::Punct(punct) => print!("Token::Punct(Punct::{punct:?})"),
    }
}

#[track_caller]
fn check_scan(input: &str, mode: Syntax, expected: &[ScanToken]) {
    let tokens = StringScanner::new(input, mode, false).collect::<Vec<_>>();

    if tokens != expected {
        for token in &tokens {
            match token {
                ScanToken::Token(token) => {
                    print!("ScanToken::Token(");
                    print_token(token);
                    print!(")");
                }
                ScanToken::Error(error) => print!("ScanToken::Error(ScanError::{error:?})"),
            }
            println!(",");
        }

        eprintln!("tokens differ from expected:");
        let difference = diff::slice(expected, &tokens);
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

#[test]
fn test_identifiers() {
    check_scan(
        r#"a aB i5 $x @efg @@. !abcd !* !*a #.# .x _z.
abcd. abcd.
QRSTUV./* end of line comment */
QrStUv./* end of line comment */ 
WXYZ. /* unterminated end of line comment
�. /* U+FFFD is not valid in an identifier
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("aB").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("i5").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("$x").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("@efg").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("@@.").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("!abcd").unwrap())),
            ScanToken::Token(Token::Punct(Punct::BangAsterisk)),
            ScanToken::Token(Token::Punct(Punct::BangAsterisk)),
            ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("#.#").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Dot)),
            ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Underscore)),
            ScanToken::Token(Token::Id(Identifier::new("z").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("abcd.").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("abcd").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("QRSTUV").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("QrStUv").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("WXYZ").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Error(ScanError::UnexpectedChar('�')),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_reserved_words() {
    check_scan(
        r#"and or not eq ge gt le lt ne all by to with
AND OR NOT EQ GE GT LE LT NE ALL BY TO WITH
andx orx notx eqx gex gtx lex ltx nex allx byx tox withx
and. with.
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Punct(Punct::And)),
            ScanToken::Token(Token::Punct(Punct::Or)),
            ScanToken::Token(Token::Punct(Punct::Not)),
            ScanToken::Token(Token::Punct(Punct::Eq)),
            ScanToken::Token(Token::Punct(Punct::Ge)),
            ScanToken::Token(Token::Punct(Punct::Gt)),
            ScanToken::Token(Token::Punct(Punct::Le)),
            ScanToken::Token(Token::Punct(Punct::Lt)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::All)),
            ScanToken::Token(Token::Punct(Punct::By)),
            ScanToken::Token(Token::Punct(Punct::To)),
            ScanToken::Token(Token::Punct(Punct::With)),
            ScanToken::Token(Token::Punct(Punct::And)),
            ScanToken::Token(Token::Punct(Punct::Or)),
            ScanToken::Token(Token::Punct(Punct::Not)),
            ScanToken::Token(Token::Punct(Punct::Eq)),
            ScanToken::Token(Token::Punct(Punct::Ge)),
            ScanToken::Token(Token::Punct(Punct::Gt)),
            ScanToken::Token(Token::Punct(Punct::Le)),
            ScanToken::Token(Token::Punct(Punct::Lt)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::All)),
            ScanToken::Token(Token::Punct(Punct::By)),
            ScanToken::Token(Token::Punct(Punct::To)),
            ScanToken::Token(Token::Punct(Punct::With)),
            ScanToken::Token(Token::Id(Identifier::new("andx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("orx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("notx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("eqx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("gex").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("gtx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("lex").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("ltx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("nex").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("allx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("byx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("tox").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("withx").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("and.").unwrap())),
            ScanToken::Token(Token::Punct(Punct::With)),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_punctuation() {
    check_scan(
        r#"~ & | = >= > <= < ~= <> ( ) , - + * / [ ] **
~&|=>=><=<~=<>(),-+*/[]**
% : ; ? _ ` { } ~
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Punct(Punct::Not)),
            ScanToken::Token(Token::Punct(Punct::And)),
            ScanToken::Token(Token::Punct(Punct::Or)),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Punct(Punct::Ge)),
            ScanToken::Token(Token::Punct(Punct::Gt)),
            ScanToken::Token(Token::Punct(Punct::Le)),
            ScanToken::Token(Token::Punct(Punct::Lt)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::LParen)),
            ScanToken::Token(Token::Punct(Punct::RParen)),
            ScanToken::Token(Token::Punct(Punct::Comma)),
            ScanToken::Token(Token::Punct(Punct::Dash)),
            ScanToken::Token(Token::Punct(Punct::Plus)),
            ScanToken::Token(Token::Punct(Punct::Asterisk)),
            ScanToken::Token(Token::Punct(Punct::Slash)),
            ScanToken::Token(Token::Punct(Punct::LSquare)),
            ScanToken::Token(Token::Punct(Punct::RSquare)),
            ScanToken::Token(Token::Punct(Punct::Exp)),
            ScanToken::Token(Token::Punct(Punct::Not)),
            ScanToken::Token(Token::Punct(Punct::And)),
            ScanToken::Token(Token::Punct(Punct::Or)),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Punct(Punct::Ge)),
            ScanToken::Token(Token::Punct(Punct::Gt)),
            ScanToken::Token(Token::Punct(Punct::Le)),
            ScanToken::Token(Token::Punct(Punct::Lt)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::Ne)),
            ScanToken::Token(Token::Punct(Punct::LParen)),
            ScanToken::Token(Token::Punct(Punct::RParen)),
            ScanToken::Token(Token::Punct(Punct::Comma)),
            ScanToken::Token(Token::Punct(Punct::Dash)),
            ScanToken::Token(Token::Punct(Punct::Plus)),
            ScanToken::Token(Token::Punct(Punct::Asterisk)),
            ScanToken::Token(Token::Punct(Punct::Slash)),
            ScanToken::Token(Token::Punct(Punct::LSquare)),
            ScanToken::Token(Token::Punct(Punct::RSquare)),
            ScanToken::Token(Token::Punct(Punct::Exp)),
            ScanToken::Token(Token::Punct(Punct::Percent)),
            ScanToken::Token(Token::Punct(Punct::Colon)),
            ScanToken::Token(Token::Punct(Punct::Semicolon)),
            ScanToken::Token(Token::Punct(Punct::Question)),
            ScanToken::Token(Token::Punct(Punct::Underscore)),
            ScanToken::Token(Token::Punct(Punct::Backtick)),
            ScanToken::Token(Token::Punct(Punct::LCurly)),
            ScanToken::Token(Token::Punct(Punct::RCurly)),
            ScanToken::Token(Token::Punct(Punct::Not)),
        ],
    );
}

#[test]
fn test_positive_numbers() {
    check_scan(
        r#"0 1 01 001. 1.
123. /* comment 1 */ /* comment 2 */
.1 0.1 00.1 00.10
5e1 6E-1 7e+1 6E+01 6e-03
.3E1 .4e-1 .5E+1 .6e+01 .7E-03
1.23e1 45.6E-1 78.9e+1 99.9E+01 11.2e-03
. 1e e1 1e+ 1e-
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Number(0.0)),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Number(123.0)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::Number(0.1)),
            ScanToken::Token(Token::Number(0.1)),
            ScanToken::Token(Token::Number(0.1)),
            ScanToken::Token(Token::Number(50.0)),
            ScanToken::Token(Token::Number(0.6)),
            ScanToken::Token(Token::Number(70.0)),
            ScanToken::Token(Token::Number(60.0)),
            ScanToken::Token(Token::Number(0.006)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Number(30.0)),
            ScanToken::Token(Token::Number(0.04)),
            ScanToken::Token(Token::Number(5.0)),
            ScanToken::Token(Token::Number(6.0)),
            ScanToken::Token(Token::Number(0.0007)),
            ScanToken::Token(Token::Number(12.3)),
            ScanToken::Token(Token::Number(4.56)),
            ScanToken::Token(Token::Number(789.0)),
            ScanToken::Token(Token::Number(999.0)),
            ScanToken::Token(Token::Number(0.0112)),
            ScanToken::Token(Token::End),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("1e"))),
            ScanToken::Token(Token::Id(Identifier::new("e1").unwrap())),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("1e+"))),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("1e-"))),
        ],
    );
}

#[test]
fn test_negative_numbers() {
    check_scan(
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
            ScanToken::Token(Token::Number(-0.0)),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Number(-123.0)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Number(-0.1)),
            ScanToken::Token(Token::Number(-0.1)),
            ScanToken::Token(Token::Number(-0.1)),
            ScanToken::Token(Token::Number(-0.1)),
            ScanToken::Token(Token::Number(-50.0)),
            ScanToken::Token(Token::Number(-0.6)),
            ScanToken::Token(Token::Number(-70.0)),
            ScanToken::Token(Token::Number(-60.0)),
            ScanToken::Token(Token::Number(-0.006)),
            ScanToken::Token(Token::Number(-3.0)),
            ScanToken::Token(Token::Number(-0.04)),
            ScanToken::Token(Token::Number(-5.0)),
            ScanToken::Token(Token::Number(-6.0)),
            ScanToken::Token(Token::Number(-0.0007)),
            ScanToken::Token(Token::Number(-12.3)),
            ScanToken::Token(Token::Number(-4.56)),
            ScanToken::Token(Token::Number(-789.0)),
            ScanToken::Token(Token::Number(-999.0)),
            ScanToken::Token(Token::Number(-0.0112)),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::Punct(Punct::Dash)),
            ScanToken::Token(Token::Punct(Punct::Dot)),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("-1e"))),
            ScanToken::Token(Token::Punct(Punct::Dash)),
            ScanToken::Token(Token::Id(Identifier::new("e1").unwrap())),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("-1e+"))),
            ScanToken::Error(ScanError::ExpectedExponent(String::from("-1e-"))),
            ScanToken::Token(Token::Number(-1.0)),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_strings() {
    check_scan(
        r#"'x' "y" 'abc'
'Don''t' "Can't" 'Won''t'
"""quoted""" '"quoted"'
'' "" '''' """"
'missing end quote
"missing double quote
'x' + "y"
+ 'z' +
'a' /* abc */ + "b" /*
+ 'c' +/* */"d"/* */+'e'
'foo'
+          /* special case: + in column 0 would ordinarily start a new command
'bar'
'foo'
 +
'bar'
'foo'
+

'bar'

+
x"4142"+'5152'
"4142"+
x'5152'
x"4142"
+u'304a'
"�あいうえお"
"abc"+U"FFFD"+u'3048'+"xyz"
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::String(String::from("x"))),
            ScanToken::Token(Token::String(String::from("y"))),
            ScanToken::Token(Token::String(String::from("abc"))),
            ScanToken::Token(Token::String(String::from("Don't"))),
            ScanToken::Token(Token::String(String::from("Can't"))),
            ScanToken::Token(Token::String(String::from("Won't"))),
            ScanToken::Token(Token::String(String::from("\"quoted\""))),
            ScanToken::Token(Token::String(String::from("\"quoted\""))),
            ScanToken::Token(Token::String(String::from(""))),
            ScanToken::Token(Token::String(String::from(""))),
            ScanToken::Token(Token::String(String::from("'"))),
            ScanToken::Token(Token::String(String::from("\""))),
            ScanToken::Error(ScanError::ExpectedQuote),
            ScanToken::Error(ScanError::ExpectedQuote),
            ScanToken::Token(Token::String(String::from("xyzabcde"))),
            ScanToken::Token(Token::String(String::from("foobar"))),
            ScanToken::Token(Token::String(String::from("foobar"))),
            ScanToken::Token(Token::String(String::from("foo"))),
            ScanToken::Token(Token::Punct(Punct::Plus)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::String(String::from("bar"))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Punct(Punct::Plus)),
            ScanToken::Token(Token::String(String::from("AB5152"))),
            ScanToken::Token(Token::String(String::from("4142QR"))),
            ScanToken::Token(Token::String(String::from("ABお"))),
            ScanToken::Token(Token::String(String::from("�あいうえお"))),
            ScanToken::Token(Token::String(String::from("abc�えxyz"))),
        ],
    );
}

#[test]
fn test_shbang() {
    check_scan(
        r#"#! /usr/bin/pspp
#! /usr/bin/pspp
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("#").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Bang)),
            ScanToken::Token(Token::Punct(Punct::Slash)),
            ScanToken::Token(Token::Id(Identifier::new("usr").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Slash)),
            ScanToken::Token(Token::Id(Identifier::new("bin").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Slash)),
            ScanToken::Token(Token::Id(Identifier::new("pspp").unwrap())),
        ],
    );
}

#[test]
fn test_comments() {
    check_scan(
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
        Syntax::Auto,
        &[
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("com").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("is").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("ambiguous").unwrap())),
            ScanToken::Token(Token::Punct(Punct::With)),
            ScanToken::Token(Token::Id(Identifier::new("COMPUTE").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("next").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_document() {
    check_scan(
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
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            ScanToken::Token(Token::String(String::from("DOCUMENT one line."))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            ScanToken::Token(Token::String(String::from("DOC more"))),
            ScanToken::Token(Token::String(String::from("    than"))),
            ScanToken::Token(Token::String(String::from("        one"))),
            ScanToken::Token(Token::String(String::from("            line."))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            ScanToken::Token(Token::String(String::from("docu"))),
            ScanToken::Token(Token::String(String::from("first.paragraph"))),
            ScanToken::Token(Token::String(String::from("isn't parsed as tokens"))),
            ScanToken::Token(Token::String(String::from(""))),
            ScanToken::Token(Token::String(String::from("second paragraph."))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_file_label() {
    check_scan(
        r#"FIL label isn't quoted.
FILE
  lab 'is quoted'.
FILE /*
/**/  lab not quoted here either

"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("FIL").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("label").unwrap())),
            ScanToken::Token(Token::String(String::from("isn't quoted"))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("FILE").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("lab").unwrap())),
            ScanToken::Token(Token::String(String::from("is quoted"))),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("FILE").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("lab").unwrap())),
            ScanToken::Token(Token::String(String::from("not quoted here either"))),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_begin_data() {
    check_scan(
        r#"begin data.
123
xxx
end data.

BEG /**/ DAT /*
5 6 7 /* x

end  data
end data
.
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("begin").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::String(String::from("123"))),
            ScanToken::Token(Token::String(String::from("xxx"))),
            ScanToken::Token(Token::Id(Identifier::new("end").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("BEG").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("DAT").unwrap())),
            ScanToken::Token(Token::String(String::from("5 6 7 /* x"))),
            ScanToken::Token(Token::String(String::from(""))),
            ScanToken::Token(Token::String(String::from("end  data"))),
            ScanToken::Token(Token::Id(Identifier::new("end").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_do_repeat() {
    check_scan(
        r#"do repeat x=a b c
          y=d e f.
  do repeat a=1 thru 5.
another command.
second command
+ third command.
end /* x */ /* y */ repeat print.
end
 repeat.
"#,
        Syntax::Auto,
        &[
            ScanToken::Token(Token::Id(Identifier::new("do").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("b").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("c").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("y").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Id(Identifier::new("d").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("e").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("f").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::String(String::from("  do repeat a=1 thru 5."))),
            ScanToken::Token(Token::String(String::from("another command."))),
            ScanToken::Token(Token::String(String::from("second command"))),
            ScanToken::Token(Token::String(String::from("+ third command."))),
            ScanToken::Token(Token::String(String::from(
                "end /* x */ /* y */ repeat print.",
            ))),
            ScanToken::Token(Token::Id(Identifier::new("end").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
            ScanToken::Token(Token::End),
        ],
    );
}

#[test]
fn test_do_repeat_batch() {
    check_scan(
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
            ScanToken::Token(Token::Id(Identifier::new("do").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("b").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("c").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("y").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Id(Identifier::new("d").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("e").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("f").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::String(String::from("do repeat a=1 thru 5"))),
            ScanToken::Token(Token::String(String::from("another command"))),
            ScanToken::Token(Token::String(String::from("second command"))),
            ScanToken::Token(Token::String(String::from("+ third command"))),
            ScanToken::Token(Token::String(String::from(
                "end /* x */ /* y */ repeat print",
            ))),
            ScanToken::Token(Token::Id(Identifier::new("end").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("do").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("#a").unwrap())),
            ScanToken::Token(Token::Punct(Punct::Equals)),
            ScanToken::Token(Token::Number(1.0)),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::String(String::from("  inner command"))),
            ScanToken::Token(Token::Id(Identifier::new("end").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("repeat").unwrap())),
        ],
    );
}

#[test]
fn test_batch_mode() {
    check_scan(
        r#"first command
     another line of first command
+  second command
third command

fourth command.
   fifth command.
"#,
        Syntax::Batch,
        &[
            ScanToken::Token(Token::Id(Identifier::new("first").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("another").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("line").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("of").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("first").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("second").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("third").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("fourth").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
            ScanToken::Token(Token::Id(Identifier::new("fifth").unwrap())),
            ScanToken::Token(Token::Id(Identifier::new("command").unwrap())),
            ScanToken::Token(Token::End),
        ],
    );
}

mod define {
    use crate::{
        identifier::Identifier,
        lex::{
            scan::ScanToken,
            segment::Syntax,
            token::{Punct, Token},
        },
    };

    use super::check_scan;

    #[test]
    fn test_simple() {
        check_scan(
            r#"define !macro1()
var1 var2 var3
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from("var1 var2 var3"))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_no_newline_after_parentheses() {
        check_scan(
            r#"define !macro1() var1 var2 var3
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from(" var1 var2 var3"))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_no_newline_before_enddefine() {
        check_scan(
            r#"define !macro1()
var1 var2 var3!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from("var1 var2 var3"))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_all_on_one_line() {
        check_scan(
            r#"define !macro1()var1 var2 var3!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from("var1 var2 var3"))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_empty() {
        check_scan(
            r#"define !macro1()
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_blank_lines() {
        check_scan(
            r#"define !macro1()


!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from(""))),
                ScanToken::Token(Token::String(String::from(""))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_arguments() {
        check_scan(
            r#"define !macro1(a(), b(), c())
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("b").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("c").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_multiline_arguments() {
        check_scan(
            r#"define !macro1(
  a(), b(
  ),
  c()
)
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Id(Identifier::new("a").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("b").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("c").unwrap())),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_arguments_start_on_second_line() {
        check_scan(
            r#"define !macro1
(x,y,z
)
content 1
content 2
!enddefine.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("y").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Comma)),
                ScanToken::Token(Token::Id(Identifier::new("z").unwrap())),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from("content 1"))),
                ScanToken::Token(Token::String(String::from("content 2"))),
                ScanToken::Token(Token::Id(Identifier::new("!enddefine").unwrap())),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_early_end_of_command_1() {
        check_scan(
            r#"define !macro1.
data list /x 1.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::End),
                ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
                ScanToken::Token(Token::Id(Identifier::new("list").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Slash)),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::Number(1.0)),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_early_end_of_command_2() {
        check_scan(
            r#"define !macro1
x.
data list /x 1.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::End),
                ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
                ScanToken::Token(Token::Id(Identifier::new("list").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Slash)),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::Number(1.0)),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_early_end_of_command_3() {
        check_scan(
            r#"define !macro1(.
x.
data list /x 1.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::End),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::End),
                ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
                ScanToken::Token(Token::Id(Identifier::new("list").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Slash)),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::Number(1.0)),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_early_end_of_command_4() {
        // Notice the command terminator at the end of the DEFINE command,
        // which should not be there and ends it early.
        check_scan(
            r#"define !macro1.
data list /x 1.
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::End),
                ScanToken::Token(Token::Id(Identifier::new("data").unwrap())),
                ScanToken::Token(Token::Id(Identifier::new("list").unwrap())),
                ScanToken::Token(Token::Punct(Punct::Slash)),
                ScanToken::Token(Token::Id(Identifier::new("x").unwrap())),
                ScanToken::Token(Token::Number(1.0)),
                ScanToken::Token(Token::End),
            ],
        );
    }

    #[test]
    fn test_missing_enddefine() {
        check_scan(
            r#"define !macro1()
content line 1
content line 2
"#,
            Syntax::Auto,
            &[
                ScanToken::Token(Token::Id(Identifier::new("define").unwrap())),
                ScanToken::Token(Token::String(String::from("!macro1"))),
                ScanToken::Token(Token::Punct(Punct::LParen)),
                ScanToken::Token(Token::Punct(Punct::RParen)),
                ScanToken::Token(Token::String(String::from("content line 1"))),
                ScanToken::Token(Token::String(String::from("content line 2"))),
            ],
        );
    }
}
