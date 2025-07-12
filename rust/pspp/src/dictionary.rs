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

//! Dictionaries and variables.

use core::str;
use std::{
    borrow::Cow,
    cmp::Ordering,
    collections::{BTreeMap, BTreeSet, HashMap, HashSet},
    fmt::{Debug, Display, Formatter, Result as FmtResult},
    hash::Hash,
    ops::{Bound, Not, RangeBounds, RangeInclusive},
    str::FromStr,
};

use encoding_rs::Encoding;
use enum_map::{Enum, EnumMap};
use indexmap::IndexSet;
use num::integer::div_ceil;
use thiserror::Error as ThisError;
use unicase::UniCase;

use crate::{
    data::Datum,
    format::{DisplayPlain, Format},
    identifier::{ByIdentifier, HasIdentifier, Identifier},
    output::pivot::{Axis3, Dimension, Footnote, Footnotes, Group, PivotTable, Value},
    settings::Show,
};

/// An index within [Dictionary::variables].
pub type DictIndex = usize;

/// Variable type.
#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum VarType {
    /// A numeric variable.
    Numeric,

    /// A string variable.
    String,
}

impl Not for VarType {
    type Output = Self;

    fn not(self) -> Self::Output {
        match self {
            Self::Numeric => Self::String,
            Self::String => Self::Numeric,
        }
    }
}

impl Not for &VarType {
    type Output = VarType;

    fn not(self) -> Self::Output {
        !*self
    }
}

impl Display for VarType {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        match self {
            VarType::Numeric => write!(f, "numeric"),
            VarType::String => write!(f, "string"),
        }
    }
}

/// [VarType], plus a width for [VarType::String].
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum VarWidth {
    Numeric,
    String(u16),
}

impl PartialOrd for VarWidth {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        match (self, other) {
            (VarWidth::Numeric, VarWidth::Numeric) => Some(Ordering::Equal),
            (VarWidth::String(a), VarWidth::String(b)) => Some(a.cmp(b)),
            _ => None,
        }
    }
}

impl VarWidth {
    pub const MAX_STRING: u16 = 32767;

    pub fn n_dict_indexes(self) -> usize {
        match self {
            VarWidth::Numeric => 1,
            VarWidth::String(w) => div_ceil(w as usize, 8),
        }
    }

    fn width_predicate(
        a: Option<VarWidth>,
        b: Option<VarWidth>,
        f: impl Fn(u16, u16) -> u16,
    ) -> Option<VarWidth> {
        match (a, b) {
            (Some(VarWidth::Numeric), Some(VarWidth::Numeric)) => Some(VarWidth::Numeric),
            (Some(VarWidth::String(a)), Some(VarWidth::String(b))) => {
                Some(VarWidth::String(f(a, b)))
            }
            _ => None,
        }
    }

    /// Returns the wider of `self` and `other`:
    /// - Numerical variable widths are equally wide.
    /// - Longer strings are wider than shorter strings.
    /// - Numerical and string types are incomparable, so result in `None`.
    /// - Any `None` in the input yields `None` in the output.
    pub fn wider(a: Option<VarWidth>, b: Option<VarWidth>) -> Option<VarWidth> {
        Self::width_predicate(a, b, |a, b| a.max(b))
    }

    /// Returns the narrower of `self` and `other` (see [`Self::wider`]).
    pub fn narrower(a: Option<VarWidth>, b: Option<VarWidth>) -> Option<VarWidth> {
        Self::width_predicate(a, b, |a, b| a.min(b))
    }

    pub fn default_display_width(&self) -> u32 {
        match self {
            VarWidth::Numeric => 8,
            VarWidth::String(width) => *width.min(&32) as u32,
        }
    }

    pub fn is_long_string(&self) -> bool {
        if let Self::String(width) = self {
            *width > 8
        } else {
            false
        }
    }

    pub fn as_string_width(&self) -> Option<usize> {
        match self {
            VarWidth::Numeric => None,
            VarWidth::String(width) => Some(*width as usize),
        }
    }

    pub fn is_numeric(&self) -> bool {
        *self == Self::Numeric
    }

    pub fn is_string(&self) -> bool {
        !self.is_numeric()
    }

    pub fn is_very_long(&self) -> bool {
        match *self {
            VarWidth::Numeric => false,
            VarWidth::String(width) => width >= 256,
        }
    }

    /// Number of bytes per segment by which the amount of space for very long
    /// string variables is allocated.
    const EFFECTIVE_VLS_CHUNK: usize = 252;

    /// Returns the number of "segments" used for writing case data for a
    /// variable with this width.  A segment is a physical variable in the
    /// system file that represents some piece of a logical variable as seen by
    /// a PSPP user.  Only very long string variables have more than one
    /// segment.
    pub fn n_segments(&self) -> usize {
        if self.is_very_long() {
            self.as_string_width()
                .unwrap()
                .div_ceil(Self::EFFECTIVE_VLS_CHUNK)
        } else {
            1
        }
    }

    /// Returns the width to allocate to the segment with the given
    /// `segment_idx` within this variable.  A segment is a physical variable in
    /// the system file that represents some piece of a logical variable as seen
    /// by a PSPP user.
    pub fn segment_alloc_width(&self, segment_idx: usize) -> usize {
        debug_assert!(segment_idx < self.n_segments());
        debug_assert!(self.is_very_long());

        if segment_idx < self.n_segments() - 1 {
            255
        } else {
            self.as_string_width().unwrap() - segment_idx * Self::EFFECTIVE_VLS_CHUNK
        }
    }

