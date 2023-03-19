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

use crate::identifier::id_match_n_nonstatic;

pub struct Match {
    pub exact: bool,
    pub missing_words: isize,
}

/// Compares `string` obtained from the user against the full name of a `command`,
/// using this algorithm:
///
///   1. Divide `command` into words `c[0]` through `c[n - 1]`.
///
///   2. Divide `string` into words `s[0]` through `s[m - 1]`.
///
///   3. Compare word `c[i]` against `s[i]` for `0 <= i < min(n, m)`, using the keyword
///      matching algorithm implemented by lex_id_match().  If any of them fail to
///      match, then `string` does not match `command` and the function returns false.
///
///   4. Otherwise, `string` and `command` match.  Set *MISSING_WORDS to n - m.  Set
///      *EXACT to false if any of the `S[i]` were found to be abbreviated in the
///      comparisons done in step 3, or to true if they were all exactly equal
///      (modulo case).  Return true.
pub fn command_match(command: &str, string: &str) -> Option<Match> {
    let mut command_words = command.split_whitespace();
    let mut string_words = string.split_whitespace();
    let mut exact = true;
    loop {
        let Some(cw) = command_words.next() else {
            return Some(Match {
                exact,
                missing_words: -(string_words.count() as isize),
            });
        };
        let Some(sw) = string_words.next() else {
            return Some(Match {
                exact,
                missing_words: 1 + command_words.count() as isize,
            });
        };
        if !id_match_n_nonstatic(cw, sw, 3) {
            return None;
        }
        if sw.len() < cw.len() {
            exact = false;
        }
    }
}

/// Matches a string against a collection of command names.
pub struct CommandMatcher<'a, T> {
    string: &'a str,
    extensible: bool,
    exact_match: Option<T>,
    n_matches: usize,
    match_: Option<T>,
    match_missing_words: isize,
}

impl<'a, T> CommandMatcher<'a, T> {
    pub fn new(string: &'a str) -> Self {
        Self {
            string,
            extensible: false,
            exact_match: None,
            n_matches: 0,
            match_: None,
            match_missing_words: 0,
        }
    }

    /// Consider `command` as a candidate for the command name being parsed. If
    /// `command` is the correct command name, then [Self::get_match] will
    /// return `aux` later.
    pub fn add(&mut self, command: &str, aux: T) {
        if let Some(Match {
            missing_words,
            exact,
        }) = command_match(command, self.string)
        {
            if missing_words > 0 {
                self.extensible = true;
            } else if exact && missing_words == 0 {
                self.exact_match = Some(aux);
            } else {
                if missing_words > self.match_missing_words {
                    self.n_matches = 0;
                }
                if missing_words >= self.match_missing_words || self.n_matches == 0 {
                    self.n_matches += 1;
                    self.match_ = Some(aux);
                    self.match_missing_words = missing_words;
                }
            }
        }
    }

    pub fn get_match(self) -> (Option<T>, isize) {
        if self.extensible {
            (None, 1)
        } else if let Some(exact_match) = self.exact_match {
            (Some(exact_match), 0)
        } else if self.n_matches == 1 {
            (self.match_, self.match_missing_words)
        } else {
            (None, self.match_missing_words)
        }
    }
}

