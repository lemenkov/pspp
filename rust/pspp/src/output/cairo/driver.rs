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

use std::{
    borrow::Cow,
    path::{Path, PathBuf},
    sync::Arc,
};

use cairo::{Context, PdfSurface};
use enum_map::{EnumMap, enum_map};
use pango::SCALE;
use serde::{Deserialize, Serialize};

use crate::output::{
    Item,
    cairo::{
        fsm::{CairoFsmStyle, parse_font_style},
        pager::{CairoPageStyle, CairoPager},
    },
    driver::Driver,
    page::PageSetup,
    pivot::{Color, Coord2, FontStyle},
};

use crate::output::pivot::Axis2;

#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CairoConfig {
    /// Output file name.
    pub file: PathBuf,

    /// Page setup.
    pub page_setup: Option<PageSetup>,
}

impl CairoConfig {
    pub fn new(path: impl AsRef<Path>) -> Self {
        Self {
            file: path.as_ref().to_path_buf(),
            page_setup: None,
        }
    }
}

pub struct CairoDriver {
    fsm_style: Arc<CairoFsmStyle>,
    page_style: Arc<CairoPageStyle>,
    pager: Option<CairoPager>,
    surface: PdfSurface,
}

impl CairoDriver {
    pub fn new(config: &CairoConfig) -> cairo::Result<Self> {
        fn scale(inches: f64) -> usize {
            (inches * 72.0 * SCALE as f64).max(0.0).round() as usize
        }

        let default_page_setup;
        let page_setup = match &config.page_setup {
            Some(page_setup) => page_setup,
            None => {
                default_page_setup = PageSetup::default();
                &default_page_setup
            }
        };
        let printable = page_setup.printable_size();
        let page_style = CairoPageStyle {
            margins: EnumMap::from_fn(|axis| {
                [
                    scale(page_setup.margins[axis][0]),
                    scale(page_setup.margins[axis][1]),
                ]
            }),
            headings: page_setup.headings.clone(),
            initial_page_number: page_setup.initial_page_number,
        };
        let size = Coord2::new(scale(printable[Axis2::X]), scale(printable[Axis2::Y]));
        let font = FontStyle {
            bold: false,
            italic: false,
            underline: false,
            markup: false,
            font: "Sans Serif".into(),
            fg: [Color::BLACK, Color::BLACK],
            bg: [Color::WHITE, Color::WHITE],
            size: 10,
        };
        let font = parse_font_style(&font);
        let fsm_style = CairoFsmStyle {
            size,
            min_break: enum_map! {
                Axis2::X => size[Axis2::X] / 2,
                Axis2::Y => size[Axis2::Y] / 2,
            },
            font,
            fg: Color::BLACK,
            use_system_colors: false,
            object_spacing: scale(page_setup.object_spacing),
            font_resolution: 72.0,
        };
        let surface = PdfSurface::new(
            page_setup.paper[Axis2::X] * 72.0,
            page_setup.paper[Axis2::Y] * 72.0,
            &config.file,
        )?;
        Ok(Self {
            fsm_style: Arc::new(fsm_style),
            page_style: Arc::new(page_style),
            pager: None,
            surface,
        })
    }
}

impl Driver for CairoDriver {
    fn name(&self) -> Cow<'static, str> {
        Cow::from("cairo")
    }

    fn write(&mut self, item: &Arc<Item>) {
        let pager = self.pager.get_or_insert_with(|| {
            let mut pager = CairoPager::new(self.page_style.clone(), self.fsm_style.clone());
            pager.add_page(Context::new(&self.surface).unwrap());
            pager
        });
        pager.add_item(item.clone());
        dbg!();
        while pager.needs_new_page() {
            dbg!();
            pager.finish_page();
            let context = Context::new(&self.surface).unwrap();
            context.show_page().unwrap();
            pager.add_page(context);
        }
        dbg!();
    }
}

impl Drop for CairoDriver {
    fn drop(&mut self) {
        dbg!();
        if self.pager.is_some() {
            dbg!();
            let context = Context::new(&self.surface).unwrap();
            context.show_page().unwrap();
        }
    }
}
