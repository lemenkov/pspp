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

#include <errno.h>
#include <stdbool.h>

#if HAVE_RSVG
#include "librsvg/rsvg.h"
#endif
#include "libpspp/assertion.h"
#include "libpspp/string-map.h"
#include "output/cairo.h"
#include "output/driver-provider.h"
#include "output/driver.h"
#include "output/chart-item.h"
#include "output/group-item.h"
#include "output/message-item.h"
#include "output/output-item.h"
#include "output/output-item-provider.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "gl/c-xvasprintf.h"
#include "gl/minmax.h"
#include "gl/tmpdir.h"
#include "gl/xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)

struct output_view_item
  {
    struct output_item *item;
    GtkWidget *drawing_area;
  };

struct psppire_output_view
  {
    struct xr_driver *xr;
    int font_height;

    GtkLayout *output;
    int render_width;
    int max_width;
    glong y;

    struct string_map render_opts;
    GtkTreeView *overview;
    GtkTreePath *cur_group;

    GtkWidget *toplevel;

    guint buttontime; /* Time of the button event */

    struct output_view_item *items;
    size_t n_items, allocated_items;
    struct output_view_item *selected_item;

    /* Variables pertaining to printing */
    GtkPrintSettings *print_settings;
    struct xr_driver *print_xrd;
    int print_item;
    int print_n_pages;
    gboolean paginated;
  };

enum
  {
    COL_NAME,                   /* Table name. */
    COL_ADDR,                   /* Pointer to the table */
    COL_Y,                      /* Y position of top of name. */
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

  struct xr_rendering *r = g_object_get_data (G_OBJECT (widget), "rendering");

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
  xr_rendering_draw (r, cr, clip.x, clip.y,
                     clip.x + clip.width, clip.y + clip.height);

  return TRUE;
}

static void
free_rendering (gpointer rendering_)
{
  struct xr_rendering *rendering = rendering_;
  xr_rendering_destroy (rendering);
}

static void
get_xr_options (struct psppire_output_view *view, struct string_map *options)
{
  string_map_clear (options);

  GtkStyleContext *context
    = gtk_widget_get_style_context (GTK_WIDGET (view->output));
  GtkStateFlags state = gtk_widget_get_state_flags (GTK_WIDGET (view->output));

  /* Use GTK+ default font as proportional font. */
  PangoFontDescription *font_desc;
  gtk_style_context_get (context, state, "font", &font_desc, NULL);
  char *font_name = pango_font_description_to_string (font_desc);
  string_map_insert (options, "prop-font", font_name);
  g_free (font_name);

  /* Derived emphasized font from proportional font. */
  pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
  font_name = pango_font_description_to_string (font_desc);
  string_map_insert (options, "emph-font", font_name);
  g_free (font_name);
  pango_font_description_free (font_desc);

  /* Pretend that the "page" has a reasonable width and a very big length,
     so that most tables can be conveniently viewed on-screen with vertical
     scrolling only.  (The length should not be increased very much because
     it is already close enough to INT_MAX when expressed as thousands of a
     point.) */
  string_map_insert_nocopy (options, xstrdup ("paper-size"),
                            xasprintf ("%dx1000000pt", view->render_width));
  string_map_insert (options, "left-margin", "0");
  string_map_insert (options, "right-margin", "0");
  string_map_insert (options, "top-margin", "0");
  string_map_insert (options, "bottom-margin", "0");
}

