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

use std::fmt::Debug;

use either::Either;
use flagset::FlagSet;

use super::{
    And, Asterisk, By, Command, Dash, Equals, Exp, Gt, InSquares, Integer, Number, Plus,
    Punctuated, Seq0, Seq1, Slash, Subcommands, VarList,
};
use crate::{
    command::{FromTokens, InParens, MismatchToError, ParseError, ParseResult, Parsed, TokenSlice},
    format::AbstractFormat,
    identifier::Identifier,
};

pub(super) fn ctables_command() -> Command {
    Command {
        allowed_states: FlagSet::full(),
        enhanced_only: false,
        testing_only: false,
        no_abbrev: false,
        name: "CTABLES",
        run: Box::new(|context| {
            let input = context.lexer.clone();
            match <CTables>::from_tokens(&input) {
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
struct CTables(Subcommands<CTablesSubcommand>);

#[derive(Debug, pspp_derive::FromTokens)]
enum CTablesSubcommand {
    Table(Table),
    Format(Seq1<Format>),
    VLabels(Seq1<VLabel>),
    SMissing(SMissing),
    PCompute(And, Identifier, Equals, keyword::Expr, InParens<Expression>),
    PProperties(And, Identifier, Seq0<PProperties>),
    Weight(keyword::Variable, Equals, Identifier),
    HideSmallCounts(keyword::Count, Equals, Integer),
    SLabels(Seq1<SLabel>),
    CLabels(CLabel),
    Categories(Seq1<Categories>),
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Table {
    rows: Option<Axis>,
    columns: Option<(By, Option<Axis>)>,
    layers: Option<(By, Option<Axis>)>,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Axis {
    Variable(Identifier, Option<InSquares<Measurement>>),
    Nest(Box<Axis>, Gt, Box<Axis>),
    Stack(Box<Axis>, Plus, Box<Axis>),
    Parens(InParens<Box<Axis>>),
    Summary(InSquares<Punctuated<Summary>>),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Measurement {
    C,
    S,
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Summary {
    function: Identifier,
    percentile: Option<Number>,
    label: Option<String>,
    format: Option<AbstractFormat>,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Format {
    MinColWidth(Equals, Width),
    MaxColWidth(Equals, Width),
    Units(Equals, Unit),
    Empty(Equals, Empty),
    Missing(Equals, String),
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(no_selector)]
enum Width {
    Default(keyword::Default),
    Width(Number),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Empty {
    Zero(keyword::Zero),
    Blank(keyword::Blank),
    Value(String),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Unit {
    Points,
    Inches,
    Cm,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum VLabel {
    Variables(Equals, VarList),
    Display(Display),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Display {
    Default,
    Name,
    Label,
    Both,
    None,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum SMissing {
    Variable,
    Listwise,
}

#[derive(pspp_derive::FromTokens)]
struct Expression(MulExpression, Seq0<(Either<Plus, Dash>, Expression)>);

impl Debug for Expression {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.1.0.is_empty() {
            self.0.fmt(f)
        } else {
            write!(f, "(")?;
            self.0.fmt(f)?;
            for (operator, operand) in &self.1.0 {
                if operator.is_left() {
                    write!(f, " + ")?;
                } else {
                    write!(f, " - ")?;
                }
                operand.fmt(f)?;
            }
            write!(f, ")")
        }
    }
}

#[derive(pspp_derive::FromTokens)]
struct MulExpression(
    PowExpression,
    Seq0<(Either<Asterisk, Slash>, PowExpression)>,
);

impl Debug for MulExpression {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.1.0.is_empty() {
            self.0.fmt(f)
        } else {
            write!(f, "(")?;
            self.0.fmt(f)?;
            for (operator, operand) in &self.1.0 {
                if operator.is_left() {
                    write!(f, " * ")?;
                } else {
                    write!(f, " / ")?;
                }
                operand.fmt(f)?;
            }
            write!(f, ")")
        }
    }
}

#[derive(pspp_derive::FromTokens)]
struct PowExpression(Terminal, Seq0<(Exp, PowExpression)>);

impl Debug for PowExpression {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.1.0.is_empty() {
            self.0.fmt(f)
        } else {
            write!(f, "(")?;
            self.0.fmt(f)?;
            for (_operator, operand) in &self.1.0 {
                write!(f, " ** {operand:?}")?;
            }
            write!(f, ")")
        }
    }
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(no_selector)]
enum Terminal {
    Category(InSquares<Category>),
    Missing(keyword::Missing),
    OtherNm(keyword::OtherNm),
    Subtotal(keyword::Subtotal, Option<InSquares<Integer>>),
    Total(keyword::Total),
    Number(Number),
    Parens(InParens<Box<Expression>>),
}

#[derive(Debug, pspp_derive::FromTokens)]
struct Category {
    min: Value,
    max: Option<(keyword::Thru, Value)>,
}

#[derive(Debug, pspp_derive::FromTokens)]
#[pspp(no_selector)]
enum Value {
    Lo(keyword::Lo),
    Hi(keyword::Hi),
    Number(Number),
    String(String),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum PProperties {
    Label(Equals, String),
    Format(Equals, Seq1<Summary>),
    HideSourceCats(Equals, Boolean),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Boolean {
    Yes,
    No,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum SLabel {
    Position(Equals, Position),
    Visible(Equals, Boolean),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Position {
    Column,
    Row,
    Layer,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum CLabel {
    Auto,
    RowLabels(Equals, LabelDestination),
    ColLabels(Equals, LabelDestination),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum LabelDestination {
    Opposite,
    Layer,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Categories {
    Variables(Equals, VarList),
    Order(Equals, Direction),
    Key(Equals, Key),
    Missing(Equals, Include),
    Total(Equals, Boolean),
    Label(Equals, String),
    Position(Equals, CategoryPosition),
    Empty(Equals, Include),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Direction {
    #[pspp(syntax = "A")]
    Ascending,
    #[pspp(syntax = "D")]
    Descending,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Key {
    Value,
    Label,
    #[pspp(no_selector)]
    Summary(Summary),
}

#[derive(Debug, pspp_derive::FromTokens)]
enum Include {
    Include,
    Exclude,
}

#[derive(Debug, pspp_derive::FromTokens)]
enum CategoryPosition {
    After,
    Before,
}

mod keyword {
    use crate::command::{FromTokens, ParseResult, TokenSlice};

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Default;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Expr;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Zero;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Blank;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Thru;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Hi;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Lo;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Missing;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct OtherNm;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Subtotal;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Total;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Variable;

    #[derive(Debug, pspp_derive::FromTokens)]
    pub struct Count;
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
        test(
            "ctables /pcompute &all_drivers =expr(1+2*3+4)
 /pcompute &all_drivers =expr(1).",
        );
    }
}
