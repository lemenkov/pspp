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

use super::{Comma, Command, Equals, Punctuated, Seq1, Subcommands};
use crate::command::{
    FromTokens, Identifier, InParens, MismatchToError, ParseError, ParseResult, Parsed, Punct,
    Token, TokenSlice, VarRange,
};

pub(super) fn descriptives_command() -> Command {
    Command {
        allowed_states: FlagSet::full(),
        enhanced_only: false,
        testing_only: false,
        no_abbrev: false,
        name: "DESCRIPTIVES",
        run: Box::new(|context| {
            let input = context.lexer.clone();
            match <Descriptives>::from_tokens(&input) {
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
struct Descriptives {
    subcommands: Subcommands<DescriptivesSubcommand>,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum DescriptivesSubcommand {
    Missing(Equals, Seq1<Missing>),
    Save,
    Statistics(Equals, Seq1<Statistic>),
    Sort(Equals, Sort),
    Format(Equals, Seq1<Format>),
    #[pspp(no_selector)]
    Variables(
        Option<(keyword::Variables, Equals)>,
        Punctuated<DescriptivesVars>,
    ),
}

mod keyword {
    use crate::command::{FromTokens, ParseResult, TokenSlice};

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Variables;
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Missing {
    Variable,
    Listwise,
    Include,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Format {
    Labels,
    NoLabels,
    Index,
    NoIndex,
    Line,
    Serial,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct DescriptivesVars {
    vars: VarRange,
    z_name: Option<InParens<Identifier>>,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Sort {
    key: SortKey,
    direction: Option<(Comma, Direction, Comma)>,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum SortKey {
    Mean,
    SMean,
    Stddev,
    Variance,
    Range,
    Min,
    Max,
    Sum,
    Skewness,
    Kurtosis,
    Name,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Direction {
    #[pspp(syntax = "A")]
    Ascending,
    #[pspp(syntax = "D")]
    Descending,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Statistic {
    Default,
    Mean,
    SeMean,
    Stddev,
    Variance,
    Range,
    Sum,
    Min,
    Max,
    Skewness,
    Kurtosis,
    All,
}

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
        test("descript a to b (c) all/stat=all/format=serial.");
    }

    #[test]
    fn include_missing() {
        test("descript all/stat=all/format=serial/missing=include.");
    }

    #[test]
    fn include_missing_listwise() {
        test("descript all/stat=all/format=serial/missing=listwise.");
        test("descript all/stat=all/format=serial/missing=listwise include.");
    }

    #[test]
    fn mean_only() {
        test("descript all/stat=mean.");
    }

    #[test]
    fn z_scores() {
        test("DESCRIPTIVES /VAR=a b, c /SAVE.");
    }

    #[test]
    fn syntax_errors() {
        test(
            "\
DESCRIPTIVES MISSING=**.
DESCRIPTIVES FORMAT=**.
DESCRIPTIVES STATISTICS=**.
DESCRIPTIVES SORT=**.
DESCRIPTIVES SORT=NAME (**).
DESCRIPTIVES SORT=NAME (A **).
DESCRIPTIVES **.
DESCRIPTIVES x/ **.
DESCRIPTIVES MISSING=INCLUDE.
",
        );
    }
}
