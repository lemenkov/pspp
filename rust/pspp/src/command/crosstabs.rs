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

use flagset::FlagSet;

use super::{By, Comma, Command, Equals, Integer, Number, Punctuated, Subcommands, VarList};
use crate::command::{
    FromTokens, InParens, MismatchToError, ParseError, ParseResult, Parsed, Punct, Token,
    TokenSlice, VarRange,
};

pub(super) fn crosstabs_command() -> Command {
    Command {
        allowed_states: FlagSet::full(),
        enhanced_only: false,
        testing_only: false,
        no_abbrev: false,
        name: "CROSSTABS",
        run: Box::new(|context| {
            let input = context.lexer.clone();
            match <Crosstabs>::from_tokens(&input) {
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
struct Crosstabs(Subcommands<CrosstabsSubcommand>);

mod keyword {
    use crate::command::{FromTokens, ParseResult, TokenSlice};

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Count;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Tables;
}

#[derive(Debug, pspp_derive::FromTokens)]
enum CrosstabsSubcommand {
    #[pspp(no_selector)]
    Tables(Option<(keyword::Tables, Equals)>, Punctuated<VarList, By>),
    Missing(Equals, Missing),
    Write(Option<(Equals, Write)>),
    HideSmallCounts(keyword::Count, Equals, Integer),
    ShowDim(Equals, Integer),
    Statistics(Equals, Punctuated<Statistic>),
    Cells(Equals, Punctuated<Cell>),
    Variables(
        Equals,
        Punctuated<(VarRange, InParens<(Integer, Comma, Integer)>)>,
    ),
    Format(Equals, Punctuated<Format>),
    Count(Equals, Punctuated<Count>),
    Method(Equals, Method),
    BarChart,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct BoundedVars {
    vars: VarRange,
    bounds: InParens<Bounds>,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Bounds {
    min: Integer,
    comma: Comma,
    max: Integer,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Method {
    Mc(Punctuated<Mc>),
    Exact(Punctuated<Exact>),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Mc {
    CIn(InParens<Number>),
    Samples(InParens<Integer>),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Exact {
    Timer(InParens<Number>),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Cell {
    Prop,
    BProp,
    Count,
    Row,
    Column,
    Total,
    Expected,
    ResId,
    SResId,
    ASResid,
    All,
    None,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Statistic {
    ChiSq,
    Phi,
    CC,
    Lambda,
    UC,
    Risk,
    BTau,
    CTau,
    Kappa,
    Gamma,
    D,
    McNemar,
    Eta,
    Corr,
    Cmh(InParens<Integer>),
    All,
    None,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Count {
    AsIs,
    Case,
    Cell,
    Round,
    Truncate,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Format {
    AValue,
    DValue,
    Tables,
    NoTables,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Missing {
    Table,
    Include,
    Report,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Write {
    None,
    Cells,
    All,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct HideSmallCounts {
    // XXX `COUNT =`
    count: Integer,
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use encoding_rs::UTF_8;

    use crate::{
        engine::Engine,
        lex::lexer::{Source, SourceFile},
    };

    fn test(syntax: &str) {
        let mut engine = Engine::new();
        engine.run(Source::new_default(&Arc::new(
            SourceFile::for_file_contents(syntax.to_string(), Some("test.sps".to_string()), UTF_8),
        )));
    }

    #[test]
    fn basics() {
        test(
            "CROSSTABS r by c /STATISTICS=CHISQ
/CELLS=COUNT EXPECTED RESID SRESID ASRESID
/HIDESMALLCOUNTS COUNT=6.
",
        );
    }

    #[test]
    fn integer_mode() {
        test("CROSSTABS VARIABLES=X (1,7) Y (1,7) /TABLES=X BY Y/WRITE=CELLS.");
    }
}
