/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2023  Free Software Foundation

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

#include "psppire-dialog-action-ctables.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "psppire-dict.h"
#include "libpspp/str.h"
#include "libpspp/llx.h"

#include "psppire-dictview.h"

#include "output/cairo-fsm.h"
#include "output/output-item.h"
#include "output/pivot-table.h"
#include "data/value-labels.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static struct xr_fsm_style *get_xr_fsm_style (GtkWidget *w);
static void psppire_dialog_action_ctables_class_init
     (PsppireDialogActionCtablesClass *class);

G_DEFINE_TYPE (PsppireDialogActionCtables, psppire_dialog_action_ctables,
               PSPPIRE_TYPE_DIALOG_ACTION);

/* Create the basis of a table.  This table contasins just two dimensions
   and nothing else. */
static struct pivot_table *make_table (void)
{
  struct pivot_table *table = pivot_table_create ("$ctables-dialog-template");
  table->show_title = false;
  table->show_caption = false;

  pivot_dimension_create (table, PIVOT_AXIS_ROW, "row");
  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, "column");

  return table;
}

/* Create a new text leaf in CAT with the string TEXT iff there isn't already
   such a leaf */
static int
category_create_leaf_once (struct pivot_category *cat, const char *text)
{
  for (int s = 0; s < cat->n_subs; ++s)
    {
      if (cat->subs[s]->name->type == PIVOT_VALUE_TEXT)
        {
        if (0 == strcmp (cat->subs[s]->name->text.id, text))
          return -1;
        }
      else
        return -1;
    }

  return pivot_category_create_leaf (cat, pivot_value_new_text (text));
}


/* Add a new pivot category to PARENT.

   CHILDREN must be NULL or a list of pivot_values.  CHILD_NAME is the name of
   the new category.

   If CHILDREN is NULL or a empty, then the new category will be a leaf with
   the name CHILD_NAME.  Otherwise the new category will be a group and the
   contents of CHILDREN will be the leaves of that group.
 */
static void
add_child_category (struct pivot_category *parent, const char *child_name,
                   struct llx_list *children)
{
  if (children && llx_is_empty (children))
    {
      pivot_category_create_leaf (parent, pivot_value_new_text (child_name));
      return;
    }

  for (struct llx *llx = llx_head (children); llx != llx_null (children);
       llx = llx_next (llx))
    {
      struct pivot_value *value = llx_data (llx);
      struct pivot_category *pc = pivot_category_create_group (parent, child_name);
      pivot_category_create_leaf (pc, pivot_value_clone (value));
    }
}

/*
  Supplement TABLE with a category to hold cells which could contain summary
  data for VAR.   PRIMARY_AXIS is the TABLE's axis which will contain the
  heading for the variable itself.  The perpendicular axis will contain the
  headings of the summary functions.

  DICT is the dictionary which contains VAR and all previously added variables.

  Returns TRUE if successfull.  False otherwise.
 */
