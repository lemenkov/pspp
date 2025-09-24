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

use std::sync::Arc;

use cairo::{Context, RecordingSurface};
use enum_map::EnumMap;
use pango::{FontDescription, Layout};

use crate::output::{
    Item, ItemCursor,
    cairo::{
        fsm::{CairoFsm, CairoFsmStyle},
        horz_align_to_pango, xr_to_pt,
    },
    page::Heading,
    pivot::Axis2,
};

#[derive(Clone, Debug)]
pub struct CairoPageStyle {
    pub margins: EnumMap<Axis2, [usize; 2]>,
    pub headings: [Heading; 2],
    pub initial_page_number: i32,
}

pub struct CairoPager {
    page_style: Arc<CairoPageStyle>,
    fsm_style: Arc<CairoFsmStyle>,
    page_index: i32,
    heading_heights: [usize; 2],
    iter: Option<ItemCursor>,
    context: Option<Context>,
    fsm: Option<CairoFsm>,
    y: usize,
}

impl CairoPager {
    pub fn new(mut page_style: Arc<CairoPageStyle>, mut fsm_style: Arc<CairoFsmStyle>) -> Self {
        let heading_heights = measure_headings(&page_style, &fsm_style);
        let total = heading_heights.iter().sum::<usize>();
        if (0..fsm_style.size[Axis2::Y]).contains(&total) {
            let fsm_style = Arc::make_mut(&mut fsm_style);
            let page_style = Arc::make_mut(&mut page_style);
            #[allow(clippy::needless_range_loop)]
            for i in 0..2 {
                page_style.margins[Axis2::Y][i] += heading_heights[i];
            }
            fsm_style.size[Axis2::Y] -= total;
        }
        Self {
            page_style,
            fsm_style,
            page_index: 0,
            heading_heights,
            iter: None,
            context: None,
            fsm: None,
            y: 0,
        }
    }

    pub fn add_page(&mut self, context: Context) {
        assert!(self.context.is_none());
        context.save().unwrap();
        self.y = 0;

        context.translate(
            xr_to_pt(self.page_style.margins[Axis2::X][0]),
            xr_to_pt(self.page_style.margins[Axis2::Y][0]),
        );

        let page_number = self.page_index + self.page_style.initial_page_number;
        self.page_index += 1;

        if self.heading_heights[0] > 0 {
            render_heading(
                &context,
                &self.fsm_style.font,
                &self.page_style.headings[0],
                page_number,
                self.fsm_style.size[Axis2::X],
                0, /* XXX*/
                self.fsm_style.font_resolution,
            );
        }
        if self.heading_heights[0] > 0 {
            render_heading(
                &context,
                &self.fsm_style.font,
                &self.page_style.headings[1],
                page_number,
                self.fsm_style.size[Axis2::X],
                self.fsm_style.size[Axis2::Y] + self.fsm_style.object_spacing,
                self.fsm_style.font_resolution,
            );
        }

        self.context = Some(context);
        self.run();
    }

    pub fn finish_page(&mut self) {
        if let Some(context) = self.context.take() {
            context.restore().unwrap();
        }
    }

    pub fn needs_new_page(&mut self) -> bool {
        if self.iter.is_some()
            && (self.context.is_none() || self.y >= self.fsm_style.size[Axis2::Y])
        {
            self.finish_page();
            true
        } else {
            false
        }
    }

    pub fn add_item(&mut self, item: Arc<Item>) {
        self.iter = Some(ItemCursor::new(item));
        self.run();
    }

    fn run(&mut self) {
        let Some(context) = self.context.as_ref().cloned() else {
            return;
        };
        if self.iter.is_none() || self.y >= self.fsm_style.size[Axis2::Y] {
            return;
        }

        loop {
            // Make sure we've got an object to render.
            let fsm = match &mut self.fsm {
                Some(fsm) => fsm,
                None => {
                    // If there are no remaining objects to render, then we're done.
                    let Some(iter) = self.iter.as_mut() else {
                        return;
                    };
                    let Some(item) = iter.cur().cloned() else {
                        self.iter = None;
                        return;
                    };
                    iter.next();
                    self.fsm
                        .insert(CairoFsm::new(self.fsm_style.clone(), true, &context, item))
                }
            };

            // Prepare to render the current object.
            let chunk = fsm.draw_slice(
                &context,
                self.fsm_style.size[Axis2::Y].saturating_sub(self.y),
            );
            self.y += chunk + self.fsm_style.object_spacing;
            context.translate(0.0, xr_to_pt(chunk + self.fsm_style.object_spacing));

            if fsm.is_done() {
                self.fsm = None;
            } else if chunk == 0 {
                assert!(self.y > 0);
                self.y = usize::MAX;
                return;
            }
        }
    }
}

fn measure_headings(page_style: &CairoPageStyle, fsm_style: &CairoFsmStyle) -> [usize; 2] {
    let surface = RecordingSurface::create(cairo::Content::Color, None).unwrap();
    let context = Context::new(&surface).unwrap();

    let mut heading_heights = Vec::with_capacity(2);
    for heading in &page_style.headings {
        let mut height = render_heading(
            &context,
            &fsm_style.font,
            heading,
            -1,
            fsm_style.size[Axis2::X],
            0,
            fsm_style.font_resolution,
        );
        if height > 0 {
            height += fsm_style.object_spacing;
        }
        heading_heights.push(height);
    }
    heading_heights.try_into().unwrap()
}

fn render_heading(
    context: &Context,
    font: &FontDescription,
    heading: &Heading,
    _page_number: i32,
    width: usize,
    base_y: usize,
    font_resolution: f64,
) -> usize {
    let pangocairo_context = pangocairo::functions::create_context(context);
    pangocairo::functions::context_set_resolution(&pangocairo_context, font_resolution);
    let layout = Layout::new(&pangocairo_context);
    layout.set_font_description(Some(font));

    let mut y = 0;
    for paragraph in &heading.0 {
        // XXX substitute heading variables
        layout.set_markup(&paragraph.markup);

        layout.set_alignment(horz_align_to_pango(paragraph.horz_align));
        layout.set_width(width as i32);

        context.save().unwrap();
        context.translate(0.0, xr_to_pt(y + base_y));
        pangocairo::functions::show_layout(context, &layout);
        context.restore().unwrap();

        y += layout.height() as usize;
    }
    y
}
