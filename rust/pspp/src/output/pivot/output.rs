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

use std::{iter::zip, ops::Range, sync::Arc};

use enum_map::{enum_map, EnumMap};
use itertools::Itertools;

use crate::output::{
    pivot::{HeadingRegion, LabelPosition, Path},
    table::{CellInner, Table},
};

use super::{
    Area, Axis2, Axis3, Border, BorderStyle, BoxBorder, Color, Coord2, Dimension, Footnote,
    IntoValueOptions, PivotTable, Rect2, RowColBorder, Stroke, Value,
};

/// All of the combinations of dimensions along an axis.
struct AxisEnumeration {
    indexes: Vec<usize>,
    stride: usize,
}

impl AxisEnumeration {
    fn len(&self) -> usize {
        self.indexes.len() / self.stride
    }

    fn is_empty(&self) -> bool {
        self.len() == 0
    }

    fn get(&self, index: usize) -> &[usize] {
        let start = self.stride * index;
        &self.indexes[start..start + self.stride]
    }

    fn iter(&self) -> AxisEnumerationIter<'_> {
        AxisEnumerationIter {
            enumeration: self,
            position: 0,
        }
    }
}

struct AxisEnumerationIter<'a> {
    enumeration: &'a AxisEnumeration,
    position: usize,
}

impl<'a> Iterator for AxisEnumerationIter<'a> {
    type Item = &'a [usize];

    fn next(&mut self) -> Option<Self::Item> {
        if self.position < self.enumeration.indexes.len() {
            let item =
                &self.enumeration.indexes[self.position..self.position + self.enumeration.stride];
            self.position += self.enumeration.stride;
            Some(item)
        } else {
            None
        }
    }
}

impl PivotTable {
    fn is_row_empty(
        &self,
        layer_indexes: &[usize],
        fixed_indexes: &[usize],
        fixed_axis: Axis3,
    ) -> bool {
        let vary_axis = fixed_axis.transpose().unwrap();
        for vary_indexes in self.axis_values(vary_axis) {
            let mut presentation_indexes = enum_map! {
                Axis3::Z => layer_indexes,
                _ => fixed_indexes,
            };
            presentation_indexes[vary_axis] = &vary_indexes;
            let data_indexes = self.convert_indexes_ptod(presentation_indexes);
            if self.get(&data_indexes).is_some() {
                return false;
            }
        }
        true
    }
    fn enumerate_axis(
        &self,
        enum_axis: Axis3,
        layer_indexes: &[usize],
        omit_empty: bool,
    ) -> AxisEnumeration {
        let axis = &self.axes[enum_axis];
        let extent = self.axis_extent(enum_axis);
        let indexes = if axis.dimensions.is_empty() {
            vec![0]
        } else if extent == 0 {
            vec![]
        } else {
            let mut enumeration =
                Vec::with_capacity(extent.checked_mul(axis.dimensions.len()).unwrap());
            if omit_empty {
                for axis_indexes in self.axis_values(enum_axis) {
                    if !self.is_row_empty(layer_indexes, &axis_indexes, enum_axis) {
                        enumeration.extend_from_slice(&axis_indexes);
                    }
                }
            }

            if enumeration.is_empty() {
                for axis_indexes in self.axis_values(enum_axis) {
                    enumeration.extend_from_slice(&axis_indexes);
                }
            }
            enumeration
        };
        AxisEnumeration {
            indexes,
            stride: axis.dimensions.len().max(1),
        }
    }

    fn create_aux_table3<I>(&self, area: Area, rows: I) -> Table
    where
        I: Iterator<Item = Box<Value>> + ExactSizeIterator,
    {
        let mut table = Table::new(
            Coord2::new(1, rows.len()),
            Coord2::new(0, 0),
            self.look.areas.clone(),
            self.borders(false),
            self.into_value_options(),
        );
        for (y, row) in rows.enumerate() {
            table.put(
                Rect2::for_cell(Coord2::new(0, y)),
                CellInner::new(area, row),
            );
        }
        table
    }

    fn create_aux_table_if_nonempty<I>(&self, area: Area, rows: I) -> Option<Table>
    where
        I: Iterator<Item = Box<Value>> + ExactSizeIterator,
    {
        if rows.len() > 0 {
            Some(self.create_aux_table3(area, rows))
        } else {
            None
        }
    }