    pub fn display_adjective(&self) -> VarWidthAdjective {
        VarWidthAdjective(*self)
    }
}

impl From<VarWidth> for VarType {
    fn from(source: VarWidth) -> Self {
        match source {
            VarWidth::Numeric => VarType::Numeric,
            VarWidth::String(_) => VarType::String,
        }
    }
}

pub struct VarWidthAdjective(VarWidth);

impl Display for VarWidthAdjective {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self.0 {
            VarWidth::Numeric => write!(f, "numeric"),
            VarWidth::String(width) => write!(f, "{width}-byte string"),
        }
    }
}

/// A collection of variables, plus additional metadata.
#[derive(Clone, Debug)]
pub struct Dictionary {
    /// The variables.
    pub variables: IndexSet<ByIdentifier<Variable>>,

    /// Indexes into `variables` of the `SPLIT FILE` variables.
    pub split_file: Vec<DictIndex>,

    /// Index of the weight variable, if any.
    ///
    /// The weight variable must be numeric.
    pub weight: Option<DictIndex>,

    /// Index of the filter variable, if any.
    ///
    /// The filter variable must be numeric.  If there is a filter variable,
    /// then data analysis excludes cases whose filter value is zero or system-
    /// or user-missing.
    pub filter: Option<DictIndex>,

    /// An optional limit on the number of cases read by procedures.
    pub case_limit: Option<u64>,

    /// Optional label (name) for the dictionary.
    pub file_label: Option<String>,

    /// Optional additional documentation associated with the dictionary.
    pub documents: Vec<String>,

    /// Named collections of variables within the dictionary.
    pub vectors: HashSet<ByIdentifier<Vector>>,

    /// Attributes for the dictionary itself.
    ///
    /// Individual variables can have their own attributes.
    pub attributes: Attributes,

    /// Multiple response sets.
    pub mrsets: BTreeSet<ByIdentifier<MultipleResponseSet>>,

    /// Variable sets.
    ///
    /// Only the GUI makes use of variable sets.
    pub variable_sets: Vec<VariableSet>,

    /// Character encoding for the dictionary and the data.
    pub encoding: &'static Encoding,
}

#[derive(Debug, ThisError)]
pub enum AddVarError {
    #[error("Duplicate variable name {0}.")]
    DuplicateVariableName(Identifier),

    #[error("Variable encoding {} does not match dictionary encoding {}.", var_encoding.name(), dict_encoding.name())]
    WrongEncoding {
        var_encoding: &'static Encoding,
        dict_encoding: &'static Encoding,
    },
}

impl Dictionary {
    /// Creates a new, empty dictionary with the specified `encoding`.
    pub fn new(encoding: &'static Encoding) -> Self {
        Self {
            variables: IndexSet::new(),
            split_file: Vec::new(),
            weight: None,
            filter: None,
            case_limit: None,
            file_label: None,
            documents: Vec::new(),
            vectors: HashSet::new(),
            attributes: Attributes::new(),
            mrsets: BTreeSet::new(),
            variable_sets: Vec::new(),
            encoding,
        }
    }

    /// Returns a reference to the weight variable, if any.
    pub fn weight_var(&self) -> Option<&Variable> {
        self.weight.map(|index| &self.variables[index].0)
    }

    /// Returns references to all the split variables, if any.
    pub fn split_vars(&self) -> Vec<&Variable> {
        self.split_file
            .iter()
            .map(|index| &self.variables[*index].0)
            .collect()
    }

    /// Adds `variable` at the end of the dictionary and returns its index.
    ///
    /// The operation fails if the dictionary already contains a variable with
    /// the same name (or a variant with different case), or if `variable`'s
    /// encoding differs from the dictionary's.
    pub fn add_var(&mut self, variable: Variable) -> Result<DictIndex, AddVarError> {
        if variable.encoding != self.encoding {
            Err(AddVarError::WrongEncoding {
                var_encoding: variable.encoding,
                dict_encoding: self.encoding,
            })
        } else {
            match self.variables.insert_full(ByIdentifier::new(variable)) {
                (index, true) => Ok(index),
                (index, false) => Err(AddVarError::DuplicateVariableName(
                    self.variables[index].name.clone(),
                )),
            }
        }
    }

    /// Reorders the variables in the dictionary so that the variable with
    /// 0-based index `from_index` is moved to `to_index`.  Other variables stay
    /// in the same relative positions.
    ///
    /// # Panics
    ///
    /// Panics if `from_index` or `to_index` is not valid.
    pub fn reorder_var(&mut self, from_index: DictIndex, to_index: DictIndex) {
        debug_assert!(from_index < self.variables.len());
        debug_assert!(to_index < self.variables.len());
        if from_index != to_index {
            self.variables.move_index(from_index, to_index);
            self.update_dict_indexes(&|index| {
                #[allow(clippy::collapsible_else_if)]
                if index == from_index {
                    Some(to_index)
                } else if from_index < to_index {
                    if index > from_index && index <= to_index {
                        Some(index - 1)
                    } else {
                        Some(index)
                    }
                } else {
                    if index >= to_index && index < from_index {
                        Some(index + 1)
                    } else {
                        Some(index)
                    }
                }
            })
        }
    }

