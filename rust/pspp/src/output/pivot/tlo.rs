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

use std::{fmt::Debug, io::Cursor};

use crate::{
    format::Decimal,
    output::pivot::{
        Axis2, Border, BoxBorder, FootnoteMarkerPosition, FootnoteMarkerType, HeadingRegion,
        LabelPosition, RowColBorder,
    },
};

use super::{Area, BorderStyle, Color, HorzAlign, Look, Stroke, VertAlign};
use binrw::{binread, BinRead, BinResult, Error as BinError};
use enum_map::enum_map;

#[binread]
#[br(little)]
#[derive(Debug)]
struct TableLook {
    pt_table_look: PtTableLook,
    pv_separator_style: PvSeparatorStyle,
    pv_cell_style: PvCellStyle,
    pv_text_style: PvTextStyle,
    #[br(parse_with = parse_default)]
    v2_styles: V2Styles,
}

pub fn parse_tlo(input: &[u8]) -> BinResult<Look> {
    let mut cursor = Cursor::new(input);
    let tlo = TableLook::read(&mut cursor)?;
    match input.len() as u64 - cursor.position() {
        0 => Ok(tlo.into()),
        extra => Err(BinError::AssertFail {
            pos: cursor.position(),
            message: format!("unexpected {extra} bytes following TLO data"),
        }),
    }
}

/// Points (72/inch) to pixels (96/inch).
fn pt_to_px(pt: i32) -> usize {
    num::cast((pt as f64 * (96.0 / 72.0)).round()).unwrap_or_default()
}

/// Pixels (96/inch) to pixels (72/inch).
fn px_to_pt(px: i32) -> i32 {
    num::cast((px as f64 * (72.0 / 96.0)).round()).unwrap_or_default()
}

/// 20ths of a point to pixels (96/inch).
fn pt20_to_px(pt20: i32) -> usize {
    num::cast((pt20 as f64 * (96.0 / 72.0 / 20.0)).round()).unwrap_or_default()
}

fn iso8859_to_string(s: &[u8]) -> String {
    s.iter().map(|byte| *byte as char).collect()
}

