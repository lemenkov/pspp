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

use std::{fmt::Debug, num::ParseFloatError, str::FromStr};

use enum_map::enum_map;
use serde::{de::Visitor, Deserialize};

use crate::{
    format::Decimal,
    output::pivot::{
        Area, AreaStyle, Axis2, Border, BorderStyle, BoxBorder, Color, FootnoteMarkerPosition,
        FootnoteMarkerType, HeadingRegion, HorzAlign, LabelPosition, Look, RowColBorder, VertAlign,
    },
};
use thiserror::Error as ThisError;

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct TableProperties {
    #[serde(rename = "@name")]
    name: Option<String>,
    general_properties: GeneralProperties,
    footnote_properties: FootnoteProperties,
    cell_format_properties: CellFormatProperties,
    border_properties: BorderProperties,
    printing_properties: PrintingProperties,
}

impl From<TableProperties> for Look {
    fn from(table_properties: TableProperties) -> Self {
        Self {
                name: table_properties.name,
                hide_empty: table_properties.general_properties.hide_empty_rows,
                row_label_position: table_properties.general_properties.row_label_position,
                heading_widths: enum_map! {
                    HeadingRegion::Columns => table_properties.general_properties.minimum_column_width..=table_properties.general_properties.maximum_column_width,
                    HeadingRegion::Rows => table_properties.general_properties.minimum_row_width..=table_properties.general_properties.maximum_row_width,
                }.map(|_k, r|(*r.start()).try_into().unwrap_or_default()..=(*r.end()).try_into().unwrap_or_default()),
                footnote_marker_type: table_properties.footnote_properties.marker_type,
                footnote_marker_position: table_properties.footnote_properties.marker_position,
                areas: enum_map! {
                    Area::Title => table_properties.cell_format_properties.title.style.as_area_style(),
                    Area::Caption => table_properties.cell_format_properties.caption.style.as_area_style(),
                    Area::Footer => table_properties.cell_format_properties.footnotes.style.as_area_style(),
                    Area::Corner => table_properties.cell_format_properties.corner_labels.style.as_area_style(),
                    Area::Labels(Axis2::X) => table_properties.cell_format_properties.column_labels.style.as_area_style(),
                    Area::Labels(Axis2::Y) => table_properties.cell_format_properties.row_labels.style.as_area_style(),
                    Area::Data => table_properties.cell_format_properties.data.style.as_area_style(),
                    Area::Layers => table_properties.cell_format_properties.layers.style.as_area_style(),
                },
                borders: enum_map!  {
                    Border::Title => table_properties.border_properties.title_layer_separator,
                    Border::OuterFrame(BoxBorder::Left) => table_properties.border_properties.left_outer_frame,
                    Border::OuterFrame(BoxBorder::Top) => table_properties.border_properties.top_outer_frame,
                    Border::OuterFrame(BoxBorder::Right) => table_properties.border_properties.right_outer_frame,
                    Border::OuterFrame(BoxBorder::Bottom) => table_properties.border_properties.bottom_outer_frame,
                    Border::InnerFrame(BoxBorder::Left) => table_properties.border_properties.left_inner_frame,
                    Border::InnerFrame(BoxBorder::Top) => table_properties.border_properties.top_inner_frame,
                    Border::InnerFrame(BoxBorder::Right) => table_properties.border_properties.right_inner_frame,
                    Border::InnerFrame(BoxBorder::Bottom) => table_properties.border_properties.bottom_inner_frame,
                    Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::X)) => table_properties.border_properties.horizontal_dimension_border_columns,
                    Border::Dimension(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => table_properties.border_properties.vertical_category_border_columns,
                    Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::X)) => table_properties.border_properties.horizontal_dimension_border_rows,
                    Border::Dimension(RowColBorder(HeadingRegion::Rows, Axis2::Y)) => table_properties.border_properties.vertical_category_border_rows,
                    Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::X)) => table_properties.border_properties.horizontal_category_border_columns,
                    Border::Category(RowColBorder(HeadingRegion::Columns, Axis2::Y)) => table_properties.border_properties.vertical_category_border_columns,
                    Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::X)) => table_properties.border_properties.horizontal_category_border_rows,
                    Border::Category(RowColBorder(HeadingRegion::Rows, Axis2::Y)) => table_properties.border_properties.vertical_category_border_rows,
                    Border::DataLeft => table_properties.border_properties.data_area_left,
                    Border::DataTop => table_properties.border_properties.data_area_top,
                },
                print_all_layers: table_properties.printing_properties.print_all_layers,
                paginate_layers: table_properties
                    .printing_properties
                    .print_each_layer_on_separate_page,
                shrink_to_fit: enum_map! {
                    Axis2::X => table_properties.printing_properties.rescale_wide_table_to_fit_page,
                    Axis2::Y => table_properties.printing_properties.rescale_long_table_to_fit_page,
                },
                top_continuation: table_properties
                    .printing_properties
                    .continuation_text_at_top,
                bottom_continuation: table_properties
                    .printing_properties
                    .continuation_text_at_bottom,
                continuation: {
                    let text = table_properties.printing_properties.continuation_text;
                    if text.is_empty() {
                        None
                    } else {
                        Some(text)
                    }
                },
                n_orphan_lines: table_properties
                    .printing_properties
                    .window_orphan_lines
                    .try_into()
                    .unwrap_or_default(),
            }
    }
}

