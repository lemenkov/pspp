/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008-2015, 2016 Free Software Foundation.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "ui/gui/psppire-output-view.h"

#include <cairo/cairo-svg.h>
#include <errno.h>
#include <stdbool.h>

#include "libpspp/assertion.h"
#include "libpspp/string-map.h"
#include "output/cairo-fsm.h"
#include "output/cairo-pager.h"
#include "output/driver-provider.h"
#include "output/driver.h"
#include "output/output-item.h"
#include "output/pivot-table.h"

#include "gl/c-xvasprintf.h"
#include "gl/minmax.h"
#include "gl/clean-temp.h"
#include "gl/xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)

struct output_view_item
  {
    struct output_item *item;
    GtkWidget *drawing_area;
    int width, height;
  };

struct psppire_output_view
  {
    struct xr_fsm_style *style;
    int object_spacing;

    GtkLayout *output;
    int render_width;
    int max_width;
    glong y;

    GtkTreeView *overview;

    GtkWidget *toplevel;

    guint buttontime; /* Time of the button event */

    struct output_view_item *items;
    size_t n_items, allocated_items;
    struct output_view_item *selected_item;

    /* Variables pertaining to printing */
    GtkPrintSettings *print_settings;

    struct xr_fsm_style *fsm_style;
    struct xr_page_style *page_style;
    struct xr_pager *pager;
    int print_item;
    int print_n_pages;
    gboolean paginated;
  };

enum
  {
    COL_LABEL,                  /* Output item label. */
    COL_ADDR,                   /* Pointer to the table */
    COL_Y,                      /* Y position of top of object. */
    N_COLS
  };

static GtkTargetList *build_target_list (const struct output_item *item);
static void clipboard_get_cb (GtkClipboard     *clipboard,
			      GtkSelectionData *selection_data,
			      guint             info,
			      gpointer          data);

/* Draws a white background on the GtkLayout to match the white background of
   each of the output items. */
static gboolean
layout_draw_callback (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  int width = gtk_widget_get_allocated_width (widget);
  int height = gtk_widget_get_allocated_height (widget);
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, 0, 0, width, height);
  return FALSE;                 /* Continue drawing the GtkDrawingAreas. */
}

static gboolean
draw_callback (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  GdkRectangle clip;
  if (!gdk_cairo_get_clip_rectangle (cr, &clip))
    return TRUE;

  struct xr_fsm *fsm = g_object_get_data (G_OBJECT (widget), "fsm");

  /* Draw the background based on the state of the widget
     which can be selected or not selected */
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, clip.x, clip.y,
			 clip.x + clip.width, clip.y + clip.height);
  /* Select the default foreground color based on current style
     and state of the widget */
  GtkStateFlags state = gtk_widget_get_state_flags (widget);
  GdkRGBA color;
  gtk_style_context_get_color (context, state, &color);
  cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
  xr_fsm_draw_region (fsm, cr, clip.x, clip.y, clip.width, clip.height);

  return TRUE;
}

static void
free_fsm (gpointer fsm_)
{
  struct xr_fsm *fsm = fsm_;
  xr_fsm_destroy (fsm);
}

static struct xr_fsm_style *
get_xr_fsm_style (struct psppire_output_view *view)
{
  GtkStyleContext *context
    = gtk_widget_get_style_context (GTK_WIDGET (view->output));
  GtkStateFlags state = gtk_widget_get_state_flags (GTK_WIDGET (view->output));

  int xr_width = view->render_width * 1000;

  PangoFontDescription *pf;
  gtk_style_context_get (context, state, "font", &pf, NULL);

  struct xr_fsm_style *style = xmalloc (sizeof *style);
  *style = (struct xr_fsm_style) {
    .ref_cnt = 1,
    .size = { [TABLE_HORZ] = xr_width, [TABLE_VERT] = INT_MAX },
    .min_break = { [TABLE_HORZ] = xr_width / 2, [TABLE_VERT] = 0 },
    .font = pf,
    .use_system_colors = true,
    .object_spacing = XR_POINT * 12,
    .font_resolution = 96.0,
  };

  return style;
}

