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

use std::{fmt::Display, fs::File, path::Path, sync::Arc};

use enum_map::EnumMap;

use crate::output::{
    Details, Item,
    cairo::{CairoConfig, CairoDriver},
    driver::Driver,
    html::HtmlDriver,
    pivot::{
        Area, Axis2, Border, BorderStyle, Class, Color, Dimension, Footnote,
        FootnoteMarkerPosition, FootnoteMarkerType, Footnotes, Group, HeadingRegion, LabelPosition,
        Look, PivotTable, RowColBorder, Stroke,
    },
    spv::SpvDriver,
};

use super::{Axis3, Value};

#[test]
fn color() {
    assert_eq!("#112233".parse(), Ok(Color::new(0x11, 0x22, 0x33)));
    assert_eq!("112233".parse(), Ok(Color::new(0x11, 0x22, 0x33)));
    assert_eq!("rgb(11,22,33)".parse(), Ok(Color::new(11, 22, 33)));
    assert_eq!(
        "rgba(11,22,33, 0.25)".parse(),
        Ok(Color::new(11, 22, 33).with_alpha(64))
    );
    assert_eq!("lavender".parse(), Ok(Color::new(230, 230, 250)));
    assert_eq!("transparent".parse(), Ok(Color::new(0, 0, 0).with_alpha(0)));
}

fn d1(title: &str, axis: Axis3) -> PivotTable {
    let dimension = Dimension::new(
        Group::new("a")
            .with_label_shown()
            .with("a1")
            .with("a2")
            .with("a3"),
    );
    let mut pt = PivotTable::new([(axis, dimension)])
        .with_title(title)
        .with_look(Arc::new(test_look()));
    for i in 0..3 {
        pt.insert(&[i], Value::new_integer(Some(i as f64)));
    }
    pt
}

#[test]
fn d1_c() {
    assert_rendering(
        "d1_c",
        &d1("Columns", Axis3::X),
        "\
Columns
╭────────╮
│    a   │
├──┬──┬──┤
│a1│a2│a3│
├──┼──┼──┤
│ 0│ 1│ 2│
╰──┴──┴──╯
",
    );
}

#[test]
fn d1_r() {
    assert_rendering(
        "d1_r",
        &d1("Rows", Axis3::Y),
        "\
Rows
╭──┬─╮
│a │ │
├──┼─┤
│a1│0│
│a2│1│
│a3│2│
╰──┴─╯
",
    );
}

fn test_look() -> Look {
    let mut look = Look::default();
    look.areas[Area::Title].cell_style.horz_align = Some(super::HorzAlign::Left);
    look.areas[Area::Title].font_style.bold = false;
    look
}

fn d2(title: &str, axes: [Axis3; 2], dimension_labels: Option<LabelPosition>) -> PivotTable {
    let d1 = Dimension::new(
        Group::new("a")
            .with_show_label(dimension_labels.is_some())
            .with("a1")
            .with("a2")
            .with("a3"),
    );

    let d2 = Dimension::new(
        Group::new("b")
            .with_show_label(dimension_labels.is_some())
            .with("b1")
            .with("b2")
            .with("b3"),
    );

    let mut pt = PivotTable::new([(axes[0], d1), (axes[1], d2)]).with_title(title);
    let mut i = 0;
    for b in 0..3 {
        for a in 0..3 {
            pt.insert(&[a, b], Value::new_integer(Some(i as f64)));
            i += 1;
        }
    }
    let look = match dimension_labels {
        Some(position) => test_look().with_row_label_position(position),
        None => test_look(),
    };
    pt.with_look(Arc::new(look))
}

#[track_caller]
pub fn assert_lines_eq<E, A>(expected: &str, expected_name: E, actual: &str, actual_name: A)
where
    E: Display,
    A: Display,
{
    if expected != actual {
        eprintln!("Unexpected output:\n--- {expected_name}\n+++ {actual_name}");
        for result in diff::lines(expected, &actual) {
            let (prefix, line) = match result {
                diff::Result::Left(line) => ('-', line),
                diff::Result::Both(line, _) => (' ', line),
                diff::Result::Right(line) => ('+', line),
            };
            let suffix = if line.trim_end().len() != line.len() {
                "$"
            } else {
                ""
            };
            eprintln!("{prefix}{line}{suffix}");
        }
        panic!();
    }
}

#[track_caller]
pub fn assert_rendering(name: &str, pivot_table: &PivotTable, expected: &str) {
    assert_lines_eq(
        expected,
        format!("{name} expected"),
        &pivot_table.to_string(),
        format!("{name} actual"),
    );

    let item = Arc::new(Item::new(Details::Table(Box::new(pivot_table.clone()))));
    if let Some(dir) = std::env::var_os("PSPP_TEST_HTML_DIR") {
        let writer = File::create(Path::new(&dir).join(name).with_extension("html")).unwrap();
        HtmlDriver::for_writer(writer).write(&item);
    }

    let item = Arc::new(Item::new(Details::Table(Box::new(pivot_table.clone()))));
    if let Some(dir) = std::env::var_os("PSPP_TEST_PDF_DIR") {
        let config = CairoConfig::new(Path::new(&dir).join(name).with_extension("pdf"));
        CairoDriver::new(&config).unwrap().write(&item);
    }

    if let Some(dir) = std::env::var_os("PSPP_TEST_SPV_DIR") {
        let writer = File::create(Path::new(&dir).join(name).with_extension("spv")).unwrap();
        SpvDriver::for_writer(writer).write(&item);
    }
}

