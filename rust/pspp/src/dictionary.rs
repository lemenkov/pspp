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

//! Dictionaries.

use core::str;
use std::{
    borrow::Cow,
    collections::{btree_set, BTreeSet, HashSet},
    ops::{Bound, Index, RangeBounds, RangeInclusive},
};

use encoding_rs::{Encoding, UTF_8};
use enum_map::{Enum, EnumMap};
use indexmap::IndexSet;
use serde::{
    ser::{SerializeMap, SerializeSeq, SerializeStruct},
    Serialize,
};
use smallvec::SmallVec;
use thiserror::Error as ThisError;
use unicase::UniCase;

use crate::{
    data::{ByteString, Datum, RawString},
    identifier::{ByIdentifier, HasIdentifier, Identifier},
    output::pivot::{
        Axis3, Dimension, Display26Adic, Footnote, Footnotes, Group, PivotTable, Value,
    },
    settings::Show,
    variable::{Attributes, VarWidth, Variable},
};

/// An index within [Dictionary::variables].
pub type DictIndex = usize;

/// A collection of variables, plus additional metadata.
#[derive(Clone, Debug)]
pub struct Dictionary {
    /// The variables.
    pub variables: IndexSet<ByIdentifier<Variable>>,

    /// Indexes into `variables` of the `SPLIT FILE` variables.
    split_file: Vec<DictIndex>,

    /// Index of the weight variable, if any.
    ///
    /// The weight variable must be numeric.
    weight: Option<DictIndex>,

    /// Index of the filter variable, if any.
    ///
    /// The filter variable must be numeric.  If there is a filter variable,
    /// then data analysis excludes cases whose filter value is zero or system-
    /// or user-missing.
    filter: Option<DictIndex>,

    /// An optional limit on the number of cases read by procedures.
    pub case_limit: Option<u64>,

    /// Optional label (name) for the dictionary.
    pub file_label: Option<String>,

    /// Optional additional documentation associated with the dictionary.
    pub documents: Vec<String>,

    /// Named collections of variables within the dictionary.
    vectors: HashSet<ByIdentifier<DictIndexVector>>,

    /// Attributes for the dictionary itself.
    ///
    /// Individual variables can have their own attributes.
    pub attributes: Attributes,

    /// Multiple response sets.
    mrsets: BTreeSet<ByIdentifier<DictIndexMultipleResponseSet>>,

    /// Variable sets.
    ///
    /// Only the GUI makes use of variable sets.
    variable_sets: Vec<DictIndexVariableSet>,

    /// Character encoding for the dictionary and the data.
    encoding: &'static Encoding,
}

impl PartialEq for Dictionary {
    fn eq(&self, other: &Self) -> bool {
        // We have to compare the dereferenced versions of fields that use
        // [ByIdentifier.  Otherwise we would just be comparing their names.
        self.variables.iter().eq(other.variables.iter())
            && self.split_file == other.split_file
            && self.weight == other.weight
            && self.filter == other.filter
            && self.case_limit == other.case_limit
            && self.file_label == other.file_label
            && self.documents == other.documents
            && self.vectors.iter().eq(other.vectors.iter())
            && self.attributes == other.attributes
            && self.mrsets.iter().eq(other.mrsets.iter())
            && self.variable_sets == other.variable_sets
            && self.encoding == other.encoding
    }
}

impl Serialize for Dictionary {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut map = serializer.serialize_struct("Dictionary", 12)?;
        map.serialize_field("variables", &self.variables)?;
        map.serialize_field("split_file", &self.split_vars())?;
        map.serialize_field("weight", &self.weight_var())?;
        map.serialize_field("filter", &self.filter_var())?;
        map.serialize_field("documents", &self.documents)?;
        map.serialize_field("vectors", &self.vectors())?;
        map.serialize_field("attributes", &self.attributes)?;
        map.serialize_field("mrsets", &self.mrsets())?;
        map.serialize_field("variable_sets", &self.variable_sets())?;
        map.serialize_field("encoding", self.encoding)?;
        map.end()
    }
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

/// Weight variable must be numeric.
#[derive(Debug, ThisError)]
#[error("Weight variable must be numeric.")]
pub struct InvalidWeightVariable;

/// Filter variable must be numeric.
#[derive(Debug, ThisError)]
#[error("Filter variable must be numeric.")]
pub struct InvalidFilterVariable;

/// Invalid dictionary index.
#[derive(Debug, Clone, ThisError)]
#[error("Invalid index {index} in dictionary with {n} variables.")]
pub struct DictIndexError {
    index: usize,
    n: usize,
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