/* Return the horizontal position to place a widget whose
   width is CHILD_WIDTH */
static gint
get_xpos (const struct psppire_output_view *view, gint child_width)
{
  GdkWindow *gdkw = gtk_widget_get_window (GTK_WIDGET (view->output));
  guint w = gdk_window_get_width (gdkw);
  int gutter = 0;
  g_object_get (view->output, "border-width", &gutter, NULL);
  return (gtk_widget_get_direction (GTK_WIDGET (view->output)) ==  GTK_TEXT_DIR_RTL) ? w - child_width - gutter: gutter;
}

static struct output_view_item *
find_selected_item (struct psppire_output_view *view)
{
  struct output_view_item *item = NULL;
  if (view == NULL)
    return NULL;
  if (view->items == NULL)
    return NULL;

  for (item = view->items; item < &view->items[view->n_items]; item++)
    {
      GtkWidget *widget = GTK_WIDGET (item->drawing_area);
      if (GTK_IS_WIDGET (widget))
        {
	  GtkStateFlags state = gtk_widget_get_state_flags (widget);
	  if (state & GTK_STATE_FLAG_SELECTED)
	    return item;
	}
    }
  return NULL;
}


static void
set_copy_action (struct psppire_output_view *view,
		 gboolean state)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (view->output));
  GAction *copy_action = g_action_map_lookup_action (G_ACTION_MAP (toplevel),
						     "copy");
  g_object_set (copy_action,
		"enabled", state,
		NULL);
}

static void
clear_selection (struct psppire_output_view *view)
{
  if (view == NULL)
    return;
  struct output_view_item *item = find_selected_item (view);
  if (item == NULL)
    return;
  set_copy_action (view, FALSE);
  GtkWidget *widget = GTK_WIDGET (item->drawing_area);
  if (GTK_IS_WIDGET (widget))
    {
      gtk_widget_unset_state_flags (widget, GTK_STATE_FLAG_SELECTED);
      gtk_widget_queue_draw (widget);
    }
}

static gboolean
off_item_button_press_event_cb (GtkWidget      *widget,
				GdkEventButton *event,
				struct psppire_output_view *view)
{
  /* buttontime is set by button_press_event_cb
     If our event->time is equal to the time from the
     button_press_event_cb, then we handle the same event.
     In that case we must not clear the selection because
     it was just set by button_press_event_cb from the item */
  if (event->time != view->buttontime)
    clear_selection (view);
  return FALSE; /* Forward the event -> DragNDrop */
}

static gboolean
button_press_event_cb (GtkWidget      *widget,
		       GdkEventButton *event,
		       struct psppire_output_view *view)
{
  view->buttontime = event->time;
  clear_selection (view);
  set_copy_action (view, TRUE);
  gtk_widget_set_state_flags (widget, GTK_STATE_FLAG_SELECTED, FALSE);
  gtk_widget_queue_draw (widget);
  return FALSE; /* Forward Event -> off_item will trigger */
}

static void
drag_data_get_cb (GtkWidget *widget, GdkDragContext *context,
		  GtkSelectionData *selection_data,
		  guint target_type, guint time,
		  struct psppire_output_view *view)
{
  view->selected_item = find_selected_item (view);
  clipboard_get_cb (NULL, selection_data, target_type, view);
}