pub const COMMAND_NAMES: &[&str] = &[
    "2SLS",
    "ACF",
    "ADD DOCUMENT",
    "ADD FILES",
    "ADD VALUE LABELS",
    "AGGREGATE",
    "ALSCAL",
    "ANACOR",
    "ANOVA",
    "APPLY DICTIONARY",
    "AUTORECODE",
    "BEGIN DATA",
    "BREAK",
    "CACHE",
    "CASEPLOT",
    "CASESTOVARS",
    "CATPCA",
    "CATREG",
    "CCF",
    "CD",
    "CLEAR TRANSFORMATIONS",
    "CLOSE FILE HANDLE",
    "CLUSTER",
    "COMPUTE",
    "CONJOINT",
    "CORRELATIONS",
    "CORRESPONDENCE",
    "COUNT",
    "COXREG",
    "CREATE",
    "CROSSTABS",
    "CSDESCRIPTIVES",
    "CSGLM",
    "CSLOGISTIC",
    "CSPLAN",
    "CSSELECT",
    "CSTABULATE",
    "CTABLES",
    "CURVEFIT",
    "DATA LIST",
    "DATAFILE ATTRIBUTE",
    "DATASET ACTIVATE",
    "DATASET CLOSE",
    "DATASET COPY",
    "DATASET DECLARE",
    "DATASET DISPLAY",
    "DATASET NAME",
    "DATE",
    "DEBUG EVALUATE",
    "DEBUG EXPAND",
    "DEBUG FLOAT FORMAT",
    "DEBUG FORMAT GUESSER",
    "DEBUG MATRIX READ",
    "DEBUG MOMENTS",
    "DEBUG PAPER SIZE",
    "DEBUG POOL",
    "DEBUG XFORM FAIL",
    "DEFINE",
    "DELETE VARIABLES",
    "DESCRIPTIVES",
    "DETECTANOMALY",
    "DISCRIMINANT",
    "DISPLAY MACROS",
    "DISPLAY VARIABLE SETS",
    "DISPLAY",
    "DO IF",
    "DO REPEAT",
    "DOCUMENT",
    "DROP DOCUMENTS",
    "ECHO",
    "EDIT",
    "ELSE IF",
    "ELSE",
    "END CASE",
    "END FILE TYPE",
    "END FILE",
    "END IF",
    "END LOOP",
    "END REPEAT",
    "ERASE",
    "EXAMINE",
    "EXECUTE",
    "EXIT",
    "EXPORT",
    "FACTOR",
    "FILE HANDLE",
    "FILE LABEL",
    "FILE TYPE",
    "FILTER",
    "FINISH",
    "FIT",
    "FLIP",
    "FORMATS",
    "FREQUENCIES",
    "GENLOG",
    "GET DATA",
    "GET TRANSLATE",
    "GET",
    "GGRAPH",
    "GLM",
    "GRAPH",
    "HILOGLINEAR",
    "HOMALS",
    "HOST",
    "IF",
    "IGRAPH",
    "IMPORT",
    "INCLUDE",
    "INFO",
    "INPUT PROGRAM",
    "INSERT",
    "KEYED DATA LIST",
    "KM",
    "LEAVE",
    "LIST",
    "LOGISTIC REGRESSION",
    "LOGLINEAR",
    "LOOP",
    "MANOVA",
    "MAPS",
    "MATCH FILES",
    "MATRIX DATA",
    "MATRIX",
    "MCONVERT",
    "MEANS",
    "MISSING VALUES",
    "MIXED",
    "MODEL CLOSE",
    "MODEL HANDLE",
    "MODEL LIST",
    "MODEL NAME",
    "MRSETS",
    "MULT RESPONSE",
    "MULTIPLE CORRESPONDENCE",
    "MVA",
    "N OF CASES",
    "N",
    "NAIVEBAYES",
    "NEW FILE",
    "NLR",
    "NOMREG",
    "NONPAR CORR",
    "NPAR TESTS",
    "NUMBERED",
    "NUMERIC",
    "OLAP CUBES",
    "OMS",
    "ONEWAY",
    "ORTHOPLAN",
    "OUTPUT MODIFY",
    "OVERALS",
    "PACF",
    "PARTIAL CORR",
    "PEARSON CORRELATIONS",
    "PERMISSIONS",
    "PLANCARDS",
    "PLUM",
    "POINT",
    "PPLOT",
    "PREDICT",
    "PREFSCAL",
    "PRESERVE",
    "PRINCALS",
    "PRINT EJECT",
    "PRINT FORMATS",
    "PRINT SPACE",
    "PRINT",
    "PROBIT",
    "PROCEDURE OUTPUT",
    "PROXIMITIES",
    "PROXSCAL",
    "Q",
    "QUICK CLUSTER",
    "QUIT",
    "RANK",
    "RATIO STATISTICS",
    "READ MODEL",
    "RECODE",
    "RECORD TYPE",
    "REFORMAT",
    "REGRESSION",
    "RELIABILITY",
    "RENAME VARIABLES",
    "REPEATING DATA",
    "REPORT",
    "REREAD",
    "RESTORE",
    "RMV",
    "ROC",
    "SAMPLE",
    "SAVE DATA COLLECTION",
    "SAVE TRANSLATE",
    "SAVE",
    "SCRIPT",
    "SEASON",
    "SELECT IF",
    "SELECTPRED",
    "SET",
    "SHOW",
    "SORT CASES",
    "SORT VARIABLES",
    "SPCHART",
    "SPECTRA",
    "SPLIT FILE",
    "STEMLEAF",
    "STRING",
    "SUBTITLE",
    "SUMMARIZE",
    "SURVIVAL",
    "SYSFILE INFO",
    "T-TEST",
    "TDISPLAY",
    "TEMPORARY",
    "TITLE",
    "TREE",
    "TSAPPLY",
    "TSET",
    "TSHOW",
    "TSMODEL",
    "TSPLOT",
    "TWOSTEP CLUSTER",
    "UNIANOVA",
    "UNNUMBERED",
    "UPDATE",
    "USE",
    "VALIDATEDATA",
    "VALUE LABELS",
    "VARCOMP",
    "VARIABLE ALIGNMENT",
    "VARIABLE ATTRIBUTE",
    "VARIABLE LABELS",
    "VARIABLE LEVEL",
    "VARIABLE ROLE",
    "VARIABLE WIDTH",
    "VARSTOCASES",
    "VECTOR",
    "VERIFY",
    "WEIGHT",
    "WLS",
    "WRITE FORMATS",
    "WRITE",
    "XEXPORT",
    "XGRAPH",
    "XSAVE",
];