    pub fn encoding(&self) -> &'static Encoding {
        self.encoding
    }

    /// Returns a reference to the weight variable, if any.
    pub fn weight_var(&self) -> Option<&Variable> {
        self.weight.map(|index| &self.variables[index].0)
    }

    /// Returns the weight variable's dictionary index.
    pub fn weight_index(&self) -> Option<DictIndex> {
        self.weight
    }

    /// Sets the weight variable to the variable with the given dictionary
    /// index.
    ///
    /// # Panic
    ///
    /// Panics if `dict_index` is not a valid dictionary index.
    pub fn set_weight(
        &mut self,
        dict_index: Option<DictIndex>,
    ) -> Result<(), InvalidWeightVariable> {
        if let Some(dict_index) = dict_index
            && !self.variables[dict_index].width.is_numeric()
        {
            Err(InvalidWeightVariable)
        } else {
            self.weight = dict_index;
            Ok(())
        }
    }

    /// Returns a reference to the filter variable, if any.
    pub fn filter_var(&self) -> Option<&Variable> {
        self.filter.map(|index| &self.variables[index].0)
    }

    /// Returns the filter variable's dictionary index.
    pub fn filter_index(&self) -> Option<DictIndex> {
        self.filter
    }

    /// Sets the filter variable to the variable with the given dictionary
    /// index.
    ///
    /// # Panic
    ///
    /// Panics if `dict_index` is not a valid dictionary index.
    pub fn set_filter(
        &mut self,
        dict_index: Option<DictIndex>,
    ) -> Result<(), InvalidFilterVariable> {
        if let Some(dict_index) = dict_index
            && !self.variables[dict_index].width.is_numeric()
        {
            Err(InvalidFilterVariable)
        } else {
            self.filter = dict_index;
            Ok(())
        }
    }

    /// Returns the split variables.
    pub fn split_vars(&self) -> MappedVariables<'_> {
        MappedVariables::new_unchecked(self, &self.split_file)
    }

    pub fn vectors(&self) -> Vectors<'_> {
        Vectors::new(self)
    }

    pub fn vectors_mut(&mut self) -> VectorsMut<'_> {
        VectorsMut::new(self)
    }

    pub fn mrsets(&self) -> MultipleResponseSets<'_> {
        MultipleResponseSets::new(self)
    }

    pub fn mrsets_mut(&mut self) -> MultipleResponseSetsMut<'_> {
        MultipleResponseSetsMut::new(self)
    }

    pub fn variable_sets(&self) -> VariableSets<'_> {
        VariableSets::new(self)
    }

    pub fn add_variable_set(&mut self, set: DictIndexVariableSet) {
        assert!(set
            .variables
            .iter()
            .all(|dict_index| *dict_index < self.variables.len()));
        self.variable_sets.push(set);
    }

    pub fn remove_variable_set(&mut self, var_set_index: usize) {
        self.variable_sets.remove(var_set_index);
    }

    /// Adds `variable` at the end of the dictionary and returns its index.
    ///
    /// The operation fails if the dictionary already contains a variable with
    /// the same name (or a variant with different case), or if `variable`'s
    /// encoding differs from the dictionary's.
    pub fn add_var(&mut self, variable: Variable) -> Result<DictIndex, AddVarError> {
        if variable.encoding() != self.encoding {
            Err(AddVarError::WrongEncoding {
                var_encoding: variable.encoding(),
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
                    Some(index - (end - start))
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

    pub fn output_variables(&self) -> OutputVariables<'_> {
        OutputVariables::new(self)
    }

    pub fn output_value_labels(&self) -> OutputValueLabels<'_> {
        OutputValueLabels::new(self)
    }

    pub fn output_variable_sets(&self) -> OutputVariableSets<'_> {
        OutputVariableSets::new(self)
    }

    pub fn output_mrsets(&self) -> OutputMrsets<'_> {
        OutputMrsets::new(self)
    }

    pub fn output_attributes(&self) -> OutputAttributes<'_> {
        OutputAttributes::new(self)
    }

    pub fn to_pivot_rows(&self) -> (Group, Vec<Value>) {
        let mut group = Group::new("Dictionary Information");
        let mut values = Vec::new();

        group.push("Label");
        values.push(
            self.file_label
                .as_ref()
                .map(Value::new_user_text)
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

    pub fn to_pivot_table(&self) -> PivotTable {
        let (group, data) = self.to_pivot_rows();
        PivotTable::new([(Axis3::Y, Dimension::new(group))]).with_data(
            data.into_iter()
                .enumerate()
                .filter(|(_row, value)| !value.is_empty())
                .map(|(row, value)| ([row], value)),
        )
    }

    pub fn all_pivot_tables(&self) -> Vec<PivotTable> {
        let mut pivot_tables = Vec::new();
        pivot_tables.push(self.to_pivot_table());
        pivot_tables.push(self.output_variables().to_pivot_table());
        pivot_tables.extend(self.output_value_labels().to_pivot_table());
        pivot_tables.extend(self.output_mrsets().to_pivot_table());
        pivot_tables.extend(self.output_attributes().to_pivot_table());
        pivot_tables.extend(self.output_variable_sets().to_pivot_table());
        pivot_tables
    }

    pub fn short_names(&self) -> Vec<SmallVec<[Identifier; 1]>> {
        struct PickShortName<'a> {
            variable_name: &'a Identifier,
            used_names: &'a mut HashSet<Identifier>,
            encoding: &'static Encoding,
            index: usize,
        }
        impl<'a> PickShortName<'a> {
            fn new(
                variable_name: &'a Identifier,
                used_names: &'a mut HashSet<Identifier>,
                encoding: &'static Encoding,
            ) -> Self {
                Self {
                    variable_name,
                    used_names,
                    encoding,
                    index: 0,
                }
            }

            fn next(&mut self) -> Identifier {
                loop {
                    let name = if self.index == 0 {
                        self.variable_name.shortened(self.encoding)
                    } else {
                        self.variable_name
                            .with_suffix(
                                &format!("_{}", Display26Adic::new_uppercase(self.index)),
                                self.encoding,
                                8,
                            )
                            .or_else(|_| {
                                Identifier::new(format!(
                                    "V{}",
                                    Display26Adic::new_uppercase(self.index)
                                ))
                            })
                            .unwrap()
                    };
                    if !self.used_names.contains(&name) {
                        self.used_names.insert(name.clone());
                        return name;
                    }
                    self.index += 1;
                }
            }
        }

        let mut used_names = HashSet::new();

        // Each variable whose name is short has the best claim to its short
        // name.
        let mut short_names: Vec<SmallVec<[Option<Identifier>; 1]>> = self
            .variables
            .iter()
            .map(|variable| {
                let n = variable.width.segments().len();
                let mut names = SmallVec::with_capacity(n);
                if self.encoding.encode(variable.name.as_str()).0.len() <= 8 {
                    used_names.insert(variable.name.clone());
                    names.push(Some(variable.name.clone()))
                }
                while names.len() < n {
                    names.push(None);
                }
                names
            })
            .collect();

        // Each variable with an assigned short name for its first segment now
        // gets it unless there is a conflict.  In case of conflict, the
        // claimant earlier in dictionary order wins.  Then similarly for
        // additional segments of very long strings.
        for (variable, short_names) in self.variables.iter().zip(short_names.iter_mut()) {
            if short_names[0].is_none()
                && let Some(short_name) = variable.short_names.first()
                && !used_names.contains(short_name)
            {
                used_names.insert(short_name.clone());
                short_names[0] = Some(short_name.clone());
            }
        }
        for (variable, short_names) in self.variables.iter().zip(short_names.iter_mut()) {
            for (index, assigned_short_name) in short_names.iter_mut().enumerate().skip(1) {
                if assigned_short_name.is_none()
                    && let Some(short_name) = variable.short_names.get(index)
                    && !used_names.contains(short_name)
                {
                    used_names.insert(short_name.clone());
                    *assigned_short_name = Some(short_name.clone());
                }
            }
        }

        // Assign short names to first segment of remaining variables,
        // then similarly for additional segments.
        for (variable, short_names) in self.variables.iter().zip(short_names.iter_mut()) {
            if short_names[0].is_none() {
                short_names[0] =
                    Some(PickShortName::new(&variable.name, &mut used_names, self.encoding).next());
            }
        }
        for (variable, short_names) in self.variables.iter().zip(short_names.iter_mut()) {
            let mut picker = PickShortName::new(&variable.name, &mut used_names, self.encoding);
            for assigned_short_name in short_names.iter_mut().skip(1) {
                if assigned_short_name.is_none() {
                    *assigned_short_name = Some(picker.next());
                }
            }
        }

        short_names
            .into_iter()
            .map(|names| names.into_iter().flatten().collect())
            .collect()
    }

    pub fn codepage_to_unicode(&mut self) {
        if self.encoding == UTF_8 {
            return;
        }

        let mut variables = IndexSet::new();
        let mut index = 0;
        for mut variable in self.variables.drain(..) {
            variable.codepage_to_unicode();
            while variables.contains(&variable) {
                index += 1;
                variable.name = Identifier::new(format!("Var{index}")).unwrap();
            }
            variables.insert(variable);
        }
        self.variables = variables;

        let mut index = 0;
        let mut vectors = self.vectors.drain().collect::<Vec<_>>();
        vectors.sort();
        for mut vector in vectors {
            vector.codepage_to_unicode();
            while self.vectors.contains(&vector) {
                index += 1;
                vector.name = Identifier::new(format!("Vec{index}")).unwrap();
            }
            self.vectors.insert(vector);
        }

        self.attributes.codepage_to_unicode();

        let mut mrsets = BTreeSet::new();
        let mut index = 0;
        while let Some(mut mrset) = self.mrsets.pop_first() {
            mrset.codepage_to_unicode();
            while mrsets.contains(&mrset) {
                index += 1;
                mrset.name = Identifier::new(format!("MrSet{index}")).unwrap();
            }
            mrsets.insert(mrset);
        }
        self.mrsets = mrsets;

        self.encoding = UTF_8;
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
        .with_title("Variables")
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
            VariableField::Label => variable.label().map(Value::new_user_text),
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
            VariableField::MissingValues if !variable.missing_values().is_empty() => {
                Some(Value::new_user_text(variable.missing_values().to_string()))
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
                if variable
                    .missing_values()
                    .contains(&datum.as_encoded(variable.encoding()))
                {
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
        Some(
            PivotTable::new([(Axis3::Y, Dimension::new(values))])
                .with_title("Value Labels")
                .with_data(
                    data.into_iter()
                        .enumerate()
                        .map(|(row, datum)| ([row], datum)),
                ),
        )
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
        Some(
            PivotTable::new([
                (Axis3::Y, Dimension::new(variable_sets)),
                (
                    Axis3::X,
                    Dimension::new(Group::new("Attributes").with("Variable")),
                ),
            ])
            .with_title("Variable Sets")
            .with_data(
                data.into_iter()
                    .enumerate()
                    .map(|(row, datum)| ([row, 0], datum)),
            ),
        )
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
        ])
        .with_title("Multiple Response Sets");
        for (row, mrset) in self.dictionary.mrsets.iter().enumerate() {
            pt.insert(&[row, 0], mrset.label.as_str());

            let mr_type_name = match &mrset.mr_type {
                MultipleResponseType::MultipleDichotomy { datum, .. } => {
                    pt.insert(
                        &[row, 2],
                        Value::new_datum(&datum.as_encoded(self.dictionary.encoding)),
                    );
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
        ])
        .with_title("Data File and Variable Attributes");
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

#[derive(Clone, Debug)]
pub struct DictIndexVector {
    pub name: Identifier,
    pub variables: Vec<DictIndex>,
}

impl DictIndexVector {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (!self.variables.is_empty()).then_some(self)
    }

    pub fn codepage_to_unicode(&mut self) {
        self.name.codepage_to_unicode();
    }
}

impl HasIdentifier for DictIndexVector {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

pub struct Vector<'a> {
    dictionary: &'a Dictionary,
    vector: &'a DictIndexVector,
}

impl<'a> Vector<'a> {
    fn new_unchecked(dictionary: &'a Dictionary, vector: &'a DictIndexVector) -> Self {
        Self { dictionary, vector }
    }
    pub fn new(
        dictionary: &'a Dictionary,
        vector: &'a DictIndexVector,
    ) -> Result<Self, DictIndexError> {
        MappedVariables::new(dictionary, &vector.variables)?;
        Ok(Self::new_unchecked(dictionary, vector))
    }
    pub fn name(&self) -> &'a Identifier {
        &self.vector.name
    }
    pub fn variables(&self) -> MappedVariables<'a> {
        MappedVariables::new_unchecked(self.dictionary, &self.vector.variables)
    }
}