static void
create_drawing_area (struct psppire_output_view *view,
                     GtkWidget *drawing_area, struct xr_fsm *r,
                     int tw, int th, const struct output_item *item)
{
  g_object_set_data_full (G_OBJECT (drawing_area),
                          "fsm", r, free_fsm);
  g_signal_connect (drawing_area, "button-press-event",
		    G_CALLBACK (button_press_event_cb), view);
  gtk_widget_add_events (drawing_area, GDK_BUTTON_PRESS_MASK);

  { /* Drag and Drop */
    GtkTargetList *tl = build_target_list (item);
    g_assert (tl);
    gtk_drag_source_set (drawing_area, GDK_BUTTON1_MASK, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_source_set_target_list (drawing_area, tl);
    gtk_target_list_unref (tl);
    g_signal_connect (drawing_area, "drag-data-get",
		      G_CALLBACK (drag_data_get_cb), view);
  }
  GtkStyleContext *context = gtk_widget_get_style_context (drawing_area);
  gtk_style_context_add_class (context,
			       GTK_STYLE_CLASS_VIEW);
  g_signal_connect (drawing_area, "draw",
                    G_CALLBACK (draw_callback), view);

  gtk_widget_set_size_request (drawing_area, tw, th);
  gint xpos = get_xpos (view, tw);

  gtk_layout_put (view->output, drawing_area, xpos, view->y);

  gtk_widget_show (drawing_area);
}

static void
rerender (struct psppire_output_view *view)
{
  struct output_view_item *item;
  GdkWindow *gdkw = gtk_widget_get_window (GTK_WIDGET (view->output));

  if (!view->n_items || ! gdkw)
    return;

  if (!view->style)
    view->style = get_xr_fsm_style (view);

  GdkWindow *win = gtk_layout_get_bin_window (view->output);
  cairo_region_t *region = gdk_window_get_visible_region (win);
  GdkDrawingContext *ctx =  gdk_window_begin_draw_frame (win, region);
  cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

  view->y = 0;
  view->max_width = 0;
  for (item = view->items; item < &view->items[view->n_items]; item++)
    {
      struct xr_fsm *r;
      GtkAllocation alloc;
      int tw, th;

      if (view->y > 0)
        view->y += view->object_spacing;

      if (item->item->type == OUTPUT_ITEM_GROUP)
        continue;

      r = xr_fsm_create_for_scrolling (item->item, view->style, cr);
      if (r == NULL)
        {
          g_warn_if_reached ();
          continue;
        }

      xr_fsm_measure (r, cr, &tw, &th);

      gint xpos = get_xpos (view, tw);

      if (!item->drawing_area)
        {
          item->drawing_area = gtk_drawing_area_new ();
          create_drawing_area (view, item->drawing_area, r, tw, th, item->item);
        }
      else
        {
          g_object_set_data_full (G_OBJECT (item->drawing_area),
                                  "fsm", r, free_fsm);
          gtk_widget_set_size_request (item->drawing_area, tw, th);
          gtk_layout_move (view->output, item->drawing_area, xpos, view->y);
        }

      if (item->item->type == OUTPUT_ITEM_TABLE)
        gtk_widget_set_tooltip_text (item->drawing_area,
                                     item->item->table->notes);

      {
	gint minw;
	gint minh;
	/* This code probably doesn't bring us anthing, but Gtk
	   shows warnings if get_preferred_width/height is not
	   called before the size_allocate below is called. */
	gtk_widget_get_preferred_width (item->drawing_area, &minw, NULL);
	gtk_widget_get_preferred_height (item->drawing_area, &minh, NULL);
	if (th > minh) th = minh;
	if (tw > minw) tw = minw;
      }
      alloc.x = xpos;
      alloc.y = view->y;
      alloc.width = tw;
      alloc.height = th;

      gtk_widget_size_allocate (item->drawing_area, &alloc);

      if (view->max_width < tw)
        view->max_width = tw;
      view->y += th;
    }

  gtk_layout_set_size (view->output,
                       view->max_width + view->object_spacing,
                       view->y + view->object_spacing);

  gdk_window_end_draw_frame (win, ctx);
  cairo_region_destroy (region);
}

static bool
init_output_view_item (struct output_view_item *view_item,
                       struct psppire_output_view *view,
                       const struct output_item *item)
{
  *view_item = (struct output_view_item) { .item = output_item_ref (item) };

  GdkWindow *win = gtk_widget_get_window (GTK_WIDGET (view->output));
  if (win && item->type != OUTPUT_ITEM_GROUP)
    {
      if (!view->style)
        view->style = get_xr_fsm_style (view);

      cairo_region_t *region = gdk_window_get_visible_region (win);
      GdkDrawingContext *ctx = gdk_window_begin_draw_frame (win, region);
      cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

      if (view->y > 0)
        view->y += view->object_spacing;

      struct xr_fsm *r = xr_fsm_create_for_scrolling (item, view->style, cr);
      if (r == NULL)
	{
	  gdk_window_end_draw_frame (win, ctx);
	  cairo_region_destroy (region);

          output_item_unref (view_item->item);
	  return false;
	}

      xr_fsm_measure (r, cr, &view_item->width, &view_item->height);
      view_item->drawing_area = gtk_drawing_area_new ();
      create_drawing_area (view, view_item->drawing_area, r, view_item->width,
                           view_item->height, item);
      gdk_window_end_draw_frame (win, ctx);
      cairo_region_destroy (region);
    }

  return true;
}

static void
psppire_output_view_put__ (struct psppire_output_view *view,
                           const struct output_item *item,
                           GtkTreePath *parent_path)
{
  if (item->type == OUTPUT_ITEM_TEXT)
    {
      char *text = text_item_get_plain_text (item);
      bool text_is_empty = text[0] == '\0';
      free (text);
      if (text_is_empty)
        return;
    }

  if (view->n_items >= view->allocated_items)
    view->items = x2nrealloc (view->items, &view->allocated_items,
                              sizeof *view->items);
  struct output_view_item *view_item = &view->items[view->n_items];
  if (!init_output_view_item (view_item, view, item))
    return;
  view->n_items++;

  GtkTreePath *path = NULL;
  if (view->overview)
    {
      GtkTreeStore *store = GTK_TREE_STORE (
        gtk_tree_view_get_model (view->overview));

      /* Create a new node in the tree and puts a reference to it in 'iter'. */
      GtkTreeIter iter;
      GtkTreeIter parent;
      if (parent_path
          && gtk_tree_path_get_depth (parent_path) > 0
          && gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
                                      &parent, parent_path))
        gtk_tree_store_append (store, &iter, &parent);
      else
        gtk_tree_store_append (store, &iter, NULL);

      gtk_tree_store_set (store, &iter,
                          COL_LABEL, output_item_get_label (item),
			  COL_ADDR, item,
                          COL_Y, view->y,
                          -1);

      /* Get the path of the new row. */
      path = gtk_tree_model_get_path (
        GTK_TREE_MODEL (store), &iter);
      gtk_tree_view_expand_row (view->overview, path, TRUE);
    }

  if (view->max_width < view_item->width)
    view->max_width = view_item->width;
  view->y += view_item->height;

  gtk_layout_set_size (view->output, view->max_width, view->y);

  if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      psppire_output_view_put__ (view, item->group.children[i], path);

  gtk_tree_path_free (path);
}

