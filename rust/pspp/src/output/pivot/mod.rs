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

//! Pivot tables.
//!
//! Pivot tables are PSPP's primary form of output.  They are analogous to the
//! pivot tables you might be familiar with from spreadsheets and databases.
//! See <https://en.wikipedia.org/wiki/Pivot_table> for a brief introduction to
//! the overall concept of a pivot table.
//!
//! In PSPP, the most important internal pieces of a pivot table are:
//!
//! - Title.  Every pivot table has a title that is displayed above it.  It also
//!   has an optional caption (displayed below it) and corner text (displayed in
//!   the upper left corner).
//!
//! - Dimensions.  A dimension consists of zero or more categories.  A category
//!   has a label, such as "df" or "Asymp. Sig." or 123 or a variable name.  The
//!   categories are the leaves of a tree whose non-leaf nodes form groups of
//!   categories.  The tree always has a root group whose label is the name of
//!   the dimension.
//!
//! - Axes.  A table has three axes: column, row, and layer.  Each dimension is
//!   assigned to an axis, and each axis has zero or more dimensions.  When an
//!   axis has more than one dimension, they are ordered from innermost to
//!   outermost.
//!
//! - Data.  A table's data consists of zero or more cells.  Each cell maps from
//!   a category for each dimension to a value, which is commonly a number but
//!   could also be a variable name or an arbitrary text string.

use std::{
    collections::HashMap,
    fmt::{Debug, Display, Write},
    io::Read,
    iter::{once, repeat, repeat_n, FusedIterator},
    ops::{Index, IndexMut, Not, Range, RangeInclusive},
    str::{from_utf8, FromStr, Utf8Error},
    sync::{Arc, OnceLock},
};

use binrw::Error as BinError;
use chrono::NaiveDateTime;
pub use color::ParseError as ParseColorError;
use color::{palette::css::TRANSPARENT, AlphaColor, Rgba8, Srgb};
use encoding_rs::{Encoding, UTF_8};
use enum_iterator::Sequence;
use enum_map::{enum_map, Enum, EnumMap};
use look_xml::TableProperties;
use quick_xml::{de::from_str, DeError};
use serde::{de::Visitor, Deserialize};
use smallstr::SmallString;
use smallvec::SmallVec;
use thiserror::Error as ThisError;
use tlo::parse_tlo;

use crate::{
    data::Datum,
    dictionary::{VarType, Variable},
    format::{Decimal, Format, Settings as FormatSettings, Type, UncheckedFormat},
    settings::{Settings, Show},
};

pub mod output;

mod look_xml;
#[cfg(test)]
pub mod test;
mod tlo;

/// Areas of a pivot table for styling purposes.
#[derive(Copy, Clone, Debug, Default, Enum, PartialEq, Eq)]
pub enum Area {
    Title,
    Caption,

    /// Footnotes,
    Footer,

    // Top-left corner.
    Corner,

    /// Labels for columns ([Axis2::X]) and rows ([Axis2::Y]).
    Labels(Axis2),

    #[default]
    Data,

    /// Layer indication.
    Layers,
}

impl Area {
    fn default_cell_style(self) -> CellStyle {
        use HorzAlign::*;
        use VertAlign::*;
        let (horz_align, vert_align, hmargins, vmargins) = match self {
            Area::Title => (Some(Center), Middle, [8, 11], [1, 8]),
            Area::Caption => (Some(Left), Top, [8, 11], [1, 1]),
            Area::Footer => (Some(Left), Top, [11, 8], [2, 3]),
            Area::Corner => (Some(Left), Bottom, [8, 11], [1, 1]),
            Area::Labels(Axis2::X) => (Some(Center), Top, [8, 11], [1, 3]),
            Area::Labels(Axis2::Y) => (Some(Left), Top, [8, 11], [1, 3]),
            Area::Data => (None, Top, [8, 11], [1, 1]),
            Area::Layers => (Some(Left), Bottom, [8, 11], [1, 3]),
        };
        CellStyle {
            horz_align,
            vert_align,
            margins: enum_map! { Axis2::X => hmargins, Axis2::Y => vmargins },
        }
    }

    fn default_font_style(self) -> FontStyle {
        FontStyle {
            bold: self == Area::Title,
            italic: false,
            underline: false,
            markup: false,
            font: String::from("Sans Serif"),
            fg: [Color::BLACK; 2],
            bg: [Color::WHITE; 2],
            size: 9,
        }
    }

    fn default_area_style(self) -> AreaStyle {
        AreaStyle {
            cell_style: self.default_cell_style(),
            font_style: self.default_font_style(),
        }
    }
}

/// Table borders for styling purposes.
#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq)]
pub enum Border {
    Title,
    OuterFrame(BoxBorder),
    InnerFrame(BoxBorder),
    Dimension(RowColBorder),
    Category(RowColBorder),
    DataLeft,
    DataTop,
}

impl Border {
    pub fn default_stroke(self) -> Stroke {
        match self {
            Self::InnerFrame(_) | Self::DataLeft | Self::DataTop => Stroke::Thick,
            Self::Dimension(
                RowColBorder(HeadingRegion::Columns, _) | RowColBorder(_, Axis2::X),
            )
            | Self::Category(RowColBorder(HeadingRegion::Columns, _)) => Stroke::Solid,
            _ => Stroke::None,
        }
    }
    pub fn default_border_style(self) -> BorderStyle {
        BorderStyle {
            stroke: self.default_stroke(),
            color: Color::BLACK,
        }
    }

    fn fallback(self) -> Self {
        match self {
            Self::Title
            | Self::OuterFrame(_)
            | Self::InnerFrame(_)
            | Self::DataLeft
            | Self::DataTop
            | Self::Category(_) => self,
            Self::Dimension(row_col_border) => Self::Category(row_col_border),
        }
    }
}

/// The borders on a box.
#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq)]
pub enum BoxBorder {
    Left,
    Top,
    Right,
    Bottom,
}

/// Borders between rows and columns.
#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq)]
pub struct RowColBorder(
    /// Row or column headings.
    pub HeadingRegion,
    /// Horizontal ([Axis2::X]) or vertical ([Axis2::Y]) borders.
    pub Axis2,
);

/// Sizing for rows or columns of a rendered table.
///
/// The comments below talk about columns and their widths but they apply
/// equally to rows and their heights.
#[derive(Default, Clone, Debug)]
pub struct Sizing {
    /// Specific column widths, in 1/96" units.
    widths: Vec<i32>,

    /// Specific page breaks: 0-based columns after which a page break must
    /// occur, e.g. a value of 1 requests a break after the second column.
    breaks: Vec<usize>,

    /// Keeps: columns to keep together on a page if possible.
    keeps: Vec<Range<usize>>,
}

#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq, Sequence)]
pub enum Axis3 {
    X,
    Y,
    Z,
}

impl Axis3 {
    fn transpose(&self) -> Option<Self> {
        match self {
            Axis3::X => Some(Axis3::Y),
            Axis3::Y => Some(Axis3::X),
            Axis3::Z => None,
        }
    }
}

impl From<Axis2> for Axis3 {
    fn from(axis2: Axis2) -> Self {
        match axis2 {
            Axis2::X => Self::X,
            Axis2::Y => Self::Y,
        }
    }
}

/// An axis within a pivot table.
#[derive(Clone, Debug, Default)]
pub struct Axis {
    /// `dimensions[0]` is the innermost dimension.
    pub dimensions: Vec<usize>,
}

pub struct AxisIterator {
    indexes: SmallVec<[usize; 4]>,
    lengths: SmallVec<[usize; 4]>,
    done: bool,
}

impl FusedIterator for AxisIterator {}
impl Iterator for AxisIterator {
    type Item = SmallVec<[usize; 4]>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.done {
            None
        } else {
            let retval = self.indexes.clone();
            for (index, len) in self.indexes.iter_mut().zip(self.lengths.iter().copied()) {
                *index += 1;
                if *index < len {
                    return Some(retval);
                };
                *index = 0;
            }
            self.done = true;
            Some(retval)
        }
    }
}