static gboolean
augment_template_table (struct pivot_table *table,
                        enum pivot_axis_type primary_axis,
                        const struct variable *var, const struct dictionary *dict)
{
  g_return_val_if_fail (table, FALSE);
  struct pivot_dimension *axis0 ;
  struct pivot_dimension *axis1 ;

  g_assert (primary_axis == PIVOT_AXIS_ROW || primary_axis == PIVOT_AXIS_COLUMN);

  if (primary_axis == PIVOT_AXIS_ROW)
    {
      axis0 = table->dimensions[0];
      axis1 = table->dimensions[1];
    }
  else
    {
      axis0 = table->dimensions[1];
      axis1 = table->dimensions[0];
    }

  const enum measure m = var_get_measure (var);
  struct pivot_value *pv_var = pivot_value_new_variable (var);

  /* Displaying the variable label in the template tends to make it too verbose
     and hard to read.  So we remove the label here. */
  free (pv_var->variable.var_label);
  pv_var->variable.var_label = NULL;

  if (m == MEASURE_NOMINAL || m == MEASURE_ORDINAL)
    {
      /* If this axis already contains headings for summary functions,
         these need to be transferred to a sub category below the one
         that we are adding.   So make a list of them here.  */
      struct llx_list summary_categories;
      llx_init (&summary_categories);
      for (int s = 0; s < axis0->root->n_subs; ++s)
        {
          if (axis0->root->subs[s]->name->type == PIVOT_VALUE_TEXT)
            {
              struct pivot_value *subtext = axis0->root->subs[s]->name;
              llx_push_tail (&summary_categories, subtext, &llx_malloc_mgr);
            }
        }
      struct pivot_category *cat =
        pivot_category_create_group__ (axis0->root, pv_var);

      /* The value labels (if any) form the categories */
      if (var_has_value_labels (var))
        {
          const struct val_labs *labels = var_get_value_labels (var);
          size_t count = val_labs_count (labels);

          const struct val_lab **array = val_labs_sorted (labels);
          for (int i = 0; i < count; ++i)
            {
              add_child_category (cat, array[i]->label, &summary_categories);
            }
          free (array);
        }
      else
        {
          add_child_category (cat, N_("Category 0"), &summary_categories);
          add_child_category (cat, N_("Category 1"), &summary_categories);
        }
      category_create_leaf_once (axis1->root, N_("Count"));

      llx_destroy (&summary_categories, NULL, NULL, &llx_malloc_mgr);
    }
  else
    {
      /* When adding a scalar variable we must check that the other axis
         doesn't also contain scalar variables.  This is not allowed.  */
      for (int s = 0; s < axis1->root->n_subs; ++s)
        {
          const struct pivot_value *name = axis1->root->subs[s]->name;
          if (name->type == PIVOT_VALUE_VARIABLE)
            {
              const struct variable *v
                = dict_lookup_var (dict, name->variable.var_name);
              g_return_val_if_fail (v, FALSE);
              enum measure meas = var_get_measure (v);
              if (meas != MEASURE_NOMINAL && meas != MEASURE_ORDINAL)
                {
                  pivot_value_destroy (pv_var);
                  return FALSE;
                }
            }
        }
      pivot_category_create_leaf (axis0->root, pv_var);
      category_create_leaf_once (axis1->root, N_("Mean"));
    }

  return  TRUE;
}

static gboolean
dialog_state_valid (PsppireDialogAction *pda)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (pda);

  if (!act->table)
    return FALSE;

  if (act->table->n_dimensions < 2)
    return FALSE;

  for (int d = 0; d < act->table->n_dimensions; ++d)
    {
      if (act->table->dimensions[d]->root->n_subs <= 0)
        return FALSE;
    }

  return TRUE;
}

static void
refresh (PsppireDialogAction *pda)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (pda);

  output_item_unref (act->graphic);
  act->graphic = NULL;

  if (act->table)
    pivot_table_unref (act->table);
  act->table = make_table ();

  gtk_widget_queue_draw (act->canvas);
  act->dragged_variable = NULL;
}

static gboolean
pad_draw (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  guint width = gtk_widget_get_allocated_width (widget);
  guint height = gtk_widget_get_allocated_height (widget);

  gtk_render_background (context, cr, 0, 0, width, height);

  cairo_rectangle (cr, 2, 2, width - 5, height - 5);

  gtk_style_context_get_color (context,
                               GTK_STATE_FLAG_DROP_ACTIVE,
                               &color);

  gdk_cairo_set_source_rgba (cr, &color);

  const double dashes[] = {10.0, 2.0};
  cairo_set_dash (cr, dashes, 2, 0.0);
  cairo_stroke (cr);

  cairo_rectangle (cr, 3, 3, width - 7, height - 7);
  color.red *= 0.5;
  color.green *= 0.5;
  color.blue *= 0.5;
  color.alpha *= 0.25;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_fill (cr);

 return FALSE;
}

static void
drag_begin (PsppireDictView  *widget,
            GdkDragContext  *context,
            PsppireDialogActionCtables *act)
{
  act->dragged_variable =
    psppire_dict_view_get_selected_variable (widget);

  if (!act->dragged_variable)
    {
      gtk_drag_cancel (context);
      return;
    }

  /* Set the icon to be displayed during dragging operation */
  enum measure m = var_get_measure (act->dragged_variable);
  struct fmt_spec fmt =   var_get_print_format (act->dragged_variable);
  const char *stock_id = get_var_measurement_stock_id (fmt.type, m);

  gtk_drag_set_icon_name (context, stock_id, 0, 0);
}