void
psppire_output_view_put (struct psppire_output_view *view,
                         const struct output_item *item)
{
  psppire_output_view_put__ (view, item, NULL);
}

static void
on_row_activate (GtkTreeView *overview,
                 GtkTreePath *path,
                 GtkTreeViewColumn *column,
                 struct psppire_output_view *view)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkAdjustment *vadj;
  GValue value = {0};
  double y, min, max;

  model = gtk_tree_view_get_model (overview);
  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;

  gtk_tree_model_get_value (model, &iter, COL_Y, &value);
  y = g_value_get_long (&value);
  g_value_unset (&value);

  vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view->output));
  min = gtk_adjustment_get_lower (vadj);
  max = gtk_adjustment_get_upper (vadj) - gtk_adjustment_get_page_size (vadj);
  if (y < min)
    y = min;
  else if (y > max)
    y = max;
  gtk_adjustment_set_value (vadj, y);
}

static void
on_style_updated (GtkWidget *toplevel, struct psppire_output_view *view)
{
  if (!view->n_items || !gtk_widget_get_window (GTK_WIDGET (view->output)))
    return;

  /* GTK+ fires this signal for trivial changes like the mouse moving in or out
     of the window.  Check whether the actual fsm options changed and
     re-render only if they did. */
  struct xr_fsm_style *style = get_xr_fsm_style (view);
  if (!view->style || !xr_fsm_style_equals (style, view->style))
    {
      xr_fsm_style_unref (view->style);
      view->style = xr_fsm_style_ref (style);
      rerender (view);
    }
  xr_fsm_style_unref (style);
}

enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_UTF8,
  SELECT_FMT_HTML,
  SELECT_FMT_SVG,
  SELECT_FMT_IMG,
  SELECT_FMT_ODT
};

static void
clear_rectangle (cairo_surface_t *surface,
                 double x0, double y0, double x1, double y1)
{
  cairo_t *cr = cairo_create (surface);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_new_path (cr);
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_fill (cr);
  cairo_destroy (cr);
}

static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  struct psppire_output_view *view = data;

  gsize length;
  gchar *text = NULL;
  struct output_driver *driver = NULL;
  char *filename;
  struct string_map options;
  struct temp_dir *td = NULL;

  if (view->selected_item == NULL)
    return;

  td = create_temp_dir ("pspp", NULL, false);
  if (td == NULL)
    {
      msg_error (errno, _("failed to create temporary directory during clipboard operation"));
      return;
    }
  filename = xasprintf ("%s/clip.tmp", td->dir_name);

  string_map_init (&options);
  string_map_insert (&options, "output-file", filename);

  switch (info)
    {
    case SELECT_FMT_UTF8:
      string_map_insert (&options, "box", "unicode");
      /* fall-through */

    case SELECT_FMT_TEXT:
      string_map_insert (&options, "format", "txt");
      string_map_insert (&options, "width", "1000");
      break;

    case SELECT_FMT_HTML:
      string_map_insert (&options, "format", "html");
      string_map_insert (&options, "borders", "false");
      string_map_insert (&options, "css", "false");
      break;

    case SELECT_FMT_SVG:
    case SELECT_FMT_IMG:
      /* see below */
      break;

    case SELECT_FMT_ODT:
      string_map_insert (&options, "format", "odt");
      break;

    default:
      g_warning ("unsupported clip target\n");
      goto finish;
      break;
    }

  if ((info == SELECT_FMT_IMG) ||
      (info == SELECT_FMT_SVG) )
    {
      GtkWidget *widget = view->selected_item->drawing_area;
      struct xr_fsm *fsm = g_object_get_data (G_OBJECT (widget), "fsm");

      GdkWindow *win = gtk_layout_get_bin_window (view->output);
      cairo_region_t *region = gdk_window_get_visible_region (win);
      GdkDrawingContext *ctx =  gdk_window_begin_draw_frame (win, region);
      cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

      int w, h;
      xr_fsm_measure (fsm, cr, &w, &h);

      gdk_window_end_draw_frame (win, ctx);
      cairo_region_destroy (region);

      cairo_surface_t *surface
        = (info == SELECT_FMT_SVG
           ? cairo_svg_surface_create (filename, w, h)
           : cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h));
      clear_rectangle (surface, 0, 0, w, h);
      cairo_t *cr2 = cairo_create (surface);
      xr_fsm_draw_all (fsm, cr2);
      cairo_destroy (cr2);
      if (info == SELECT_FMT_IMG)
        {
          GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface (surface,
                                                           0, 0, w, h);
          if (pixbuf)
            {
              gtk_selection_data_set_pixbuf (selection_data, pixbuf);
              g_object_unref (pixbuf);
            }
        }
      cairo_surface_destroy (surface);
    }
  else
    {
      driver = output_driver_create (&options);
      if (driver == NULL)
	goto finish;

      driver->class->submit (driver, view->selected_item->item);

      if (driver->class->flush)
	driver->class->flush (driver);

      /* Some drivers (eg: the odt one) don't write anything until they
	 are closed */
      output_driver_destroy (driver);
      driver = NULL;
    }

  if (info != SELECT_FMT_IMG
      && g_file_get_contents (filename, &text, &length, NULL))
    gtk_selection_data_set (selection_data,
                            gtk_selection_data_get_target (selection_data),
                            8, (const guchar *) text, length);

 finish:

  if (driver != NULL)
    output_driver_destroy (driver);

  g_free (text);

  unlink (filename);
  free (filename);
  cleanup_temp_dir (td);
}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
}