#[derive(Deserialize, Debug)]
struct GeneralProperties {
    #[serde(rename = "@hideEmptyRows")]
    hide_empty_rows: bool,

    #[serde(rename = "@maximumColumnWidth")]
    maximum_column_width: i64,

    #[serde(rename = "@minimumColumnWidth")]
    minimum_column_width: i64,

    #[serde(rename = "@maximumRowWidth")]
    maximum_row_width: i64,

    #[serde(rename = "@minimumRowWidth")]
    minimum_row_width: i64,

    #[serde(rename = "@rowDimensionLabels")]
    row_label_position: LabelPosition,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
struct FootnoteProperties {
    #[serde(rename = "@markerPosition")]
    marker_position: FootnoteMarkerPosition,

    #[serde(rename = "@numberFormat")]
    marker_type: FootnoteMarkerType,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
struct CellFormatProperties {
    caption: CellStyleHolder,
    column_labels: CellStyleHolder,
    corner_labels: CellStyleHolder,
    data: CellStyleHolder,
    footnotes: CellStyleHolder,
    layers: CellStyleHolder,
    row_labels: CellStyleHolder,
    title: CellStyleHolder,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
struct CellStyleHolder {
    style: CellStyle,
}

#[derive(Deserialize, Debug, Default)]
#[serde(default)]
struct CellStyle {
    #[serde(rename = "@alternatingColor")]
    alternating_color: Option<Color>,
    #[serde(rename = "@alternatingTextColor")]
    alternating_text_color: Option<Color>,
    #[serde(rename = "@color")]
    color: Option<Color>,
    #[serde(rename = "@color2")]
    color2: Option<Color>,
    #[serde(rename = "@font-family")]
    font_family: String,
    #[serde(rename = "@font-size")]
    font_size: Dimension,
    #[serde(rename = "@font-style")]
    font_style: FontStyle,
    #[serde(rename = "@font-weight")]
    font_weight: FontWeight,
    #[serde(rename = "@font-underline")]
    font_underline: FontUnderline,
    #[serde(rename = "@labelLocationVertical")]
    label_location_vertical: LabelLocationVertical,
    #[serde(rename = "@margin-bottom")]
    margin_bottom: Dimension,
    #[serde(rename = "@margin-left")]
    margin_left: Dimension,
    #[serde(rename = "@margin-right")]
    margin_right: Dimension,
    #[serde(rename = "@margin-top")]
    margin_top: Dimension,
    #[serde(rename = "@textAlignment", default)]
    text_alignment: TextAlignment,
    #[serde(rename = "@decimal-offset")]
    decimal_offset: Dimension,
}

impl CellStyle {
    fn as_area_style(&self) -> AreaStyle {
        AreaStyle {
            cell_style: super::CellStyle {
                horz_align: match self.text_alignment {
                    TextAlignment::Left => Some(HorzAlign::Left),
                    TextAlignment::Right => Some(HorzAlign::Right),
                    TextAlignment::Center => Some(HorzAlign::Center),
                    TextAlignment::Decimal => Some(HorzAlign::Decimal {
                        offset: self.decimal_offset.as_px_f64(),
                        decimal: Decimal::Dot,
                    }),
                    TextAlignment::Mixed => None,
                },
                vert_align: match self.label_location_vertical {
                    LabelLocationVertical::Positive => VertAlign::Top,
                    LabelLocationVertical::Negative => VertAlign::Bottom,
                    LabelLocationVertical::Center => VertAlign::Middle,
                },
                margins: enum_map! {
                    Axis2::X => [self.margin_left.as_px_i32(), self.margin_right.as_px_i32()],
                    Axis2::Y => [self.margin_top.as_px_i32(), self.margin_bottom.as_px_i32()],
                },
            },
            font_style: super::FontStyle {
                bold: self.font_weight == FontWeight::Bold,
                italic: self.font_style == FontStyle::Italic,
                underline: self.font_underline == FontUnderline::Underline,
                markup: false,
                font: self.font_family.clone(),
                fg: [
                    self.color.unwrap_or(Color::BLACK),
                    self.alternating_text_color.unwrap_or(Color::BLACK),
                ],
                bg: [
                    self.color2.unwrap_or(Color::BLACK),
                    self.alternating_color.unwrap_or(Color::BLACK),
                ],
                size: self.font_size.as_pt_i32(),
            },
        }
    }
}

#[derive(Deserialize, Debug, Default, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
enum FontStyle {
    #[default]
    Regular,
    Italic,
}

#[derive(Deserialize, Debug, Default, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
enum FontWeight {
    #[default]
    Regular,
    Bold,
}

#[derive(Deserialize, Debug, Default, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
enum FontUnderline {
    #[default]
    None,
    Underline,
}

#[derive(Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
enum TextAlignment {
    Left,
    Right,
    Center,
    Decimal,
    #[default]
    Mixed,
}

#[derive(Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
enum LabelLocationVertical {
    /// Top.
    #[default]
    Positive,