static void
create_xr (struct psppire_output_view *view)
{
  get_xr_options (view, &view->render_opts);

  struct string_map options;
  string_map_clone (&options, &view->render_opts);

  GdkWindow *win = gtk_layout_get_bin_window (view->output);
  cairo_region_t *region = gdk_window_get_visible_region (win);
  GdkDrawingContext *ctx = gdk_window_begin_draw_frame (win, region);
  cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

  view->xr = xr_driver_create (cr, &options);
  string_map_destroy (&options);

  struct text_item *text_item = text_item_create (TEXT_ITEM_LOG, "X");
  struct xr_rendering *r
    = xr_rendering_create (view->xr, text_item_super (text_item), cr);
  xr_rendering_measure (r, NULL, &view->font_height);
  xr_rendering_destroy (r);
  text_item_unref (text_item);

  gdk_window_end_draw_frame (win, ctx);
  cairo_region_destroy (region);
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
      if GTK_IS_WIDGET (widget)
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
                     GtkWidget *drawing_area, struct xr_rendering *r,
                     int tw, int th, const struct output_item *item)
{
  struct string_map options = STRING_MAP_INITIALIZER (options);
  string_map_insert (&options, "transparent", "true");
  string_map_insert (&options, "systemcolors", "true");
  xr_rendering_apply_options (r, &options);

  g_object_set_data_full (G_OBJECT (drawing_area),
                          "rendering", r, free_rendering);
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

  if (view->xr == NULL)
    create_xr (view);

  GdkWindow *win = gtk_layout_get_bin_window (view->output);
  cairo_region_t *region = gdk_window_get_visible_region (win);
  GdkDrawingContext *ctx =  gdk_window_begin_draw_frame (win, region);
  cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

  view->y = 0;
  view->max_width = 0;
  for (item = view->items; item < &view->items[view->n_items]; item++)
    {
      struct xr_rendering *r;
      GtkAllocation alloc;
      int tw, th;

      if (view->y > 0)
        view->y += view->font_height / 2;

      if (is_group_open_item (item->item))
        continue;

      r = xr_rendering_create (view->xr, item->item, cr);
      if (r == NULL)
        {
          g_warn_if_reached ();
          continue;
        }

      xr_rendering_measure (r, &tw, &th);

      gint xpos = get_xpos (view, tw);

      if (!item->drawing_area)
        {
          item->drawing_area = gtk_drawing_area_new ();
          create_drawing_area (view, item->drawing_area, r, tw, th, item->item);
        }
      else
        {
          g_object_set_data_full (G_OBJECT (item->drawing_area),
                                  "rendering", r, free_rendering);
          gtk_widget_set_size_request (item->drawing_area, tw, th);
          gtk_layout_move (view->output, item->drawing_area, xpos, view->y);
        }

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
                       view->max_width + view->font_height,
                       view->y + view->font_height);

  gdk_window_end_draw_frame (win, ctx);
  cairo_region_destroy (region);
}