#define CBTARGETS                                           \
CT ( ctn1, "STRING",        0, SELECT_FMT_TEXT )            \
CT ( ctn2, "TEXT",          0, SELECT_FMT_TEXT )            \
CT ( ctn3, "COMPOUND_TEXT", 0, SELECT_FMT_TEXT )            \
CT ( ctn4, "text/plain",    0, SELECT_FMT_TEXT )            \
CT ( ctn5, "UTF8_STRING",   0, SELECT_FMT_UTF8 )            \
CT ( ctn6, "text/plain;charset=utf-8", 0, SELECT_FMT_UTF8 ) \
CT ( ctn7, "text/html",     0, SELECT_FMT_HTML )            \
CT ( ctn8, "image/svg+xml", 0, SELECT_FMT_SVG )

#define CT(ID, TARGET, FLAGS, INFO) static gchar ID[] = TARGET;
CBTARGETS
#undef CT
static gchar ctnlast[] = "application/vnd.oasis.opendocument.text";

static const GtkTargetEntry targets[] = {
#define CT(ID, TARGET, FLAGS, INFO) { ID, FLAGS, INFO },
  CBTARGETS
#undef CT
  { ctnlast, 0, SELECT_FMT_ODT }
};

static GtkTargetList *
build_target_list (const struct output_item *item)
{
  GtkTargetList *tl = gtk_target_list_new (targets, G_N_ELEMENTS (targets));
  g_return_val_if_fail (tl, NULL);
  if (item->type == OUTPUT_ITEM_TABLE || item->type == OUTPUT_ITEM_CHART)
    gtk_target_list_add_image_targets (tl, SELECT_FMT_IMG, TRUE);
  return tl;
}

static void
on_copy (struct psppire_output_view *view)
{
  GtkWidget *widget = GTK_WIDGET (view->overview);
  GtkClipboard *cb = gtk_widget_get_clipboard (widget, GDK_SELECTION_CLIPBOARD);

  struct output_view_item *ov_item = find_selected_item (view);
  if (ov_item == NULL)
    return;
  view->selected_item = ov_item;
  GtkTargetList *tl = build_target_list (ov_item->item);
  g_return_if_fail (tl);
  gint no_of_targets = 0;
  GtkTargetEntry *ta = gtk_target_table_new_from_list (tl, &no_of_targets);
  g_return_if_fail (ta);
  if (!gtk_clipboard_set_with_data (cb, ta, no_of_targets,
                                    clipboard_get_cb, clipboard_clear_cb,
                                    view))
    clipboard_clear_cb (cb, view);

  gtk_target_list_unref (tl);
  gtk_target_table_free (ta,no_of_targets);
}

static void
on_size_allocate (GtkWidget    *widget,
                  GdkRectangle *allocation,
                  struct psppire_output_view *view)
{
  view->render_width = MAX (300, allocation->width);
  rerender (view);
}

static void
on_realize (GtkWidget *overview, GObject *view)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (overview));

  GAction *copy_action = g_action_map_lookup_action (G_ACTION_MAP (toplevel),
						     "copy");

  GAction *select_all_action = g_action_map_lookup_action (G_ACTION_MAP (toplevel),
							   "select-all");

  g_object_set (copy_action, "enabled", FALSE, NULL);
  g_object_set (select_all_action, "enabled", FALSE, NULL);

  g_signal_connect_swapped (copy_action, "activate",
                            G_CALLBACK (on_copy), view);

}