    fn borders(&self, printing: bool) -> EnumMap<Border, BorderStyle> {
        EnumMap::from_fn(|border| {
            resolve_border_style(border, &self.look.borders, printing && self.show_grid_lines)
        })
    }

    pub fn output_body(&self, layer_indexes: &[usize], printing: bool) -> Table {
        let headings = EnumMap::from_fn(|axis| Headings::new(self, axis, layer_indexes));

        let data = Coord2::from_fn(|axis| headings[axis].width());
        let mut stub = Coord2::from_fn(|axis| headings[!axis].height());
        if headings[Axis2::Y].row_label_position == LabelPosition::Corner && stub.y() == 0 {
            stub[Axis2::Y] = 1;
        }
        let mut body = Table::new(
            Coord2::from_fn(|axis| data[axis] + stub[axis]),
            stub,
            self.look.areas.clone(),
            self.borders(printing),
            self.into_value_options(),
        );

        for h in [Axis2::X, Axis2::Y] {
            headings[h].render(&mut body, stub[h], h.into(), false, false);
        }

        for (row_indexes, y) in headings[Axis2::Y].values.iter().zip(stub[Axis2::Y]..) {
            for (column_indexes, x) in headings[Axis2::X].values.iter().zip(stub[Axis2::X]..) {
                let presentation_indexes = enum_map! {
                    Axis3::X => &column_indexes,
                    Axis3::Y => &row_indexes,
                    Axis3::Z => layer_indexes,
                };
                let data_indexes = self.convert_indexes_ptod(presentation_indexes);
                let value = self.get(&data_indexes);
                body.put(
                    Rect2::new(x..x + 1, y..y + 1),
                    CellInner {
                        rotate: false,
                        area: Area::Data,
                        value: Box::new(value.cloned().unwrap_or_default()),
                    },
                );
            }
        }

        // Insert corner text, but only if there's a stub and only if row labels
        // are not in the corner.
        if self.corner_text.is_some()
            && self.look.row_label_position == LabelPosition::Nested
            && stub.x() > 0
            && stub.y() > 0
        {
            body.put(
                Rect2::new(0..stub.x(), 0..stub.y()),
                CellInner::new(Area::Corner, self.corner_text.clone().unwrap_or_default()),
            );
        }

        if body.n.x() > 0 && body.n.y() > 0 {
            body.h_line(Border::InnerFrame(BoxBorder::Top), 0..body.n.x(), 0);
            body.h_line(
                Border::InnerFrame(BoxBorder::Bottom),
                0..body.n.x(),
                body.n.y(),
            );
            body.v_line(Border::InnerFrame(BoxBorder::Left), 0, 0..body.n.y());
            body.v_line(
                Border::InnerFrame(BoxBorder::Right),
                body.n.x(),
                0..body.n.y(),
            );

            body.h_line(Border::DataTop, 0..body.n.x(), stub.y());
            body.v_line(Border::DataLeft, stub.x(), 0..body.n.y());
        }
        body
    }

    pub fn output_title(&self) -> Option<Table> {
        Some(self.create_aux_table3(Area::Title, [self.title.as_ref()?.clone()].into_iter()))
    }

    pub fn output_layers(&self, layer_indexes: &[usize]) -> Option<Table> {
        let mut layers = Vec::new();
        for (dimension, &layer_index) in zip(
            self.axes[Axis3::Z]
                .dimensions
                .iter()
                .map(|index| &self.dimensions[*index]),
            layer_indexes,
        ) {
            if !dimension.is_empty() {
                layers.push(dimension.nth_leaf(layer_index).unwrap().name.clone());
            }
        }
        layers.reverse();

        self.create_aux_table_if_nonempty(Area::Layers, layers.into_iter())
    }

    pub fn output_caption(&self) -> Option<Table> {
        Some(self.create_aux_table3(Area::Caption, [self.caption.as_ref()?.clone()].into_iter()))
    }