pub struct MappedVariables<'a> {
    dictionary: &'a Dictionary,
    dict_indexes: &'a [DictIndex],
}

impl<'a> MappedVariables<'a> {
    fn new_unchecked(dictionary: &'a Dictionary, dict_indexes: &'a [DictIndex]) -> Self {
        Self {
            dictionary,
            dict_indexes,
        }
    }

    pub fn new(
        dictionary: &'a Dictionary,
        dict_indexes: &'a [DictIndex],
    ) -> Result<Self, DictIndexError> {
        let n = dictionary.variables.len();
        for index in dict_indexes.iter().copied() {
            if index >= n {
                return Err(DictIndexError { index, n });
            }
        }
        Ok(Self::new_unchecked(dictionary, dict_indexes))
    }

    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        self.dict_indexes.len()
    }

    pub fn get(&self, index: usize) -> Option<&'a Variable> {
        self.dict_indexes
            .get(index)
            .map(|dict_index| &*self.dictionary.variables[*dict_index])
    }

    pub fn iter(&self) -> MappedVariablesIter<'a> {
        MappedVariablesIter::new(self.dictionary, self.dict_indexes.iter())
    }

    pub fn dict_indexes(&self) -> &[DictIndex] {
        self.dict_indexes
    }
}

impl<'a> Index<usize> for MappedVariables<'a> {
    type Output = Variable;