impl PivotTable {
    pub fn with_look(mut self, look: Arc<Look>) -> Self {
        self.look = look;
        self
    }
    pub fn insert_number(&mut self, data_indexes: &[usize], number: Option<f64>, class: Class) {
        let format = match class {
            Class::Other => Settings::global().default_format,
            Class::Integer => Format::F40,
            Class::Correlations => Format::F40_3,
            Class::Significance => Format::F40_3,
            Class::Percent => Format::PCT40_1,
            Class::Residual => Format::F40_2,
            Class::Count => Format::F40, // XXX
        };
        let value = Value::new(ValueInner::Number(NumberValue {
            show: None,
            format,
            honor_small: class == Class::Other,
            value: number,
            var_name: None,
            value_label: None,
        }));
        self.insert(data_indexes, value);
    }

    pub fn with_footnotes(mut self, footnotes: Footnotes) -> Self {
        debug_assert!(self.footnotes.is_empty());
        self.footnotes = footnotes;
        self
    }
    fn axis_values(&self, axis: Axis3) -> AxisIterator {
        AxisIterator {
            indexes: repeat_n(0, self.axes[axis].dimensions.len()).collect(),
            lengths: self.axis_dimensions(axis).map(|d| d.len()).collect(),
            done: self.axis_extent(axis) == 0,
        }
    }

    fn axis_extent(&self, axis: Axis3) -> usize {
        self.axis_dimensions(axis).map(|d| d.len()).product()
    }
}

/// Dimensions.
///
/// A [Dimension] identifies the categories associated with a single dimension
/// within a multidimensional pivot table.
///
/// A dimension contains a collection of categories, which are the leaves in a
/// tree of groups.
///
/// (A dimension or a group can contain zero categories, but this is unusual.
/// If a dimension contains no categories, then its table cannot contain any
/// data.)
#[derive(Clone, Debug)]
pub struct Dimension {
    /// Hierarchy of categories within the dimension.  The groups and categories
    /// are sorted in the order that should be used for display.  This might be
    /// different from the original order produced for output if the user
    /// adjusted it.
    ///
    /// The root must always be a group, although it is allowed to have no
    /// subcategories.
    pub root: Group,

    /// Ordering of leaves for presentation.
    ///
    /// This is a permutation of `0..n` where `n` is the number of leaves.  It
    /// maps from an index in presentation order to an index in data order.
    pub presentation_order: Vec<usize>,

    /// Display.
    pub hide_all_labels: bool,
}

pub type GroupVec<'a> = SmallVec<[&'a Group; 4]>;
pub struct Path<'a> {
    groups: GroupVec<'a>,
    leaf: &'a Leaf,
}

impl Dimension {
    pub fn new(root: Group) -> Self {
        Dimension {
            presentation_order: (0..root.len()).collect(),
            root,
            hide_all_labels: false,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the number of (leaf) categories in this dimension.
    pub fn len(&self) -> usize {
        self.root.len()
    }

    pub fn nth_leaf(&self, index: usize) -> Option<&Leaf> {
        self.root.nth_leaf(index)
    }

    pub fn leaf_path(&self, index: usize) -> Option<Path<'_>> {
        self.root.leaf_path(index, SmallVec::new())
    }

    pub fn with_all_labels_hidden(self) -> Self {
        Self {
            hide_all_labels: true,
            ..self
        }
    }
}

#[derive(Clone, Debug)]
pub struct Group {
    len: usize,
    pub name: Box<Value>,

    /// The child categories.
    ///
    /// A group usually has multiple children, but it is allowed to have
    /// only one or even (pathologically) none.
    pub children: Vec<Category>,

    /// Whether to show the group's label.
    pub show_label: bool,
}

impl Group {
    pub fn new(name: impl Into<Value>) -> Group {
        Self {
            len: 0,
            name: Box::new(name.into()),
            children: Vec::new(),
            show_label: false,
        }
    }

    pub fn push(&mut self, child: impl Into<Category>) {
        let mut child = child.into();
        if let Category::Group(group) = &mut child {
            group.show_label = true;
        }
        self.len += child.len();
        self.children.push(child);
    }

    pub fn with(mut self, child: impl Into<Category>) -> Self {
        self.push(child);
        self
    }

    pub fn with_multiple<C>(mut self, children: impl IntoIterator<Item = C>) -> Self
    where
        C: Into<Category>,
    {
        self.extend(children);
        self
    }

    pub fn with_label_shown(self) -> Self {
        self.with_show_label(true)
    }

    pub fn with_show_label(mut self, show_label: bool) -> Self {
        self.show_label = show_label;
        self
    }

    pub fn nth_leaf(&self, mut index: usize) -> Option<&Leaf> {
        for child in &self.children {
            let len = child.len();
            if index < len {
                return child.nth_leaf(index);
            }
            index -= len;
        }
        None
    }