void
psppire_output_view_put (struct psppire_output_view *view,
                         const struct output_item *item)
{
  struct output_view_item *view_item;
  GtkWidget *drawing_area;
  struct string name;
  int tw, th;

  if (is_group_close_item (item))
    {
      if (view->cur_group)
        {
          if (!gtk_tree_path_up (view->cur_group))
            {
              gtk_tree_path_free (view->cur_group);
              view->cur_group = NULL;
            }
        }
      return;
    }
  else if (is_text_item (item))
    {
      const struct text_item *text_item = to_text_item (item);
      const char *text = text_item_get_text (text_item);
      if (text[0] == '\0')
        return;
    }

  if (view->n_items >= view->allocated_items)
    view->items = x2nrealloc (view->items, &view->allocated_items,
                                sizeof *view->items);
  view_item = &view->items[view->n_items++];
  view_item->item = output_item_ref (item);
  view_item->drawing_area = NULL;

  GdkWindow *win = gtk_widget_get_window (GTK_WIDGET (view->output));
  if (is_group_open_item (item))
    tw = th = 0;
  else if (win)
    {
      view_item->drawing_area = drawing_area = gtk_drawing_area_new ();

      if (view->xr == NULL)
        create_xr (view);

      cairo_region_t *region = gdk_window_get_visible_region (win);
      GdkDrawingContext *ctx = gdk_window_begin_draw_frame (win, region);
      cairo_t *cr = gdk_drawing_context_get_cairo_context (ctx);

      if (view->y > 0)
        view->y += view->font_height / 2;

      struct xr_rendering *r = xr_rendering_create (view->xr, item, cr);
      if (r == NULL)
	{
	  gdk_window_end_draw_frame (win, ctx);
	  cairo_region_destroy (region);
	  return;
	}

      xr_rendering_measure (r, &tw, &th);
      create_drawing_area (view, drawing_area, r, tw, th, item);
      gdk_window_end_draw_frame (win, ctx);
      cairo_region_destroy (region);
    }
  else
    tw = th = 0;

  if (view->overview)
    {
      GtkTreeStore *store = GTK_TREE_STORE (
        gtk_tree_view_get_model (view->overview));

      ds_init_empty (&name);

      /* Create a new node in the tree and puts a reference to it in 'iter'. */
      GtkTreeIter iter;
      GtkTreeIter parent;
      if (view->cur_group
          && gtk_tree_path_get_depth (view->cur_group) > 0
          && gtk_tree_model_get_iter (GTK_TREE_MODEL (store),
                                      &parent, view->cur_group))
        gtk_tree_store_append (store, &iter, &parent);
      else
        gtk_tree_store_append (store, &iter, NULL);

      if (is_group_open_item (item))
        {
          gtk_tree_path_free (view->cur_group);
          view->cur_group = gtk_tree_model_get_path (GTK_TREE_MODEL (store),
                                                     &iter);
        }

      ds_clear (&name);
      if (is_text_item (item))
        {
          const struct text_item *text_item = to_text_item (item);
          ds_put_cstr (&name, text_item_type_to_string (
                         text_item_get_type (text_item)));
        }
      else if (is_message_item (item))
        {
          const struct message_item *msg_item = to_message_item (item);
          const struct msg *msg = message_item_get_msg (msg_item);
          ds_put_format (&name, "%s: %s", _("Message"),
                         msg_severity_to_string (msg->severity));
        }
      else if (is_table_item (item))
        {
          const struct table_item_text *title
            = table_item_get_title (to_table_item (item));
          if (title != NULL)
            ds_put_format (&name, "Table: %s", title->content);
          else
            ds_put_cstr (&name, "Table");
        }
      else if (is_chart_item (item))
        {
          const char *s = chart_item_get_title (to_chart_item (item));
          if (s != NULL)
            ds_put_format (&name, "Chart: %s", s);
          else
            ds_put_cstr (&name, "Chart");
        }
      else if (is_group_open_item (item))
        ds_put_cstr (&name, to_group_open_item (item)->command_name);
      gtk_tree_store_set (store, &iter,
                          COL_NAME, ds_cstr (&name),
			  COL_ADDR, item,
                          COL_Y, (view->y),
                          -1);
      ds_destroy (&name);

      GtkTreePath *path = gtk_tree_model_get_path (
        GTK_TREE_MODEL (store), &iter);
      gtk_tree_view_expand_row (view->overview, path, TRUE);
      gtk_tree_path_free (path);
    }

  if (view->max_width < tw)
    view->max_width = tw;
  view->y += th;

  gtk_layout_set_size (view->output, view->max_width, view->y);
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
     of the window.  Check whether the actual rendering options changed and
     re-render only if they did. */
  struct string_map options = STRING_MAP_INITIALIZER (options);
  get_xr_options (view, &options);
  if (!string_map_equals (&options, &view->render_opts))
    {
      xr_driver_destroy (view->xr);
      view->xr = NULL;

      rerender (view);
    }
  string_map_destroy (&options);
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

/* GNU Hurd doesn't have PATH_MAX.  Use a fallback.
   Temporary directory names are usually not that long.  */
#ifndef PATH_MAX
# define PATH_MAX 1024
#endif

/* Returns a pixbuf from a svg file      */
/* You must unref the pixbuf after usage */
static GdkPixbuf *
derive_pixbuf_from_svg (const char *filename)
{
  GError *err = NULL;
  GdkPixbuf *pixbuf = NULL;
#if HAVE_RSVG
  RsvgHandle *handle = rsvg_handle_new_from_file (filename, &err);
  if (err == NULL)
    {
      rsvg_handle_set_dpi (handle, 300.0);
      pixbuf = rsvg_handle_get_pixbuf (handle);
      g_object_unref (handle);
    }
#else
  pixbuf = gdk_pixbuf_new_from_file (filename, &err);
#endif
  if (err != NULL)
    {
      msg (ME, _("Could not open file %s during copy operation: %s"),
	   filename, err->message);
      g_error_free (err);
      return NULL;
    }
  return pixbuf;
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
  char dirname[PATH_MAX], *filename;
  struct string_map options;

  if (view->selected_item == NULL)
    return;

  if (path_search (dirname, sizeof dirname, NULL, NULL, true)
      || mkdtemp (dirname) == NULL)
    {
      msg_error (errno, _("failed to create temporary directory during clipboard operation"));
      return;
    }
  filename = xasprintf ("%s/clip.tmp", dirname);

  string_map_init (&options);
  string_map_insert (&options, "output-file", filename);

  switch (info)
    {
    case SELECT_FMT_UTF8:
      string_map_insert (&options, "box", "unicode");
      /* fall-through */

    case SELECT_FMT_TEXT:
      string_map_insert (&options, "format", "txt");
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
      struct xr_rendering *r = g_object_get_data (G_OBJECT (widget), "rendering");
      xr_draw_svg_file (r, filename);
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

  if (info == SELECT_FMT_IMG)
    {
      GdkPixbuf *pixbuf = derive_pixbuf_from_svg (filename);
      if (pixbuf)
	{
	  gtk_selection_data_set_pixbuf (selection_data, pixbuf);
	  g_object_unref (pixbuf);
	}
    }
  else if (g_file_get_contents (filename, &text, &length, NULL))
    gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
			    8,
			    (const guchar *) text, length);

 finish:

  if (driver != NULL)
    output_driver_destroy (driver);

  g_free (text);

  unlink (filename);
  free (filename);
  rmdir (dirname);
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
  if (is_table_item (item) ||
      is_chart_item (item))
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
  view->xr = NULL;
  view->font_height = 0;
  view->output = output;
  view->render_width = 0;
  view->max_width = 0;
  view->y = 0;
  string_map_init (&view->render_opts);
  view->overview = overview;
  view->cur_group = NULL;
  view->toplevel = gtk_widget_get_toplevel (GTK_WIDGET (output));
  view->buttontime = 0;
  view->items = NULL;
  view->n_items = view->allocated_items = 0;
  view->selected_item = NULL;
  view->print_settings = NULL;
  view->print_xrd = NULL;
  view->print_item = 0;
  view->print_n_pages = 0;
  view->paginated = FALSE;

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
                                G_TYPE_STRING,  /* COL_NAME */
                                G_TYPE_POINTER, /* COL_ADDR */
                                G_TYPE_LONG));  /* COL_Y */
      gtk_tree_view_set_model (overview, model);
      g_object_unref (model);

      column = gtk_tree_view_column_new ();
      gtk_tree_view_append_column (GTK_TREE_VIEW (overview), column);
      renderer = gtk_cell_renderer_text_new ();
      gtk_tree_view_column_pack_start (column, renderer, TRUE);
      gtk_tree_view_column_add_attribute (column, renderer, "text", COL_NAME);

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

  string_map_destroy (&view->render_opts);

  for (i = 0; i < view->n_items; i++)
    output_item_unref (view->items[i].item);
  free (view->items);
  view->items = NULL;
  view->n_items = view->allocated_items = 0;

  if (view->print_settings != NULL)
    g_object_unref (view->print_settings);

  xr_driver_destroy (view->xr);

  if (view->cur_group)
    gtk_tree_path_free (view->cur_group);

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

  /*
    For all platforms except windows, gtk_print_context_get_dpi_[xy] returns 72.
    Windows returns 600.
  */
  double xres = gtk_print_context_get_dpi_x (context);
  double yres = gtk_print_context_get_dpi_y (context);

  /* This means that the cairo context now has its dimensions in Points */
  cairo_scale (cr, xres / 72.0, yres / 72.0);

  return cr;
}