    /// Bottom.
    Negative,

    /// Center.
    Center,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
struct BorderProperties {
    bottom_inner_frame: BorderStyle,
    bottom_outer_frame: BorderStyle,
    data_area_left: BorderStyle,
    data_area_top: BorderStyle,
    horizontal_category_border_columns: BorderStyle,
    horizontal_category_border_rows: BorderStyle,
    horizontal_dimension_border_columns: BorderStyle,
    horizontal_dimension_border_rows: BorderStyle,
    left_inner_frame: BorderStyle,
    left_outer_frame: BorderStyle,
    right_inner_frame: BorderStyle,
    right_outer_frame: BorderStyle,
    title_layer_separator: BorderStyle,
    top_inner_frame: BorderStyle,
    top_outer_frame: BorderStyle,
    vertical_category_border_columns: BorderStyle,
    vertical_category_border_rows: BorderStyle,
    vertical_dimension_border_rows: BorderStyle,
    vertical_dimension_border_columns: BorderStyle,
}

#[derive(Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase", default)]
struct PrintingProperties {
    #[serde(rename = "@printAllLayers")]
    print_all_layers: bool,

    #[serde(rename = "@printEachLayerOnSeparatePage")]
    print_each_layer_on_separate_page: bool,

    #[serde(rename = "@rescaleWideTableToFitPage")]
    rescale_wide_table_to_fit_page: bool,

