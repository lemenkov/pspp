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

//! Tables.
//!
//! A table is a rectangular grid of cells.  Cells can be joined to form larger
//! cells.  Rows and columns can be separated by rules of various types.  Rows
//! at the top and bottom of a table and columns at the left and right edges of
//! a table can be designated as headers, which means that if the table must be
//! broken across more than one page, those rows or columns are repeated on each
//! page.
//!
//! Some drivers use tables as an implementation detail of rendering pivot
//! tables.

use std::{ops::Range, sync::Arc};

use enum_map::{enum_map, EnumMap};
use ndarray::{Array, Array2};

use crate::output::pivot::{Coord2, DisplayValue, Footnote, HorzAlign, ValueInner};

use super::pivot::{
    Area, AreaStyle, Axis2, Border, BorderStyle, HeadingRegion, Rect2, Value, ValueOptions,
};

#[derive(Clone, Debug)]
pub struct CellRef<'a> {
    pub coord: Coord2,
    pub content: &'a Content,
}

impl CellRef<'_> {
    pub fn inner(&self) -> &CellInner {
        self.content.inner()
    }

    pub fn is_empty(&self) -> bool {
        self.content.is_empty()
    }

    pub fn rect(&self) -> Rect2 {
        self.content.rect(self.coord)
    }

    pub fn next_x(&self) -> usize {
        self.content.next_x(self.coord.x())
    }

    pub fn is_top_left(&self) -> bool {
        self.content.is_top_left(self.coord)
    }

    pub fn span(&self, axis: Axis2) -> usize {
        self.content.span(axis)
    }
    pub fn col_span(&self) -> usize {
        self.span(Axis2::X)
    }
    pub fn row_span(&self) -> usize {
        self.span(Axis2::Y)
    }
}

#[derive(Clone, Debug)]
pub enum Content {
    Value(CellInner),
    Join(Arc<Cell>),
}

impl Content {
    pub fn inner(&self) -> &CellInner {
        match self {
            Content::Value(cell_inner) => cell_inner,
            Content::Join(cell) => &cell.inner,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.inner().is_empty()
    }

    /// Returns the rectangle that this cell covers, only if the cell contains
    /// that information. (Joined cells always do, and other cells usually
    /// don't.)
    pub fn joined_rect(&self) -> Option<&Rect2> {
        match self {
            Content::Join(cell) => Some(&cell.region),
            _ => None,
        }
    }

    /// Returns the rectangle that this cell covers. If the cell doesn't contain
    /// that information, returns a rectangle containing `coord`.
    pub fn rect(&self, coord: Coord2) -> Rect2 {
        match self {
            Content::Join(cell) => cell.region.clone(),
            _ => Rect2::for_cell(coord),
        }
    }

    pub fn next_x(&self, x: usize) -> usize {
        self.joined_rect()
            .map_or(x + 1, |region| region[Axis2::X].end)
    }

    pub fn is_top_left(&self, coord: Coord2) -> bool {
        self.joined_rect().is_none_or(|r| coord == r.top_left())
    }

    pub fn span(&self, axis: Axis2) -> usize {
        self.joined_rect().map_or(1, |r| {
            let range = &r.0[axis];
            range.end - range.start
        })
    }

    pub fn col_span(&self) -> usize {
        self.span(Axis2::X)
    }
    pub fn row_span(&self) -> usize {
        self.span(Axis2::Y)
    }
}

impl Default for Content {
    fn default() -> Self {
        Self::Value(CellInner::default())
    }
}

#[derive(Clone, Debug)]
pub struct Cell {
    inner: CellInner,

    /// Occupied table region.
    region: Rect2,
}

impl Cell {
    fn new(inner: CellInner, region: Rect2) -> Self {
        Self { inner, region }
    }
}

#[derive(Clone, Debug, Default)]
pub struct CellInner {
    /// Rotate cell contents 90 degrees?
    pub rotate: bool,

    /// The area that the cell belongs to.
    pub area: Area,

    pub value: Box<Value>,
}

impl CellInner {
    pub fn new(area: Area, value: Box<Value>) -> Self {
        Self {
            rotate: false,
            area,
            value,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.value.inner.is_empty()
    }
}

/// A table.
#[derive(derive_more::Debug)]
pub struct Table {
    /// Number of rows and columns.
    pub n: Coord2,

    /// Table header rows and columns.
    pub h: Coord2,

    pub contents: Array2<Content>,

    /// Styles for areas of the table.
    #[debug(skip)]
    pub areas: EnumMap<Area, AreaStyle>,

    /// Styles for borders in the table.
    #[debug(skip)]
    pub borders: EnumMap<Border, BorderStyle>,

    /// Horizontal ([Axis2::Y]) and vertical ([Axis2::X]) rules.
    pub rules: EnumMap<Axis2, Array2<Border>>,

    /// How to present values.
    #[debug(skip)]
    pub value_options: ValueOptions,
}

impl Table {
    pub fn new(
        n: Coord2,
        headers: Coord2,
        areas: EnumMap<Area, AreaStyle>,
        borders: EnumMap<Border, BorderStyle>,
        value_options: ValueOptions,
    ) -> Self {
        Self {
            n,
            h: headers,
            contents: Array::default((n.x(), n.y())),
            areas,
            borders,
            rules: enum_map! {
                Axis2::X => Array::from_elem((n.x() + 1, n.y()), Border::Title),
                Axis2::Y => Array::from_elem((n.x(), n.y() + 1), Border::Title),
            },
            value_options,
        }
    }