#[test]
fn d2_cc() {
    assert_rendering(
        "d2_cc",
        &d2("Columns", [Axis3::X, Axis3::X], None),
        "\
Columns
╭────────┬────────┬────────╮
│   b1   │   b2   │   b3   │
├──┬──┬──┼──┬──┬──┼──┬──┬──┤
│a1│a2│a3│a1│a2│a3│a1│a2│a3│
├──┼──┼──┼──┼──┼──┼──┼──┼──┤
│ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│
╰──┴──┴──┴──┴──┴──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_cc_with_dim_labels() {
    assert_rendering(
        "d2_cc_with_dim_labels",
        &d2("Columns", [Axis3::X, Axis3::X], Some(LabelPosition::Corner)),
        "\
Columns
╭──────────────────────────╮
│             b            │
├────────┬────────┬────────┤
│   b1   │   b2   │   b3   │
├────────┼────────┼────────┤
│    a   │    a   │    a   │
├──┬──┬──┼──┬──┬──┼──┬──┬──┤
│a1│a2│a3│a1│a2│a3│a1│a2│a3│
├──┼──┼──┼──┼──┼──┼──┼──┼──┤
│ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│
╰──┴──┴──┴──┴──┴──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_rr() {
    assert_rendering(
        "d2_rr",
        &d2("Rows", [Axis3::Y, Axis3::Y], None),
        "\
Rows
╭─────┬─╮
│b1 a1│0│
│   a2│1│
│   a3│2│
├─────┼─┤
│b2 a1│3│
│   a2│4│
│   a3│5│
├─────┼─┤
│b3 a1│6│
│   a2│7│
│   a3│8│
╰─────┴─╯
",
    );
}

#[test]
fn d2_rr_with_corner_dim_labels() {
    assert_rendering(
        "d2_rr_with_corner_dim_labels",
        &d2(
            "Rows - Corner",
            [Axis3::Y, Axis3::Y],
            Some(LabelPosition::Corner),
        ),
        "\
Rows - Corner
╭─────┬─╮
│b  a │ │
├─────┼─┤
│b1 a1│0│
│   a2│1│
│   a3│2│
├─────┼─┤
│b2 a1│3│
│   a2│4│
│   a3│5│
├─────┼─┤
│b3 a1│6│
│   a2│7│
│   a3│8│
╰─────┴─╯
",
    );
}

#[test]
fn d2_rr_with_nested_dim_labels() {
    assert_rendering(
        "d2_rr_with_nested_dim_labels",
        &d2(
            "Rows - Nested",
            [Axis3::Y, Axis3::Y],
            Some(LabelPosition::Nested),
        ),
        "\
Rows - Nested
╭─────────┬─╮
│b b1 a a1│0│
│       a2│1│
│       a3│2│
│ ╶───────┼─┤
│  b2 a a1│3│
│       a2│4│
│       a3│5│
│ ╶───────┼─┤
│  b3 a a1│6│
│       a2│7│
│       a3│8│
╰─────────┴─╯
",
    );
}

#[test]
fn d2_cr() {
    assert_rendering(
        "d2_cr",
        &d2("Column x Row", [Axis3::X, Axis3::Y], None),
        "\
Column x Row
╭──┬──┬──┬──╮
│  │a1│a2│a3│
├──┼──┼──┼──┤
│b1│ 0│ 1│ 2│
│b2│ 3│ 4│ 5│
│b3│ 6│ 7│ 8│
╰──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_cr_with_corner_dim_labels() {
    assert_rendering(
        "d2_cr_with_corner_dim_labels",
        &d2(
            "Column x Row - Corner",
            [Axis3::X, Axis3::Y],
            Some(LabelPosition::Corner),
        ),
        "\
Column x Row - Corner
╭──┬────────╮
│  │    a   │
│  ├──┬──┬──┤
│b │a1│a2│a3│
├──┼──┼──┼──┤
│b1│ 0│ 1│ 2│
│b2│ 3│ 4│ 5│
│b3│ 6│ 7│ 8│
╰──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_cr_with_nested_dim_labels() {
    assert_rendering(
        "d2_cr_with_nested_dim_labels",
        &d2(
            "Column x Row - Nested",
            [Axis3::X, Axis3::Y],
            Some(LabelPosition::Nested),
        ),
        "\
Column x Row - Nested
╭────┬────────╮
│    │    a   │
│    ├──┬──┬──┤
│    │a1│a2│a3│
├────┼──┼──┼──┤
│b b1│ 0│ 1│ 2│
│  b2│ 3│ 4│ 5│
│  b3│ 6│ 7│ 8│
╰────┴──┴──┴──╯
",
    );
}

#[test]
fn d2_rc() {
    assert_rendering(
        "d2_rc",
        &d2("Row x Column", [Axis3::Y, Axis3::X], None),
        "\
Row x Column
╭──┬──┬──┬──╮
│  │b1│b2│b3│
├──┼──┼──┼──┤
│a1│ 0│ 3│ 6│
│a2│ 1│ 4│ 7│
│a3│ 2│ 5│ 8│
╰──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_rc_with_corner_dim_labels() {
    assert_rendering(
        "d2_rc_with_corner_dim_labels",
        &d2(
            "Row x Column - Corner",
            [Axis3::Y, Axis3::X],
            Some(LabelPosition::Corner),
        ),
        "\
Row x Column - Corner
╭──┬────────╮
│  │    b   │
│  ├──┬──┬──┤
│a │b1│b2│b3│
├──┼──┼──┼──┤
│a1│ 0│ 3│ 6│
│a2│ 1│ 4│ 7│
│a3│ 2│ 5│ 8│
╰──┴──┴──┴──╯
",
    );
}

#[test]
fn d2_rc_with_nested_dim_labels() {
    assert_rendering(
        "d2_rc_with_nested_dim_labels",
        &d2(
            "Row x Column - Nested",
            [Axis3::Y, Axis3::X],
            Some(LabelPosition::Nested),
        ),
        "\
Row x Column - Nested
╭────┬────────╮
│    │    b   │
│    ├──┬──┬──┤
│    │b1│b2│b3│
├────┼──┼──┼──┤
│a a1│ 0│ 3│ 6│
│  a2│ 1│ 4│ 7│
│  a3│ 2│ 5│ 8│
╰────┴──┴──┴──╯
",
    );
}

#[test]
fn d2_cl() {
    let pivot_table = d2("Column x b1", [Axis3::X, Axis3::Z], None);
    assert_rendering(
        "d2_cl-layer0",
        &pivot_table,
        "\
Column x b1
b1
╭──┬──┬──╮
│a1│a2│a3│
├──┼──┼──┤
│ 0│ 1│ 2│
╰──┴──┴──╯
",
    );

    let pivot_table = pivot_table
        .with_layer(&[1])
        .with_title(Value::new_text("Column x b2"));
    assert_rendering(
        "d2_cl-layer1",
        &pivot_table,
        "\
Column x b2
b2
╭──┬──┬──╮
│a1│a2│a3│
├──┼──┼──┤
│ 3│ 4│ 5│
╰──┴──┴──╯
",
    );

    let pivot_table = pivot_table
        .with_all_layers()
        .with_title(Value::new_text("Column (All Layers)"));
    assert_rendering(
        "d2_cl-all_layers",
        &pivot_table,
        "\
Column (All Layers)
b1
╭──┬──┬──╮
│a1│a2│a3│
├──┼──┼──┤
│ 0│ 1│ 2│
╰──┴──┴──╯

Column (All Layers)
b2
╭──┬──┬──╮
│a1│a2│a3│
├──┼──┼──┤
│ 3│ 4│ 5│
╰──┴──┴──╯

Column (All Layers)
b3
╭──┬──┬──╮
│a1│a2│a3│
├──┼──┼──┤
│ 6│ 7│ 8│
╰──┴──┴──╯
",
    );
}

#[test]
fn d2_rl() {
    let pivot_table = d2("Row x b1", [Axis3::Y, Axis3::Z], None);
    assert_rendering(
        "d2_rl-layer0",
        &pivot_table,
        "\
Row x b1
b1
╭──┬─╮
│a1│0│
│a2│1│
│a3│2│
╰──┴─╯
",
    );

    let pivot_table = pivot_table
        .with_layer(&[1])
        .with_title(Value::new_text("Row x b2"));
    assert_rendering(
        "d2_rl-layer1",
        &pivot_table,
        "\
Row x b2
b2
╭──┬─╮
│a1│3│
│a2│4│
│a3│5│
╰──┴─╯
",
    );

    let pivot_table = pivot_table
        .with_all_layers()
        .with_title(Value::new_text("Row (All Layers)"));
    assert_rendering(
        "d2_rl-all_layers",
        &pivot_table,
        "\
Row (All Layers)
b1
╭──┬─╮
│a1│0│
│a2│1│
│a3│2│
╰──┴─╯

Row (All Layers)
b2
╭──┬─╮
│a1│3│
│a2│4│
│a3│5│
╰──┴─╯

Row (All Layers)
b3
╭──┬─╮
│a1│6│
│a2│7│
│a3│8│
╰──┴─╯
",
    );
}

#[test]
fn d3() {
    let a = (
        Axis3::Z,
        Dimension::new(Group::new("a").with("a1").with("a2").with("a3")),
    );
    let b = (
        Axis3::Z,
        Dimension::new(Group::new("b").with("b1").with("b2").with("b3").with("b4")),
    );
    let c = (
        Axis3::X,
        Dimension::new(
            Group::new("c")
                .with("c1")
                .with("c2")
                .with("c3")
                .with("c4")
                .with("c5"),
        ),
    );
    let mut pt = PivotTable::new([a, b, c])
        .with_title("Column x b1 x a1")
        .with_look(Arc::new(test_look()));
    let mut i = 0;
    for c in 0..5 {
        for b in 0..4 {
            for a in 0..3 {
                pt.insert(&[a, b, c], Value::new_integer(Some(i as f64)));
                i += 1;
            }
        }
    }
    assert_rendering(
        "d3-layer0_0",
        &pt,
        "\
Column x b1 x a1
b1
a1
╭──┬──┬──┬──┬──╮
│c1│c2│c3│c4│c5│
├──┼──┼──┼──┼──┤
│ 0│12│24│36│48│
╰──┴──┴──┴──┴──╯
",
    );

    let pt = pt.with_layer(&[0, 1]).with_title("Column x b2 x a1");
    assert_rendering(
        "d3-layer0_1",
        &pt,
        "\
Column x b2 x a1
b2
a1
╭──┬──┬──┬──┬──╮
│c1│c2│c3│c4│c5│
├──┼──┼──┼──┼──┤
│ 3│15│27│39│51│
╰──┴──┴──┴──┴──╯
",
    );

    let pt = pt.with_layer(&[1, 2]).with_title("Column x b3 x a2");
    assert_rendering(
        "d3-layer1_2",
        &pt,
        "\
Column x b3 x a2
b3
a2
╭──┬──┬──┬──┬──╮
│c1│c2│c3│c4│c5│
├──┼──┼──┼──┼──┤
│ 7│19│31│43│55│
╰──┴──┴──┴──┴──╯
",
    );
}

#[test]
fn title_and_caption() {
    let pivot_table =
        d2("Title", [Axis3::X, Axis3::Y], None).with_caption(Value::new_text("Caption"));
    assert_rendering(
        "title_and_caption",
        &pivot_table,
        "\
Title
╭──┬──┬──┬──╮
│  │a1│a2│a3│
├──┼──┼──┼──┤
│b1│ 0│ 1│ 2│
│b2│ 3│ 4│ 5│
│b3│ 6│ 7│ 8│
╰──┴──┴──┴──╯
Caption
",
    );

    let pivot_table = pivot_table.with_show_title(false);
    assert_rendering(
        "caption",
        &pivot_table,
        "\
╭──┬──┬──┬──╮
│  │a1│a2│a3│
├──┼──┼──┼──┤
│b1│ 0│ 1│ 2│
│b2│ 3│ 4│ 5│
│b3│ 6│ 7│ 8│
╰──┴──┴──┴──╯
Caption
",
    );

    let pivot_table = pivot_table.with_show_caption(false);
    assert_rendering(
        "no_title_or_caption",
        &pivot_table,
        "\
╭──┬──┬──┬──╮
│  │a1│a2│a3│
├──┼──┼──┼──┤
│b1│ 0│ 1│ 2│
│b2│ 3│ 4│ 5│
│b3│ 6│ 7│ 8│
╰──┴──┴──┴──╯
",
    );
}

fn footnote_table(show_f0: bool) -> PivotTable {
    let mut footnotes = Footnotes::new();
    let f0 = footnotes.push(
        Footnote::new("First footnote")
            .with_marker("*")
            .with_show(show_f0),
    );
    let f1 = footnotes.push(Footnote::new("Second footnote"));
    let a = (
        Axis3::X,
        Dimension::new(
            Group::new(Value::new_text("A").with_footnote(&f0))
                .with_label_shown()
                .with(Value::new_text("B").with_footnote(&f1))
                .with(Value::new_text("C").with_footnote(&f0).with_footnote(&f1)),
        ),
    );
    let d = (
        Axis3::Y,
        Dimension::new(
            Group::new(Value::new_text("D").with_footnote(&f1))
                .with_label_shown()
                .with(Value::new_text("E").with_footnote(&f0))
                .with(Value::new_text("F").with_footnote(&f1).with_footnote(&f0)),
        ),
    );
    let look = test_look().with_row_label_position(LabelPosition::Nested);
    let mut pt = PivotTable::new([a, d]).with_title(
        Value::new_text("Pivot Table with Alphabetic Subscript Footnotes").with_footnote(&f0),
    );
    pt.insert(&[0, 0], Value::new_number(Some(0.0)));
    pt.insert(&[1, 0], Value::new_number(Some(1.0)).with_footnote(&f0));
    pt.insert(&[0, 1], Value::new_number(Some(2.0)).with_footnote(&f1));
    pt.insert(
        &[1, 1],
        Value::new_number(Some(3.0))
            .with_footnote(&f0)
            .with_footnote(&f1),
    );
    pt.with_look(Arc::new(look))
        .with_footnotes(footnotes)
        .with_caption(Value::new_text("Caption").with_footnote(&f0))
        .with_corner_text(
            Value::new_text("Corner")
                .with_footnote(&f0)
                .with_footnote(&f1),
        )
}

#[test]
fn footnote_alphabetic_subscript() {
    assert_rendering(
        "footnote_alphabetic_subscript",
        &footnote_table(true),
        "\
Pivot Table with Alphabetic Subscript Footnotes[*]
╭────────────┬──────────────────╮
│            │       A[*]       │
│            ├───────┬──────────┤
│Corner[*][b]│  B[b] │  C[*][b] │
├────────────┼───────┼──────────┤
│D[b] E[*]   │    .00│   1.00[*]│
│     F[*][b]│2.00[b]│3.00[*][b]│
╰────────────┴───────┴──────────╯
Caption[*]
*. First footnote
b. Second footnote
",
    );
}

#[test]
fn footnote_alphabetic_superscript() {
    let mut pt = footnote_table(true);
    let f0 = pt.footnotes.0[0].clone();
    pt = pt.with_title(
        Value::new_text("Pivot Table with Alphabetic Superscript Footnotes").with_footnote(&f0),
    );
    pt.look_mut().footnote_marker_position = FootnoteMarkerPosition::Superscript;
    assert_rendering(
        "footnote_alphabetic_superscript",
        &pt,
        "\
Pivot Table with Alphabetic Superscript Footnotes[*]
╭────────────┬──────────────────╮
│            │       A[*]       │
│            ├───────┬──────────┤
│Corner[*][b]│  B[b] │  C[*][b] │
├────────────┼───────┼──────────┤
│D[b] E[*]   │    .00│   1.00[*]│
│     F[*][b]│2.00[b]│3.00[*][b]│
╰────────────┴───────┴──────────╯
Caption[*]
*. First footnote
b. Second footnote
",
    );
}

#[test]
fn footnote_numeric_subscript() {
    let mut pt = footnote_table(true);
    let f0 = pt.footnotes.0[0].clone();
    pt = pt.with_title(
        Value::new_text("Pivot Table with Numeric Subscript Footnotes").with_footnote(&f0),
    );
    pt.look_mut().footnote_marker_type = FootnoteMarkerType::Numeric;
    assert_rendering(
        "footnote_numeric_subscript",
        &pt,
        "\
Pivot Table with Numeric Subscript Footnotes[*]
╭────────────┬──────────────────╮
│            │       A[*]       │
│            ├───────┬──────────┤
│Corner[*][2]│  B[2] │  C[*][2] │
├────────────┼───────┼──────────┤
│D[2] E[*]   │    .00│   1.00[*]│
│     F[*][2]│2.00[2]│3.00[*][2]│
╰────────────┴───────┴──────────╯
Caption[*]
*. First footnote
2. Second footnote
",
    );
}

#[test]
fn footnote_numeric_superscript() {
    let mut pt = footnote_table(true);
    let f0 = pt.footnotes.0[0].clone();
    pt = pt.with_title(
        Value::new_text("Pivot Table with Numeric Superscript Footnotes").with_footnote(&f0),
    );
    pt.look_mut().footnote_marker_type = FootnoteMarkerType::Numeric;
    pt.look_mut().footnote_marker_position = FootnoteMarkerPosition::Superscript;
    assert_rendering(
        "footnote_numeric_superscript",
        &pt,
        "\
Pivot Table with Numeric Superscript Footnotes[*]
╭────────────┬──────────────────╮
│            │       A[*]       │
│            ├───────┬──────────┤
│Corner[*][2]│  B[2] │  C[*][2] │
├────────────┼───────┼──────────┤
│D[2] E[*]   │    .00│   1.00[*]│
│     F[*][2]│2.00[2]│3.00[*][2]│
╰────────────┴───────┴──────────╯
Caption[*]
*. First footnote
2. Second footnote
",
    );
}

#[test]
fn footnote_hidden() {
    assert_rendering(
        "footnote_hidden",
        &footnote_table(false),
        "\
Pivot Table with Alphabetic Subscript Footnotes[*]
╭────────────┬──────────────────╮
│            │       A[*]       │
│            ├───────┬──────────┤
│Corner[*][b]│  B[b] │  C[*][b] │
├────────────┼───────┼──────────┤
│D[b] E[*]   │    .00│   1.00[*]│
│     F[*][b]│2.00[b]│3.00[*][b]│
╰────────────┴───────┴──────────╯
Caption[*]
b. Second footnote
",
    );
}

#[test]
fn no_dimension() {
    let pivot_table = PivotTable::new([])
        .with_title("No Dimensions")
        .with_look(Arc::new(test_look()));
    assert_rendering(
        "no_dimension",
        &pivot_table,
        "No Dimensions
╭╮
╰╯
",
    );
}

#[test]
fn empty_dimensions() {
    let look = Arc::new(test_look().with_omit_empty(false));

    let d1 = (Axis3::X, Dimension::new(Group::new("a")));
    let pivot_table = PivotTable::new([d1])
        .with_title("One Empty Dimension")
        .with_look(look.clone());
    assert_rendering("one_empty_dimension", &pivot_table, "One Empty Dimension\n");

    let d1 = (Axis3::X, Dimension::new(Group::new("a")));
    let d2 = (Axis3::X, Dimension::new(Group::new("b").with_label_shown()));
    let pivot_table = PivotTable::new([d1, d2])
        .with_title("Two Empty Dimensions")
        .with_look(look.clone());
    assert_rendering(
        "two_empty_dimensions",
        &pivot_table,
        "Two Empty Dimensions\n",
    );

    let d1 = (Axis3::X, Dimension::new(Group::new("a")));
    let d2 = (Axis3::X, Dimension::new(Group::new("b").with_label_shown()));
    let d3 = (
        Axis3::X,
        Dimension::new(Group::new("c").with("c1").with("c2")),
    );
    let pivot_table = PivotTable::new([d1, d2, d3])
        .with_title("Three Dimensions, Two Empty")
        .with_look(look.clone());
    assert_rendering(
        "three_dimensions_two_empty",
        &pivot_table,
        "Three Dimensions, Two Empty\n",
    );
}

#[test]
fn empty_groups() {
    let d1 = (
        Axis3::X,
        Dimension::new(Group::new("a").with("a1").with(Group::new("a2")).with("a3")),
    );

    let d2 = (
        Axis3::Y,
        Dimension::new(Group::new("b").with(Group::new("b1")).with("b2").with("b3")),
    );

    let mut pt = PivotTable::new([d1, d2]).with_title("Empty Groups");
    let mut i = 0;
    for b in 0..2 {
        for a in 0..2 {
            pt.insert(&[a, b], Value::new_integer(Some(i as f64)));
            i += 1;
        }
    }
    let pivot_table = pt.with_look(Arc::new(test_look().with_omit_empty(false)));
    assert_rendering(
        "empty_groups",
        &pivot_table,
        "\
Empty Groups
╭──┬──┬──╮
│  │a1│a3│
├──┼──┼──┤
│b2│ 0│ 1│
│b3│ 2│ 3│
╰──┴──┴──╯
",
    );
}

fn d4(
    title: &str,
    borders: EnumMap<Border, BorderStyle>,
    show_dimension_labels: bool,
) -> PivotTable {
    let a = (
        Axis3::X,
        Dimension::new(
            Group::new("a")
                .with_show_label(show_dimension_labels)
                .with("a1")
                .with(Group::new("ag1").with("a2").with("a3")),
        ),
    );
    let b = (
        Axis3::X,
        Dimension::new(
            Group::new("b")
                .with_show_label(show_dimension_labels)
                .with(Group::new("bg1").with("b1").with("b2"))
                .with("b3"),
        ),
    );
    let c = (
        Axis3::Y,
        Dimension::new(
            Group::new("c")
                .with_show_label(show_dimension_labels)
                .with("c1")
                .with(Group::new("cg1").with("c2").with("c3")),
        ),
    );
    let d = (
        Axis3::Y,
        Dimension::new(
            Group::new("d")
                .with_show_label(show_dimension_labels)
                .with(Group::new("dg1").with("d1").with("d2"))
                .with("d3"),
        ),
    );
    let mut pivot_table = PivotTable::new([a, b, c, d])
        .with_title(title)
        .with_look(Arc::new(test_look().with_borders(borders)));
    let mut i = 0;
    for d in 0..3 {
        for c in 0..3 {
            for b in 0..3 {
                for a in 0..3 {
                    pivot_table.insert(&[a, b, c, d], Value::new_integer(Some(i as f64)));
                    i += 1;
                }
            }
        }
    }
    pivot_table
}

#[test]
fn dimension_borders_1() {
    let pivot_table = d4(
        "Dimension Borders 1",
        EnumMap::from_fn(|border| match border {
            Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::X))
            | Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => SOLID_BLUE,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "dimension_borders_1",
        &pivot_table,
        "\
Dimension Borders 1
                           b
                     bg1       │
                 b1   │   b2   │   b3
                  a   │    a   │    a
                │ ag1 │  │ ag1 │  │ ag1
d      c      a1│a2 a3│a1│a2 a3│a1│a2 a3
dg1 d1     c1  0│ 1  2│ 3│ 4  5│ 6│ 7  8
      ╶─────────┼─────┼──┼─────┼──┼─────
       cg1 c2  9│10 11│12│13 14│15│16 17
           c3 18│19 20│21│22 23│24│25 26
   ╶────────────┼─────┼──┼─────┼──┼─────
    d2     c1 27│28 29│30│31 32│33│34 35
      ╶─────────┼─────┼──┼─────┼──┼─────
       cg1 c2 36│37 38│39│40 41│42│43 44
           c3 45│46 47│48│49 50│51│52 53
────────────────┼─────┼──┼─────┼──┼─────
    d3     c1 54│55 56│57│58 59│60│61 62
      ╶─────────┼─────┼──┼─────┼──┼─────
       cg1 c2 63│64 65│66│67 68│69│70 71
           c3 72│73 74│75│76 77│78│79 80
",
    );
}

#[test]
fn dimension_borders_2() {
    let pivot_table = d4(
        "Dimension Borders 2",
        EnumMap::from_fn(|border| match border {
            Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::Y))
            | Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::X)) => SOLID_BLUE,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "dimension_borders_2",
        &pivot_table,
        "\
Dimension Borders 2
                           b
                     bg1
                 b1       b2       b3
             ╶──────────────────────────
                  a        a        a
                  ag1      ag1      ag1
d      c      a1 a2 a3 a1 a2 a3 a1 a2 a3
dg1 d1│    c1  0  1  2  3  4  5  6  7  8
      │cg1 c2  9 10 11 12 13 14 15 16 17
      │    c3 18 19 20 21 22 23 24 25 26
    d2│    c1 27 28 29 30 31 32 33 34 35
      │cg1 c2 36 37 38 39 40 41 42 43 44
      │    c3 45 46 47 48 49 50 51 52 53
    d3│    c1 54 55 56 57 58 59 60 61 62
      │cg1 c2 63 64 65 66 67 68 69 70 71
      │    c3 72 73 74 75 76 77 78 79 80
",
    );
}

#[test]
fn category_borders_1() {
    let pivot_table = d4(
        "Category Borders 1",
        EnumMap::from_fn(|border| match border {
            Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::X))
            | Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => DASHED_RED,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "category_borders_1",
        &pivot_table,
        "\
Category Borders 1
                           b
                     bg1       ┊
                 b1   ┊   b2   ┊   b3
                  a   ┊    a   ┊    a
                ┊ ag1 ┊  ┊ ag1 ┊  ┊ ag1
d      c      a1┊a2┊a3┊a1┊a2┊a3┊a1┊a2┊a3
dg1 d1     c1  0┊ 1┊ 2┊ 3┊ 4┊ 5┊ 6┊ 7┊ 8
      ╌╌╌╌╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
       cg1 c2  9┊10┊11┊12┊13┊14┊15┊16┊17
          ╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
           c3 18┊19┊20┊21┊22┊23┊24┊25┊26
   ╌╌╌╌╌╌╌╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
    d2     c1 27┊28┊29┊30┊31┊32┊33┊34┊35
      ╌╌╌╌╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
       cg1 c2 36┊37┊38┊39┊40┊41┊42┊43┊44
          ╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
           c3 45┊46┊47┊48┊49┊50┊51┊52┊53
╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
    d3     c1 54┊55┊56┊57┊58┊59┊60┊61┊62
      ╌╌╌╌╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
       cg1 c2 63┊64┊65┊66┊67┊68┊69┊70┊71
          ╌╌╌╌╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌+╌╌
           c3 72┊73┊74┊75┊76┊77┊78┊79┊80
",
    );
}

#[test]
fn category_borders_2() {
    let pivot_table = d4(
        "Category Borders 2",
        EnumMap::from_fn(|border| match border {
            Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::Y))
            | Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::X)) => DASHED_RED,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "category_borders_2",
        &pivot_table,
        "\
Category Borders 2
                           b
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                     bg1
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                 b1       b2       b3
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                  a        a        a
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                  ag1      ag1      ag1
                ╌╌╌╌╌╌╌  ╌╌╌╌╌╌╌  ╌╌╌╌╌╌
d      c      a1 a2 a3 a1 a2 a3 a1 a2 a3
dg1┊d1┊    c1  0  1  2  3  4  5  6  7  8
   ┊  ┊cg1┊c2  9 10 11 12 13 14 15 16 17
   ┊  ┊   ┊c3 18 19 20 21 22 23 24 25 26
   ┊d2┊    c1 27 28 29 30 31 32 33 34 35
   ┊  ┊cg1┊c2 36 37 38 39 40 41 42 43 44
   ┊  ┊   ┊c3 45 46 47 48 49 50 51 52 53
    d3┊    c1 54 55 56 57 58 59 60 61 62
      ┊cg1┊c2 63 64 65 66 67 68 69 70 71
      ┊   ┊c3 72 73 74 75 76 77 78 79 80
",
    );
}

#[test]
fn category_and_dimension_borders_1() {
    let pivot_table = d4(
        "Category and Dimension Borders 1",
        EnumMap::from_fn(|border| match border {
            Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::X))
            | Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => SOLID_BLUE,
            Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::X))
            | Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => DASHED_RED,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "category_and_dimension_borders_1",
        &pivot_table,
        "\
Category and Dimension Borders 1
                           b
                     bg1       │
                 b1   │   b2   │   b3
                  a   │    a   │    a
                │ ag1 │  │ ag1 │  │ ag1
d      c      a1│a2┊a3│a1│a2┊a3│a1│a2┊a3
dg1 d1     c1  0│ 1┊ 2│ 3│ 4┊ 5│ 6│ 7┊ 8
      ╶─────────┼──┼──┼──┼──┼──┼──┼──┼──
       cg1 c2  9│10┊11│12│13┊14│15│16┊17
          ╌╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
           c3 18│19┊20│21│22┊23│24│25┊26
   ╶────────────┼──┼──┼──┼──┼──┼──┼──┼──
    d2     c1 27│28┊29│30│31┊32│33│34┊35
      ╶─────────┼──┼──┼──┼──┼──┼──┼──┼──
       cg1 c2 36│37┊38│39│40┊41│42│43┊44
          ╌╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
           c3 45│46┊47│48│49┊50│51│52┊53
────────────────┼──┼──┼──┼──┼──┼──┼──┼──
    d3     c1 54│55┊56│57│58┊59│60│61┊62
      ╶─────────┼──┼──┼──┼──┼──┼──┼──┼──
       cg1 c2 63│64┊65│66│67┊68│69│70┊71
          ╌╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
           c3 72│73┊74│75│76┊77│78│79┊80
",
    );
}

#[test]
fn category_and_dimension_borders_2() {
    let pivot_table = d4(
        "Category and Dimension Borders 2",
        EnumMap::from_fn(|border| match border {
            Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::Y))
            | Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::X)) => SOLID_BLUE,
            Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::Y))
            | Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::X)) => DASHED_RED,
            _ => BorderStyle::none(),
        }),
        true,
    );
    assert_rendering(
        "category_and_dimension_borders_2",
        &pivot_table,
        "\
Category and Dimension Borders 2
                           b
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                     bg1
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                 b1       b2       b3
             ╶──────────────────────────
                  a        a        a
             ╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
                  ag1      ag1      ag1
                ╌╌╌╌╌╌╌  ╌╌╌╌╌╌╌  ╌╌╌╌╌╌
d      c      a1 a2 a3 a1 a2 a3 a1 a2 a3
dg1┊d1│    c1  0  1  2  3  4  5  6  7  8
   ┊  │cg1┊c2  9 10 11 12 13 14 15 16 17
   ┊  │   ┊c3 18 19 20 21 22 23 24 25 26
   ┊d2│    c1 27 28 29 30 31 32 33 34 35
   ┊  │cg1┊c2 36 37 38 39 40 41 42 43 44
   ┊  │   ┊c3 45 46 47 48 49 50 51 52 53
    d3│    c1 54 55 56 57 58 59 60 61 62
      │cg1┊c2 63 64 65 66 67 68 69 70 71
      │   ┊c3 72 73 74 75 76 77 78 79 80
",
    );
}