    pub fn leaf_path<'a>(&'a self, mut index: usize, mut groups: GroupVec<'a>) -> Option<Path<'a>> {
        for child in &self.children {
            let len = child.len();
            if index < len {
                groups.push(self);
                return child.leaf_path(index, groups);
            }
            index -= len;
        }
        None
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn name(&self) -> &Value {
        &self.name
    }
}

impl<C> Extend<C> for Group
where
    C: Into<Category>,
{
    fn extend<T: IntoIterator<Item = C>>(&mut self, children: T) {
        let children = children.into_iter();
        self.children.reserve(children.size_hint().0);
        for child in children {
            self.push(child);
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct Footnotes(pub Vec<Arc<Footnote>>);

impl Footnotes {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn push(&mut self, footnote: Footnote) -> Arc<Footnote> {
        let footnote = Arc::new(footnote.with_index(self.0.len()));
        self.0.push(footnote.clone());
        footnote
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

#[derive(Clone, Debug)]
pub struct Leaf {
    name: Box<Value>,
}

impl Leaf {
    pub fn new(name: Value) -> Self {
        Self {
            name: Box::new(name),
        }
    }
    pub fn name(&self) -> &Value {
        &self.name
    }
}

/// Pivot result classes.
///
/// These are used to mark [Leaf] categories as having particular types of data,
/// to set their numeric formats.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Class {
    Other,
    Integer,
    Correlations,
    Significance,
    Percent,
    Residual,
    Count,
}

/// A pivot_category is a leaf (a category) or a group.
#[derive(Clone, Debug)]
pub enum Category {
    Group(Group),
    Leaf(Leaf),
}

impl Category {
    pub fn name(&self) -> &Value {
        match self {
            Category::Group(group) => &group.name,
            Category::Leaf(leaf) => &leaf.name,
        }
    }

    pub fn len(&self) -> usize {
        match self {
            Category::Group(group) => group.len,
            Category::Leaf(_) => 1,
        }
    }

    pub fn nth_leaf(&self, index: usize) -> Option<&Leaf> {
        match self {
            Category::Group(group) => group.nth_leaf(index),
            Category::Leaf(leaf) => {
                if index == 0 {
                    Some(leaf)
                } else {
                    None
                }
            }
        }
    }

    pub fn leaf_path<'a>(&'a self, index: usize, groups: GroupVec<'a>) -> Option<Path<'a>> {
        match self {
            Category::Group(group) => group.leaf_path(index, groups),
            Category::Leaf(leaf) => {
                if index == 0 {
                    Some(Path { groups, leaf })
                } else {
                    None
                }
            }
        }
    }

    pub fn show_label(&self) -> bool {
        match self {
            Category::Group(group) => group.show_label,
            Category::Leaf(_) => true,
        }
    }
}

impl From<Group> for Category {
    fn from(group: Group) -> Self {
        Self::Group(group)
    }
}

impl From<Leaf> for Category {
    fn from(group: Leaf) -> Self {
        Self::Leaf(group)
    }
}

impl From<Value> for Category {
    fn from(name: Value) -> Self {
        Leaf::new(name).into()
    }
}

impl From<&Variable> for Category {
    fn from(variable: &Variable) -> Self {
        Value::new_variable(variable).into()
    }
}

impl From<&str> for Category {
    fn from(name: &str) -> Self {
        Self::Leaf(Leaf::new(Value::new_text(name)))
    }
}

/// Styling for a pivot table.
///
/// The division between this and the style information in [PivotTable] seems
/// fairly arbitrary.  The ultimate reason for the division is simply because
/// that's how SPSS documentation and file formats do it.
#[derive(Clone, Debug)]
pub struct Look {
    pub name: Option<String>,

    /// Whether to hide rows or columns whose cells are all empty.
    pub hide_empty: bool,

    pub row_label_position: LabelPosition,

    /// Ranges of column widths in the two heading regions, in 1/96" units.
    pub heading_widths: EnumMap<HeadingRegion, RangeInclusive<usize>>,

    /// Kind of markers to use for footnotes.
    pub footnote_marker_type: FootnoteMarkerType,

    /// Where to put the footnote markers.
    pub footnote_marker_position: FootnoteMarkerPosition,

    /// Styles for areas of the pivot table.
    pub areas: EnumMap<Area, AreaStyle>,

    /// Styles for borders in the pivot table.
    pub borders: EnumMap<Border, BorderStyle>,

    pub print_all_layers: bool,

    pub paginate_layers: bool,

    pub shrink_to_fit: EnumMap<Axis2, bool>,

    pub top_continuation: bool,

    pub bottom_continuation: bool,

    pub continuation: Option<String>,

    pub n_orphan_lines: usize,
}

impl Look {
    pub fn with_omit_empty(mut self, omit_empty: bool) -> Self {
        self.hide_empty = omit_empty;
        self
    }
    pub fn with_row_label_position(mut self, row_label_position: LabelPosition) -> Self {
        self.row_label_position = row_label_position;
        self
    }
    pub fn with_borders(mut self, borders: EnumMap<Border, BorderStyle>) -> Self {
        self.borders = borders;
        self
    }
}

impl Default for Look {
    fn default() -> Self {
        Self {
            name: None,
            hide_empty: true,
            row_label_position: LabelPosition::default(),
            heading_widths: EnumMap::from_fn(|region| match region {
                HeadingRegion::Rows => 36..=72,
                HeadingRegion::Columns => 36..=120,
            }),
            footnote_marker_type: FootnoteMarkerType::default(),
            footnote_marker_position: FootnoteMarkerPosition::default(),
            areas: EnumMap::from_fn(Area::default_area_style),
            borders: EnumMap::from_fn(Border::default_border_style),
            print_all_layers: false,
            paginate_layers: false,
            shrink_to_fit: EnumMap::from_fn(|_| false),
            top_continuation: false,
            bottom_continuation: false,
            continuation: None,
            n_orphan_lines: 0,
        }
    }
}

#[derive(ThisError, Debug)]
pub enum ParseLookError {
    #[error(transparent)]
    XmlError(#[from] DeError),

    #[error(transparent)]
    Utf8Error(#[from] Utf8Error),

    #[error(transparent)]
    BinError(#[from] BinError),

    #[error(transparent)]
    IoError(#[from] std::io::Error),
}

impl Look {
    pub fn shared_default() -> Arc<Look> {
        static LOOK: OnceLock<Arc<Look>> = OnceLock::new();
        LOOK.get_or_init(|| Arc::new(Look::default())).clone()
    }

    pub fn from_xml(xml: &str) -> Result<Self, ParseLookError> {
        Ok(from_str::<TableProperties>(xml)
            .map_err(ParseLookError::from)?
            .into())
    }

    pub fn from_binary(tlo: &[u8]) -> Result<Self, ParseLookError> {
        parse_tlo(tlo).map_err(ParseLookError::from)
    }

    pub fn from_data(data: &[u8]) -> Result<Self, ParseLookError> {
        if data.starts_with(b"\xff\xff\0\0") {
            Self::from_binary(data)
        } else {
            Self::from_xml(from_utf8(data).map_err(ParseLookError::from)?)
        }
    }

    pub fn from_reader<R>(mut reader: R) -> Result<Self, ParseLookError>
    where
        R: Read,
    {
        let mut buffer = Vec::new();
        reader
            .read_to_end(&mut buffer)
            .map_err(ParseLookError::from)?;
        Self::from_data(&buffer)
    }
}

/// Position for group labels.
#[derive(Copy, Clone, Debug, Default, Deserialize, PartialEq, Eq)]
pub enum LabelPosition {
    /// Hierarachically enclosing the categories.
    ///
    /// For column labels, group labels appear above the categories.  For row
    /// labels, group labels appear to the left of the categories.
    ///
    /// ```text
    /// ┌────┬──────────────┐   ┌─────────┬──────────┐
    /// │    │    nested    │   │         │ columns  │
    /// │    ├────┬────┬────┤   ├──────┬──┼──────────┤
    /// │    │ a1 │ a2 │ a3 │   │      │a1│...data...│
    /// ├────┼────┼────┼────┤   │nested│a2│...data...│
    /// │    │data│data│data│   │      │a3│...data...│
    /// │    │ .  │ .  │ .  │   └──────┴──┴──────────┘
    /// │rows│ .  │ .  │ .  │
    /// │    │ .  │ .  │ .  │
    /// └────┴────┴────┴────┘
    /// ```
    #[serde(rename = "nested")]
    Nested,

    /// In the corner (row labels only).
    ///
    /// ```text
    /// ┌──────┬──────────┐
    /// │corner│ columns  │
    /// ├──────┼──────────┤
    /// │    a1│...data...│
    /// │    a2│...data...│
    /// │    a3│...data...│
    /// └──────┴──────────┘
    /// ```
    #[default]
    #[serde(rename = "inCorner")]
    Corner,
}

/// The heading region of a rendered pivot table:
///
/// ```text
/// ┌──────────────────┬─────────────────────────────────────────────────┐
/// │                  │                  column headings                │
/// │                  ├─────────────────────────────────────────────────┤
/// │      corner      │                                                 │
/// │       and        │                                                 │
/// │   row headings   │                      data                       │
/// │                  │                                                 │
/// │                  │                                                 │
/// └──────────────────┴─────────────────────────────────────────────────┘
/// ```
#[derive(Copy, Clone, Debug, PartialEq, Eq, Enum)]
pub enum HeadingRegion {
    Rows,
    Columns,
}

impl From<Axis2> for HeadingRegion {
    fn from(axis: Axis2) -> Self {
        match axis {
            Axis2::X => HeadingRegion::Columns,
            Axis2::Y => HeadingRegion::Rows,
        }
    }
}

#[derive(Clone, Debug)]
pub struct AreaStyle {
    pub cell_style: CellStyle,
    pub font_style: FontStyle,
}

#[derive(Clone, Debug)]
pub struct CellStyle {
    /// `None` means "mixed" alignment: align strings to the left, numbers to
    /// the right.
    pub horz_align: Option<HorzAlign>,
    pub vert_align: VertAlign,

    /// Margins in 1/96" units.
    ///
    /// `margins[Axis2::X][0]` is the left margin.
    /// `margins[Axis2::X][1]` is the right margin.
    /// `margins[Axis2::Y][0]` is the top margin.
    /// `margins[Axis2::Y][1]` is the bottom margin.
    pub margins: EnumMap<Axis2, [i32; 2]>,
}

#[derive(Copy, Clone, Debug, PartialEq)]
pub enum HorzAlign {
    /// Right aligned.
    Right,

    /// Left aligned.
    Left,

    /// Centered.
    Center,

    /// Align the decimal point at the specified position.
    Decimal {
        /// Decimal offset from the right side of the cell, in 1/96" units.
        offset: f64,

        /// Decimal character.
        decimal: Decimal,
    },
}

impl HorzAlign {
    pub fn for_mixed(var_type: VarType) -> Self {
        match var_type {
            VarType::Numeric => Self::Right,
            VarType::String => Self::Left,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum VertAlign {
    /// Top alignment.
    Top,

    /// Centered,
    Middle,

    /// Bottom alignment.
    Bottom,
}

#[derive(Clone, Debug)]
pub struct FontStyle {
    pub bold: bool,
    pub italic: bool,
    pub underline: bool,
    pub markup: bool,
    pub font: String,

    /// `fg[0]` is the usual foreground color.
    ///
    /// `fg[1]` is used only in [Area::Data] for odd-numbered rows.
    pub fg: [Color; 2],

    /// `bg[0]` is the usual background color.
    ///
    /// `bg[1]` is used only in [Area::Data] for odd-numbered rows.
    pub bg: [Color; 2],

    /// In 1/72" units.
    pub size: i32,
}

#[derive(Copy, Clone, PartialEq, Eq)]
pub struct Color {
    pub alpha: u8,
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Color {
    pub const BLACK: Color = Color::new(0, 0, 0);
    pub const WHITE: Color = Color::new(255, 255, 255);
    pub const RED: Color = Color::new(255, 0, 0);
    pub const BLUE: Color = Color::new(0, 0, 255);
    pub const TRANSPARENT: Color = Color::new(0, 0, 0).with_alpha(0);

    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self {
            alpha: 255,
            r,
            g,
            b,
        }
    }

    pub const fn with_alpha(self, alpha: u8) -> Self {
        Self { alpha, ..self }
    }

    pub const fn without_alpha(self) -> Self {
        self.with_alpha(255)
    }

    pub fn display_css(&self) -> DisplayCss {
        DisplayCss(*self)
    }
}

impl Debug for Color {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.display_css())
    }
}

impl From<Rgba8> for Color {
    fn from(Rgba8 { r, g, b, a }: Rgba8) -> Self {
        Self::new(r, g, b).with_alpha(a)
    }
}

impl FromStr for Color {
    type Err = ParseColorError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        fn is_bare_hex(s: &str) -> bool {
            let s = s.trim();
            s.chars().count() == 6 && s.chars().all(|c| c.is_ascii_hexdigit())
        }
        let color: AlphaColor<Srgb> = match s.parse() {
            Err(ParseColorError::UnknownColorSyntax) if is_bare_hex(s) => {
                ("#".to_owned() + s).parse()
            }
            Err(ParseColorError::UnknownColorSyntax)
                if s.trim().eq_ignore_ascii_case("transparent") =>
            {
                Ok(TRANSPARENT)
            }
            other => other,
        }?;
        Ok(color.to_rgba8().into())
    }
}

impl<'de> Deserialize<'de> for Color {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        struct ColorVisitor;

        impl<'de> Visitor<'de> for ColorVisitor {
            type Value = Color;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("\"#rrggbb\" or \"rrggbb\" or web color name")
            }

            fn visit_borrowed_str<E>(self, v: &'de str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                v.parse().map_err(E::custom)
            }
        }

        deserializer.deserialize_str(ColorVisitor)
    }
}

pub struct DisplayCss(Color);

impl Display for DisplayCss {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Color { alpha, r, g, b } = self.0;
        match alpha {
            255 => write!(f, "#{r:02x}{g:02x}{b:02x}"),
            _ => write!(f, "rgb({r}, {g}, {b}, {:.2})", alpha as f64 / 255.0),
        }
    }
}

#[derive(Copy, Clone, Debug, Deserialize)]
pub struct BorderStyle {
    #[serde(rename = "@borderStyleType")]
    pub stroke: Stroke,

    #[serde(rename = "@color")]
    pub color: Color,
}

impl BorderStyle {
    pub const fn none() -> Self {
        Self {
            stroke: Stroke::None,
            color: Color::BLACK,
        }
    }

    pub fn is_none(&self) -> bool {
        self.stroke.is_none()
    }

    /// Returns a border style that "combines" the two arguments, that is, that
    /// gives a reasonable choice for a rule for different reasons should have
    /// both styles.
    pub fn combine(self, other: BorderStyle) -> Self {
        Self {
            stroke: self.stroke.combine(other.stroke),
            color: self.color,
        }
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Enum, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Stroke {
    None,
    Solid,
    Dashed,
    Thick,
    Thin,
    Double,
}

impl Stroke {
    pub fn is_none(&self) -> bool {
        self == &Self::None
    }

    /// Returns a stroke that "combines" the two arguments, that is, that gives
    /// a reasonable stroke choice for a rule for different reasons should have
    /// both styles.
    pub fn combine(self, other: Stroke) -> Self {
        self.max(other)
    }
}

/// An axis of a 2-dimensional table.
#[derive(Copy, Clone, Debug, Enum, PartialEq, Eq)]
pub enum Axis2 {
    X,
    Y,
}

impl Axis2 {
    pub fn new_enum<T>(x: T, y: T) -> EnumMap<Axis2, T> {
        EnumMap::from_array([x, y])
    }
}

impl Not for Axis2 {
    type Output = Self;

    fn not(self) -> Self::Output {
        match self {
            Self::X => Self::Y,
            Self::Y => Self::X,
        }
    }
}

/// A 2-dimensional `(x,y)` pair.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, Hash)]
pub struct Coord2(pub EnumMap<Axis2, usize>);

impl Coord2 {
    pub fn new(x: usize, y: usize) -> Self {
        use Axis2::*;
        Self(enum_map! {
            X => x,
            Y => y
        })
    }

    pub fn for_axis((a, az): (Axis2, usize), bz: usize) -> Self {
        let mut coord = Self::default();
        coord[a] = az;
        coord[!a] = bz;
        coord
    }

    pub fn from_fn<F>(f: F) -> Self
    where
        F: FnMut(Axis2) -> usize,
    {
        Self(EnumMap::from_fn(f))
    }

    pub fn x(&self) -> usize {
        self.0[Axis2::X]
    }

    pub fn y(&self) -> usize {
        self.0[Axis2::Y]
    }

    pub fn get(&self, axis: Axis2) -> usize {
        self.0[axis]
    }
}

impl From<EnumMap<Axis2, usize>> for Coord2 {
    fn from(value: EnumMap<Axis2, usize>) -> Self {
        Self(value)
    }
}

impl Index<Axis2> for Coord2 {
    type Output = usize;

    fn index(&self, index: Axis2) -> &Self::Output {
        &self.0[index]
    }
}

impl IndexMut<Axis2> for Coord2 {
    fn index_mut(&mut self, index: Axis2) -> &mut Self::Output {
        &mut self.0[index]
    }
}

#[derive(Clone, Debug, Default)]
pub struct Rect2(pub EnumMap<Axis2, Range<usize>>);

impl Rect2 {
    pub fn new(x_range: Range<usize>, y_range: Range<usize>) -> Self {
        Self(enum_map! {
            Axis2::X => x_range.clone(),
            Axis2::Y => y_range.clone(),
        })
    }
    pub fn for_cell(cell: Coord2) -> Self {
        Self::new(cell.x()..cell.x() + 1, cell.y()..cell.y() + 1)
    }
    pub fn for_ranges((a, a_range): (Axis2, Range<usize>), b_range: Range<usize>) -> Self {
        let b = !a;
        let mut ranges = EnumMap::default();
        ranges[a] = a_range;
        ranges[b] = b_range;
        Self(ranges)
    }
    pub fn top_left(&self) -> Coord2 {
        use Axis2::*;
        Coord2::new(self[X].start, self[Y].start)
    }
    pub fn from_fn<F>(f: F) -> Self
    where
        F: FnMut(Axis2) -> Range<usize>,
    {
        Self(EnumMap::from_fn(f))
    }
    pub fn translate(self, offset: Coord2) -> Rect2 {
        Self::from_fn(|axis| self[axis].start + offset[axis]..self[axis].end + offset[axis])
    }
    pub fn is_empty(&self) -> bool {
        self[Axis2::X].is_empty() || self[Axis2::Y].is_empty()
    }
}

impl From<EnumMap<Axis2, Range<usize>>> for Rect2 {
    fn from(value: EnumMap<Axis2, Range<usize>>) -> Self {
        Self(value)
    }
}

impl Index<Axis2> for Rect2 {
    type Output = Range<usize>;

    fn index(&self, index: Axis2) -> &Self::Output {
        &self.0[index]
    }
}

impl IndexMut<Axis2> for Rect2 {
    fn index_mut(&mut self, index: Axis2) -> &mut Self::Output {
        &mut self.0[index]
    }
}

#[derive(Copy, Clone, Debug, Default, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum FootnoteMarkerType {
    /// a, b, c, ...
    #[default]
    Alphabetic,

    /// 1, 2, 3, ...
    Numeric,
}

#[derive(Copy, Clone, Debug, Default, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum FootnoteMarkerPosition {
    /// Subscripts.
    #[default]
    Subscript,

    /// Superscripts.
    Superscript,
}

#[derive(Copy, Clone, Debug)]
pub struct ValueOptions {
    pub show_values: Option<Show>,

    pub show_variables: Option<Show>,

    pub small: f64,

    /// Where to put the footnote markers.
    pub footnote_marker_type: FootnoteMarkerType,
}

impl Default for ValueOptions {
    fn default() -> Self {
        Self {
            show_values: None,
            show_variables: None,
            small: 0.0001,
            footnote_marker_type: FootnoteMarkerType::default(),
        }
    }
}

pub trait IntoValueOptions {
    fn into_value_options(self) -> ValueOptions;
}

impl IntoValueOptions for () {
    fn into_value_options(self) -> ValueOptions {
        ValueOptions::default()
    }
}

impl IntoValueOptions for &PivotTable {
    fn into_value_options(self) -> ValueOptions {
        self.value_options()
    }
}

impl IntoValueOptions for &ValueOptions {
    fn into_value_options(self) -> ValueOptions {
        *self
    }
}

impl IntoValueOptions for ValueOptions {
    fn into_value_options(self) -> ValueOptions {
        self
    }
}

#[derive(Clone, Debug)]
pub struct PivotTable {
    pub look: Arc<Look>,

    pub rotate_inner_column_labels: bool,

    pub rotate_outer_row_labels: bool,

    pub show_grid_lines: bool,

    pub show_title: bool,

    pub show_caption: bool,

    pub show_values: Option<Show>,

    pub show_variables: Option<Show>,

    pub weight_format: Format,

    /// Current layer indexes, with `axes[Axis3::Z].dimensions.len()` elements.
    /// `current_layer[i]` is an offset into
    /// `axes[Axis3::Z].dimensions[i].data_leaves[]`, except that a dimension
    /// can have zero leaves, in which case `current_layer[i]` is zero and
    /// there's no corresponding leaf.
    pub current_layer: Vec<usize>,

    /// Column and row sizing and page breaks.
    pub sizing: EnumMap<Axis2, Option<Box<Sizing>>>,

    /// Format settings.
    pub settings: FormatSettings,

    /// Numeric grouping character (usually `.` or `,`).
    pub grouping: Option<char>,

    pub small: f64,

    pub command_local: Option<String>,
    pub command_c: Option<String>,
    pub language: Option<String>,
    pub locale: Option<String>,
    pub dataset: Option<String>,
    pub datafile: Option<String>,
    pub date: Option<NaiveDateTime>,
    pub footnotes: Footnotes,
    pub title: Option<Box<Value>>,
    pub subtype: Option<Box<Value>>,
    pub corner_text: Option<Box<Value>>,
    pub caption: Option<Box<Value>>,
    pub notes: Option<String>,
    pub dimensions: Vec<Dimension>,
    pub axes: EnumMap<Axis3, Axis>,
    pub cells: HashMap<usize, Value>,
}

impl PivotTable {
    pub fn with_title(mut self, title: impl Into<Value>) -> Self {
        self.title = Some(Box::new(title.into()));
        self.show_title = true;
        self
    }

    pub fn with_caption(mut self, caption: Value) -> Self {
        self.caption = Some(Box::new(caption));
        self.show_caption = true;
        self
    }

    pub fn with_corner_text(mut self, corner_text: Value) -> Self {
        self.corner_text = Some(Box::new(corner_text));
        self
    }

    pub fn with_subtype(self, subtype: Value) -> Self {
        Self {
            subtype: Some(Box::new(subtype)),
            ..self
        }
    }

    pub fn with_show_title(mut self, show_title: bool) -> Self {
        self.show_title = show_title;
        self
    }

    pub fn with_show_caption(mut self, show_caption: bool) -> Self {
        self.show_caption = show_caption;
        self
    }

    pub fn with_layer(mut self, layer: &[usize]) -> Self {
        debug_assert_eq!(layer.len(), self.current_layer.len());
        if self.look.print_all_layers {
            self.look_mut().print_all_layers = false;
        }
        self.current_layer.clear();
        self.current_layer.extend_from_slice(layer);
        self
    }

    pub fn with_all_layers(mut self) -> Self {
        if !self.look.print_all_layers {
            self.look_mut().print_all_layers = true;
        }
        self
    }

    pub fn look_mut(&mut self) -> &mut Look {
        Arc::make_mut(&mut self.look)
    }

    pub fn with_show_empty(mut self) -> Self {
        if self.look.hide_empty {
            self.look_mut().hide_empty = false;
        }
        self
    }

    pub fn with_hide_empty(mut self) -> Self {
        if !self.look.hide_empty {
            self.look_mut().hide_empty = true;
        }
        self
    }

    pub fn label(&self) -> String {
        match &self.title {
            Some(title) => title.display(self).to_string(),
            None => String::from("Table"),
        }
    }

    pub fn title(&self) -> &Value {
        match &self.title {
            Some(title) => &*title,
            None => {
                static EMPTY: Value = Value::empty();
                &EMPTY
            }
        }
    }

    pub fn subtype(&self) -> &Value {
        match &self.subtype {
            Some(subtype) => &*subtype,
            None => {
                static EMPTY: Value = Value::empty();
                &EMPTY
            }
        }
    }
}

impl Default for PivotTable {
    fn default() -> Self {
        Self {
            look: Look::shared_default(),
            rotate_inner_column_labels: false,
            rotate_outer_row_labels: false,
            show_grid_lines: false,
            show_title: true,
            show_caption: true,
            show_values: None,
            show_variables: None,
            weight_format: Format::F40,
            current_layer: Vec::new(),
            sizing: EnumMap::default(),
            settings: FormatSettings::default(), // XXX from settings
            grouping: None,
            small: 0.0001, // XXX from settings.
            command_local: None,
            command_c: None, // XXX from current command name.
            language: None,
            locale: None,
            dataset: None,
            datafile: None,
            date: None,
            footnotes: Footnotes::new(),
            subtype: None,
            title: None,
            corner_text: None,
            caption: None,
            notes: None,
            dimensions: Vec::new(),
            axes: EnumMap::default(),
            cells: HashMap::new(),
        }
    }
}

fn cell_index<I>(data_indexes: &[usize], dimensions: I) -> usize
where
    I: ExactSizeIterator<Item = usize>,
{
    debug_assert_eq!(data_indexes.len(), dimensions.len());
    let mut index = 0;
    for (dimension, data_index) in dimensions.zip(data_indexes.iter()) {
        debug_assert!(*data_index < dimension);
        index = dimension * index + data_index;
    }
    index
}

impl PivotTable {
    pub fn new(dimensions_and_axes: impl IntoIterator<Item = (Axis3, Dimension)>) -> Self {
        let mut dimensions = Vec::new();
        let mut axes = EnumMap::<Axis3, Axis>::default();
        for (axis, dimension) in dimensions_and_axes {
            axes[axis].dimensions.push(dimensions.len());
            dimensions.push(dimension);
        }
        Self {
            look: Settings::global().look.clone(),
            current_layer: repeat_n(0, axes[Axis3::Z].dimensions.len()).collect(),
            axes,
            dimensions,
            ..Self::default()
        }
    }
    fn cell_index(&self, data_indexes: &[usize]) -> usize {
        cell_index(data_indexes, self.dimensions.iter().map(|d| d.len()))
    }

    pub fn insert(&mut self, data_indexes: &[usize], value: impl Into<Value>) {
        self.cells
            .insert(self.cell_index(data_indexes), value.into());
    }

    pub fn get(&self, data_indexes: &[usize]) -> Option<&Value> {
        self.cells.get(&self.cell_index(data_indexes))
    }

    pub fn with_data<I>(mut self, iter: impl IntoIterator<Item = (I, Value)>) -> Self
    where
        I: AsRef<[usize]>,
    {
        self.extend(iter);
        self
    }

    /// Converts per-axis presentation-order indexes in `presentation_indexes`,
    /// into data indexes for each dimension.
    fn convert_indexes_ptod(
        &self,
        presentation_indexes: EnumMap<Axis3, &[usize]>,
    ) -> SmallVec<[usize; 4]> {
        let mut data_indexes = SmallVec::from_elem(0, self.dimensions.len());
        for (axis, presentation_indexes) in presentation_indexes {
            for (&dim_index, &pindex) in self.axes[axis]
                .dimensions
                .iter()
                .zip(presentation_indexes.iter())
            {
                data_indexes[dim_index] = self.dimensions[dim_index].presentation_order[pindex];
            }
        }
        data_indexes
    }

    /// Returns an iterator for the layer axis:
    ///
    /// - If `print` is true and `self.look.print_all_layers`, then the iterator
    ///   will visit all values of the layer axis.
    ///
    /// - Otherwise, the iterator will just visit `self.current_layer`.
    pub fn layers(&self, print: bool) -> Box<dyn Iterator<Item = SmallVec<[usize; 4]>>> {
        if print && self.look.print_all_layers {
            Box::new(self.axis_values(Axis3::Z))
        } else {
            Box::new(once(SmallVec::from_slice(&self.current_layer)))
        }
    }

    pub fn value_options(&self) -> ValueOptions {
        ValueOptions {
            show_values: self.show_values,
            show_variables: self.show_variables,
            small: self.small,
            footnote_marker_type: self.look.footnote_marker_type,
        }
    }

    pub fn transpose(&mut self) {
        self.axes.swap(Axis3::X, Axis3::Y);
    }

    pub fn axis_dimensions(
        &self,
        axis: Axis3,
    ) -> impl DoubleEndedIterator<Item = &Dimension> + ExactSizeIterator {
        self.axes[axis]
            .dimensions
            .iter()
            .copied()
            .map(|index| &self.dimensions[index])
    }

    fn find_dimension(&self, dim_index: usize) -> Option<(Axis3, usize)> {
        debug_assert!(dim_index < self.dimensions.len());
        for axis in enum_iterator::all::<Axis3>() {
            for (position, dimension) in self.axes[axis].dimensions.iter().copied().enumerate() {
                if dimension == dim_index {
                    return Some((axis, position));
                }
            }
        }
        None
    }
    pub fn move_dimension(&mut self, dim_index: usize, new_axis: Axis3, new_position: usize) {
        let (old_axis, old_position) = self.find_dimension(dim_index).unwrap();
        if old_axis == new_axis && old_position == new_position {
            return;
        }

        // Update the current layer, if necessary.  If we're moving within the
        // layer axis, preserve the current layer.
        match (old_axis, new_axis) {
            (Axis3::Z, Axis3::Z) => {
                // Rearrange the layer axis.
                if old_position < new_position {
                    self.current_layer[old_position..=new_position].rotate_left(1);
                } else {
                    self.current_layer[new_position..=old_position].rotate_right(1);
                }
            }
            (Axis3::Z, _) => {
                // A layer is becoming a row or column.
                self.current_layer.remove(old_position);
            }
            (_, Axis3::Z) => {
                // A row or column is becoming a layer.
                self.current_layer.insert(new_position, 0);
            }
            _ => (),
        }

        self.axes[old_axis].dimensions.remove(old_position);
        self.axes[new_axis]
            .dimensions
            .insert(new_position, dim_index);
    }
}

impl<I> Extend<(I, Value)> for PivotTable
where
    I: AsRef<[usize]>,
{
    fn extend<T: IntoIterator<Item = (I, Value)>>(&mut self, iter: T) {
        for (data_indexes, value) in iter {
            self.insert(data_indexes.as_ref(), value);
        }
    }
}

#[derive(Clone, Debug)]
pub struct Footnote {
    index: usize,
    pub content: Box<Value>,
    pub marker: Option<Box<Value>>,
    pub show: bool,
}

impl Footnote {
    pub fn new(content: impl Into<Value>) -> Self {
        Self {
            index: 0,
            content: Box::new(content.into()),
            marker: None,
            show: true,
        }
    }
    pub fn with_marker(mut self, marker: impl Into<Value>) -> Self {
        self.marker = Some(Box::new(marker.into()));
        self
    }

    pub fn with_show(mut self, show: bool) -> Self {
        self.show = show;
        self
    }

    pub fn with_index(mut self, index: usize) -> Self {
        self.index = index;
        self
    }

    pub fn display_marker(&self, options: impl IntoValueOptions) -> DisplayMarker<'_> {
        DisplayMarker {
            footnote: self,
            options: options.into_value_options(),
        }
    }

    pub fn display_content(&self, options: impl IntoValueOptions) -> DisplayValue<'_> {
        self.content.display(options)
    }

    pub fn index(&self) -> usize {
        self.index
    }
}

pub struct DisplayMarker<'a> {
    footnote: &'a Footnote,
    options: ValueOptions,
}

impl Display for DisplayMarker<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if let Some(marker) = &self.footnote.marker {
            write!(f, "{}", marker.display(self.options).without_suffixes())
        } else {
            let i = self.footnote.index + 1;
            match self.options.footnote_marker_type {
                FootnoteMarkerType::Alphabetic => write!(f, "{}", Display26Adic(i)),
                FootnoteMarkerType::Numeric => write!(f, "{i}"),
            }
        }
    }
}

pub struct Display26Adic(pub usize);

impl Display for Display26Adic {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut output = SmallVec::<[u8; 16]>::new();
        let mut number = self.0;
        while number > 0 {
            number -= 1;
            let digit = (number % 26) as u8;
            output.push(digit + b'a');
            number /= 26;
        }
        output.reverse();
        write!(f, "{}", from_utf8(&output).unwrap())
    }
}

/// The content of a single pivot table cell.
///
/// A [Value] is also a pivot table's title, caption, footnote marker and
/// contents, and so on.
///
/// A given [Value] is one of:
///
/// 1. A number resulting from a calculation.
///
///    A number has an associated display format (usually [F] or [Pct]).  This
///    format can be set directly, but that is not usually the easiest way.
///    Instead, it is usually true that all of the values in a single category
///    should have the same format (e.g. all "Significance" values might use
///    format `F40.3`), so PSPP makes it easy to set the default format for a
///    category while creating the category.  See pivot_dimension_create() for
///    more details.
///
///    [F]: crate::format::Type::F
///    [Pct]: crate::format::Type::Pct
///
/// 2. A numeric or string value obtained from data ([ValueInner::Number] or
///    [ValueInner::String]).  If such a value corresponds to a variable, then the
///    variable's name can be attached to the pivot_value.  If the value has a
///    value label, then that can also be attached.  When a label is present,
///    the user can control whether to show the value or the label or both.
///
/// 3. A variable name ([ValueInner::Variable]).  The variable label, if any, can
///    be attached too, and again the user can control whether to show the value
///    or the label or both.
///
/// 4. A text string ([ValueInner::Text).  The value stores the string in English
///    and translated into the output language (localized).  Use
///    pivot_value_new_text() or pivot_value_new_text_format() for those cases.
///    In some cases, only an English or a localized version is available for
///    one reason or another, although this is regrettable; in those cases, use
///    pivot_value_new_user_text() or pivot_value_new_user_text_nocopy().
///
/// 5. A template. PSPP doesn't create these itself yet, but it can read and
///    interpret those created by SPSS.
#[derive(Clone, Default)]
pub struct Value {
    pub inner: ValueInner,
    pub styling: Option<Box<ValueStyle>>,
}

impl Value {
    fn new(inner: ValueInner) -> Self {
        Self {
            inner,
            styling: None,
        }
    }
    pub fn new_number_with_format(x: Option<f64>, format: Format) -> Self {
        Self::new(ValueInner::Number(NumberValue {
            show: None,
            format,
            honor_small: false,
            value: x,
            var_name: None,
            value_label: None,
        }))
    }
    pub fn new_variable(variable: &Variable) -> Self {
        Self::new(ValueInner::Variable(VariableValue {
            show: None,
            var_name: String::from(variable.name.as_str()),
            variable_label: variable.label.clone(),
        }))
    }
    pub fn new_datum(value: &Datum, encoding: &'static Encoding) -> Self {
        match value {
            Datum::Number(number) => Self::new_number(*number),
            Datum::String(string) => Self::new_user_text(string.decode(encoding).into_owned()),
        }
    }
    pub fn new_variable_value(variable: &Variable, value: &Datum) -> Self {
        let var_name = Some(variable.name.as_str().into());
        let value_label = variable.value_labels.get(value).map(String::from);
        match value {
            Datum::Number(number) => Self::new(ValueInner::Number(NumberValue {
                show: None,
                format: match variable.print_format.var_type() {
                    VarType::Numeric => variable.print_format,
                    VarType::String => {
                        #[cfg(debug_assertions)]
                        panic!("cannot create numeric pivot value with string format");

                        #[cfg(not(debug_assertions))]
                        Format::F8_2
                    }
                },
                honor_small: false,
                value: *number,
                var_name,
                value_label,
            })),
            Datum::String(string) => Self::new(ValueInner::String(StringValue {
                show: None,
                hex: variable.print_format.type_() == Type::AHex,
                s: string.decode(variable.encoding).into_owned(),
                var_name,
                value_label,
            })),
        }
    }
    pub fn new_number(x: Option<f64>) -> Self {
        Self::new_number_with_format(x, Format::F8_2)
    }
    pub fn new_integer(x: Option<f64>) -> Self {
        Self::new_number_with_format(x, Format::F40)
    }
    pub fn new_text(s: impl Into<String>) -> Self {
        Self::new_user_text(s)
    }
    pub fn new_user_text(s: impl Into<String>) -> Self {
        let s: String = s.into();
        if s.is_empty() {
            Self::default()
        } else {
            Self::new(ValueInner::Text(TextValue {
                user_provided: true,
                local: s.clone(),
                c: s.clone(),
                id: s.clone(),
            }))
        }
    }
    pub fn with_footnote(mut self, footnote: &Arc<Footnote>) -> Self {
        self.add_footnote(footnote);
        self
    }
    pub fn add_footnote(&mut self, footnote: &Arc<Footnote>) {
        let footnotes = &mut self.styling.get_or_insert_default().footnotes;
        footnotes.push(footnote.clone());
        footnotes.sort_by_key(|f| f.index);
    }
    pub fn with_show_value_label(mut self, show: Option<Show>) -> Self {
        let new_show = show;
        match &mut self.inner {
            ValueInner::Number(NumberValue { show, .. })
            | ValueInner::String(StringValue { show, .. }) => {
                *show = new_show;
            }
            _ => (),
        }
        self
    }
    pub fn with_show_variable_label(mut self, show: Option<Show>) -> Self {
        if let ValueInner::Variable(variable_value) = &mut self.inner {
            variable_value.show = show;
        }
        self
    }
    pub fn with_value_label(mut self, label: Option<String>) -> Self {
        match &mut self.inner {
            ValueInner::Number(NumberValue { value_label, .. })
            | ValueInner::String(StringValue { value_label, .. }) => *value_label = label.clone(),
            _ => (),
        }
        self
    }
    pub const fn empty() -> Self {
        Value {
            inner: ValueInner::Empty,
            styling: None,
        }
    }
    pub const fn is_empty(&self) -> bool {
        self.inner.is_empty() && self.styling.is_none()
    }
}

impl From<&str> for Value {
    fn from(value: &str) -> Self {
        Self::new_text(value)
    }
}

impl From<String> for Value {
    fn from(value: String) -> Self {
        Self::new_text(value)
    }
}

impl From<&Variable> for Value {
    fn from(variable: &Variable) -> Self {
        Self::new_variable(variable)
    }
}

pub struct DisplayValue<'a> {
    inner: &'a ValueInner,
    markup: bool,
    subscripts: &'a [String],
    footnotes: &'a [Arc<Footnote>],
    options: ValueOptions,
    show_value: bool,
    show_label: Option<&'a str>,
}