static void
create_xr_print_driver (GtkPrintContext *context, struct psppire_output_view *view)
{
  struct string_map options;
  GtkPageSetup *page_setup;
  double width, height;
  double left_margin;
  double right_margin;
  double top_margin;
  double bottom_margin;

  page_setup = gtk_print_context_get_page_setup (context);
  width = gtk_page_setup_get_paper_width (page_setup, GTK_UNIT_MM);
  height = gtk_page_setup_get_paper_height (page_setup, GTK_UNIT_MM);
  left_margin = gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM);
  right_margin = gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM);
  top_margin = gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM);
  bottom_margin = gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM);

  string_map_init (&options);
  string_map_insert_nocopy (&options, xstrdup ("paper-size"),
                            c_xasprintf("%.2fx%.2fmm", width, height));
  string_map_insert_nocopy (&options, xstrdup ("left-margin"),
                            c_xasprintf ("%.2fmm", left_margin));
  string_map_insert_nocopy (&options, xstrdup ("right-margin"),
                            c_xasprintf ("%.2fmm", right_margin));
  string_map_insert_nocopy (&options, xstrdup ("top-margin"),
                            c_xasprintf ("%.2fmm", top_margin));
  string_map_insert_nocopy (&options, xstrdup ("bottom-margin"),
                            c_xasprintf ("%.2fmm", bottom_margin));

  view->print_xrd = xr_driver_create (get_cairo_context_from_print_context (context), &options);

  string_map_destroy (&options);
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
      xr_driver_output_item (view->print_xrd,
                             view->items[view->print_item++].item);
      while (xr_driver_need_new_page (view->print_xrd))
	{
	  xr_driver_next_page (view->print_xrd, get_cairo_context_from_print_context (context));
	  view->print_n_pages ++;
	}
      return FALSE;
    }
  else
    {
      gtk_print_operation_set_n_pages (operation, view->print_n_pages);

      /* Re-create the driver to do the real printing. */
      xr_driver_destroy (view->print_xrd);
      create_xr_print_driver (context, view);
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
  view->print_n_pages = 1;
  view->paginated = FALSE;
}

