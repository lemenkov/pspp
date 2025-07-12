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

use super::{ScanError, StringScanner};

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
fn check_scan(input: &str, mode: Syntax, expected: &[Result<Token, ScanError>]) {
    let tokens = StringScanner::new(input, mode, false).collect::<Vec<_>>();

    if tokens != expected {
        for token in &tokens {
            match token {
                Ok(token) => {
                    print!("Ok(");
                    print_token(token);
                    print!(")");
                }
                Err(error) => print!("Err(ScanError::{error:?})"),
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
            Ok(Token::Id(Identifier::new("a").unwrap())),
            Ok(Token::Id(Identifier::new("aB").unwrap())),
            Ok(Token::Id(Identifier::new("i5").unwrap())),
            Ok(Token::Id(Identifier::new("$x").unwrap())),
            Ok(Token::Id(Identifier::new("@efg").unwrap())),
            Ok(Token::Id(Identifier::new("@@.").unwrap())),
            Ok(Token::Id(Identifier::new("!abcd").unwrap())),
            Ok(Token::Punct(Punct::BangAsterisk)),
            Ok(Token::Punct(Punct::BangAsterisk)),
            Ok(Token::Id(Identifier::new("a").unwrap())),
            Ok(Token::Id(Identifier::new("#.#").unwrap())),
            Ok(Token::Punct(Punct::Dot)),
            Ok(Token::Id(Identifier::new("x").unwrap())),
            Ok(Token::Punct(Punct::Underscore)),
            Ok(Token::Id(Identifier::new("z").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("abcd.").unwrap())),
            Ok(Token::Id(Identifier::new("abcd").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("QRSTUV").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("QrStUv").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("WXYZ").unwrap())),
            Ok(Token::End),
            Err(ScanError::UnexpectedChar('�')),
            Ok(Token::End),
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
            Ok(Token::Punct(Punct::And)),
            Ok(Token::Punct(Punct::Or)),
            Ok(Token::Punct(Punct::Not)),
            Ok(Token::Punct(Punct::Eq)),
            Ok(Token::Punct(Punct::Ge)),
            Ok(Token::Punct(Punct::Gt)),
            Ok(Token::Punct(Punct::Le)),
            Ok(Token::Punct(Punct::Lt)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::All)),
            Ok(Token::Punct(Punct::By)),
            Ok(Token::Punct(Punct::To)),
            Ok(Token::Punct(Punct::With)),
            Ok(Token::Punct(Punct::And)),
            Ok(Token::Punct(Punct::Or)),
            Ok(Token::Punct(Punct::Not)),
            Ok(Token::Punct(Punct::Eq)),
            Ok(Token::Punct(Punct::Ge)),
            Ok(Token::Punct(Punct::Gt)),
            Ok(Token::Punct(Punct::Le)),
            Ok(Token::Punct(Punct::Lt)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::All)),
            Ok(Token::Punct(Punct::By)),
            Ok(Token::Punct(Punct::To)),
            Ok(Token::Punct(Punct::With)),
            Ok(Token::Id(Identifier::new("andx").unwrap())),
            Ok(Token::Id(Identifier::new("orx").unwrap())),
            Ok(Token::Id(Identifier::new("notx").unwrap())),
            Ok(Token::Id(Identifier::new("eqx").unwrap())),
            Ok(Token::Id(Identifier::new("gex").unwrap())),
            Ok(Token::Id(Identifier::new("gtx").unwrap())),
            Ok(Token::Id(Identifier::new("lex").unwrap())),
            Ok(Token::Id(Identifier::new("ltx").unwrap())),
            Ok(Token::Id(Identifier::new("nex").unwrap())),
            Ok(Token::Id(Identifier::new("allx").unwrap())),
            Ok(Token::Id(Identifier::new("byx").unwrap())),
            Ok(Token::Id(Identifier::new("tox").unwrap())),
            Ok(Token::Id(Identifier::new("withx").unwrap())),
            Ok(Token::Id(Identifier::new("and.").unwrap())),
            Ok(Token::Punct(Punct::With)),
            Ok(Token::End),
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
            Ok(Token::Punct(Punct::Not)),
            Ok(Token::Punct(Punct::And)),
            Ok(Token::Punct(Punct::Or)),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Punct(Punct::Ge)),
            Ok(Token::Punct(Punct::Gt)),
            Ok(Token::Punct(Punct::Le)),
            Ok(Token::Punct(Punct::Lt)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::LParen)),
            Ok(Token::Punct(Punct::RParen)),
            Ok(Token::Punct(Punct::Comma)),
            Ok(Token::Punct(Punct::Dash)),
            Ok(Token::Punct(Punct::Plus)),
            Ok(Token::Punct(Punct::Asterisk)),
            Ok(Token::Punct(Punct::Slash)),
            Ok(Token::Punct(Punct::LSquare)),
            Ok(Token::Punct(Punct::RSquare)),
            Ok(Token::Punct(Punct::Exp)),
            Ok(Token::Punct(Punct::Not)),
            Ok(Token::Punct(Punct::And)),
            Ok(Token::Punct(Punct::Or)),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Punct(Punct::Ge)),
            Ok(Token::Punct(Punct::Gt)),
            Ok(Token::Punct(Punct::Le)),
            Ok(Token::Punct(Punct::Lt)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::Ne)),
            Ok(Token::Punct(Punct::LParen)),
            Ok(Token::Punct(Punct::RParen)),
            Ok(Token::Punct(Punct::Comma)),
            Ok(Token::Punct(Punct::Dash)),
            Ok(Token::Punct(Punct::Plus)),
            Ok(Token::Punct(Punct::Asterisk)),
            Ok(Token::Punct(Punct::Slash)),
            Ok(Token::Punct(Punct::LSquare)),
            Ok(Token::Punct(Punct::RSquare)),
            Ok(Token::Punct(Punct::Exp)),
            Ok(Token::Punct(Punct::Percent)),
            Ok(Token::Punct(Punct::Colon)),
            Ok(Token::Punct(Punct::Semicolon)),
            Ok(Token::Punct(Punct::Question)),
            Ok(Token::Punct(Punct::Underscore)),
            Ok(Token::Punct(Punct::Backtick)),
            Ok(Token::Punct(Punct::LCurly)),
            Ok(Token::Punct(Punct::RCurly)),
            Ok(Token::Punct(Punct::Not)),
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
            Ok(Token::Number(0.0)),
            Ok(Token::Number(1.0)),
            Ok(Token::Number(1.0)),
            Ok(Token::Number(1.0)),
            Ok(Token::Number(1.0)),
            Ok(Token::End),
            Ok(Token::Number(123.0)),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Number(1.0)),
            Ok(Token::Number(0.1)),
            Ok(Token::Number(0.1)),
            Ok(Token::Number(0.1)),
            Ok(Token::Number(50.0)),
            Ok(Token::Number(0.6)),
            Ok(Token::Number(70.0)),
            Ok(Token::Number(60.0)),
            Ok(Token::Number(0.006)),
            Ok(Token::End),
            Ok(Token::Number(30.0)),
            Ok(Token::Number(0.04)),
            Ok(Token::Number(5.0)),
            Ok(Token::Number(6.0)),
            Ok(Token::Number(0.0007)),
            Ok(Token::Number(12.3)),
            Ok(Token::Number(4.56)),
            Ok(Token::Number(789.0)),
            Ok(Token::Number(999.0)),
            Ok(Token::Number(0.0112)),
            Ok(Token::End),
            Err(ScanError::ExpectedExponent(String::from("1e"))),
            Ok(Token::Id(Identifier::new("e1").unwrap())),
            Err(ScanError::ExpectedExponent(String::from("1e+"))),
            Err(ScanError::ExpectedExponent(String::from("1e-"))),
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
            Ok(Token::Number(-0.0)),
            Ok(Token::Number(-1.0)),
            Ok(Token::Number(-1.0)),
            Ok(Token::Number(-1.0)),
            Ok(Token::Number(-1.0)),
            Ok(Token::End),
            Ok(Token::Number(-123.0)),
            Ok(Token::End),
            Ok(Token::Number(-0.1)),
            Ok(Token::Number(-0.1)),
            Ok(Token::Number(-0.1)),
            Ok(Token::Number(-0.1)),
            Ok(Token::Number(-50.0)),
            Ok(Token::Number(-0.6)),
            Ok(Token::Number(-70.0)),
            Ok(Token::Number(-60.0)),
            Ok(Token::Number(-0.006)),
            Ok(Token::Number(-3.0)),
            Ok(Token::Number(-0.04)),
            Ok(Token::Number(-5.0)),
            Ok(Token::Number(-6.0)),
            Ok(Token::Number(-0.0007)),
            Ok(Token::Number(-12.3)),
            Ok(Token::Number(-4.56)),
            Ok(Token::Number(-789.0)),
            Ok(Token::Number(-999.0)),
            Ok(Token::Number(-0.0112)),
            Ok(Token::Number(-1.0)),
            Ok(Token::Punct(Punct::Dash)),
            Ok(Token::Punct(Punct::Dot)),
            Err(ScanError::ExpectedExponent(String::from("-1e"))),
            Ok(Token::Punct(Punct::Dash)),
            Ok(Token::Id(Identifier::new("e1").unwrap())),
            Err(ScanError::ExpectedExponent(String::from("-1e+"))),
            Err(ScanError::ExpectedExponent(String::from("-1e-"))),
            Ok(Token::Number(-1.0)),
            Ok(Token::End),
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
            Ok(Token::String(String::from("x"))),
            Ok(Token::String(String::from("y"))),
            Ok(Token::String(String::from("abc"))),
            Ok(Token::String(String::from("Don't"))),
            Ok(Token::String(String::from("Can't"))),
            Ok(Token::String(String::from("Won't"))),
            Ok(Token::String(String::from("\"quoted\""))),
            Ok(Token::String(String::from("\"quoted\""))),
            Ok(Token::String(String::from(""))),
            Ok(Token::String(String::from(""))),
            Ok(Token::String(String::from("'"))),
            Ok(Token::String(String::from("\""))),
            Err(ScanError::ExpectedQuote),
            Err(ScanError::ExpectedQuote),
            Ok(Token::String(String::from("xyzabcde"))),
            Ok(Token::String(String::from("foobar"))),
            Ok(Token::String(String::from("foobar"))),
            Ok(Token::String(String::from("foo"))),
            Ok(Token::Punct(Punct::Plus)),
            Ok(Token::End),
            Ok(Token::String(String::from("bar"))),
            Ok(Token::End),
            Ok(Token::Punct(Punct::Plus)),
            Ok(Token::String(String::from("AB5152"))),
            Ok(Token::String(String::from("4142QR"))),
            Ok(Token::String(String::from("ABお"))),
            Ok(Token::String(String::from("�あいうえお"))),
            Ok(Token::String(String::from("abc�えxyz"))),
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
            Ok(Token::Id(Identifier::new("#").unwrap())),
            Ok(Token::Punct(Punct::Bang)),
            Ok(Token::Punct(Punct::Slash)),
            Ok(Token::Id(Identifier::new("usr").unwrap())),
            Ok(Token::Punct(Punct::Slash)),
            Ok(Token::Id(Identifier::new("bin").unwrap())),
            Ok(Token::Punct(Punct::Slash)),
            Ok(Token::Id(Identifier::new("pspp").unwrap())),
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
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("com").unwrap())),
            Ok(Token::Id(Identifier::new("is").unwrap())),
            Ok(Token::Id(Identifier::new("ambiguous").unwrap())),
            Ok(Token::Punct(Punct::With)),
            Ok(Token::Id(Identifier::new("COMPUTE").unwrap())),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("next").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
            Ok(Token::End),
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
            Ok(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            Ok(Token::String(String::from("DOCUMENT one line."))),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            Ok(Token::String(String::from("DOC more"))),
            Ok(Token::String(String::from("    than"))),
            Ok(Token::String(String::from("        one"))),
            Ok(Token::String(String::from("            line."))),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("DOCUMENT").unwrap())),
            Ok(Token::String(String::from("docu"))),
            Ok(Token::String(String::from("first.paragraph"))),
            Ok(Token::String(String::from("isn't parsed as tokens"))),
            Ok(Token::String(String::from(""))),
            Ok(Token::String(String::from("second paragraph."))),
            Ok(Token::End),
            Ok(Token::End),
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
            Ok(Token::Id(Identifier::new("FIL").unwrap())),
            Ok(Token::Id(Identifier::new("label").unwrap())),
            Ok(Token::String(String::from("isn't quoted"))),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("FILE").unwrap())),
            Ok(Token::Id(Identifier::new("lab").unwrap())),
            Ok(Token::String(String::from("is quoted"))),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("FILE").unwrap())),
            Ok(Token::Id(Identifier::new("lab").unwrap())),
            Ok(Token::String(String::from("not quoted here either"))),
            Ok(Token::End),
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
            Ok(Token::Id(Identifier::new("begin").unwrap())),
            Ok(Token::Id(Identifier::new("data").unwrap())),
            Ok(Token::End),
            Ok(Token::String(String::from("123"))),
            Ok(Token::String(String::from("xxx"))),
            Ok(Token::Id(Identifier::new("end").unwrap())),
            Ok(Token::Id(Identifier::new("data").unwrap())),
            Ok(Token::End),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("BEG").unwrap())),
            Ok(Token::Id(Identifier::new("DAT").unwrap())),
            Ok(Token::String(String::from("5 6 7 /* x"))),
            Ok(Token::String(String::from(""))),
            Ok(Token::String(String::from("end  data"))),
            Ok(Token::Id(Identifier::new("end").unwrap())),
            Ok(Token::Id(Identifier::new("data").unwrap())),
            Ok(Token::End),
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
            Ok(Token::Id(Identifier::new("do").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
            Ok(Token::Id(Identifier::new("x").unwrap())),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Id(Identifier::new("a").unwrap())),
            Ok(Token::Id(Identifier::new("b").unwrap())),
            Ok(Token::Id(Identifier::new("c").unwrap())),
            Ok(Token::Id(Identifier::new("y").unwrap())),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Id(Identifier::new("d").unwrap())),
            Ok(Token::Id(Identifier::new("e").unwrap())),
            Ok(Token::Id(Identifier::new("f").unwrap())),
            Ok(Token::End),
            Ok(Token::String(String::from("  do repeat a=1 thru 5."))),
            Ok(Token::String(String::from("another command."))),
            Ok(Token::String(String::from("second command"))),
            Ok(Token::String(String::from("+ third command."))),
            Ok(Token::String(String::from(
                "end /* x */ /* y */ repeat print.",
            ))),
            Ok(Token::Id(Identifier::new("end").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
            Ok(Token::End),
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
            Ok(Token::Id(Identifier::new("do").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
            Ok(Token::Id(Identifier::new("x").unwrap())),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Id(Identifier::new("a").unwrap())),
            Ok(Token::Id(Identifier::new("b").unwrap())),
            Ok(Token::Id(Identifier::new("c").unwrap())),
            Ok(Token::Id(Identifier::new("y").unwrap())),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Id(Identifier::new("d").unwrap())),
            Ok(Token::Id(Identifier::new("e").unwrap())),
            Ok(Token::Id(Identifier::new("f").unwrap())),
            Ok(Token::End),
            Ok(Token::String(String::from("do repeat a=1 thru 5"))),
            Ok(Token::String(String::from("another command"))),
            Ok(Token::String(String::from("second command"))),
            Ok(Token::String(String::from("+ third command"))),
            Ok(Token::String(String::from(
                "end /* x */ /* y */ repeat print",
            ))),
            Ok(Token::Id(Identifier::new("end").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("do").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
            Ok(Token::Id(Identifier::new("#a").unwrap())),
            Ok(Token::Punct(Punct::Equals)),
            Ok(Token::Number(1.0)),
            Ok(Token::End),
            Ok(Token::String(String::from("  inner command"))),
            Ok(Token::Id(Identifier::new("end").unwrap())),
            Ok(Token::Id(Identifier::new("repeat").unwrap())),
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
            Ok(Token::Id(Identifier::new("first").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::Id(Identifier::new("another").unwrap())),
            Ok(Token::Id(Identifier::new("line").unwrap())),
            Ok(Token::Id(Identifier::new("of").unwrap())),
            Ok(Token::Id(Identifier::new("first").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("second").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("third").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("fourth").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
            Ok(Token::Id(Identifier::new("fifth").unwrap())),
            Ok(Token::Id(Identifier::new("command").unwrap())),
            Ok(Token::End),
        ],
    );
}

mod define {
    use crate::{
        identifier::Identifier,
        lex::{
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from("var1 var2 var3"))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from(" var1 var2 var3"))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from("var1 var2 var3"))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from("var1 var2 var3"))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from(""))),
                Ok(Token::String(String::from(""))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Id(Identifier::new("a").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("b").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("c").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Id(Identifier::new("a").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("b").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("c").unwrap())),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("y").unwrap())),
                Ok(Token::Punct(Punct::Comma)),
                Ok(Token::Id(Identifier::new("z").unwrap())),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from("content 1"))),
                Ok(Token::String(String::from("content 2"))),
                Ok(Token::Id(Identifier::new("!enddefine").unwrap())),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::End),
                Ok(Token::Id(Identifier::new("data").unwrap())),
                Ok(Token::Id(Identifier::new("list").unwrap())),
                Ok(Token::Punct(Punct::Slash)),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::Number(1.0)),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::End),
                Ok(Token::Id(Identifier::new("data").unwrap())),
                Ok(Token::Id(Identifier::new("list").unwrap())),
                Ok(Token::Punct(Punct::Slash)),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::Number(1.0)),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::End),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::End),
                Ok(Token::Id(Identifier::new("data").unwrap())),
                Ok(Token::Id(Identifier::new("list").unwrap())),
                Ok(Token::Punct(Punct::Slash)),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::Number(1.0)),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::End),
                Ok(Token::Id(Identifier::new("data").unwrap())),
                Ok(Token::Id(Identifier::new("list").unwrap())),
                Ok(Token::Punct(Punct::Slash)),
                Ok(Token::Id(Identifier::new("x").unwrap())),
                Ok(Token::Number(1.0)),
                Ok(Token::End),
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
                Ok(Token::Id(Identifier::new("define").unwrap())),
                Ok(Token::String(String::from("!macro1"))),
                Ok(Token::Punct(Punct::LParen)),
                Ok(Token::Punct(Punct::RParen)),
                Ok(Token::String(String::from("content line 1"))),
                Ok(Token::String(String::from("content line 2"))),
            ],
        );
    }
}