impl<'a> DisplayValue<'a> {
    pub fn subscripts(&self) -> impl Iterator<Item = &str> {
        self.subscripts.iter().map(String::as_str)
    }

    pub fn has_subscripts(&self) -> bool {
        !self.subscripts.is_empty()
    }

    pub fn footnotes(&self) -> impl Iterator<Item = DisplayMarker<'_>> {
        self.footnotes
            .iter()
            .filter(|f| f.show)
            .map(|f| f.display_marker(self.options))
    }

    pub fn has_footnotes(&self) -> bool {
        self.footnotes().next().is_some()
    }

    pub fn without_suffixes(self) -> Self {
        Self {
            subscripts: &[],
            footnotes: &[],
            ..self
        }
    }

    /// Returns this display split into `(body, suffixes)` where `suffixes` is
    /// subscripts and footnotes and `body` is everything else.
    pub fn split_suffixes(self) -> (Self, Self) {
        let suffixes = Self {
            inner: &ValueInner::Empty,
            ..self
        };
        (self.without_suffixes(), suffixes)
    }

    pub fn with_styling(mut self, styling: &'a ValueStyle) -> Self {
        if let Some(area_style) = &styling.style {
            self.markup = area_style.font_style.markup;
        }
        self.subscripts = styling.subscripts.as_slice();
        self.footnotes = styling.footnotes.as_slice();
        self
    }

    pub fn with_font_style(self, font_style: &FontStyle) -> Self {
        Self {
            markup: font_style.markup,
            ..self
        }
    }

    pub fn with_subscripts(self, subscripts: &'a [String]) -> Self {
        Self { subscripts, ..self }
    }

    pub fn with_footnotes(self, footnotes: &'a [Arc<Footnote>]) -> Self {
        Self { footnotes, ..self }
    }

    pub fn is_empty(&self) -> bool {
        self.inner.is_empty() && self.subscripts.is_empty() && self.footnotes.is_empty()
    }

    fn small(&self) -> f64 {
        self.options.small
    }

    pub fn var_type(&self) -> VarType {
        match self.inner {
            ValueInner::Number(NumberValue { .. }) if self.show_label.is_none() => VarType::Numeric,
            _ => VarType::String,
        }
    }

    fn template(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        template: &str,
        args: &[Vec<Value>],
    ) -> std::fmt::Result {
        let mut iter = template.as_bytes().iter();
        while let Some(c) = iter.next() {
            match c {
                b'\\' => {
                    let c = *iter.next().unwrap_or(&b'\\') as char;
                    let c = if c == 'n' { '\n' } else { c };
                    write!(f, "{c}")?;
                }
                b'^' => {
                    let (index, rest) = consume_int(iter.as_slice());
                    iter = rest.iter();
                    let Some(arg) = args.get(index.wrapping_sub(1)) else {
                        continue;
                    };
                    if let Some(arg) = arg.first() {
                        write!(f, "{}", arg.display(self.options))?;
                    }
                }
                b'[' => {
                    let (a, rest) = extract_inner_template(iter.as_slice());
                    let (b, rest) = extract_inner_template(rest);
                    let rest = rest.strip_prefix(b"]").unwrap_or(rest);
                    let (index, rest) = consume_int(rest);
                    iter = rest.iter();

                    let Some(mut args) = args.get(index.wrapping_sub(1)).map(|vec| vec.as_slice())
                    else {
                        continue;
                    };
                    let (mut template, mut escape) =
                        if !a.is_empty() { (a, b'%') } else { (b, b'^') };
                    while !args.is_empty() {
                        let n_consumed = self.inner_template(f, template, escape, args)?;
                        if n_consumed == 0 {
                            break;
                        }
                        args = &args[n_consumed..];

                        template = b;
                        escape = b'^';
                    }
                }
                c => write!(f, "{c}")?,
            }
        }
        Ok(())
    }

    fn inner_template(
        &self,
        f: &mut std::fmt::Formatter<'_>,
        template: &[u8],
        escape: u8,
        args: &[Value],
    ) -> Result<usize, std::fmt::Error> {
        let mut iter = template.iter();
        let mut args_consumed = 0;
        while let Some(c) = iter.next() {
            match c {
                b'\\' => {
                    let c = *iter.next().unwrap_or(&b'\\') as char;
                    let c = if c == 'n' { '\n' } else { c };
                    write!(f, "{c}")?;
                }
                c if *c == escape => {
                    let (index, rest) = consume_int(iter.as_slice());
                    iter = rest.iter();
                    let Some(arg) = args.get(index.wrapping_sub(1)) else {
                        continue;
                    };
                    args_consumed = args_consumed.max(index);
                    write!(f, "{}", arg.display(self.options))?;
                }
                c => write!(f, "{c}")?,
            }
        }
        Ok(args_consumed)
    }
}