    /// Evaluates `keep` on each variable in the dictionary and deletes
    /// variables for which it returns false.
    pub fn retain_vars<F>(&mut self, keep: F)
    where
        F: Fn(&Variable) -> bool,
    {
        let mut deleted = Vec::new();
        let mut index = 0;
        self.variables.retain(|var_by_id| {
            let keep = keep(&var_by_id.0);
            if !keep {
                deleted.push(index);
            }
            index += 1;
            keep
        });
        if !deleted.is_empty() {
            self.update_dict_indexes(&|index| deleted.binary_search(&index).err())
        }
    }

    /// Deletes the variables whose indexes are in the given `range`.
    ///
    /// # Panic
    ///
    /// Panics if any part of `range` is outside the valid range of variable
    /// indexes.
    pub fn delete_vars<R>(&mut self, range: R)
    where
        R: RangeBounds<DictIndex>,
    {
        let start = match range.start_bound() {
            Bound::Included(&start) => start,
            Bound::Excluded(&start) => start + 1,
            Bound::Unbounded => 0,
        };
        let end = match range.end_bound() {
            Bound::Included(&end) => end + 1,
            Bound::Excluded(&end) => end,
            Bound::Unbounded => self.variables.len(),
        };
        if end > start {
            self.variables.drain(start..end);
            self.update_dict_indexes(&|index| {
                if index < start {
                    Some(index)
                } else if index < end {
                    None
                } else {
                    Some(index - end - start)
                }
            })
        }
    }

    fn update_dict_indexes<F>(&mut self, f: &F)
    where
        F: Fn(DictIndex) -> Option<DictIndex>,
    {
        update_dict_index_vec(&mut self.split_file, f);
        self.weight = self.weight.and_then(f);
        self.filter = self.filter.and_then(f);
        self.vectors = self
            .vectors
            .drain()
            .filter_map(|vector_by_id| {
                vector_by_id
                    .0
                    .with_updated_dict_indexes(f)
                    .map(ByIdentifier::new)
            })
            .collect();
        self.mrsets = std::mem::take(&mut self.mrsets)
            .into_iter()
            .filter_map(|mrset_by_id| {
                mrset_by_id
                    .0
                    .with_updated_dict_indexes(f)
                    .map(ByIdentifier::new)
            })
            .collect();
        self.variable_sets = self
            .variable_sets
            .drain(..)
            .filter_map(|var_set| var_set.with_updated_dict_indexes(f))
            .collect();
    }

    /// Attempts to change the name of the variable with the given `index` to
    /// `new_name`.  Returns `Ok(())` if successful; otherwise, if `new_name`
    /// would duplicate the name of some other variable, returns `new_name` as
    /// an error.
    pub fn try_rename_var(&mut self, index: usize, new_name: Identifier) -> Result<(), Identifier> {
        let mut variable = self.variables.swap_remove_index(index).unwrap();
        let may_rename = !self.variables.contains(&new_name.0);
        let retval = if may_rename {
            variable.name = new_name;
            variable.short_names = Vec::new();
            Ok(())
        } else {
            Err(new_name)
        };
        assert!(self.variables.insert(variable));
        self.variables.swap_indices(self.variables.len() - 1, index);
        retval
    }

    /// Changes the name of the variable with given `index` to `new_name`.
    ///
    /// # Panics
    ///
    /// Panics if the new name duplicates the name of some existing variable.
    pub fn rename_var(&mut self, index: usize, new_name: Identifier) {
        assert!(self.try_rename_var(index, new_name).is_ok());
    }

    pub fn output_variables(&self) -> OutputVariables {
        OutputVariables::new(self)
    }

    pub fn output_value_labels(&self) -> OutputValueLabels {
        OutputValueLabels::new(self)
    }

    pub fn output_variable_sets(&self) -> OutputVariableSets {
        OutputVariableSets::new(self)
    }

    pub fn output_mrsets(&self) -> OutputMrsets {
        OutputMrsets::new(self)
    }

    pub fn output_attributes(&self) -> OutputAttributes {
        OutputAttributes::new(self)
    }

    pub fn to_pivot_rows(&self) -> (Group, Vec<Value>) {
        let mut group = Group::new("Dictionary Information");
        let mut values = Vec::new();

        group.push("Label");
        values.push(
            self.file_label
                .as_ref()
                .map(|label| Value::new_user_text(label))
                .unwrap_or_default(),
        );

        group.push("Variables");
        values.push(Value::new_integer(Some(self.variables.len() as f64)));

        group.push("Weight");
        match self.weight_var() {
            Some(variable) => values.push(Value::new_variable(variable)),
            None => values.push(Value::empty()),
        }

        group.push("Documents");
        values.push(Value::new_user_text(
            self.documents
                .iter()
                .flat_map(|s| [s.as_str(), "\n"])
                .collect::<String>(),
        ));

        (group, values)
    }
}

pub struct OutputVariables<'a> {
    dictionary: &'a Dictionary,
    fields: EnumMap<VariableField, bool>,
}

