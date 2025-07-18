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

#![allow(dead_code)]
use std::{
    borrow::Cow,
    sync::{Arc, OnceLock},
};

use enum_map::EnumMap;
use pivot::PivotTable;
use serde::Serialize;

use crate::{
    message::Diagnostic,
    output::pivot::{Axis3, BorderStyle, Dimension, Group, Look},
};

use self::pivot::Value;

pub mod cairo;
pub mod csv;
pub mod driver;
pub mod html;
pub mod json;
pub mod page;
pub mod pivot;
pub mod render;
pub mod spv;
pub mod table;
pub mod text;
pub mod text_line;

/// A single output item.
#[derive(Serialize)]
pub struct Item {
    /// The localized label for the item that appears in the outline pane in the
    /// output viewer and in PDF outlines.  This is `None` if no label has been
    /// explicitly set.
    label: Option<String>,

    /// A locale-invariant identifier for the command that produced the output,
    /// which may be `None` if unknown or if a command did not produce this
    /// output.
    command_name: Option<String>,

    /// For a group item, this is true if the group's subtree should
    /// be expanded in an outline view, false otherwise.
    ///
    /// For other kinds of output items, this is true to show the item's
    /// content, false to hide it.  The item's label is always shown in an
    /// outline view.
    show: bool,

    /// Item details.
    details: Details,
}

impl Item {
    pub fn new(details: impl Into<Details>) -> Self {
        let details = details.into();
        Self {
            label: None,
            command_name: details.command_name().cloned(),
            show: true,
            details: details,
        }
    }

    pub fn label(&self) -> Cow<'static, str> {
        match &self.label {
            Some(label) => Cow::from(label.clone()),
            None => self.details.label(),
        }
    }
}

impl<T> From<T> for Item
where
    T: Into<Details>,
{
    fn from(value: T) -> Self {
        Self::new(value)
    }
}

#[derive(Serialize)]
pub enum Details {
    Chart,
    Image,
    Group(Vec<Arc<Item>>),
    Message(Box<Diagnostic>),
    PageBreak,
    Table(Box<PivotTable>),
    Text(Box<Text>),
}

impl Details {
    pub fn as_group(&self) -> Option<&[Arc<Item>]> {
        match self {
            Self::Group(children) => Some(children.as_slice()),
            _ => None,
        }
    }

    pub fn command_name(&self) -> Option<&String> {
        match self {
            Details::Chart
            | Details::Image
            | Details::Group(_)
            | Details::Message(_)
            | Details::PageBreak
            | Details::Text(_) => None,
            Details::Table(pivot_table) => pivot_table.command_c.as_ref(),
        }
    }

    pub fn label(&self) -> Cow<'static, str> {
        match self {
            Details::Chart => todo!(),
            Details::Image => todo!(),
            Details::Group(_) => Cow::from("Group"),
            Details::Message(diagnostic) => Cow::from(diagnostic.severity.as_title_str()),
            Details::PageBreak => Cow::from("Page Break"),
            Details::Table(pivot_table) => Cow::from(pivot_table.label()),
            Details::Text(text) => Cow::from(text.type_.as_str()),
        }
    }

    pub fn is_page_break(&self) -> bool {
        match self {
            Self::PageBreak => true,
            _ => false,
        }
    }
}

impl<A> FromIterator<A> for Details
where
    A: Into<Arc<Item>>,
{
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = A>,
    {
        Self::Group(iter.into_iter().map(|value| value.into()).collect())
    }
}

impl From<Diagnostic> for Details {
    fn from(value: Diagnostic) -> Self {
        Self::Message(Box::new(value))
    }
}

impl From<Box<Diagnostic>> for Details {
    fn from(value: Box<Diagnostic>) -> Self {
        Self::Message(value)
    }
}

impl From<PivotTable> for Details {
    fn from(value: PivotTable) -> Self {
        Self::Table(Box::new(value))
    }
}

impl From<Box<PivotTable>> for Details {
    fn from(value: Box<PivotTable>) -> Self {
        Self::Table(value)
    }
}

impl From<Text> for Details {
    fn from(value: Text) -> Self {
        Self::Text(Box::new(value))
    }
}

impl From<Box<Text>> for Details {
    fn from(value: Box<Text>) -> Self {
        Self::Text(value)
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Text {
    type_: TextType,

    content: Value,
}

impl Text {
    pub fn new_log(value: impl Into<Value>) -> Self {
        Self {
            type_: TextType::Log,
            content: value.into(),
        }
    }
}

fn text_item_table_look() -> Arc<Look> {
    static LOOK: OnceLock<Arc<Look>> = OnceLock::new();
    LOOK.get_or_init(|| {
        Arc::new({
            let mut look = Look::default().with_borders(EnumMap::from_fn(|_| BorderStyle::none()));
            for style in look.areas.values_mut() {
                style.cell_style.margins = EnumMap::from_fn(|_| [0, 0]);
            }
            look
        })
    })
    .clone()
}

impl From<Text> for PivotTable {
    fn from(value: Text) -> Self {
        let dimension =
            Dimension::new(Group::new(Value::new_text("Text")).with(Value::new_user_text("null")))
                .with_all_labels_hidden();
        PivotTable::new([(Axis3::Y, dimension)])
            .with_look(text_item_table_look())
            .with_data([(&[0], value.content)])
            .with_subtype(Value::new_user_text("Text"))
    }
}

impl From<&Diagnostic> for Text {
    fn from(value: &Diagnostic) -> Self {
        Self::new_log(value.to_string())
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TextType {
    /// `TITLE` and `SUBTITLE` commands.
    PageTitle,

    /// Title,
    Title,

    /// Syntax printback logging.
    Syntax,

    /// Other logging.
    Log,
}

impl TextType {
    pub fn as_str(&self) -> &'static str {
        match self {
            TextType::PageTitle => "Page Title",
            TextType::Title => "Title",
            TextType::Syntax => "Log",
            TextType::Log => "Log",
        }
    }

    pub fn as_xml_str(&self) -> &'static str {
        match self {
            TextType::PageTitle => "page-title",
            TextType::Title => "title",
            TextType::Syntax | TextType::Log => "log",
        }
    }
}

pub struct ItemCursor {
    cur: Option<Arc<Item>>,
    stack: Vec<(Arc<Item>, usize)>,
}

impl ItemCursor {
    pub fn new(start: Arc<Item>) -> Self {
        Self {
            cur: Some(start),
            stack: Vec::new(),
        }
    }

    pub fn cur(&self) -> Option<&Arc<Item>> {
        self.cur.as_ref()
    }

    pub fn next(&mut self) {
        let Some(cur) = self.cur.take() else {
            return;
        };
        match cur.details {
            Details::Group(ref children) if !children.is_empty() => {
                self.cur = Some(children[0].clone());
                self.stack.push((cur, 1));
            }
            _ => {
                while let Some((item, index)) = self.stack.pop() {
                    let children = item.details.as_group().unwrap();
                    if index < children.len() {
                        self.cur = Some(children[index].clone());
                        self.stack.push((item, index + 1));
                        return;
                    }
                }
            }
        }
    }
}