fn consume_int(input: &[u8]) -> (usize, &[u8]) {
    let mut n = 0;
    for (index, c) in input.iter().enumerate() {
        if !c.is_ascii_digit() {
            return (n, &input[index..]);
        }
        n = n * 10 + (c - b'0') as usize;
    }
    (n, &[])
}

fn extract_inner_template(input: &[u8]) -> (&[u8], &[u8]) {
    for (index, c) in input.iter().copied().enumerate() {
        if c == b':' && (index == 0 || input[index - 1] != b'\\') {
            return input.split_at(index);
        }
    }
    (input, &[])
}

fn interpret_show(
    global_show: impl Fn() -> Show,
    table_show: Option<Show>,
    value_show: Option<Show>,
    label: &str,
) -> (bool, Option<&str>) {
    match value_show.or(table_show).unwrap_or_else(global_show) {
        Show::Value => (true, None),
        Show::Label => (false, Some(label)),
        Show::Both => (true, Some(label)),
    }
}

impl Display for DisplayValue<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.inner {
            ValueInner::Number(NumberValue {
                format,
                honor_small,
                value,
                ..
            }) => {
                if self.show_value {
                    let format = if format.type_() == Type::F
                        && *honor_small
                        && value.is_some_and(|value| value != 0.0 && value.abs() < self.small())
                    {
                        UncheckedFormat::new(Type::E, 40, format.d() as u8).fix()
                    } else {
                        *format
                    };
                    let mut buf = SmallString::<[u8; 40]>::new();
                    write!(&mut buf, "{}", Datum::Number(*value).display(format, UTF_8)).unwrap();
                    write!(f, "{}", buf.trim_start_matches(' '))?;
                }
                if let Some(label) = self.show_label {
                    if self.show_value {
                        write!(f, " ")?;
                    }
                    f.write_str(label)?;
                }
                Ok(())
            }