    fn index(&self, index: usize) -> &Self::Output {
        &self.dictionary.variables[self.dict_indexes[index]]
    }
}

impl<'a> Serialize for MappedVariables<'a> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut seq = serializer.serialize_seq(Some(self.len()))?;
        for variable in self {
            seq.serialize_element(&variable.name)?;
        }
        seq.end()
    }
}

impl<'a> IntoIterator for &MappedVariables<'a> {
    type Item = &'a Variable;

    type IntoIter = MappedVariablesIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

pub struct MappedVariablesIter<'a> {
    dictionary: &'a Dictionary,
    dict_indexes: std::slice::Iter<'a, DictIndex>,
}

impl<'a> MappedVariablesIter<'a> {
    pub fn new(dictionary: &'a Dictionary, dict_indexes: std::slice::Iter<'a, DictIndex>) -> Self {
        Self {
            dictionary,
            dict_indexes,
        }
    }
}

impl<'a> Iterator for MappedVariablesIter<'a> {
    type Item = &'a Variable;

    fn next(&mut self) -> Option<Self::Item> {
        self.dict_indexes
            .next()
            .map(|dict_index| &*self.dictionary.variables[*dict_index])
    }
}

pub struct VectorsIter<'a> {
    dictionary: &'a Dictionary,
    iter: std::collections::hash_set::Iter<'a, ByIdentifier<DictIndexVector>>,
}