static void
end_print (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   struct psppire_output_view *view)
{
  xr_driver_destroy (view->print_xrd);
}


static void
draw_page (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   gint               page_number,
	   struct psppire_output_view *view)
{
  xr_driver_next_page (view->print_xrd, get_cairo_context_from_print_context (context));
  while (!xr_driver_need_new_page (view->print_xrd)
         && view->print_item < view->n_items)
    xr_driver_output_item (view->print_xrd, view->items [view->print_item++].item);
}


void
psppire_output_view_print (struct psppire_output_view *view,
                           GtkWindow *parent_window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (view->print_settings != NULL)
    gtk_print_operation_set_print_settings (print, view->print_settings);

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

struct psppire_output_view_driver
  {
    struct output_driver driver;
    struct psppire_output_view *view;
  };

static struct psppire_output_view_driver *
psppire_output_view_driver_cast (struct output_driver *driver)
{
  return UP_CAST (driver, struct psppire_output_view_driver, driver);
}

static void
psppire_output_view_submit (struct output_driver *this,
                            const struct output_item *item)
{
  struct psppire_output_view_driver *povd = psppire_output_view_driver_cast (this);

  if (is_table_item (item))
    psppire_output_view_put (povd->view, item);
}

static struct output_driver_class psppire_output_view_driver_class =
  {
    "PSPPIRE Output View",      /* name */
    NULL,                       /* destroy */
    psppire_output_view_submit, /* submit */
    NULL,                       /* flush */
  };

void
psppire_output_view_register_driver (struct psppire_output_view *view)
{
  struct psppire_output_view_driver *povd;
  struct output_driver *d;

  povd = xzalloc (sizeof *povd);
  povd->view = view;
  d = &povd->driver;
  output_driver_init (d, &psppire_output_view_driver_class, "PSPPIRE Output View",
                      SETTINGS_DEVICE_UNFILTERED);
  output_driver_register (d);
}
