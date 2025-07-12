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

use either::Either;
use flagset::FlagSet;

use super::{Comma, Command, Equals, Integer, Punctuated, Seq0, Seq1, Slash};
use crate::{
    command::{FromTokens, InParens, MismatchToError, ParseError, ParseResult, Parsed, TokenSlice},
    identifier::Identifier,
};

pub(super) fn data_list_command() -> Command {
    Command {
        allowed_states: FlagSet::full(),
        enhanced_only: false,
        testing_only: false,
        no_abbrev: false,
        name: "DATA LIST",
        run: Box::new(|context| {
            match <DataList>::from_tokens(&context.lexer) {
                Ok(Parsed {
                    value,
                    rest: _,
                    diagnostics,
                }) => {
                    println!("\n{value:#?}");
                    //println!("rest: {rest:?}");
                    println!("warnings: {diagnostics:?}");
                    //println!("{:?}", DescriptivesSubcommand::from_tokens(subcommand.0));
                }
                Err(error) => {
                    println!("{error:?}");
                }
            }
        }),
    }
}

#[derive(Debug, pspp_derive::FromTokens)]
struct DataList(Seq1<Setting>, Seq1<Record>);

#[derive(Debug, pspp_derive::FromTokens)]
enum Setting {
    File(Equals, Either<String, Identifier>),
    Encoding(Equals, String),
    Fixed,
    Free(Option<InParens<Punctuated<Delimiter>>>),
    List(Option<InParens<Punctuated<Delimiter>>>),
    Records(Equals, Integer),
    Skip(Equals, Integer),
    Table,
    NoTable,
    End(Equals, Identifier),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Delimiter {
    #[pspp(no_selector)]
    String(String),
    Tab,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Record {
    slash: Slash,
    record: Option<Integer>,
    variables: Seq0<Variable>,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Variable {
    names: Seq1<Identifier>,
    location: Location,
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(no_selector)]
enum Location {
    Columns(
        Integer,
        Option<Integer>,
        Option<InParens<(Identifier, Option<(Comma, Integer)>)>>,
    ),
    Fortran(InParens<Punctuated<(Option<Integer>, Format)>>),
    Asterisk,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Format(Identifier);

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use encoding_rs::UTF_8;

    use crate::{
        engine::Engine,
        lex::lexer::{Source, SyntaxFile},
    };

    fn test(syntax: &str) {
        let mut engine = Engine::new();
        engine.run(Source::new_default(&Arc::new(SyntaxFile::new(
            syntax.to_string(),
            Some("test.sps".to_string()),
            UTF_8,
        ))));
    }

    #[test]
    fn basics() {
        test(
            "data list fixed notable
        /1 start 1-20 (adate)
        /2 end 1-20 (adate)
        /3 count 1-3.",
        );
    }

    #[test]
    fn syntax_errors() {
        test(
            "DATA LIST FILE=**.
DATA LIST ENCODING=**.
DATA LIST RECORDS=1 RECORDS=2.
DATA LIST RECORDS=0.
DATA LIST SKIP=-1.
DATA LIST END=**.
INPUT PROGRAM.
DATA LIST END=xyzzy END=xyzzy.
END INPUT PROGRAM.
INPUT PROGRAM.
DATA LIST END=**.
END INPUT PROGRAM.
DATA LIST XYZZY.
DATA LIST FREE LIST.
DATA LIST LIST (**).
DATA LIST **.
DATA LIST ENCODING='xyzzy'/x.
INPUT PROGRAM.
DATA LIST LIST END=xyzzy/x.
END INPUT PROGRAM.
DATA LIST FIXED/0.
DATA LIST FIXED/ **.
DATA LIST FIXED/x 1.5.
DATA LIST FIXED/x -1.
DATA LIST FIXED/x 5-3.
DATA LIST FIXED/x y 1-3.
DATA LIST FIXED/x 1-5 (xyzzy).
DATA LIST FIXED/x 1-5 (**).
DATA LIST FIXED/x 1 (F,5).
DATA LIST FIXED/x (2F8.0).
DATA LIST FIXED/x **.
DATA LIST FIXED/x 1 x 2.
INPUT PROGRAM.
DATA LIST FIXED/x 1.
DATA LIST FIXED/x 1 (a).
END INPUT PROGRAM.
INPUT PROGRAM.
DATA LIST FIXED/y 2 (a).
DATA LIST FIXED/y 3-4 (a).
END INPUT PROGRAM.
DATA LIST FIXED RECORDS=1/x y(F2/F3).
DATA LIST FIXED RECORDS=1//.
DATA LIST FIXED RECORDS=1/.
",
        );
    }
}