impl<'a> OutputVariables<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self {
            dictionary,
            fields: EnumMap::from_fn(|_field: VariableField| true),
        }
    }
    pub fn to_pivot_table(&self) -> PivotTable {
        let mut names = Group::new("Name");
        for variable in &self.dictionary.variables {
            names.push(Value::new_variable(variable));
        }

        let mut attributes = Group::new("Attributes");
        let mut columns = Vec::new();
        for field in self
            .fields
            .iter()
            .filter_map(|(field, include)| include.then_some(field))
        {
            columns.push((field, attributes.len()));
            attributes.push(field.as_str());
        }

        let mut pt = PivotTable::new([
            (Axis3::Y, Dimension::new(names)),
            (Axis3::X, Dimension::new(attributes)),
        ])
        .with_show_empty();
        for (var_index, variable) in self.dictionary.variables.iter().enumerate() {
            for (field, field_index) in &columns {
                if let Some(value) = Self::get_field_value(var_index, variable, *field) {
                    pt.insert(&[var_index, *field_index], value);
                }
            }
        }

        pt
    }

    fn get_field_value(index: usize, variable: &Variable, field: VariableField) -> Option<Value> {
        match field {
            VariableField::Position => Some(Value::new_integer(Some(index as f64 + 1.0))),
            VariableField::Label => variable.label().map(|label| Value::new_user_text(label)),
            VariableField::Measure => variable
                .measure
                .map(|measure| Value::new_text(measure.as_str())),
            VariableField::Role => Some(Value::new_text(variable.role.as_str())),
            VariableField::Width => Some(Value::new_integer(Some(variable.display_width as f64))),
            VariableField::Alignment => Some(Value::new_text(variable.alignment.as_str())),
            VariableField::PrintFormat => {
                Some(Value::new_user_text(variable.print_format.to_string()))
            }
            VariableField::WriteFormat => {
                Some(Value::new_user_text(variable.write_format.to_string()))
            }
            VariableField::MissingValues if !variable.missing_values.is_empty() => {
                Some(Value::new_user_text(
                    variable
                        .missing_values
                        .display(variable.encoding)
                        .to_string(),
                ))
            }
            VariableField::MissingValues => None,
        }
    }
}

pub struct OutputValueLabels<'a> {
    dictionary: &'a Dictionary,
}

impl<'a> OutputValueLabels<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self { dictionary }
    }
    pub fn any_value_labels(&self) -> bool {
        self.dictionary
            .variables
            .iter()
            .any(|variable| !variable.value_labels.is_empty())
    }
    pub fn to_pivot_table(&self) -> Option<PivotTable> {
        if !self.any_value_labels() {
            return None;
        }

        let mut values = Group::new("Variable Value").with_label_shown();
        let mut footnotes = Footnotes::new();
        let missing_footnote = footnotes.push(Footnote::new("User-missing value"));
        let mut data = Vec::new();
        for variable in self
            .dictionary
            .variables
            .iter()
            .filter(|var| !var.value_labels.is_empty())
        {
            let mut group = Group::new(&**variable);
            let mut sorted_value_labels = variable.value_labels.0.iter().collect::<Vec<_>>();
            sorted_value_labels.sort();
            for (datum, label) in sorted_value_labels {
                let mut value = Value::new_variable_value(variable, datum)
                    .with_show_value_label(Some(Show::Value));
                if variable.missing_values.contains(datum) {
                    value.add_footnote(&missing_footnote);
                }
                group.push(value);

                data.push(
                    Value::new_variable_value(variable, datum)
                        .with_show_value_label(Some(Show::Label))
                        .with_value_label(Some(escape_value_label(label.as_str()).into())),
                );
            }
            values.push(group);
        }
        let mut pt = PivotTable::new([(Axis3::Y, Dimension::new(values))]);
        for (row, datum) in data.into_iter().enumerate() {
            pt.insert(&[row], datum);
        }
        Some(pt)
    }
}

#[derive(Copy, Clone, Debug, Enum)]
enum VariableField {
    Position,
    Label,
    Measure,
    Role,
    Width,
    Alignment,
    PrintFormat,
    WriteFormat,
    MissingValues,
}

impl VariableField {
    pub fn as_str(&self) -> &'static str {
        match self {
            VariableField::Position => "Position",
            VariableField::Label => "Label",
            VariableField::Measure => "Measurement Level",
            VariableField::Role => "Role",
            VariableField::Width => "Width",
            VariableField::Alignment => "Alignment",
            VariableField::PrintFormat => "Print Format",
            VariableField::WriteFormat => "Write Format",
            VariableField::MissingValues => "Missing Values",
        }
    }
}

pub struct OutputVariableSets<'a> {
    dictionary: &'a Dictionary,
}

impl<'a> OutputVariableSets<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self { dictionary }
    }
    pub fn any_variable_sets(&self) -> bool {
        !self.dictionary.variable_sets.is_empty()
    }
    pub fn to_pivot_table(&self) -> Option<PivotTable> {
        if !self.any_variable_sets() {
            return None;
        }

        let mut variable_sets = Group::new("Variable Set and Position").with_label_shown();
        let mut data = Vec::new();
        for vs in &self.dictionary.variable_sets {
            let mut group = Group::new(vs.name.as_str());
            for (variable, index) in vs.variables.iter().copied().zip(1usize..) {
                group.push(Value::new_integer(Some(index as f64)));
                data.push(Value::new_variable(&self.dictionary.variables[variable]));
            }
            if vs.variables.is_empty() {
                group.push(Value::new_text("n/a"));
                data.push(Value::new_text("(empty)"));
            }
            variable_sets.push(group);
        }
        let mut pt = PivotTable::new([
            (Axis3::Y, Dimension::new(variable_sets)),
            (
                Axis3::X,
                Dimension::new(Group::new("Attributes").with("Variable")),
            ),
        ]);
        for (row, datum) in data.into_iter().enumerate() {
            pt.insert(&[row, 0], datum);
        }
        Some(pt)
    }
}