            ValueInner::String(StringValue { s, .. })
            | ValueInner::Variable(VariableValue { var_name: s, .. }) => {
                match (self.show_value, self.show_label) {
                    (true, None) => write!(f, "{s}"),
                    (false, Some(label)) => write!(f, "{label}"),
                    (true, Some(label)) => write!(f, "{s} {label}"),
                    (false, None) => unreachable!(),
                }
            }

            ValueInner::Text(TextValue { local, .. }) => {
                /*
                if self
                    .inner
                    .styling
                    .as_ref()
                    .is_some_and(|styling| styling.style.font_style.markup)
                {
                    todo!();
                }*/
                f.write_str(local)
            }

            ValueInner::Template(TemplateValue { args, local, .. }) => {
                self.template(f, local, args)
            }

            ValueInner::Empty => Ok(()),
        }?;

        for (subscript, delimiter) in self.subscripts.iter().zip(once('_').chain(repeat(','))) {
            write!(f, "{delimiter}{subscript}")?;
        }

        for footnote in self.footnotes {
            write!(f, "[{}]", footnote.display_marker(self.options))?;
        }

        Ok(())
    }
}

impl Value {
    // Returns an object that will format this value, including subscripts and
    // superscripts and footnotes.  `options` controls whether variable and
    // value labels are included.
    pub fn display(&self, options: impl IntoValueOptions) -> DisplayValue<'_> {
        let display = self.inner.display(options.into_value_options());
        match &self.styling {
            Some(styling) => display.with_styling(styling),
            None => display,
        }
    }
}