impl<'a> VectorsIter<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self {
            dictionary,
            iter: dictionary.vectors.iter(),
        }
    }
}
impl<'a> Iterator for VectorsIter<'a> {
    type Item = Vector<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter
            .next()
            .map(|vector| Vector::new_unchecked(self.dictionary, vector))
    }
}

#[derive(Debug)]
pub struct VectorsMut<'a>(&'a mut Dictionary);

impl<'a> VectorsMut<'a> {
    fn new(dictionary: &'a mut Dictionary) -> Self {
        Self(dictionary)
    }
    pub fn as_vectors(&'a self) -> Vectors<'a> {
        Vectors(self.0)
    }
    pub fn insert(&mut self, vector: DictIndexVector) -> Result<(), DictIndexError> {
        Vector::new(self.0, &vector)?;
        self.0.vectors.insert(ByIdentifier(vector));
        Ok(())
    }
}

#[derive(Debug)]
pub struct Vectors<'a>(&'a Dictionary);

impl<'a> Vectors<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self(dictionary)
    }
    pub fn is_empty(&self) -> bool {
        self.0.vectors.is_empty()
    }
    pub fn len(&self) -> usize {
        self.0.vectors.len()
    }
    pub fn get(&self, name: &Identifier) -> Option<Vector<'a>> {
        self.0
            .vectors
            .get(&name.0)
            .map(|vector| Vector::new_unchecked(self.0, vector))
    }
    pub fn iter(&self) -> VectorsIter<'a> {
        VectorsIter::new(self.0)
    }
}

impl<'a> IntoIterator for &Vectors<'a> {
    type Item = Vector<'a>;

    type IntoIter = VectorsIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a> Serialize for Vectors<'a> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut map = serializer.serialize_map(Some(self.len()))?;
        for vector in self {
            map.serialize_key(vector.name())?;
            map.serialize_value(&vector.variables())?;
        }
        map.end()
    }
}

#[derive(Copy, Clone, Debug)]
pub struct VariableSet<'a> {
    dictionary: &'a Dictionary,
    variable_set: &'a DictIndexVariableSet,
}

impl<'a> PartialEq for VariableSet<'a> {
    fn eq(&self, other: &Self) -> bool {
        self.variable_set == other.variable_set
    }
}

impl<'a> VariableSet<'a> {
    pub fn name(&self) -> &'a String {
        &self.variable_set.name
    }
    pub fn variables(&self) -> MappedVariables<'a> {
        MappedVariables::new_unchecked(self.dictionary, &self.variable_set.variables)
    }
}

#[derive(Debug)]
pub struct VariableSets<'a>(&'a Dictionary);

impl<'a> VariableSets<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self(dictionary)
    }
    pub fn is_empty(&self) -> bool {
        self.0.variable_sets.is_empty()
    }
    pub fn len(&self) -> usize {
        self.0.variable_sets.len()
    }
    pub fn get(&self, index: usize) -> Option<VariableSet<'a>> {
        self.0
            .variable_sets
            .get(index)
            .map(|variable_set| VariableSet {
                dictionary: self.0,
                variable_set,
            })
    }
    pub fn iter(&self) -> VariableSetsIter<'a> {
        VariableSetsIter::new(self.0)
    }
}

impl<'a> IntoIterator for &VariableSets<'a> {
    type Item = VariableSet<'a>;

    type IntoIter = VariableSetsIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a> Serialize for VariableSets<'a> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut map = serializer.serialize_map(Some(self.len()))?;
        for variable_set in self {
            map.serialize_key(variable_set.name())?;
            map.serialize_value(&variable_set.variables())?;
        }
        map.end()
    }
}

pub struct VariableSetsIter<'a> {
    dictionary: &'a Dictionary,
    iter: std::slice::Iter<'a, DictIndexVariableSet>,
}

impl<'a> VariableSetsIter<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self {
            dictionary,
            iter: dictionary.variable_sets.iter(),
        }
    }
}
impl<'a> Iterator for VariableSetsIter<'a> {
    type Item = VariableSet<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|variable_set| VariableSet {
            dictionary: self.dictionary,
            variable_set,
        })
    }
}

#[derive(Debug)]
pub struct MultipleResponseSetsMut<'a>(&'a mut Dictionary);