pub struct OutputMrsets<'a> {
    dictionary: &'a Dictionary,
}

impl<'a> OutputMrsets<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self { dictionary }
    }
    pub fn any_mrsets(&self) -> bool {
        !self.dictionary.mrsets.is_empty()
    }
    pub fn to_pivot_table(&self) -> Option<PivotTable> {
        if !self.any_mrsets() {
            return None;
        }

        let attributes = Group::new("Attributes")
            .with("Label")
            .with("Encoding")
            .with("Counted Value")
            .with("Member Variables");

        let mut mrsets = Group::new("Name").with_label_shown();
        for mrset in &self.dictionary.mrsets {
            mrsets.push(mrset.name.as_str());
        }
        let mut pt = PivotTable::new([
            (Axis3::Y, Dimension::new(mrsets)),
            (Axis3::X, Dimension::new(attributes)),
        ]);
        for (row, mrset) in self.dictionary.mrsets.iter().enumerate() {
            pt.insert(&[row, 0], mrset.label.as_str());

            let mr_type_name = match &mrset.mr_type {
                MultipleResponseType::MultipleDichotomy { datum, .. } => {
                    pt.insert(&[row, 2], Value::new_datum(datum, self.dictionary.encoding));
                    "Dichotomies"
                }
                MultipleResponseType::MultipleCategory => "Categories",
            };

            pt.insert(&[row, 1], Value::new_text(mr_type_name));
            pt.insert(
                &[row, 3],
                mrset
                    .variables
                    .iter()
                    .flat_map(|index| [self.dictionary.variables[*index].name.as_str(), "\n"])
                    .collect::<String>(),
            );
        }
        Some(pt)
    }
}

pub struct OutputAttributes<'a> {
    dictionary: &'a Dictionary,

    /// Include attributes whose names begin with `@`?
    include_at: bool,
}

impl<'a> OutputAttributes<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self {
            dictionary,
            include_at: true,
        }
    }
    pub fn without_at(self) -> Self {
        Self {
            include_at: false,
            ..self
        }
    }
    pub fn any_attributes(&self) -> bool {
        self.attribute_sets().next().is_some()
    }
    fn attribute_sets(&self) -> impl Iterator<Item = (Option<&Variable>, &Attributes)> {
        std::iter::once((None, &self.dictionary.attributes))
            .chain(
                self.dictionary
                    .variables
                    .iter()
                    .map(|var| (Some(&**var), &var.attributes)),
            )
            .filter(|(_name, attributes)| attributes.has_any(self.include_at))
    }
    pub fn to_pivot_table(&self) -> Option<PivotTable> {
        if !self.any_attributes() {
            return None;
        }

        let mut variables = Group::new("Variable and Name").with_label_shown();
        let mut data = Vec::new();
        for (variable, attributes) in self.attribute_sets() {
            let group_name = match variable {
                Some(variable) => Value::new_variable(variable),
                None => Value::new_text("(dataset)"),
            };
            let mut group = Group::new(group_name);
            for (name, values) in &attributes.0 {
                if values.len() == 1 {
                    group.push(Value::new_user_text(name.as_str()));
                } else {
                    for index in 1..=values.len() {
                        group.push(Value::new_user_text(format!("{name}[{index}]")));
                    }
                }
                for value in values {
                    data.push(Value::new_user_text(value));
                }
            }
            variables.push(group);
        }
        let values = Group::new("Value").with("Value");
        let mut pt = PivotTable::new([
            (Axis3::X, Dimension::new(values)),
            (Axis3::Y, Dimension::new(variables)),
        ]);
        for (row, datum) in data.into_iter().enumerate() {
            pt.insert(&[0, row], datum);
        }
        Some(pt)
    }
}

fn update_dict_index_vec<F>(dict_indexes: &mut Vec<DictIndex>, f: F)
where
    F: Fn(DictIndex) -> Option<DictIndex>,
{
    dict_indexes.retain_mut(|index| {
        if let Some(new) = f(*index) {
            *index = new;
            true
        } else {
            false
        }
    });
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Role {
    #[default]
    Input,
    Target,
    Both,
    None,
    Partition,
    Split,
}

impl Role {
    fn as_str(&self) -> &'static str {
        match self {
            Role::Input => "Input",
            Role::Target => "Target",
            Role::Both => "Both",
            Role::None => "None",
            Role::Partition => "Partition",
            Role::Split => "Split",
        }
    }
}

impl FromStr for Role {
    type Err = InvalidRole;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        for (string, value) in [
            ("input", Role::Input),
            ("target", Role::Target),
            ("both", Role::Both),
            ("none", Role::None),
            ("partition", Role::Partition),
            ("split", Role::Split),
        ] {
            if string.eq_ignore_ascii_case(s) {
                return Ok(value);
            }
        }
        Err(InvalidRole::UnknownRole(s.into()))
    }
}