impl Debug for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.display(()).to_string())
    }
}

#[derive(Clone, Debug)]
pub struct NumberValue {
    pub show: Option<Show>,
    pub format: Format,
    pub honor_small: bool,
    pub value: Option<f64>,
    pub var_name: Option<String>,
    pub value_label: Option<String>,
}

#[derive(Clone, Debug)]
pub struct StringValue {
    pub show: Option<Show>,
    pub hex: bool,

    /// If `hex` is true, this string should already be hex digits
    /// (otherwise it would be impossible to encode non-UTF-8 data).
    pub s: String,
    pub var_name: Option<String>,
    pub value_label: Option<String>,
}

#[derive(Clone, Debug)]
pub struct VariableValue {
    pub show: Option<Show>,
    pub var_name: String,
    pub variable_label: Option<String>,
}

#[derive(Clone, Debug)]
pub struct TextValue {
    pub user_provided: bool,
    /// Localized.
    pub local: String,
    /// English.
    pub c: String,
    /// Identifier.
    pub id: String,
}

#[derive(Clone, Debug)]
pub struct TemplateValue {
    pub args: Vec<Vec<Value>>,
    pub local: String,
    pub id: String,
}

#[derive(Clone, Debug, Default)]
pub enum ValueInner {
    Number(NumberValue),
    String(StringValue),
    Variable(VariableValue),
    Text(TextValue),
    Template(TemplateValue),