    #[serde(rename = "@rescaleLongTableToFitPage")]
    rescale_long_table_to_fit_page: bool,

    #[serde(rename = "@windowOrphanLines")]
    window_orphan_lines: i64,

    #[serde(rename = "@continuationText")]
    continuation_text: String,

    #[serde(rename = "@continuationTextAtBottom")]
    continuation_text_at_bottom: bool,

    #[serde(rename = "@continuationTextAtTop")]
    continuation_text_at_top: bool,
}

#[derive(Copy, Clone, Default, PartialEq)]
struct Dimension(
    /// In inches.
    f64,
);

impl Dimension {
    fn as_px_f64(self) -> f64 {
        self.0 * 96.0
    }
    fn as_px_i32(self) -> i32 {
        num::cast(self.as_px_f64() + 0.5).unwrap_or_default()
    }
    fn as_pt_f64(self) -> f64 {
        self.0 * 72.0
    }
    fn as_pt_i32(self) -> i32 {
        num::cast(self.as_pt_f64() + 0.5).unwrap_or_default()
    }
}

impl Debug for Dimension {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:.2}in", self.0)
    }
}

impl FromStr for Dimension {
    type Err = DimensionParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.trim_start();
        let unit = s.trim_start_matches(|c: char| c.is_ascii_digit() || c == '.');
        let number: f64 = s[..s.len() - unit.len()]
            .parse()
            .map_err(DimensionParseError::ParseFloatError)?;
        let divisor = match unit.trim() {
            // Inches.
            "in" | "인치" | "pol." | "cala" | "cali" => 1.0,

            // Device-independent pixels.
            "px" => 96.0,

            // Points.
            "pt" | "пт" | "" => 72.0,

            // Centimeters.
            "cm" | "см" => 2.54,

            other => return Err(DimensionParseError::InvalidUnit(other.into())),
        };
        Ok(Dimension(number / divisor))
    }
}

#[derive(ThisError, Debug, PartialEq, Eq)]
enum DimensionParseError {
    /// Invalid number.
    #[error("{0}")]
    ParseFloatError(ParseFloatError),

    /// Unknown unit.
    #[error("Unknown unit {0:?}")]
    InvalidUnit(String),
}

impl<'de> Deserialize<'de> for Dimension {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        struct DimensionVisitor;

        impl<'de> Visitor<'de> for DimensionVisitor {
            type Value = Dimension;

            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a string")
            }

            fn visit_borrowed_str<E>(self, v: &'de str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                v.parse().map_err(E::custom)
            }
        }

        deserializer.deserialize_str(DimensionVisitor)
    }
}

#[cfg(test)]
mod test {
    use std::str::FromStr;

    use quick_xml::de::from_str;

    use crate::output::pivot::look_xml::{Dimension, DimensionParseError, TableProperties};