impl TryFrom<i32> for Role {
    type Error = InvalidRole;

    fn try_from(value: i32) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Role::Input),
            1 => Ok(Role::Target),
            2 => Ok(Role::Both),
            3 => Ok(Role::None),
            4 => Ok(Role::Partition),
            5 => Ok(Role::Split),
            _ => Err(InvalidRole::UnknownRole(value.to_string())),
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Attributes(pub BTreeMap<Identifier, Vec<String>>);

impl Attributes {
    pub fn new() -> Self {
        Self(BTreeMap::new())
    }

    pub fn contains_name(&self, name: &Identifier) -> bool {
        self.0.contains_key(name)
    }

    pub fn insert(&mut self, name: Identifier, values: Vec<String>) {
        self.0.insert(name, values);
    }

    pub fn append(&mut self, other: &mut Self) {
        self.0.append(&mut other.0)
    }

    pub fn role(&self) -> Result<Option<Role>, InvalidRole> {
        self.try_into()
    }

    pub fn iter(&self, include_at: bool) -> impl Iterator<Item = (&Identifier, &[String])> {
        self.0.iter().filter_map(move |(name, values)| {
            if include_at || !name.0.starts_with('@') {
                Some((name, values.as_slice()))
            } else {
                None
            }
        })
    }

    pub fn has_any(&self, include_at: bool) -> bool {
        self.iter(include_at).next().is_some()
    }
}

#[derive(Clone, Debug, ThisError, PartialEq, Eq)]
pub enum InvalidRole {
    #[error("Unknown role {0:?}.")]
    UnknownRole(String),

    #[error("Role attribute $@Role must have exactly one value (not {0}).")]
    InvalidValues(usize),
}

impl TryFrom<&Attributes> for Option<Role> {
    type Error = InvalidRole;

    fn try_from(value: &Attributes) -> Result<Self, Self::Error> {
        let role = Identifier::new("$@Role").unwrap();
        value.0.get(&role).map_or(Ok(None), |attribute| {
            if let Ok([string]) = <&[String; 1]>::try_from(attribute.as_slice()) {
                match string.parse::<i32>() {
                    Ok(integer) => Ok(Some(Role::try_from(integer)?)),
                    Err(_) => Err(InvalidRole::UnknownRole(string.clone())),
                }
            } else {
                Err(InvalidRole::InvalidValues(attribute.len()))
            }
        })
    }
}

/// A variable, usually inside a [Dictionary].
#[derive(Clone, Debug)]
pub struct Variable {
    /// The variable's name.
    ///
    /// PSPP variable names are case-insensitive.
    pub name: Identifier,

    /// Variable width.
    pub width: VarWidth,

    /// User-missing values.
    ///
    /// Numeric variables also have a system-missing value (represented as
    /// `None`).
    ///
    /// Both kinds of missing values are excluded from most analyses.
    pub missing_values: MissingValues,

    /// Output format used in most contexts.
    pub print_format: Format,

    /// Output format used on the `WRITE` command.
    pub write_format: Format,

    /// Value labels, to associate a number (or a string) with a more meaningful
    /// description, e.g. 1 -> Apple, 2 -> Banana, ...
    pub value_labels: ValueLabels,

    /// Variable label, an optional meaningful description for the variable
    /// itself.
    pub label: Option<String>,

    /// Measurement level for the variable's data.
    pub measure: Option<Measure>,

    /// Role in data analysis.
    pub role: Role,

    /// Width of data column in GUI.
    pub display_width: u32,

    /// Data alignment in GUI.
    pub alignment: Alignment,

    /// Whether to retain values of the variable from one case to the next.
    pub leave: bool,

    /// For compatibility with old software that supported at most 8-character
    /// variable names.
    pub short_names: Vec<Identifier>,

    /// Variable attributes.
    pub attributes: Attributes,

    /// Encoding for [Value]s inside this variable.
    ///
    /// The variables in a [Dictionary] must all use the same encoding as the
    /// dictionary.
    pub encoding: &'static Encoding,
}

pub fn escape_value_label(unescaped: &str) -> Cow<'_, str> {
    if unescaped.contains("\n") {
        unescaped.replace("\n", "\\n").into()
    } else {
        unescaped.into()
    }
}

pub fn unescape_value_label(escaped: &str) -> Cow<'_, str> {
    if escaped.contains("\\n") {
        escaped.replace("\\n", "\n").into()
    } else {
        escaped.into()
    }
}

impl Variable {
    pub fn new(name: Identifier, width: VarWidth, encoding: &'static Encoding) -> Self {
        let var_type = VarType::from(width);
        let leave = name.class().must_leave();
        Self {
            name,
            width,
            missing_values: MissingValues::default(),
            print_format: Format::default_for_width(width),
            write_format: Format::default_for_width(width),
            value_labels: ValueLabels::new(),
            label: None,
            measure: Measure::default_for_type(var_type),
            role: Role::default(),
            display_width: width.default_display_width(),
            alignment: Alignment::default_for_type(var_type),
            leave,
            short_names: Vec::new(),
            attributes: Attributes::new(),
            encoding,
        }
    }

    pub fn is_numeric(&self) -> bool {
        self.width.is_numeric()
    }

    pub fn is_string(&self) -> bool {
        self.width.is_string()
    }