struct psppire_output_view *
psppire_output_view_new (GtkLayout *output, GtkTreeView *overview)
{
  struct psppire_output_view *view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;

  GtkTreeModel *model;

  view = xmalloc (sizeof *view);
  *view = (struct psppire_output_view) {
    .object_spacing = 10,
    .output = output,
    .overview = overview,
    .toplevel = gtk_widget_get_toplevel (GTK_WIDGET (output)),
  };

  g_signal_connect (output, "draw", G_CALLBACK (layout_draw_callback), NULL);

  g_signal_connect (output, "style-updated", G_CALLBACK (on_style_updated), view);

  g_signal_connect (output, "size-allocate", G_CALLBACK (on_size_allocate), view);

  gtk_widget_add_events (GTK_WIDGET (output), GDK_BUTTON_PRESS_MASK);
  g_signal_connect (output, "button-press-event",
		    G_CALLBACK (off_item_button_press_event_cb), view);

  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (output)),
			       GTK_STYLE_CLASS_VIEW);

  if (overview)
    {
      g_signal_connect (overview, "realize", G_CALLBACK (on_realize), view);

      model = GTK_TREE_MODEL (gtk_tree_store_new (
                                N_COLS,
                                G_TYPE_STRING,  /* COL_LABEL */
                                G_TYPE_POINTER, /* COL_ADDR */
                                G_TYPE_LONG));  /* COL_Y */
      gtk_tree_view_set_model (overview, model);
      g_object_unref (model);

      column = gtk_tree_view_column_new ();
      gtk_tree_view_append_column (GTK_TREE_VIEW (overview), column);
      renderer = gtk_cell_renderer_text_new ();
      gtk_tree_view_column_pack_start (column, renderer, TRUE);
      gtk_tree_view_column_add_attribute (column, renderer, "text", COL_LABEL);

      g_signal_connect (GTK_TREE_VIEW (overview),
                        "row-activated", G_CALLBACK (on_row_activate), view);
    }

  return view;
}

void
psppire_output_view_destroy (struct psppire_output_view *view)
{
  size_t i;

  if (!view)
    return;

  g_signal_handlers_disconnect_by_func (view->output,
                                        G_CALLBACK (on_style_updated), view);

  xr_fsm_style_unref (view->style);

  for (i = 0; i < view->n_items; i++)
    output_item_unref (view->items[i].item);
  free (view->items);
  view->items = NULL;
  view->n_items = view->allocated_items = 0;

  if (view->print_settings != NULL)
    g_object_unref (view->print_settings);

  free (view);
}

void
psppire_output_view_clear (struct psppire_output_view *view)
{
  size_t i;

  view->max_width = 0;
  view->y = 0;

  for (i = 0; i < view->n_items; i++)
    {
      gtk_container_remove (GTK_CONTAINER (view->output),
                            view->items[i].drawing_area);
      output_item_unref (view->items[i].item);
    }
  free (view->items);
  view->items = NULL;
  view->n_items = view->allocated_items = 0;
}

/* Export. */

void
psppire_output_view_export (struct psppire_output_view *view,
                            struct string_map *options)
{
  struct output_driver *driver;

  driver = output_driver_create (options);
  if (driver)
    {
      size_t i;

      for (i = 0; i < view->n_items; i++)
        driver->class->submit (driver, view->items[i].item);
      output_driver_destroy (driver);
    }
}

/* Print. */

static cairo_t *
get_cairo_context_from_print_context (GtkPrintContext *context)
{
  cairo_t *cr = gtk_print_context_get_cairo_context (context);
  return cairo_reference (cr);
}