#[derive(ThisError, Clone, Debug)]
pub enum MrSetError {
    #[error("{0}")]
    DictIndexError(#[from] DictIndexError),

    /// Counted value {value} has width {width}, but it must be no wider than
    /// {max_width}, the width of the narrowest variable in multiple response
    /// set {mr_set}.
    #[error("Counted value {value} has width {width}, but it must be no wider than {max_width}, the width of the narrowest variable in multiple response set {mr_set}.")]
    TooWideMDGroupCountedValue {
        /// Multiple response set name.
        mr_set: Identifier,
        /// Counted value.
        value: String,
        /// Width of counted value.
        width: usize,
        /// Maximum allowed width of counted value.
        max_width: u16,
    },

    /// Multiple response set {0} contains both string and numeric variables.
    #[error("Multiple response set {0} contains both string and numeric variables.")]
    MixedMrSet(
        /// Multiple response set name.
        Identifier,
    ),
}

impl<'a> MultipleResponseSetsMut<'a> {
    fn new(dictionary: &'a mut Dictionary) -> Self {
        Self(dictionary)
    }

    pub fn mrsets(&'a self) -> MultipleResponseSets<'a> {
        MultipleResponseSets::new(self.0)
    }

    pub fn insert(&mut self, mrset: DictIndexMultipleResponseSet) -> Result<(), MrSetError> {
        MultipleResponseSet::new(self.0, &mrset)?;
        self.0.mrsets.insert(ByIdentifier(mrset));
        Ok(())
    }
}

#[derive(Clone, Debug)]
pub struct MultipleResponseSets<'a>(&'a Dictionary);

impl<'a> MultipleResponseSets<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self(dictionary)
    }

    pub fn is_empty(&self) -> bool {
        self.0.mrsets.is_empty()
    }

    pub fn len(&self) -> usize {
        self.0.mrsets.len()
    }

    pub fn get(&self, name: &Identifier) -> Option<MultipleResponseSet<'a>> {
        self.0
            .mrsets
            .get(&name.0)
            .map(|mrset| MultipleResponseSet::new_unchecked(self.0, mrset))
    }

    pub fn iter(&self) -> MultipleResponseSetIter<'a> {
        MultipleResponseSetIter::new(self.0)
    }
}

impl<'a> IntoIterator for &MultipleResponseSets<'a> {
    type Item = MultipleResponseSet<'a>;

    type IntoIter = MultipleResponseSetIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

pub struct MultipleResponseSetIter<'a> {
    dictionary: &'a Dictionary,
    iter: btree_set::Iter<'a, ByIdentifier<DictIndexMultipleResponseSet>>,
}

impl<'a> MultipleResponseSetIter<'a> {
    fn new(dictionary: &'a Dictionary) -> Self {
        Self {
            dictionary,
            iter: dictionary.mrsets.iter(),
        }
    }
}

impl<'a> Iterator for MultipleResponseSetIter<'a> {
    type Item = MultipleResponseSet<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter
            .next()
            .map(|set| MultipleResponseSet::new_unchecked(self.dictionary, set))
    }
}

impl<'a> Serialize for MultipleResponseSets<'a> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut seq = serializer.serialize_seq(Some(self.len()))?;
        for set in self {
            seq.serialize_element(&set)?;
        }
        seq.end()
    }
}

/// Variables that represent multiple responses to a survey question.
#[derive(Clone, Debug)]
pub struct MultipleResponseSet<'a> {
    dictionary: &'a Dictionary,
    mrset: &'a DictIndexMultipleResponseSet,
}

impl<'a> MultipleResponseSet<'a> {
    fn new_unchecked(dictionary: &'a Dictionary, mrset: &'a DictIndexMultipleResponseSet) -> Self {
        Self { dictionary, mrset }
    }

    fn new(
        dictionary: &'a Dictionary,
        mrset: &'a DictIndexMultipleResponseSet,
    ) -> Result<Self, MrSetError> {
        let variables = MappedVariables::new(dictionary, &mrset.variables)?;
        let (min_width, _max_width) = Self::widths(&variables)
            .ok_or_else(|| MrSetError::MixedMrSet(mrset.name.clone()))?
            .into_inner();

        if let MultipleResponseType::MultipleDichotomy { datum, labels: _ } = &mrset.mr_type {
            match (datum, min_width) {
                (Datum::Number(_), VarWidth::Numeric) => (),
                (Datum::String(s), VarWidth::String(min_width)) => {
                    if s.without_trailing_spaces().len() > min_width as usize {}
                }
                _ => return Err(MrSetError::MixedMrSet(mrset.name.clone())),
            }
        }
        Ok(Self::new_unchecked(dictionary, mrset))
    }

    fn widths(variables: &MappedVariables<'_>) -> Option<RangeInclusive<VarWidth>> {
        variables
            .iter()
            .map(|v| Some((v.width, v.width)))
            .reduce(|a, b| {
                let (na, wa) = a?;
                let (nb, wb) = b?;
                Some((VarWidth::narrower(na, nb)?, VarWidth::wider(wa, wb)?))
            })
            .flatten()
            .map(|(min_width, max_width)| min_width..=max_width)
    }

