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
    command::parse_command,
    lex::lexer::{Source, TokenSlice},
    macros::MacroSet,
    message::Diagnostic,
};
use std::rc::Rc;

#[derive(Default)]
pub struct Engine;

impl Engine {
    pub fn new() -> Self {
        Self
    }
    pub fn run(&mut self, mut source: Source) {
        let macros = MacroSet::new();
        while let Some(tokens) = source.read_command(&macros) {
            let error: Box<dyn Fn(Diagnostic)> = Box::new(|diagnostic| {
                println!("{diagnostic}");
            });
            parse_command(TokenSlice::new(Rc::new(tokens)), &error);
        }
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use encoding_rs::UTF_8;

    use crate::lex::lexer::{Source, SyntaxFile};

    use super::Engine;

    #[test]
    #[ignore]
    fn test_echo() {
        let mut engine = Engine::new();
        engine.run(Source::new_default(&Arc::new(SyntaxFile::new(
            "ECHO 'hi there'.\nECHO 'bye there'.\n".to_string(),
            Some("test.sps".to_string()),
            UTF_8,
        ))));
    }

    #[test]
    fn test_descriptives() {
        let mut engine = Engine::new();
        engine.run(Source::new_default(&Arc::new(
            SyntaxFile::new(
                "DESCRIPTIVES VARIABLES=a (za) b to c/MISSING=x y z/MISSING=VARIABLE INCLUDE/STATISTICS=DEFAULT/SAVE/SORT=SKEWNESS (A)\n".to_string(),
                Some("test.sps".to_string()),
                UTF_8,
            ),
        )));
    }
}