static void
create_xr_print_driver (GtkPrintContext *context, struct psppire_output_view *view)
{
  GtkPageSetup *ps = gtk_print_context_get_page_setup (context);

  enum { H = TABLE_HORZ, V = TABLE_VERT };
  int paper[TABLE_N_AXES] = {
    [H] = gtk_page_setup_get_paper_width (ps, GTK_UNIT_POINTS) * XR_POINT,
    [V] = gtk_page_setup_get_paper_height (ps, GTK_UNIT_POINTS) * XR_POINT,
  };

  /* These are all 1/2 inch.  The "margins" that GTK+ gives us are useless:
     they are the printer's imagable area. */
  int margins[TABLE_N_AXES][2] = {
    [H][0] = XR_POINT * 36,
    [H][1] = XR_POINT * 36,
    [V][0] = XR_POINT * 36,
    [V][1] = XR_POINT * 36,
  };

  double size[TABLE_N_AXES];
  for (int a = 0; a < TABLE_N_AXES; a++)
    size[a] = paper[a] - margins[a][0] - margins[a][1];

  view->page_style = xmalloc (sizeof *view->page_style);
  *view->page_style = (struct xr_page_style) {
    .ref_cnt = 1,

    .margins = {
      [H] = { margins[H][0], margins[H][1] },
      [V] = { margins[V][0], margins[V][1] },
    },
    .initial_page_number = 1,
  };

  view->fsm_style = xmalloc (sizeof *view->fsm_style);
  *view->fsm_style = (struct xr_fsm_style) {
    .ref_cnt = 1,

    .size = { [H] = size[H], [V] = size[V] },
    .min_break = { [H] = size[H] / 2, [V] = size[V] / 2 },
    .font = pango_font_description_from_string ("Sans Serif 10"),
    .fg = CELL_COLOR_BLACK,
    .use_system_colors = false,
    .object_spacing = 12 * XR_POINT,
    .font_resolution = 72.0
  };

  view->pager = xr_pager_create (view->page_style, view->fsm_style);
}

static gboolean
paginate (GtkPrintOperation *operation,
	  GtkPrintContext   *context,
	  struct psppire_output_view *view)
{
  if (view->paginated)
    {
      /* Sometimes GTK+ emits this signal again even after pagination is
         complete.  Don't let that screw up printing. */
      return TRUE;
    }
  else if (view->print_item < view->n_items)
    {
      xr_pager_add_item (view->pager, view->items[view->print_item++].item);
      while (xr_pager_needs_new_page (view->pager))
	{
	  xr_pager_add_page (view->pager,
                             get_cairo_context_from_print_context (context));
	  view->print_n_pages ++;
	}
      return FALSE;
    }
  else
    {
      gtk_print_operation_set_n_pages (operation, MAX (1, view->print_n_pages));

      /* Re-create the driver to do the real printing. */
      xr_pager_destroy (view->pager);
      view->pager = xr_pager_create (view->page_style, view->fsm_style);
      view->print_item = 0;
      view->paginated = TRUE;

      return TRUE;
    }
}

static void
begin_print (GtkPrintOperation *operation,
	     GtkPrintContext   *context,
	     struct psppire_output_view *view)
{
  create_xr_print_driver (context, view);

  view->print_item = 0;
  view->print_n_pages = 0;
  view->paginated = FALSE;
}

static void
end_print (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   struct psppire_output_view *view)
{
  xr_pager_destroy (view->pager);
  view->pager = NULL;
}


static void
draw_page (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   gint               page_number,
	   struct psppire_output_view *view)
{
  xr_pager_add_page (view->pager,
                     get_cairo_context_from_print_context (context));
  while (!xr_pager_needs_new_page (view->pager)
         && view->print_item < view->n_items)
    xr_pager_add_item (view->pager, view->items [view->print_item++].item);
}


void
psppire_output_view_print (struct psppire_output_view *view,
                           GtkWindow *parent_window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (view->print_settings != NULL)
    gtk_print_operation_set_print_settings (print, view->print_settings);

  gtk_print_operation_set_use_full_page (print, TRUE);
  gtk_print_operation_set_unit (print, GTK_UNIT_POINTS);

  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), view);
  g_signal_connect (print, "end_print",   G_CALLBACK (end_print),   view);
  g_signal_connect (print, "paginate",    G_CALLBACK (paginate),    view);
  g_signal_connect (print, "draw_page",   G_CALLBACK (draw_page),   view);

  res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 parent_window, NULL);

  if (res == GTK_PRINT_OPERATION_RESULT_APPLY)
    {
      if (view->print_settings != NULL)
        g_object_unref (view->print_settings);
      view->print_settings = g_object_ref (gtk_print_operation_get_print_settings (print));
    }

  g_object_unref (print);
}