    pub fn output_footnotes(&self, footnotes: &[Arc<Footnote>]) -> Option<Table> {
        self.create_aux_table_if_nonempty(
            Area::Footer,
            footnotes.iter().map(|f| {
                Box::new(Value::new_user_text(format!(
                    "{}. {}",
                    f.display_marker(self),
                    f.display_content(self)
                )))
            }),
        )
    }

    pub fn output(&self, layer_indexes: &[usize], printing: bool) -> OutputTables {
        // Produce most of the tables.
        let title = self.show_title.then(|| self.output_title()).flatten();
        let layers = self.output_layers(layer_indexes);
        let body = self.output_body(layer_indexes, printing);
        let caption = self.show_caption.then(|| self.output_caption()).flatten();

        // Then collect the footnotes from those tables.
        let tables = [
            title.as_ref(),
            layers.as_ref(),
            Some(&body),
            caption.as_ref(),
        ];
        let footnotes =
            self.output_footnotes(&self.collect_footnotes(tables.into_iter().flatten()));

        OutputTables {
            title,
            layers,
            body,
            caption,
            footnotes,
        }
    }

    fn nonempty_layer_dimensions(&self) -> impl Iterator<Item = &Dimension> {
        self.axes[Axis3::Z]
            .dimensions
            .iter()
            .rev()
            .map(|index| &self.dimensions[*index])
            .filter(|d| !d.root.is_empty())
    }

    fn collect_footnotes<'a>(&self, tables: impl Iterator<Item = &'a Table>) -> Vec<Arc<Footnote>> {
        if self.footnotes.is_empty() {
            return Vec::new();
        }

        let mut refs = Vec::with_capacity(self.footnotes.0.len());
        for table in tables {
            for cell in table.cells() {
                if let Some(styling) = &cell.inner().value.styling {
                    refs.extend(
                        styling
                            .footnotes
                            .iter()
                            .filter(|footnote| footnote.show)
                            .cloned(),
                    );
                }
            }
        }
        refs.sort_by_key(|f| f.index);
        refs.dedup_by_key(|f| f.index);
        refs
    }
}

pub struct OutputTables {
    pub title: Option<Table>,
    pub layers: Option<Table>,
    pub body: Table,
    pub caption: Option<Table>,
    pub footnotes: Option<Table>,
}

impl Path<'_> {
    pub fn get(&self, y: usize, height: usize) -> Option<&Value> {
        if y + 1 == height {
            Some(&self.leaf.name)
        } else {
            self.groups.get(y).map(|group| &*group.name)
        }
    }
}

struct Heading<'a> {
    dimension: &'a Dimension,
    height: usize,
    columns: Vec<Path<'a>>,
}

impl<'a> Heading<'a> {
    fn new(
        dimension: &'a Dimension,
        dim_index: usize,
        column_enumeration: &AxisEnumeration,
    ) -> Option<Self> {
        if dimension.hide_all_labels {
            return None;
        }

        let mut columns = Vec::new();
        let mut height = 0;
        for indexes in column_enumeration.iter() {
            let mut path = dimension
                .leaf_path(dimension.presentation_order[indexes[dim_index]])
                .unwrap();
            path.groups.retain(|group| group.show_label);
            height = height.max(1 + path.groups.len());
            columns.push(path);
        }

        Some(Self {
            dimension,
            height,
            columns,
        })
    }

    fn move_dimension_labels_to_corner(&mut self) -> bool {
        if self.dimension.root.show_label {
            for column in self.columns.iter_mut() {
                column.groups.remove(0);
            }
            self.height -= 1;
            true
        } else {
            false
        }
    }