const SOLID_BLUE: BorderStyle = BorderStyle {
    stroke: Stroke::Solid,
    color: Color::BLUE,
};

const DASHED_RED: BorderStyle = BorderStyle {
    stroke: Stroke::Dashed,
    color: Color::RED,
};

#[test]
fn category_and_dimension_borders_3() {
    let pivot_table = d4(
        "Category and Dimension Borders 3",
        EnumMap::from_fn(|border| match border {
            Border::Dimension(_) => SOLID_BLUE,
            Border::Category(_) => DASHED_RED,
            _ => BorderStyle::none(),
        }),
        false,
    );
    assert_rendering(
        "category_and_dimension_borders_3",
        &pivot_table,
        "\
Category and Dimension Borders 3
                     bg1       │
             ╌╌╌╌╌╌╌╌╌┬╌╌╌╌╌╌╌╌┤
                 b1   │   b2   │   b3
             ╶──┬─────┼──┬─────┼──┬─────
                │ ag1 │  │ ag1 │  │ ag1
                ├╌╌┬╌╌┤  ├╌╌┬╌╌┤  ├╌╌┬╌╌
              a1│a2┊a3│a1│a2┊a3│a1│a2┊a3
dg1┊d1│    c1  0│ 1┊ 2│ 3│ 4┊ 5│ 6│ 7┊ 8
   ┊  ├───┬─────┼──┼──┼──┼──┼──┼──┼──┼──
   ┊  │cg1┊c2  9│10┊11│12│13┊14│15│16┊17
   ┊  │   ├╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
   ┊  │   ┊c3 18│19┊20│21│22┊23│24│25┊26
   ├──┼───┴─────┼──┼──┼──┼──┼──┼──┼──┼──
   ┊d2│    c1 27│28┊29│30│31┊32│33│34┊35
   ┊  ├───┬─────┼──┼──┼──┼──┼──┼──┼──┼──
   ┊  │cg1┊c2 36│37┊38│39│40┊41│42│43┊44
   ┊  │   ├╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
   ┊  │   ┊c3 45│46┊47│48│49┊50│51│52┊53
───┴──┼───┴─────┼──┼──┼──┼──┼──┼──┼──┼──
    d3│    c1 54│55┊56│57│58┊59│60│61┊62
      ├───┬─────┼──┼──┼──┼──┼──┼──┼──┼──
      │cg1┊c2 63│64┊65│66│67┊68│69│70┊71
      │   ├╌╌╌╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌┼╌╌┼╌╌+╌╌
      │   ┊c3 72│73┊74│75│76┊77│78│79┊80
",
    );
}