    pub fn get(&self, coord: Coord2) -> CellRef<'_> {
        CellRef {
            coord,
            content: &self.contents[[coord.x(), coord.y()]],
        }
    }

    pub fn get_rule(&self, axis: Axis2, pos: Coord2) -> BorderStyle {
        self.borders[self.rules[axis][[pos.x(), pos.y()]]]
    }

    pub fn put(&mut self, region: Rect2, inner: CellInner) {
        use Axis2::*;
        if region[X].len() == 1 && region[Y].len() == 1 {
            self.contents[[region[X].start, region[Y].start]] = Content::Value(inner);
        } else {
            let cell = Arc::new(Cell::new(inner, region.clone()));
            for y in region[Y].clone() {
                for x in region[X].clone() {
                    self.contents[[x, y]] = Content::Join(cell.clone())
                }
            }
        }
    }

    pub fn h_line(&mut self, border: Border, x: Range<usize>, y: usize) {
        for x in x {
            self.rules[Axis2::Y][[x, y]] = border;
        }
    }

    pub fn v_line(&mut self, border: Border, x: usize, y: Range<usize>) {
        for y in y {
            self.rules[Axis2::X][[x, y]] = border;
        }
    }

    pub fn draw_line(
        &mut self,
        border: Border,
        (a, a_value): (Axis2, usize),
        b_range: Range<usize>,
    ) {
        match a {
            Axis2::X => self.h_line(border, b_range, a_value),
            Axis2::Y => self.v_line(border, a_value, b_range),
        }
    }

    pub fn iter_x(&self, y: usize) -> XIter<'_> {
        XIter {
            table: self,
            x: None,
            y,
        }
    }

    /// The heading region that `pos` is part of, if any.
    pub fn heading_region(&self, pos: Coord2) -> Option<HeadingRegion> {
        if pos.x() < self.h.x() {
            Some(HeadingRegion::Rows)
        } else if pos.y() < self.h.y() {
            Some(HeadingRegion::Columns)
        } else {
            None
        }
    }

    /// Iterates across all of the cells in the table, visiting each of them
    /// once in top-down, left-to-right order. Spanned cells are visited once,
    /// at the point in the iteration where their top-left cell would appear if
    /// they were not spanned.
    pub fn cells(&self) -> Cells<'_> {
        Cells::new(self)
    }

    pub fn is_empty(&self) -> bool {
        self.n[Axis2::X] == 0 || self.n[Axis2::Y] == 0
    }
}

pub struct XIter<'a> {
    table: &'a Table,
    x: Option<usize>,
    y: usize,
}

impl Iterator for XIter<'_> {
    type Item = usize;

    fn next(&mut self) -> Option<Self::Item> {
        let next_x = self
            .x
            .map_or(0, |x| self.table.get(Coord2::new(x, self.y)).next_x());
        if next_x >= self.table.n.x() {
            None
        } else {
            self.x = Some(next_x);
            Some(next_x)
        }
    }
}

/// Iterator for all of the cells in a table (see [Table::cells]).
pub struct Cells<'a> {
    table: &'a Table,
    next: Option<CellRef<'a>>,
}

impl<'a> Cells<'a> {
    fn new(table: &'a Table) -> Self {
        Self {
            table,
            next: if table.is_empty() {
                None
            } else {
                Some(table.get(Coord2::new(0, 0)))
            },
        }
    }
}

impl<'a> Iterator for Cells<'a> {
    type Item = CellRef<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        use Axis2::*;
        let this = self.next.as_ref()?.clone();

        let mut next = this.clone();
        self.next = loop {
            let next_x = next.next_x();
            let coord = if next_x < self.table.n[X] {
                Coord2::new(next_x, next.coord.y())
            } else if next.coord.y() + 1 < self.table.n[Y] {
                Coord2::new(0, next.coord.y() + 1)
            } else {
                break None;
            };
            next = self.table.get(coord);
            if next.is_top_left() {
                break Some(next);
            }
        };
        Some(this)
    }
}

pub struct DrawCell<'a> {
    pub rotate: bool,
    pub inner: &'a ValueInner,
    pub style: &'a AreaStyle,
    pub subscripts: &'a [String],
    pub footnotes: &'a [Arc<Footnote>],
    pub value_options: &'a ValueOptions,
}

impl<'a> DrawCell<'a> {
    pub fn new(inner: &'a CellInner, table: &'a Table) -> Self {
        let default_area_style = &table.areas[inner.area];
        let (style, subscripts, footnotes) = if let Some(styling) = &inner.value.styling {
            (
                styling.style.as_ref().unwrap_or(default_area_style),
                styling.subscripts.as_slice(),
                styling.footnotes.as_slice(),
            )
        } else {
            (default_area_style, [].as_slice(), [].as_slice())
        };
        Self {
            rotate: inner.rotate,
            inner: &inner.value.inner,
            style,
            subscripts,
            footnotes,
            value_options: &table.value_options,
        }
    }

    pub fn display(&self) -> DisplayValue<'a> {
        self.inner
            .display(self.value_options)
            .with_font_style(&self.style.font_style)
            .with_subscripts(self.subscripts)
            .with_footnotes(self.footnotes)
    }

    pub fn horz_align(&self, display: &DisplayValue) -> HorzAlign {
        self.style
            .cell_style
            .horz_align
            .unwrap_or_else(|| HorzAlign::for_mixed(display.var_type()))
    }
}