    fn render(
        &self,
        table: &mut Table,
        vrules: &mut [bool],
        h: Axis2,
        h_ofs: usize,
        v_ofs: usize,
        region: HeadingRegion,
        rotate_inner_labels: bool,
        rotate_outer_labels: bool,
        inner: bool,
        dimension_label_position: LabelPosition,
    ) {
        let v = !h;

        for row in 0..self.height {
            // Find all the categories, dropping columns without a category.
            let categories = self.columns.iter().enumerate().filter_map(|(x, column)| {
                column.get(row, self.height).map(|name| (x..x + 1, name))
            });

            // Merge adjacent identical categories (but don't merge across a vertical rule).
            let categories = categories
                .coalesce(|(a_r, a), (b_r, b)| {
                    if a_r.end == b_r.start && !vrules[b_r.start] && std::ptr::eq(a, b) {
                        Ok((a_r.start..b_r.end, a))
                    } else {
                        Err(((a_r, a), (b_r, b)))
                    }
                })
                .collect::<Vec<_>>();
            for (Range { start: x1, end: x2 }, name) in categories.iter().cloned() {
                let y1 = v_ofs + row;
                let y2 = y1 + 1;
                table.put(
                    Rect2::for_ranges((h, x1 + h_ofs..x2 + h_ofs), y1..y2),
                    CellInner {
                        rotate: {
                            let is_outer_row = y1 == 0;
                            let is_inner_row = y2 == self.height;
                            (rotate_inner_labels && is_inner_row)
                                || (rotate_outer_labels && is_outer_row)
                        },
                        area: Area::Labels(h),
                        value: Box::new(name.clone()),
                    },
                );

                // Draw all the vertical lines in our running example, other
                // than the far left and far right ones.  Only the ones that
                // start in the last row of the heading are drawn with the
                // "category" style, the rest with the "dimension" style,
                // e.g. only the `║` below are category style:
                //
                // ```text
                // ┌─────────────────────────────────────────────────────┐
                // │                         bbbb                        │
                // ├─────────────────┬─────────────────┬─────────────────┤
                // │      bbbb1      │      bbbb2      │      bbbb3      │
                // ├─────────────────┼─────────────────┼─────────────────┤
                // │       aaaa      │       aaaa      │       aaaa      │
                // ├─────╥─────╥─────┼─────╥─────╥─────┼─────╥─────╥─────┤
                // │aaaa1║aaaa2║aaaa3│aaaa1║aaaa2║aaaa3│aaaa1║aaaa2║aaaa3│
                // └─────╨─────╨─────┴─────╨─────╨─────┴─────╨─────╨─────┘
                //```
                let row_col = RowColBorder(region, v);
                let border = if row == self.height - 1 && inner {
                    Border::Category(row_col)
                } else {
                    Border::Dimension(row_col)
                };
                for x in [x1, x2] {
                    if !vrules[x] {
                        table.draw_line(border, (v, x + h_ofs), y1..table.n[v]);
                        vrules[x] = true;
                    }
                }

                // Draws the horizontal lines within a dimension, that is, those
                // that separate a category (or group) from its parent group or
                // dimension's label.  Our running example doesn't have groups
                // but the `═════` lines below show the separators between
                // categories and their dimension label:
                //
                // ```text
                // ┌─────────────────────────────────────────────────────┐
                // │                         bbbb                        │
                // ╞═════════════════╤═════════════════╤═════════════════╡
                // │      bbbb1      │      bbbb2      │      bbbb3      │
                // ├─────────────────┼─────────────────┼─────────────────┤
                // │       aaaa      │       aaaa      │       aaaa      │
                // ╞═════╤═════╤═════╪═════╤═════╤═════╪═════╤═════╤═════╡
                // │aaaa1│aaaa2│aaaa3│aaaa1│aaaa2│aaaa3│aaaa1│aaaa2│aaaa3│
                // └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
                // ```
                if row + 1 < self.height {
                    table.draw_line(Border::Category(RowColBorder(region, h)), (h, y2), {
                        if row == 0
                            && !self.columns[0].groups.is_empty()
                            && std::ptr::eq(
                                &*self.columns[0].groups[0].name,
                                &*self.dimension.root.name,
                            )
                        {
                            h_ofs..table.n[h]
                        } else {
                            h_ofs + x1..h_ofs + x2
                        }
                    });
                }
            }
        }

        if dimension_label_position == LabelPosition::Corner {
            table.put(
                Rect2::new(v_ofs..v_ofs + 1, 0..h_ofs),
                CellInner {
                    rotate: false,
                    area: Area::Corner,
                    value: self.dimension.root.name.clone(),
                },
            );
        }
    }
}

struct Headings<'a> {
    headings: Vec<Heading<'a>>,
    row_label_position: LabelPosition,
    h: Axis2,
    values: AxisEnumeration,
}