    #[test]
    fn dimension() {
        assert_eq!(Dimension::from_str("1"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str("1pt"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str("1пт"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str("1.0"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str(" 1.0"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str(" 1.0 "), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str("1.0 pt"), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str("1.0pt "), Ok(Dimension(1.0 / 72.0)));
        assert_eq!(Dimension::from_str(" 1.0pt "), Ok(Dimension(1.0 / 72.0)));

        assert_eq!(Dimension::from_str("1in"), Ok(Dimension(1.0)));

        assert_eq!(Dimension::from_str("96px"), Ok(Dimension(1.0)));

        assert_eq!(Dimension::from_str("2.54cm"), Ok(Dimension(1.0)));

        assert_eq!(
            Dimension::from_str(""),
            Err(DimensionParseError::ParseFloatError(
                "".parse::<f64>().unwrap_err()
            ))
        );
        assert_eq!(
            Dimension::from_str("1.2.3"),
            Err(DimensionParseError::ParseFloatError(
                "1.2.3".parse::<f64>().unwrap_err()
            ))
        );
        assert_eq!(
            Dimension::from_str("1asdf"),
            Err(DimensionParseError::InvalidUnit("asdf".into()))
        );
    }

    #[test]
    fn look() {
        const XML: &str = r##"
<?xml version="1.0" encoding="UTF-8"?>
<tableProperties xmlns="http://www.ibm.com/software/analytics/spss/xml/table-looks" xmlns:vizml="http://www.ibm.com/software/analytics/spss/xml/visualization" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.ibm.com/software/analytics/spss/xml/table-looks http://www.ibm.com/software/analytics/spss/xml/table-looks/table-looks-1.4.xsd">
    <generalProperties hideEmptyRows="true" maximumColumnWidth="72" maximumRowWidth="120" minimumColumnWidth="36" minimumRowWidth="36" rowDimensionLabels="inCorner"/>
    <footnoteProperties markerPosition="subscript" numberFormat="alphabetic"/>
    <cellFormatProperties>
        <title>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="bold" font-underline="none" labelLocationVertical="center" margin-bottom="6pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="left"/>
        </title>
        <caption>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="positive" margin-bottom="0pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="left"/>
        </caption>
        <footnotes>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="positive" margin-bottom="2pt" margin-left="8pt" margin-right="6pt" margin-top="1pt" textAlignment="left"/>
        </footnotes>
        <cornerLabels>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="negative" margin-bottom="0pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="left"/>
        </cornerLabels>
        <columnLabels>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="negative" margin-bottom="2pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="center"/>
        </columnLabels>
        <rowLabels>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="positive" margin-bottom="2pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="left"/>
        </rowLabels>
        <data>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="positive" margin-bottom="0pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="mixed"/>
        </data>
        <layers>
            <vizml:style color="#000000" color2="#ffffff" font-family="Sans Serif" font-size="9pt" font-weight="regular" font-underline="none" labelLocationVertical="negative" margin-bottom="2pt" margin-left="6pt" margin-right="8pt" margin-top="0pt" textAlignment="left"/>
        </layers>
    </cellFormatProperties>
    <borderProperties>
        <titleLayerSeparator borderStyleType="none" color="#000000"/>
        <leftOuterFrame borderStyleType="none" color="#000000"/>
        <topOuterFrame borderStyleType="none" color="#000000"/>
        <rightOuterFrame borderStyleType="none" color="#000000"/>
        <bottomOuterFrame borderStyleType="none" color="#000000"/>
        <leftInnerFrame borderStyleType="thick" color="#000000"/>
        <topInnerFrame borderStyleType="thick" color="#000000"/>
        <rightInnerFrame borderStyleType="thick" color="#000000"/>
        <bottomInnerFrame borderStyleType="thick" color="#000000"/>
        <dataAreaLeft borderStyleType="thick" color="#000000"/>
        <dataAreaTop borderStyleType="thick" color="#000000"/>
        <horizontalDimensionBorderRows borderStyleType="solid" color="#000000"/>
        <verticalDimensionBorderRows borderStyleType="none" color="#000000"/>
        <horizontalDimensionBorderColumns borderStyleType="solid" color="#000000"/>
        <verticalDimensionBorderColumns borderStyleType="solid" color="#000000"/>
        <horizontalCategoryBorderRows borderStyleType="none" color="#000000"/>
        <verticalCategoryBorderRows borderStyleType="none" color="#000000"/>
        <horizontalCategoryBorderColumns borderStyleType="solid" color="#000000"/>
        <verticalCategoryBorderColumns borderStyleType="solid" color="#000000"/>
    </borderProperties>
    <printingProperties printAllLayers="true" rescaleLongTableToFitPage="false" rescaleWideTableToFitPage="false" windowOrphanLines="5"/>
</tableProperties>
"##;
        let table_properties: TableProperties = from_str(XML).unwrap();
        dbg!(&table_properties);
    }
}