static void
drag_end (PsppireDictView  *widget,
          GdkDragContext  *context,
          PsppireDialogActionCtables *act)
{
  act->dragged_variable = NULL;
}

static gboolean
drag_failed (GtkWidget      *widget,
             GdkDragContext *context,
             GtkDragResult   result,
             PsppireDialogActionCtables *act)
{
  act->dragged_variable = NULL;
  return FALSE;
}

static gboolean
drag_drop_pad (GtkWidget      *widget,
               GdkDragContext *context,
               int             x,
               int             y,
               guint           time,
               PsppireDialogAction *pda)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (pda);

  enum pivot_axis_type axis
    = (act->rows_pad == widget) ? PIVOT_AXIS_ROW : PIVOT_AXIS_COLUMN;

  PsppireDict *dict = PSPPIRE_DICT_VIEW (pda->source)->dict;

  gboolean ok
    = augment_template_table (act->table, axis, act->dragged_variable,
                              dict->dict);
  gtk_drag_finish (context, ok, FALSE, time);

  if (!ok)
    return TRUE;

  act->table = pivot_table_ref (act->table);
  output_item_unref (act->graphic);
  act->graphic = table_item_create (pivot_table_unshare (act->table));
  gtk_widget_queue_draw (act->canvas);

  return TRUE;
}

static gchar f1[]="ctables-dialog";

static const GtkTargetEntry targets[1] = {
    {f1, GTK_TARGET_SAME_APP, 2},
  };

static gboolean
canvas_draw (GtkWidget *widget, cairo_t *cr, PsppireDialogActionCtables *act)
{
  GdkRectangle clip;
  if (!gdk_cairo_get_clip_rectangle (cr, &clip))
    return TRUE;
  struct xr_fsm_style *style = NULL;
  struct xr_fsm *fsm = NULL;


  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  guint width = gtk_widget_get_allocated_width (widget);
  guint height = gtk_widget_get_allocated_height (widget);

  gtk_render_background (context, cr, 0, 0, width, height);

  if (act->graphic)
    {
      style = get_xr_fsm_style (widget);
      fsm = xr_fsm_create_for_scrolling (act->graphic, style, cr);
      xr_fsm_draw_region (fsm, cr, clip.x, clip.y, clip.width, clip.height);
    }

  gtk_style_context_get_color (context,
                               gtk_style_context_get_state (context),
                               &color);
  gdk_cairo_set_source_rgba (cr, &color);

  cairo_fill (cr);

  if (fsm)
    xr_fsm_destroy (fsm);

  if (style)
      xr_fsm_style_unref (style);

  return FALSE;
}

static struct xr_fsm_style *
get_xr_fsm_style (GtkWidget *w)
{
  GtkStyleContext *context = gtk_widget_get_style_context (w);
  GtkStateFlags state = gtk_widget_get_state_flags (w);

  int xr_width = 500 * 1000;

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

static GtkBuilder *
psppire_dialog_action_ctables_activate (PsppireDialogAction *pda, GVariant *param)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (pda);

  GtkBuilder *xml = builder_new ("ctables.ui");
  act->cols_pad = get_widget_assert (xml, "columns-pad");
  act->rows_pad = get_widget_assert (xml, "rows-pad");
  act->canvas = get_widget_assert (xml, "template-canvas");
  g_signal_connect (act->rows_pad, "draw", G_CALLBACK (pad_draw), NULL);
  g_signal_connect (act->cols_pad, "draw", G_CALLBACK (pad_draw), NULL);

  g_signal_connect (act->canvas, "draw", G_CALLBACK (canvas_draw), pda);

  gtk_drag_dest_set (act->rows_pad, GTK_DEST_DEFAULT_ALL, targets, 1,
                     GDK_ACTION_LINK);
  gtk_drag_dest_set (act->cols_pad, GTK_DEST_DEFAULT_ALL, targets, 1,
                     GDK_ACTION_LINK);

  g_signal_connect (act->rows_pad, "drag-drop", G_CALLBACK (drag_drop_pad), pda);
  g_signal_connect (act->cols_pad, "drag-drop", G_CALLBACK (drag_drop_pad), pda);

  pda->dialog = get_widget_assert   (xml, "tables-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  gtk_drag_source_set (pda->source, GDK_BUTTON1_MASK, targets, 1, GDK_ACTION_LINK);

  g_signal_connect (pda->source, "drag-begin", G_CALLBACK (drag_begin), pda);
  g_signal_connect (pda->source, "drag-end", G_CALLBACK (drag_end), pda);
  g_signal_connect (pda->source, "drag-failed", G_CALLBACK (drag_failed), pda);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
                                           (ContentsAreValid) dialog_state_valid);
  return xml;
}