    pub fn name(&self) -> &Identifier {
        &self.mrset.name
    }

    pub fn label(&self) -> &String {
        &self.mrset.label
    }

    pub fn width(&self) -> RangeInclusive<VarWidth> {
        Self::widths(&self.variables()).unwrap()
    }

    pub fn mr_type(&self) -> &MultipleResponseType {
        &self.mrset.mr_type
    }

    pub fn variables(&self) -> MappedVariables<'a> {
        MappedVariables::new_unchecked(self.dictionary, &self.mrset.variables)
    }
}

impl<'a> Serialize for MultipleResponseSet<'a> {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let mut map = serializer.serialize_map(Some(5))?;
        map.serialize_entry("name", self.name())?;
        map.serialize_entry("label", self.label())?;
        map.serialize_entry("width", &self.width())?;
        map.serialize_entry("type", self.mr_type())?;
        map.serialize_entry("variables", &self.variables())?;
        map.end()
    }
}

/// Variables that represent multiple responses to a survey question.
#[derive(Clone, Debug, Serialize)]
pub struct DictIndexMultipleResponseSet {
    /// The set's name.
    pub name: Identifier,

    /// A description for the set.
    pub label: String,

    /// What kind of multiple response set this is.
    pub mr_type: MultipleResponseType,

    /// The variables comprising the set.
    pub variables: Vec<DictIndex>,
}

impl DictIndexMultipleResponseSet {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (self.variables.len() > 1).then_some(self)
    }

    pub fn codepage_to_unicode(&mut self) {
        self.name.codepage_to_unicode();
    }
}

impl HasIdentifier for DictIndexMultipleResponseSet {
    fn identifier(&self) -> &UniCase<String> {
        &self.name.0
    }
}

/// The type of a [MultipleResponseSet].
#[derive(Clone, Debug, Serialize)]
pub enum MultipleResponseType {
    /// A "multiple dichotomy set", analogous to a survey question with a set of
    /// checkboxes.  Each variable in the set is treated in a Boolean fashion:
    /// one value (the "counted value") means that the box was checked, and any
    /// other value means that it was not.
    MultipleDichotomy {
        datum: Datum<ByteString>,
        labels: CategoryLabels,
    },

    /// A "multiple category set", a survey question where the respondent is
    /// instructed to list up to N choices.  Each variable represents one of the
    /// responses.
    MultipleCategory,
}

impl MultipleResponseType {
    pub fn supported_before_v14(&self) -> bool {
        !matches!(
            self,
            MultipleResponseType::MultipleDichotomy {
                labels: CategoryLabels::CountedValues { .. },
                datum: _,
            }
        )
    }