impl<'a> Headings<'a> {
    fn new(pt: &'a PivotTable, h: Axis2, layer_indexes: &[usize]) -> Self {
        let column_enumeration = pt.enumerate_axis(h.into(), layer_indexes, pt.look.hide_empty);

        let mut headings = pt.axes[h.into()]
            .dimensions
            .iter()
            .copied()
            .enumerate()
            .rev()
            .filter_map(|(axis_index, dim_index)| {
                Heading::new(&pt.dimensions[dim_index], axis_index, &column_enumeration)
            })
            .collect::<Vec<_>>();

        let row_label_position = if h == Axis2::Y
            && pt.look.row_label_position == LabelPosition::Corner
            && headings
                .iter_mut()
                .map(|heading| heading.move_dimension_labels_to_corner())
                .filter(|x| *x)
                .count()
                > 0
        {
            LabelPosition::Corner
        } else {
            LabelPosition::Nested
        };

        Self {
            headings,
            row_label_position,
            h,
            values: column_enumeration,
        }
    }

    fn height(&self) -> usize {
        self.headings.iter().map(|h| h.height).sum()
    }

    fn width(&self) -> usize {
        self.values.len()
    }

    fn render(
        &self,
        table: &mut Table,
        h_ofs: usize,
        region: HeadingRegion,
        rotate_inner_labels: bool,
        rotate_outer_labels: bool,
    ) {
        if self.headings.is_empty() {
            return;
        }

        let h = self.h;
        let n_columns = self.width();
        let mut vrules = vec![false; n_columns + 1];
        vrules[0] = true;
        vrules[n_columns] = true;

        let mut v_ofs = 0;
        for (index, heading) in self.headings.iter().enumerate() {
            let inner = index == self.headings.len() - 1;
            heading.render(
                table,
                &mut vrules,
                h,
                h_ofs,
                v_ofs,
                region,
                rotate_inner_labels,
                rotate_outer_labels,
                inner,
                self.row_label_position,
            );
            v_ofs += heading.height;
            if !inner {
                // Draw the horizontal line between dimensions, e.g. the `=====`
                // line here:
                //
                // ```text
                // ┌─────────────────────────────────────────────────────┐ __
                // │                         bbbb                        │  │
                // ├─────────────────┬─────────────────┬─────────────────┤  │dim "bbbb"
                // │      bbbb1      │      bbbb2      │      bbbb3      │ _│
                // ╞═════════════════╪═════════════════╪═════════════════╡ __
                // │       aaaa      │       aaaa      │       aaaa      │  │
                // ├─────┬─────┬─────┼─────┬─────┬─────┼─────┬─────┬─────┤  │dim "aaaa"
                // │aaaa1│aaaa2│aaaa3│aaaa1│aaaa2│aaaa3│aaaa1│aaaa2│aaaa3│ _│
                // └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
                // ```
                table.draw_line(
                    Border::Dimension(RowColBorder(region, h)),
                    (h, v_ofs),
                    h_ofs..table.n[h],
                );
            }
        }
    }
}

pub fn try_range<R>(range: R, bounds: std::ops::RangeTo<usize>) -> Option<std::ops::Range<usize>>
where
    R: std::ops::RangeBounds<usize>,
{
    let len = bounds.end;

    let start = match range.start_bound() {
        std::ops::Bound::Included(&start) => start,
        std::ops::Bound::Excluded(start) => start.checked_add(1)?,
        std::ops::Bound::Unbounded => 0,
    };

    let end = match range.end_bound() {
        std::ops::Bound::Included(end) => end.checked_add(1)?,
        std::ops::Bound::Excluded(&end) => end,
        std::ops::Bound::Unbounded => len,
    };

    if start > end || end > len {
        None
    } else {
        Some(std::ops::Range { start, end })
    }
}

fn resolve_border_style(
    border: Border,
    borders: &EnumMap<Border, BorderStyle>,
    show_grid_lines: bool,
) -> BorderStyle {
    let style = borders[border];
    if style.stroke != Stroke::None {
        style
    } else {
        let style = borders[border.fallback()];
        if style.stroke != Stroke::None || !show_grid_lines {
            style
        } else {
            BorderStyle {
                stroke: Stroke::Dashed,
                color: Color::BLACK,
            }
        }
    }
}