/*
  Return  an array of integers which contain the axes of the table.
  The array is arranged in the order ROW, COLUMN, LAYER.
  The elements of the array are the indices of the table->dimensions member,
  which contain that integer.  If there is no such member then the element will
  be -1.

  In other words, it is the inverse of x: f(x) -> table->dimensions[x], but
  adjusted to the order ROW, COLUMN, LAYER

  The caller must free this when no longer needed.
 */
static size_t *get_dimensions_permutation (const struct pivot_table *table)
{
  size_t *perm = xcalloc (PIVOT_N_AXES, sizeof *perm);
  for (size_t s = 0; s < PIVOT_N_AXES; ++s)
    perm[s] = -1;

  for (size_t s = 0; s < table->n_dimensions; ++s)
    {
      switch (table->dimensions[s]->axis_type)
        {
        case PIVOT_AXIS_ROW:
          perm[0] = s;
          break;
        case PIVOT_AXIS_COLUMN:
          perm[1] = s;
          break;
        case PIVOT_AXIS_LAYER:
          perm[2] = s;
          break;
        default:
          g_assert_not_reached ();
        }
    }

  return perm;
}

static char *
generate_syntax (const PsppireDialogAction *pda)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (pda);
  const struct pivot_table *table = act->table;

  GString *string = g_string_new ("CTABLES");

  g_string_append (string, " /TABLE");

  size_t *perm = get_dimensions_permutation (table);

  for (size_t idx = 0; idx < PIVOT_N_AXES; ++idx)
    {
      if (perm[idx] == -1)
        continue;

      const struct pivot_dimension *dim = table->dimensions[perm[idx]];

      bool first_variable = true;
      for (int s = 0; s < dim->root->n_subs; ++s)
        {
          const struct pivot_category *sub = dim->root->subs[s];

          if (sub->name->type == PIVOT_VALUE_VARIABLE)
            {
              if (idx > 0 && first_variable)
                {
                  g_string_append (string, " BY");
                }

              g_string_append (string, " ");
              if (!first_variable)
                g_string_append (string, "+ ");
              g_string_append (string, sub->name->variable.var_name);
              first_variable = false;
            }
        }
    }

  free (perm);

  g_string_append (string, ".\n");

  return g_string_free_and_steal (string);
}


static void
psppire_dialog_action_ctables_dispose (GObject *obj)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (obj);

  if (act->dispose_has_run)
    return;
  act->dispose_has_run = TRUE;

  G_OBJECT_CLASS (psppire_dialog_action_ctables_parent_class)->dispose (obj);
}

static void
psppire_dialog_action_ctables_finalize (GObject *obj)
{
  PsppireDialogActionCtables *act = PSPPIRE_DIALOG_ACTION_CTABLES (obj);

  output_item_unref (act->graphic);
  pivot_table_unref (act->table);

  G_OBJECT_CLASS (psppire_dialog_action_ctables_parent_class)->finalize (obj);
}

static void
psppire_dialog_action_ctables_class_init (PsppireDialogActionCtablesClass *class)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate
    = psppire_dialog_action_ctables_activate;

  G_OBJECT_CLASS (class)->dispose = psppire_dialog_action_ctables_dispose;
  G_OBJECT_CLASS (class)->finalize = psppire_dialog_action_ctables_finalize;;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_ctables_init (PsppireDialogActionCtables *act)
{
  act->graphic = NULL;
  act->table = NULL;
  act->dragged_variable = NULL;
}