    pub fn label_from_var_label(&self) -> bool {
        matches!(
            self,
            MultipleResponseType::MultipleDichotomy {
                labels: CategoryLabels::CountedValues {
                    use_var_label_as_mrset_label: true,
                },
                ..
            }
        )
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
pub enum CategoryLabels {
    VarLabels,
    CountedValues { use_var_label_as_mrset_label: bool },
}

#[derive(Clone, Debug, PartialEq)]
pub struct DictIndexVariableSet {
    pub name: String,
    pub variables: Vec<DictIndex>,
}

impl DictIndexVariableSet {
    fn with_updated_dict_indexes(
        mut self,
        f: impl Fn(DictIndex) -> Option<DictIndex>,
    ) -> Option<Self> {
        update_dict_index_vec(&mut self.variables, f);
        (!self.variables.is_empty()).then_some(self)
    }
}

#[cfg(test)]
mod tests {
    use encoding_rs::{UTF_8, WINDOWS_1252};
    use smallvec::SmallVec;

    use crate::{
        data::Datum,
        dictionary::{
            CategoryLabels, DictIndexMultipleResponseSet, DictIndexVector, Dictionary,
            MultipleResponseType,
        },
        identifier::Identifier,
        variable::{VarWidth, Variable},
    };

    #[test]
    fn short_names() {
        for (variables, expected, encoding) in [
            (
                [("VariableName1", 1), ("VARIABLE", 1), ("VariableName2", 1)],
                vec![vec!["Variab_A"], vec!["VARIABLE"], vec!["Variab_B"]],
                UTF_8,
            ),
            (
                [
                    ("LongVarNameA", 1),
                    ("LongVarNameB", 1),
                    ("LongVarNameC", 1),
                ],
                vec![vec!["LongVarN"], vec!["LongVa_A"], vec!["LongVa_B"]],
                UTF_8,
            ),
            (
                [
                    ("LongVarNameA", 300),
                    ("LongVarNameB", 1),
                    ("LongVarNameC", 1),
                ],
                vec![
                    vec!["LongVarN", "LongVa_C"],
                    vec!["LongVa_A"],
                    vec!["LongVa_B"],
                ],
                UTF_8,
            ),
            (
                [
                    // The accented letters are 2 bytes and the katakana is 3
                    // bytes in UTF-8.
                    ("éèäスîVarNameA", 300),
                    ("éèäスVarNameB", 1),
                    ("éèäîVarNameC", 1),
                ],
                vec![vec!["éèä", "éèä_B"], vec!["éèä_A"], vec!["éèäî"]],
                UTF_8,
            ),
            (
                [
                    // This version uses `e` with modifying acute accent in the
                    // first name.
                    ("e\u{301}èäスîVarNameA", 300),
                    ("éèäスVarNameB", 1),
                    ("éèäîVarNameC", 1),
                ],
                vec![vec!["e\u{301}èä", "e\u{301}è_A"], vec!["éèä"], vec!["éèäî"]],
                UTF_8,
            ),
            (
                [
                    // The accented letters are only 1 byte in windows-1252.
                    ("éèäîVarNameA", 300),
                    ("éèäîVarNameB", 1),
                    ("éèäîVarNameC", 1),
                ],
                vec![
                    vec!["éèäîVarN", "éèäîVa_C"],
                    vec!["éèäîVa_A"],
                    vec!["éèäîVa_B"],
                ],
                WINDOWS_1252,
            ),
        ] {
            let mut dict = Dictionary::new(encoding);
            for (name, width) in variables {
                dict.add_var(Variable::new(
                    Identifier::new(name).unwrap(),
                    VarWidth::String(width),
                    encoding,
                ))
                .unwrap();
            }
            let expected = expected
                .into_iter()
                .map(|names| {
                    names
                        .into_iter()
                        .map(|name| Identifier::new(name).unwrap())
                        .collect::<SmallVec<[_; 1]>>()
                })
                .collect::<Vec<_>>();
            assert_eq!(expected, dict.short_names());
        }
    }

    #[test]
    fn codepage_to_unicode() {
        let mut dictionary = Dictionary::new(WINDOWS_1252);

        dictionary
            .add_var(Variable::new(
                Identifier::new("ééééééééééééééééééééééééééééééééa").unwrap(),
                VarWidth::Numeric,
                WINDOWS_1252,
            ))
            .unwrap();
        dictionary
            .add_var(Variable::new(
                Identifier::new("ééééééééééééééééééééééééééééééééb").unwrap(),
                VarWidth::Numeric,
                WINDOWS_1252,
            ))
            .unwrap();

        dictionary
            .vectors_mut()
            .insert(DictIndexVector {
                name: Identifier::new("àààààààààààààààààààààààààààààààà").unwrap(),
                variables: vec![0, 1],
            })
            .unwrap();
        dictionary
            .vectors_mut()
            .insert(DictIndexVector {
                name: Identifier::new("ààààààààààààààààààààààààààààààààx").unwrap(),
                variables: vec![1, 0],
            })
            .unwrap();

        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("üüüüüüüüüüüüüüüüüüüüüüüüüüüüüüüüasdf").unwrap(),
                label: String::from("my mcgroup"),
                mr_type: MultipleResponseType::MultipleCategory,
                variables: vec![0, 1],
            })
            .unwrap();
        dictionary
            .mrsets_mut()
            .insert(DictIndexMultipleResponseSet {
                name: Identifier::new("üüüüüüüüüüüüüüüüüüüüüüüüüüüüüüüüquux").unwrap(),
                label: String::new(),
                mr_type: MultipleResponseType::MultipleDichotomy {
                    datum: Datum::Number(Some(55.0)),
                    labels: CategoryLabels::VarLabels,
                },
                variables: vec![0, 1],
            })
            .unwrap();

        dictionary.codepage_to_unicode();
        dbg!(&dictionary);
        assert_eq!(
            &dictionary.variables[0].name,
            "éééééééééééééééééééééééééééééééé"
        );
        assert_eq!(&dictionary.variables[1].name, "Var1");
        assert_eq!(
            dictionary
                .vectors()
                .get(&Identifier::new("àààààààààààààààààààààààààààààààà").unwrap())
                .unwrap()
                .variables()
                .dict_indexes(),
            &[0, 1]
        );
        assert_eq!(
            dictionary
                .vectors()
                .get(&Identifier::new("Vec1").unwrap())
                .unwrap()
                .variables()
                .dict_indexes(),
            &[1, 0]
        );
        assert!(matches!(
            dictionary
                .mrsets()
                .get(&Identifier::new("üüüüüüüüüüüüüüüüüüüüüüüüüüüüüüüü").unwrap())
                .unwrap()
                .mr_type(),
            MultipleResponseType::MultipleCategory
        ));
        assert!(matches!(
            dictionary
                .mrsets()
                .get(&Identifier::new("MrSet1").unwrap())
                .unwrap()
                .mr_type(),
            MultipleResponseType::MultipleDichotomy { .. }
        ));
    }
}