    #[default]
    Empty,
}

impl ValueInner {
    pub const fn is_empty(&self) -> bool {
        matches!(self, Self::Empty)
    }
    fn show(&self) -> Option<Show> {
        match self {
            ValueInner::Number(NumberValue { show, .. })
            | ValueInner::String(StringValue { show, .. })
            | ValueInner::Variable(VariableValue { show, .. }) => *show,
            _ => None,
        }
    }

    fn label(&self) -> Option<&str> {
        self.value_label().or_else(|| self.variable_label())
    }

    fn value_label(&self) -> Option<&str> {
        match self {
            ValueInner::Number(NumberValue { value_label, .. })
            | ValueInner::String(StringValue { value_label, .. }) => {
                value_label.as_ref().map(String::as_str)
            }
            _ => None,
        }
    }

    fn variable_label(&self) -> Option<&str> {
        match self {
            ValueInner::Variable(VariableValue { variable_label, .. }) => {
                variable_label.as_ref().map(String::as_str)
            }
            _ => None,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct ValueStyle {
    pub style: Option<AreaStyle>,
    pub subscripts: Vec<String>,
    pub footnotes: Vec<Arc<Footnote>>,
}

impl ValueStyle {
    pub fn is_empty(&self) -> bool {
        self.style.is_none() && self.subscripts.is_empty() && self.footnotes.is_empty()
    }
}

impl ValueInner {
    // Returns an object that will format this value.  Settings on `options`
    // control whether variable and value labels are included.
    pub fn display(&self, options: impl IntoValueOptions) -> DisplayValue<'_> {
        let options = options.into_value_options();
        let (show_value, show_label) = if let Some(value_label) = self.value_label() {
            interpret_show(
                || Settings::global().show_values,
                options.show_values,
                self.show(),
                value_label,
            )
        } else if let Some(variable_label) = self.variable_label() {
            interpret_show(
                || Settings::global().show_variables,
                options.show_variables,
                self.show(),
                variable_label,
            )
        } else {
            (true, None)
        };
        DisplayValue {
            inner: self,
            markup: false,
            subscripts: &[],
            footnotes: &[],
            options,
            show_value,
            show_label,
        }
    }
}