    pub fn label(&self) -> Option<&String> {
        self.label.as_ref()
    }

    pub fn resize(&mut self, width: VarWidth) {
        if self.missing_values.is_resizable(width) {
            self.missing_values.resize(width);
        } else {
            self.missing_values = MissingValues::default();
        }

        if self.value_labels.is_resizable(width) {
            self.value_labels.resize(width);
        } else {
            self.value_labels = ValueLabels::default();
        }

        self.print_format.resize(width);
        self.write_format.resize(width);

        self.width = width;
    }
}

impl HasIdentifier for Variable {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

#[derive(Clone, Debug)]
pub struct Vector {
    pub name: Identifier,
    pub variables: Vec<DictIndex>,
}

impl Vector {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (!self.variables.is_empty()).then_some(self)
    }
}

impl HasIdentifier for Vector {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

/// Variables that represent multiple responses to a survey question.
#[derive(Clone, Debug)]
pub struct MultipleResponseSet {
    /// The set's name.
    pub name: Identifier,

    /// A description for the set.
    pub label: String,

    /// Range of widths among the variables.
    pub width: RangeInclusive<VarWidth>,

    /// What kind of multiple response set this is.
    pub mr_type: MultipleResponseType,

    /// The variables comprising the set.
    pub variables: Vec<DictIndex>,
}

impl MultipleResponseSet {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (self.variables.len() > 1).then_some(self)
    }
}

impl HasIdentifier for MultipleResponseSet {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

/// The type of a [MultipleResponseSet].
#[derive(Clone, Debug)]
pub enum MultipleResponseType {
    /// A "multiple dichotomy set", analogous to a survey question with a set of
    /// checkboxes.  Each variable in the set is treated in a Boolean fashion:
    /// one value (the "counted value") means that the box was checked, and any
    /// other value means that it was not.
    MultipleDichotomy {
        datum: Datum,
        labels: CategoryLabels,
    },

    /// A "multiple category set", a survey question where the respondent is
    /// instructed to list up to N choices.  Each variable represents one of the
    /// responses.
    MultipleCategory,
}

#[derive(Clone, Debug)]
pub struct VariableSet {
    pub name: String,
    pub variables: Vec<DictIndex>,
}

impl VariableSet {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (!self.variables.is_empty()).then_some(self)
    }
}

#[derive(Clone, Debug, Default)]
pub struct ValueLabels(pub HashMap<Datum, String>);

impl ValueLabels {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    pub fn get(&self, datum: &Datum) -> Option<&str> {
        self.0.get(datum).map(|s| s.as_str())
    }

    pub fn insert(&mut self, datum: Datum, label: String) -> Option<String> {
        self.0.insert(datum, label)
    }

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        self.0.keys().all(|datum| datum.is_resizable(width))
    }

    pub fn resize(&mut self, width: VarWidth) {
        self.0 = self
            .0
            .drain()
            .map(|(mut datum, string)| {
                datum.resize(width);
                (datum, string)
            })
            .collect();
    }
}

#[derive(Clone, Default)]
pub struct MissingValues {
    /// Individual missing values, up to 3 of them.
    values: Vec<Datum>,

    /// Optional range of missing values.
    range: Option<MissingValueRange>,
}

impl Debug for MissingValues {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        DisplayMissingValues {
            mv: self,
            encoding: None,
        }
        .fmt(f)
    }
}

#[derive(Copy, Clone, Debug)]
pub enum MissingValuesError {
    TooMany,
    TooWide,
    MixedTypes,
}

impl MissingValues {
    pub fn new(
        mut values: Vec<Datum>,
        range: Option<MissingValueRange>,
    ) -> Result<Self, MissingValuesError> {
        if values.len() > 3 {
            return Err(MissingValuesError::TooMany);
        }

        let mut var_type = None;
        for value in values.iter_mut() {
            value.trim_end();
            match value.width() {
                VarWidth::String(w) if w > 8 => return Err(MissingValuesError::TooWide),
                _ => (),
            }
            if var_type.is_some_and(|t| t != value.var_type()) {
                return Err(MissingValuesError::MixedTypes);
            }
            var_type = Some(value.var_type());
        }

        if var_type == Some(VarType::String) && range.is_some() {
            return Err(MissingValuesError::MixedTypes);
        }

        Ok(Self { values, range })
    }

    pub fn is_empty(&self) -> bool {
        self.values.is_empty() && self.range.is_none()
    }

    pub fn var_type(&self) -> Option<VarType> {
        if let Some(datum) = self.values.first() {
            Some(datum.var_type())
        } else if self.range.is_some() {
            Some(VarType::Numeric)
        } else {
            None
        }
    }

    pub fn contains(&self, value: &Datum) -> bool {
        if self
            .values
            .iter()
            .any(|datum| datum.eq_ignore_trailing_spaces(value))
        {
            return true;
        }

        match value {
            Datum::Number(Some(number)) => self.range.is_some_and(|range| range.contains(*number)),
            _ => false,
        }
    }

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        self.values.iter().all(|datum| datum.is_resizable(width))
            && self.range.iter().all(|range| range.is_resizable(width))
    }

    pub fn resize(&mut self, width: VarWidth) {
        for datum in &mut self.values {
            datum.resize(width);
        }
        if let Some(range) = &mut self.range {
            range.resize(width);
        }
    }

    pub fn display(&self, encoding: &'static Encoding) -> DisplayMissingValues<'_> {
        DisplayMissingValues {
            mv: self,
            encoding: Some(encoding),
        }
    }
}