#[test]
fn small_numbers() {
    let exponent = (
        Axis3::Y,
        Dimension::new(
            Group::new("exponent")
                .with("0")
                .with("-1")
                .with("-2")
                .with("-3")
                .with("-4")
                .with("-5")
                .with("-6")
                .with("-7")
                .with("-8")
                .with("-9")
                .with_label_shown(),
        ),
    );
    let sign = (
        Axis3::X,
        Dimension::new(
            Group::new("sign")
                .with("positive")
                .with("negative")
                .with_label_shown(),
        ),
    );
    let rc = (
        Axis3::X,
        Dimension::new(
            Group::new("result class")
                .with("general")
                .with("specific")
                .with_label_shown(),
        ),
    );
    let mut pt = PivotTable::new([exponent, sign, rc]).with_title("small numbers");
    pt.insert_number(&[0, 0, 0], Some(1.0), Class::Other);
    pt.insert_number(&[1, 0, 0], Some(0.1), Class::Other);
    pt.insert_number(&[2, 0, 0], Some(0.01), Class::Other);
    pt.insert_number(&[3, 0, 0], Some(0.001), Class::Other);
    pt.insert_number(&[4, 0, 0], Some(0.0001), Class::Other);
    pt.insert_number(&[5, 0, 0], Some(0.00001), Class::Other);
    pt.insert_number(&[6, 0, 0], Some(0.000001), Class::Other);
    pt.insert_number(&[7, 0, 0], Some(0.0000001), Class::Other);
    pt.insert_number(&[8, 0, 0], Some(0.00000001), Class::Other);
    pt.insert_number(&[9, 0, 0], Some(0.000000001), Class::Other);
    pt.insert_number(&[0, 0, 1], Some(-1.0), Class::Residual);
    pt.insert_number(&[1, 0, 1], Some(-0.1), Class::Residual);
    pt.insert_number(&[2, 0, 1], Some(-0.01), Class::Residual);
    pt.insert_number(&[3, 0, 1], Some(-0.001), Class::Residual);
    pt.insert_number(&[4, 0, 1], Some(-0.0001), Class::Residual);
    pt.insert_number(&[5, 0, 1], Some(-0.00001), Class::Residual);
    pt.insert_number(&[6, 0, 1], Some(-0.000001), Class::Residual);
    pt.insert_number(&[7, 0, 1], Some(-0.0000001), Class::Residual);
    pt.insert_number(&[8, 0, 1], Some(-0.00000001), Class::Residual);
    pt.insert_number(&[9, 0, 1], Some(-0.000000001), Class::Residual);
    pt.insert_number(&[0, 1, 0], Some(1.0), Class::Other);
    pt.insert_number(&[1, 1, 0], Some(0.1), Class::Other);
    pt.insert_number(&[2, 1, 0], Some(0.01), Class::Other);
    pt.insert_number(&[3, 1, 0], Some(0.001), Class::Other);
    pt.insert_number(&[4, 1, 0], Some(0.0001), Class::Other);
    pt.insert_number(&[5, 1, 0], Some(0.00001), Class::Other);
    pt.insert_number(&[6, 1, 0], Some(0.000001), Class::Other);
    pt.insert_number(&[7, 1, 0], Some(0.0000001), Class::Other);
    pt.insert_number(&[8, 1, 0], Some(0.00000001), Class::Other);
    pt.insert_number(&[9, 1, 0], Some(0.000000001), Class::Other);
    pt.insert_number(&[0, 1, 1], Some(-1.0), Class::Residual);
    pt.insert_number(&[1, 1, 1], Some(-0.1), Class::Residual);
    pt.insert_number(&[2, 1, 1], Some(-0.01), Class::Residual);
    pt.insert_number(&[3, 1, 1], Some(-0.001), Class::Residual);
    pt.insert_number(&[4, 1, 1], Some(-0.0001), Class::Residual);
    pt.insert_number(&[5, 1, 1], Some(-0.00001), Class::Residual);
    pt.insert_number(&[6, 1, 1], Some(-0.000001), Class::Residual);
    pt.insert_number(&[7, 1, 1], Some(-0.0000001), Class::Residual);
    pt.insert_number(&[8, 1, 1], Some(-0.00000001), Class::Residual);
    pt.insert_number(&[9, 1, 1], Some(-0.000000001), Class::Residual);
    let pivot_table = pt.with_look(Arc::new(test_look()));
    assert_rendering(
        "small_numbers",
        &pivot_table,
        "\
small numbers
╭────────┬─────────────────────────────────────╮
│        │             result class            │
│        ├───────────────────┬─────────────────┤
│        │      general      │     specific    │
│        ├───────────────────┼─────────────────┤
│        │        sign       │       sign      │
│        ├─────────┬─────────┼────────┬────────┤
│exponent│ positive│ negative│positive│negative│
├────────┼─────────┼─────────┼────────┼────────┤
│0       │     1.00│     1.00│   -1.00│   -1.00│
│-1      │      .10│      .10│    -.10│    -.10│
│-2      │      .01│      .01│    -.01│    -.01│
│-3      │      .00│      .00│     .00│     .00│
│-4      │      .00│      .00│     .00│     .00│
│-5      │1.00E-005│1.00E-005│     .00│     .00│
│-6      │1.00E-006│1.00E-006│     .00│     .00│
│-7      │1.00E-007│1.00E-007│     .00│     .00│
│-8      │1.00E-008│1.00E-008│     .00│     .00│
│-9      │1.00E-009│1.00E-009│     .00│     .00│
╰────────┴─────────┴─────────┴────────┴────────╯
",
    );
}