impl From<TableLook> for Look {
    fn from(look: TableLook) -> Self {
        let flags = look.pt_table_look.flags;
        Self {
            name: None,
            hide_empty: (flags & 2) != 0,
            row_label_position: if look.pt_table_look.nested_row_labels {
                LabelPosition::Nested
            } else {
                LabelPosition::Corner
            },
            heading_widths: enum_map! {
                    HeadingRegion::Columns => look.v2_styles.min_column_width..=look.v2_styles.max_column_width,
                    HeadingRegion::Rows => look.v2_styles.min_row_width..=look.v2_styles.max_row_width,
                }.map(|_k, range| pt_to_px(*range.start())..=pt_to_px(*range.end())),
            footnote_marker_type: if (flags & 4) != 0 {
                FootnoteMarkerType::Numeric
            } else {
                FootnoteMarkerType::Alphabetic
            },
            footnote_marker_position: if look.pt_table_look.footnote_marker_subscripts {
                FootnoteMarkerPosition::Subscript
            } else {
                FootnoteMarkerPosition::Superscript
            },
            areas: enum_map! {
                    Area::Title => super::AreaStyle::from_tlo(look.pv_cell_style.title_color, &look.pv_text_style.title_style),
                    Area::Caption => (&look.pv_text_style.caption).into(),
                    Area::Footer => (&look.pv_text_style.footer).into(),
                    Area::Corner => (&look.pv_text_style.corner).into(),
                    Area::Labels(Axis2::X) => (&look.pv_text_style.column_labels).into(),
                    Area::Labels(Axis2::Y) => (&look.pv_text_style.row_labels).into(),
                    Area::Data => (&look.pv_text_style.data).into(),
                    Area::Layers => (&look.pv_text_style.layers).into(),
            },
                borders: enum_map!  {
                    Border::Title => look.v2_styles.title,
                    Border::OuterFrame(BoxBorder::Left) => look.v2_styles.left_outer_frame,
                    Border::OuterFrame(BoxBorder::Top) => look.v2_styles.top_outer_frame,
                    Border::OuterFrame(BoxBorder::Right) => look.v2_styles.right_outer_frame,
                    Border::OuterFrame(BoxBorder::Bottom) => look.v2_styles.bottom_outer_frame,
                    Border::InnerFrame(BoxBorder::Left) => look.v2_styles.left_inner_frame,
                    Border::InnerFrame(BoxBorder::Top) => look.v2_styles.top_inner_frame,
                    Border::InnerFrame(BoxBorder::Right) => look.v2_styles.right_inner_frame,
                    Border::InnerFrame(BoxBorder::Bottom) => look.v2_styles.bottom_inner_frame,
                    Border::Dimension(RowColBorder(region, direction)) => {
                        match (region, direction) {
                            (HeadingRegion::Columns, Axis2::X) => look.pv_separator_style.horizontal_dimension_columns,
                            (HeadingRegion::Columns, Axis2::Y) => look.pv_separator_style.vertical_dimension_columns,
                            (HeadingRegion::Rows, Axis2::X) => look.pv_separator_style.horizontal_dimension_rows,
                            (HeadingRegion::Rows, Axis2::Y) => look.pv_separator_style.vertical_dimension_rows,
                        }
                    },
                    Border::Category(RowColBorder(region, direction)) => {
                        match (region, direction) {
                            (HeadingRegion::Columns, Axis2::X) => look.pv_separator_style.horizontal_category_columns,
                            (HeadingRegion::Columns, Axis2::Y) => look.pv_separator_style.vertical_category_columns,
                            (HeadingRegion::Rows, Axis2::X) => look.pv_separator_style.horizontal_category_rows,
                            (HeadingRegion::Rows, Axis2::Y) => look.pv_separator_style.vertical_category_rows,
                        }
                    },
                    Border::DataLeft => look.v2_styles.data_left,
                    Border::DataTop => look.v2_styles.data_top
                },
            print_all_layers: (flags & 8) != 0,
            paginate_layers: (flags & 0x40) != 0,
            shrink_to_fit: enum_map! {
                Axis2::X => (flags & 0x10) != 0,
                Axis2::Y => (flags & 0x20) != 0
            },
            top_continuation: (flags & 0x80) != 0,
            bottom_continuation: (flags & 0x100) != 0,
            continuation: {
                let s = &look.v2_styles.continuation;
                if s.is_empty() {
                    None
                } else {
                    Some(s.clone())
                }
            },
            n_orphan_lines: 0
        }
    }
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct PtTableLook {
    #[br(temp)]
    #[br(assert(&tag.string == b"PTTableLook"))]
    tag: Tag,

    #[br(assert(version == 0 || version == 2, "PTTableLook version {version} not supported (expected 0 or 2)."))]
    #[br(temp)]
    version: u8,

    flags: u16,

    #[br(magic = b"\0\0")]
    #[br(parse_with = parse_bool)]
    nested_row_labels: bool,

    #[br(magic = b"\0")]
    #[br(parse_with = parse_bool)]
    footnote_marker_subscripts: bool,

    #[br(temp, magic = b"\0\x36\0\0\0\x12\0\0\0")]
    _tmp: (),
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct PvSeparatorStyle {
    #[br(assert(&tag.string == b"PVSeparatorStyle"))]
    tag: Tag,

    #[br(magic = b"\0")]
    #[br(map = |separator: Separator| separator.into())]
    horizontal_dimension_rows: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    vertical_dimension_rows: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    horizontal_category_rows: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    vertical_category_rows: BorderStyle,

    #[br(magic = b"\x03\x80\0")]
    #[br(map = |separator: Separator| separator.into())]
    horizontal_dimension_columns: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    vertical_dimension_columns: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    horizontal_category_columns: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    vertical_category_columns: BorderStyle,
}

#[binread]
#[br(little)]
#[derive(Debug)]
enum Separator {
    #[br(magic = 0u16)]
    None,
    #[br(magic = 1u16)]
    Some {
        color: Color,
        style: u16,
        width: u16,
    },
}

impl From<Separator> for BorderStyle {
    fn from(separator: Separator) -> Self {
        match separator {
            Separator::None => BorderStyle {
                stroke: Stroke::None,
                color: Color::BLACK,
            },
            Separator::Some {
                color,
                style,
                width,
            } => BorderStyle {
                stroke: match (style, width) {
                    (0, 0) => Stroke::Thin,
                    (0, 2 | 3) => Stroke::Thick,
                    (1, _) => Stroke::Double,
                    (2, _) => Stroke::Dashed,
                    _ => Stroke::Solid,
                },
                color,
            },
        }
    }
}

impl BinRead for Color {
    type Args<'a> = ();

    fn read_options<R: std::io::Read + std::io::Seek>(
        reader: &mut R,
        endian: binrw::Endian,
        _args: (),
    ) -> BinResult<Self> {
        let raw = <u32>::read_options(reader, endian, ())?;
        Ok(Color::new(raw as u8, (raw >> 8) as u8, (raw >> 16) as u8))
    }
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct PvCellStyle {
    #[br(assert(&tag.string == b"PVCellStyle"))]
    tag: Tag,

    #[br(map = |src: AreaColor| src.into())]
    title_color: Color,
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct AreaColor {
    #[br(magic = b"\0\x01\0")]
    color10: Color,
    color0: Color,
    shading: u8,
    #[br(temp, magic = 0u8)]
    _tmp: (),
}

impl From<AreaColor> for Color {
    fn from(area_color: AreaColor) -> Self {
        match area_color.shading {
            0 => area_color.color0,
            x1 @ 1..=9 => {
                let Color {
                    r: r0,
                    g: g0,
                    b: b0,
                    ..
                } = area_color.color0;
                let Color {
                    r: r1,
                    g: g1,
                    b: b1,
                    ..
                } = area_color.color10;
                fn mix(c0: u32, c1: u32, x1: u32) -> u8 {
                    let x0 = 10 - x1;
                    ((c0 * x0 + c1 * x1) / 10) as u8
                }
                Color::new(
                    mix(r0 as u32, r1 as u32, x1 as u32),
                    mix(g0 as u32, g1 as u32, x1 as u32),
                    mix(b0 as u32, b1 as u32, x1 as u32),
                )
            }
            _ => area_color.color10,
        }
    }
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct PvTextStyle {
    #[br(assert(&tag.string == b"PVTextStyle"))]
    tag: Tag,

    #[br(magic = 0u8)]
    title_style: AreaStyle,
    layers: MostAreas,
    corner: MostAreas,
    row_labels: MostAreas,
    column_labels: MostAreas,
    data: MostAreas,
    caption: MostAreas,
    footer: MostAreas,
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct MostAreas {
    #[br(magic = b"\x06\x80")]
    #[br(map = |src: AreaColor| src.into())]
    color: Color,

    #[br(magic = b"\x08\x80\0")]
    style: AreaStyle,
}

impl From<&MostAreas> for super::AreaStyle {
    fn from(area: &MostAreas) -> Self {
        Self::from_tlo(area.color, &area.style)
    }
}

impl super::AreaStyle {
    fn from_tlo(bg: Color, style: &AreaStyle) -> Self {
        Self {
            cell_style: super::CellStyle {
                horz_align: match style.halign {
                    0 => Some(HorzAlign::Left),
                    1 => Some(HorzAlign::Right),
                    2 => Some(HorzAlign::Center),
                    4 => Some(HorzAlign::Decimal {
                        offset: style.decimal_offset as f64 / (72.0 * 20.0) * 96.0,
                        decimal: Decimal::Comma,
                    }),
                    _ => None,
                },
                vert_align: match style.valign {
                    0 => VertAlign::Top,
                    1 => VertAlign::Bottom,
                    _ => VertAlign::Middle,
                },
                margins: {
                    fn convert(pt20: u16) -> i32 {
                        num::cast(pt20_to_px(pt20 as i32)).unwrap_or_default()
                    }
                    enum_map! {
                        Axis2::X => [convert(style.left_margin), convert(style.right_margin)],
                        Axis2::Y => [convert(style.top_margin), convert(style.bottom_margin)]
                    }
                },
            },
            font_style: super::FontStyle {
                bold: style.weight > 400,
                italic: style.italic,
                underline: style.underline,
                markup: false,
                font: style.font_name.string.clone(),
                fg: {
                    let fg = style.text_color;
                    [fg, fg]
                },
                bg: [bg, bg],
                size: -style.font_size * 3 / 4,
            },
        }
    }
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct AreaStyle {
    valign: u16,
    halign: u16,
    decimal_offset: u16,
    left_margin: u16,
    right_margin: u16,
    top_margin: u16,
    bottom_margin: u16,
    #[br(magic = b"\0\0\x01\0")]
    font_size: i32,
    stretch: u16,
    #[br(magic = 0u16)]
    rotation_angle: u32,
    #[br(magic = 0u32)]
    weight: u16,
    #[br(magic = 0u16)]
    #[br(parse_with = parse_bool)]
    italic: bool,
    #[br(parse_with = parse_bool)]
    underline: bool,
    #[br(parse_with = parse_bool)]
    strike_through: bool,
    rtf_charset_number: u32,
    x: u8,
    font_name: U8String,
    text_color: Color,
    #[br(temp, magic = 0u16)]
    _tmp: (),
}

#[binread]
#[br(little)]
#[derive(Debug)]
struct V2Styles {
    #[br(map = |separator: Separator| separator.into())]
    title: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    left_inner_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    right_inner_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    top_inner_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    bottom_inner_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    left_outer_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    right_outer_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    top_outer_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    bottom_outer_frame: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    data_left: BorderStyle,
    #[br(map = |separator: Separator| separator.into())]
    data_top: BorderStyle,
    #[br(map = |s: U8String| s.string)]
    continuation: String,
    min_column_width: i32,
    max_column_width: i32,
    min_row_width: i32,
    max_row_width: i32,
}

impl Default for V2Styles {
    fn default() -> Self {
        Self {
            title: Border::Title.default_border_style(),
            left_inner_frame: Border::InnerFrame(BoxBorder::Left).default_border_style(),
            right_inner_frame: Border::InnerFrame(BoxBorder::Right).default_border_style(),
            top_inner_frame: Border::InnerFrame(BoxBorder::Top).default_border_style(),
            bottom_inner_frame: Border::InnerFrame(BoxBorder::Bottom).default_border_style(),
            left_outer_frame: Border::OuterFrame(BoxBorder::Left).default_border_style(),
            right_outer_frame: Border::OuterFrame(BoxBorder::Right).default_border_style(),
            top_outer_frame: Border::OuterFrame(BoxBorder::Top).default_border_style(),
            bottom_outer_frame: Border::OuterFrame(BoxBorder::Bottom).default_border_style(),
            data_left: Border::DataLeft.default_border_style(),
            data_top: Border::DataTop.default_border_style(),
            continuation: Default::default(),
            min_column_width: 36,
            max_column_width: 72,
            min_row_width: 36,
            max_row_width: 120,
        }
    }
}

#[binrw::parser(reader, endian)]
fn parse_bool() -> BinResult<bool> {
    let byte = <u8>::read_options(reader, endian, ())?;
    match byte {
        0 => Ok(false),
        1 => Ok(true),
        _ => Err(BinError::NoVariantMatch {
            pos: reader.stream_position()? - 1,
        }),
    }
}

#[binrw::parser(reader, endian)]
fn parse_option<'a, T>(args: T::Args<'a>) -> BinResult<Option<T>>
where
    T: BinRead,
{
    match <T>::read_options(reader, endian, args) {
        Err(error) => match error {
            BinError::Io(_) => Err(error),
            _ => Ok(None),
        },
        Ok(inner) => Ok(Some(inner)),
    }
}

#[binrw::parser(reader, endian)]
fn parse_default<'a, T>(args: T::Args<'a>) -> BinResult<T>
where
    T: BinRead + Default,
{
    Ok(parse_option(reader, endian, (args,))?.unwrap_or_default())
}

#[binread]
#[br(little)]
struct Tag {
    #[br(magic = b"\xff\xff\0\0")]
    #[br(temp)]
    length: u16,

    #[br(count = length)]
    string: Vec<u8>,
}

impl Debug for Tag {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", String::from_utf8_lossy(&self.string))
    }
}

#[binread]
#[br(little)]
struct U8String {
    #[br(temp)]
    length: u8,

    #[br(temp, count = length)]
    data: Vec<u8>,

    #[br(calc = iso8859_to_string(&data))]
    string: String,
}

impl Debug for U8String {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", &self.string)
    }
}

#[cfg(test)]
mod test {
    use crate::output::pivot::tlo::parse_tlo;

    #[test]
    fn parse() {
        let look = parse_tlo(include_bytes!("test1.tlo"));
        println!("{look:#?}");
    }
}