pub struct DisplayMissingValues<'a> {
    mv: &'a MissingValues,
    encoding: Option<&'static Encoding>,
}

impl<'a> Display for DisplayMissingValues<'a> {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        if let Some(range) = &self.mv.range {
            write!(f, "{range}")?;
            if !self.mv.values.is_empty() {
                write!(f, "; ")?;
            }
        }

        for (i, value) in self.mv.values.iter().enumerate() {
            if i > 0 {
                write!(f, "; ")?;
            }
            match self.encoding {
                Some(encoding) => value.display_plain(encoding).fmt(f)?,
                None => value.fmt(f)?,
            }
        }

        if self.mv.is_empty() {
            write!(f, "none")?;
        }
        Ok(())
    }
}

#[derive(Copy, Clone)]
pub enum MissingValueRange {
    In { low: f64, high: f64 },
    From { low: f64 },
    To { high: f64 },
}

impl MissingValueRange {
    pub fn new(low: f64, high: f64) -> Self {
        const LOWEST: f64 = f64::MIN.next_up();
        match (low, high) {
            (f64::MIN | LOWEST, _) => Self::To { high },
            (_, f64::MAX) => Self::From { low },
            (_, _) => Self::In { low, high },
        }
    }

    pub fn low(&self) -> Option<f64> {
        match self {
            MissingValueRange::In { low, .. } | MissingValueRange::From { low } => Some(*low),
            MissingValueRange::To { .. } => None,
        }
    }

    pub fn high(&self) -> Option<f64> {
        match self {
            MissingValueRange::In { high, .. } | MissingValueRange::To { high } => Some(*high),
            MissingValueRange::From { .. } => None,
        }
    }

    pub fn contains(&self, number: f64) -> bool {
        match self {
            MissingValueRange::In { low, high } => (*low..*high).contains(&number),
            MissingValueRange::From { low } => number >= *low,
            MissingValueRange::To { high } => number <= *high,
        }
    }

    pub fn is_resizable(&self, width: VarWidth) -> bool {
        width.is_numeric()
    }

    pub fn resize(&self, width: VarWidth) {
        assert_eq!(width, VarWidth::Numeric);
    }
}

impl Display for MissingValueRange {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self.low() {
            Some(low) => low.display_plain().fmt(f)?,
            None => write!(f, "LOW")?,
        }

        write!(f, " THRU ")?;

        match self.high() {
            Some(high) => high.display_plain().fmt(f)?,
            None => write!(f, "HIGH")?,
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Alignment {
    Left,
    Right,
    Center,
}

impl Alignment {
    pub fn default_for_type(var_type: VarType) -> Self {
        match var_type {
            VarType::Numeric => Self::Right,
            VarType::String => Self::Left,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Alignment::Left => "Left",
            Alignment::Right => "Right",
            Alignment::Center => "Center",
        }
    }
}

/// [Level of measurement](https://en.wikipedia.org/wiki/Level_of_measurement).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Measure {
    /// Nominal values can only be compared for equality.
    Nominal,

    /// Ordinal values can be meaningfully ordered.
    Ordinal,

    /// Scale values can be meaningfully compared for the degree of difference.
    Scale,
}

impl Measure {
    pub fn default_for_type(var_type: VarType) -> Option<Measure> {
        match var_type {
            VarType::Numeric => None,
            VarType::String => Some(Self::Nominal),
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Measure::Nominal => "Nominal",
            Measure::Ordinal => "Ordinal",
            Measure::Scale => "Scale",
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum CategoryLabels {
    VarLabels,
    CountedValues,
}

#[cfg(test)]
mod test {
    use std::collections::HashSet;

    use unicase::UniCase;

    use crate::identifier::Identifier;

    use super::{ByIdentifier, HasIdentifier};

    #[derive(PartialEq, Eq, Debug, Clone)]
    struct Variable {
        name: Identifier,
        value: i32,
    }

    impl HasIdentifier for Variable {
        fn identifier(&self) -> &UniCase<String> {
            &self.name.0
        }
    }

    #[test]
    fn test() {
        // Variables should not be the same if their values differ.
        let abcd = Identifier::new("abcd").unwrap();
        let abcd1 = Variable {
            name: abcd.clone(),
            value: 1,
        };
        let abcd2 = Variable {
            name: abcd,
            value: 2,
        };
        assert_ne!(abcd1, abcd2);

        // But `ByName` should treat them the same.
        let abcd1_by_name = ByIdentifier::new(abcd1);
        let abcd2_by_name = ByIdentifier::new(abcd2);
        assert_eq!(abcd1_by_name, abcd2_by_name);

        // And a `HashSet` of `ByName` should also treat them the same.
        let mut vars: HashSet<ByIdentifier<Variable>> = HashSet::new();
        assert!(vars.insert(ByIdentifier::new(abcd1_by_name.0.clone())));
        assert!(!vars.insert(ByIdentifier::new(abcd2_by_name.0.clone())));
        assert_eq!(
            vars.get(&UniCase::new(String::from("abcd")))
                .unwrap()
                .0
                .value,
            1
        );
    }
}
