/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2011, 2012, 2013, 2017,
   2020  Free Software Foundation

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

#include <gtk/gtk.h>
#include "psppire-dictview.h"
#include "psppire-dict.h"
#include "dict-display.h"
#include "psppire-conf.h"
#include "options-dialog.h"
#include <data/format.h>
#include <libpspp/i18n.h>
#include "helper.h"

#include "psppire-dialog.h"
#include "psppire-var-info.h"
#include "psppire-buttonbox.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dict_view_class_init    (PsppireDictViewClass *class);
static void psppire_dict_view_init          (PsppireDictView      *dict_view);

G_DEFINE_TYPE (PsppireDictView, psppire_dict_view, GTK_TYPE_TREE_VIEW)

static void
psppire_dict_view_finalize (GObject *object)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  gtk_widget_destroy (dict_view->menu);
}

/* Properties */
enum
{
  PROP_0,
  PROP_DICTIONARY,
  PROP_PREDICATE,
  PROP_SELECTION_MODE
};



/* A GtkTreeModelFilterVisibleFunc to filter lines in the treeview */
static gboolean
filter_variables (GtkTreeModel *tmodel, GtkTreeIter *titer, gpointer data)
{
  var_predicate_func *predicate = data;
  struct variable *var;
  GtkTreeModel *model = NULL;
  GtkTreeIter iter ;
  PsppireDict *dict ;
  GtkTreePath *path ;
  gint *idx;

  get_base_model (tmodel, titer, &model, &iter);

  dict = PSPPIRE_DICT (model);
  path = gtk_tree_model_get_path (model, &iter);
  idx = gtk_tree_path_get_indices (path);
  var =  psppire_dict_get_variable (dict, *idx);

  gtk_tree_path_free (path);

  return predicate (var);
}

static gint
unsorted (GtkTreeModel *model,
     GtkTreeIter *a,
     GtkTreeIter *b,
     gpointer user_data)
{
  struct variable *var_a;
  struct variable *var_b;

  gtk_tree_model_get (model, a, DICT_TVM_COL_VAR,  &var_a, -1);
  gtk_tree_model_get (model, b, DICT_TVM_COL_VAR,  &var_b, -1);

  gint rval = compare_var_ptrs_by_dict_index (&var_a, &var_b, NULL);

  var_unref (var_a);
  var_unref (var_b);

  return rval;
}

static gint
sort_by_name (GtkTreeModel *model,
     GtkTreeIter *a,
     GtkTreeIter *b,
     gpointer user_data)
{
  struct variable *var_a;
  struct variable *var_b;

  gtk_tree_model_get (model, a, DICT_TVM_COL_VAR,  &var_a, -1);
  gtk_tree_model_get (model, b, DICT_TVM_COL_VAR,  &var_b, -1);

  gint rval =  g_strcmp0 (var_get_name (var_a), var_get_name (var_b));

  var_unref (var_a);
  var_unref (var_b);

  return rval;
}


static gint
sort_by_label (GtkTreeModel *model,
     GtkTreeIter *a,
     GtkTreeIter *b,
     gpointer user_data)
{
  struct variable *var_a;
  struct variable *var_b;

  gtk_tree_model_get (model, a, DICT_TVM_COL_VAR,  &var_a, -1);
  gtk_tree_model_get (model, b, DICT_TVM_COL_VAR,  &var_b, -1);

  gint rval = g_strcmp0 (var_get_label (var_a), var_get_label (var_b));

  var_unref (var_a);
  var_unref (var_b);

  return rval;
}


static gint
default_sort (GtkTreeModel *model,
     GtkTreeIter *a,
     GtkTreeIter *b,
     gpointer user_data)
{
  int what = -1;
  psppire_conf_get_enum (psppire_conf_new (), "VariableLists", "sort-order",
			 PSPP_TYPE_OPTIONS_VAR_ORDER, &what);

  switch (what)
    {
    default:
      return unsorted (model, a, b, user_data);
      break;
    case PSPP_OPTIONS_VAR_ORDER_NAME:
      return sort_by_name (model, a, b, user_data);
      break;
    case PSPP_OPTIONS_VAR_ORDER_LABEL:
      return sort_by_label (model, a, b, user_data);
      break;
    }

  g_assert_not_reached ();
}




static void
set_model (PsppireDictView *dict_view)
{
  GtkTreeModel *model = NULL;

  if (dict_view->dict == NULL)
    return;

  dict_view->sorted_model = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (dict_view->dict));
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (dict_view->sorted_model), default_sort, dict_view, 0);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (dict_view->sorted_model),
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);

   if (dict_view->predicate)
    {
      model = gtk_tree_model_filter_new (dict_view->sorted_model,	 NULL);

      gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (model),
					      filter_variables,
					      dict_view->predicate,
					      NULL);
    }
  else
    {
      model = dict_view->sorted_model;
      g_object_ref (model);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dict_view), model);
  g_object_unref (model);
}

static void
psppire_dict_view_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      dict_view->dict = g_value_get_object (value);
      break;
    case PROP_PREDICATE:
      dict_view->predicate = g_value_get_pointer (value);
      break;
    case PROP_SELECTION_MODE:
      {
	GtkTreeSelection *selection =
	  gtk_tree_view_get_selection (GTK_TREE_VIEW (dict_view));

	GtkSelectionMode mode = g_value_get_enum (value);

	gtk_tree_selection_set_mode (selection, mode);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };


  set_model (dict_view);
}


static void
psppire_dict_view_get_property (GObject         *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      g_value_set_object (value, dict_view->dict);
      break;
    case PROP_PREDICATE:
      g_value_set_pointer (value, dict_view->predicate);
      break;
    case PROP_SELECTION_MODE:
      {
	GtkTreeSelection *selection =
	  gtk_tree_view_get_selection (GTK_TREE_VIEW (dict_view));

	g_value_set_enum (value, gtk_tree_selection_get_mode (selection));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}



static void
psppire_dict_view_class_init (PsppireDictViewClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_dict_view_finalize;

  GParamSpec *predicate_spec =
    g_param_spec_pointer ("predicate",
			  "Predicate",
			  "A predicate function",
			  G_PARAM_READABLE | G_PARAM_WRITABLE);


  GParamSpec *selection_mode_spec =
    g_param_spec_enum ("selection-mode",
		       "Selection Mode",
		       "How many things can be selected",
		       GTK_TYPE_SELECTION_MODE,
		       GTK_SELECTION_MULTIPLE,
		       G_PARAM_CONSTRUCT | G_PARAM_READABLE | G_PARAM_WRITABLE);

  object_class->set_property = psppire_dict_view_set_property;
  object_class->get_property = psppire_dict_view_get_property;

  g_object_class_override_property (object_class,
				    PROP_DICTIONARY,
				    "model");

  g_object_class_install_property (object_class,
                                   PROP_PREDICATE,
                                   predicate_spec);

  g_object_class_install_property (object_class,
                                   PROP_SELECTION_MODE,
                                   selection_mode_spec);
}

static gboolean
use_labels (PsppireDictView *dv)
{
  gboolean disp_labels = TRUE;

  if (gtk_check_menu_item_get_inconsistent (GTK_CHECK_MENU_ITEM
					    (dv->override_button)))
    {
      psppire_conf_get_boolean (psppire_conf_new (),
				"VariableLists", "display-labels", &disp_labels);
    }
  else
    {
      disp_labels = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM
						    (dv->override_button));
    }
  return disp_labels;
}


/* A GtkTreeCellDataFunc which renders the name and/or label of the
   variable */
static void
var_description_cell_data_func (GtkTreeViewColumn *col,
				GtkCellRenderer *cell,
				GtkTreeModel *top_model,
				GtkTreeIter *top_iter,
				gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  struct variable *var;
  GtkTreeIter iter;
  GtkTreeModel *model;

  get_base_model (top_model, top_iter, &model, &iter);

  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  if (var_has_label (var) && use_labels (dv))
    {
      gchar *text = g_markup_printf_escaped (
				     "<span stretch=\"condensed\">%s</span>",
				     var_get_label (var));

      g_object_set (cell, "markup", text, NULL);
      g_free (text);
    }
  else
    {
      g_object_set (cell, "text", var_get_name (var), NULL);
    }

  var_unref (var);
}



/* A GtkTreeCellDataFunc which sets the icon appropriate to the type
   of variable */
static void
var_icon_cell_data_func (GtkTreeViewColumn *col,
		       GtkCellRenderer *cell,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       gpointer data)
{
  struct variable *var;

  gtk_tree_model_get (model, iter, DICT_TVM_COL_VAR, &var, -1);

  g_object_set (cell,
		"stock-size", GTK_ICON_SIZE_MENU,
		"icon-name", get_var_measurement_stock_id (var_get_print_format (var)->type,
							   var_get_measure (var)),
                NULL);

  var_unref (var);
}

const char *
get_var_measurement_stock_id (enum fmt_type type, enum measure measure)
{
  switch (fmt_get_category (type))
    {
    case FMT_CAT_STRING:
      switch (measure)
	{
	case MEASURE_NOMINAL: return "measure-string-nominal";
	case MEASURE_ORDINAL: return "measure-string-ordinal";
	case MEASURE_SCALE:   return "role-none";
        case n_MEASURES: break;
	}
      break;

    case FMT_CAT_DATE:
    case FMT_CAT_TIME:
      switch (measure)
        {
        case MEASURE_NOMINAL: return "measure-date-nominal";
        case MEASURE_ORDINAL: return "measure-date-ordinal";
        case MEASURE_SCALE:   return "measure-date-scale";
        case n_MEASURES: break;
        }
      break;

    default:
      switch (measure)
        {
        case MEASURE_NOMINAL: return "measure-nominal";
        case MEASURE_ORDINAL: return "measure-ordinal";
        case MEASURE_SCALE:   return "measure-scale";
        case n_MEASURES: break;
	}
      break;
    }

  g_return_val_if_reached ("");
}



/* Sets the tooltip to be the name of the variable under the cursor */
static gboolean
set_tooltip_for_variable (GtkTreeView  *treeview,
			  gint        x,
			  gint        y,
			  gboolean    keyboard_mode,
			  GtkTooltip *tooltip,
			  gpointer    user_data)
{
  gint bx, by;
  GtkTreeIter iter;
  GtkTreePath *path;
  GtkTreeModel *tree_model;
  struct variable *var = NULL;
  gboolean ok;

  gtk_tree_view_convert_widget_to_bin_window_coords (treeview,
                                                     x, y, &bx, &by);

  if (!gtk_tree_view_get_path_at_pos (treeview, bx, by,
                                      &path, NULL, NULL, NULL))
    return FALSE;

  tree_model = gtk_tree_view_get_model (treeview);

  gtk_tree_view_set_tooltip_row (treeview, tooltip, path);

  ok = gtk_tree_model_get_iter (tree_model, &iter, path);

  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  gtk_tree_model_get (tree_model, &iter, DICT_TVM_COL_VAR,  &var, -1);

  if (! var_has_label (var))
    {
      var_unref (var);
      return FALSE;
    }

  {
    const gchar *tip ;
    GtkTreeModel *m;

    get_base_model (tree_model, NULL, &m, NULL);

    if (use_labels (PSPPIRE_DICT_VIEW (treeview)))
      tip = var_get_name (var);
    else
      tip = var_get_label (var);

    gtk_tooltip_set_text (tooltip, tip);
  }

  var_unref (var);
  return TRUE;
}

static struct variable *
psppire_dict_view_iter_to_var (PsppireDictView *dict_view,
                               GtkTreeIter *top_iter)
{
  GtkTreeView *treeview = GTK_TREE_VIEW (dict_view);
  GtkTreeModel *top_model = gtk_tree_view_get_model (treeview);

  struct variable *var;
  GtkTreeModel *model;
  GtkTreeIter iter;

  get_base_model (top_model, top_iter, &model, &iter);
  g_assert (PSPPIRE_IS_DICT (model));

  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  return var;
}

static gboolean
show_menu (PsppireDictView *dv, GdkEvent *event, gpointer data)
{
  GdkEventButton *button_event = (GdkEventButton *) event;
  if (button_event->button != 3)
    return FALSE;

  dv->var_under_cursor = NULL;
  GtkTreePath *path = NULL;
  gboolean is_row =
    gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (dv),
                                   button_event->x, button_event->y,
                                   &path, NULL, NULL, NULL);
  if (is_row)
    {
      GtkTreeIter iter;
      GtkTreeModel *top_model = gtk_tree_view_get_model (GTK_TREE_VIEW (dv));
      gtk_tree_model_get_iter (top_model, &iter, path);
      dv->var_under_cursor = psppire_dict_view_iter_to_var (dv, &iter);
    }
  gtk_tree_path_free (path);

  gtk_widget_set_sensitive (GTK_WIDGET (dv->var_info_check), is_row);

  gtk_menu_popup_at_pointer (GTK_MENU (dv->menu), event);

  return TRUE;
}

static void
toggle_label_preference (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);

  gboolean global_setting = TRUE;
  psppire_conf_get_boolean (psppire_conf_new (),
			    "VariableLists", "display-labels", &global_setting);

  if (gtk_check_menu_item_get_inconsistent (checkbox))
    gtk_check_menu_item_set_active (checkbox, !global_setting);

  gtk_check_menu_item_set_inconsistent (checkbox, FALSE);

  gtk_widget_queue_draw (GTK_WIDGET (dv));
}


static void
set_sort_criteria (GtkCheckMenuItem *checkbox, PsppireDictView *dv, GtkTreeIterCompareFunc func)
{
  if (!gtk_check_menu_item_get_active (checkbox))
    {
      gtk_widget_queue_draw (GTK_WIDGET (dv));
      return;
    }


  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (dv->sorted_model), func, 0, 0);


  gtk_widget_queue_draw (GTK_WIDGET (dv));
}

static void
set_sort_criteria_default (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  set_sort_criteria (checkbox, dv, default_sort);
}


static void
set_sort_criteria_name (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  set_sort_criteria (checkbox, dv, sort_by_name);
}


static void
set_sort_criteria_label (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  set_sort_criteria (checkbox, dv, sort_by_label);
}


static void
set_sort_criteria_unsorted (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  set_sort_criteria (checkbox, dv, unsorted);
}

static void
check_item_set_inactive (GtkCheckMenuItem *cmi)
{
  gtk_check_menu_item_set_active (cmi, FALSE);
}

static void
pop_up_down_variable_information (GtkCheckMenuItem *cmi, PsppireDictView *dv)
{
  gboolean active;
  g_object_get (cmi, "active", &active, NULL);

  g_return_if_fail (dv->var_under_cursor);

  if (!active)
    return;

  GtkWidget *dialog = psppire_dialog_new ();
  g_object_set (dialog,
                "title", _("Variable Details"),
                "help-page", "Manipulating-Variables",
                NULL);

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
  gtk_container_add (GTK_CONTAINER (dialog), box);

  GtkWidget *var_info = psppire_var_info_new ();
  g_object_set (var_info, "variable", dv->var_under_cursor, NULL);
  gtk_box_pack_start (GTK_BOX (box), var_info, TRUE, TRUE, 5);

  GtkWidget *button_box = psppire_button_box_new ();
  g_object_set (button_box,
                "buttons", PSPPIRE_BUTTON_CLOSE_MASK | PSPPIRE_BUTTON_HELP_MASK,
                NULL);
  g_object_set (button_box, "layout-style", GTK_BUTTONBOX_SPREAD, NULL);
  gtk_box_pack_start (GTK_BOX (box), button_box, FALSE, TRUE, 5);

  g_signal_connect_swapped (dialog, "unmap",
                            G_CALLBACK (check_item_set_inactive), cmi);

  gtk_widget_show_all (box);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  psppire_dialog_run (PSPPIRE_DIALOG (dialog));
}

static void
psppire_dict_view_init (PsppireDictView *dict_view)
{
  GtkTreeViewColumn *col = gtk_tree_view_column_new ();

  GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new ();

  dict_view->prefer_labels_override = FALSE;
  dict_view->sorted_model = NULL;
  dict_view->var_under_cursor = NULL;

  gtk_tree_view_column_set_title (col, _("Variable"));

  gtk_tree_view_column_pack_start (col, renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_icon_cell_data_func,
					   NULL, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_description_cell_data_func,
					   dict_view, NULL);

  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  /* FIXME: make this a value in terms of character widths */
  gtk_tree_view_column_set_min_width (col, 150);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dict_view), col);

  g_object_set (dict_view,
		"has-tooltip", TRUE,
		"headers-visible", FALSE,
		NULL);

  g_signal_connect (dict_view, "query-tooltip",
		    G_CALLBACK (set_tooltip_for_variable), NULL);

  dict_view->menu = gtk_menu_new ();

  {
    GSList *group = NULL;
    GtkWidget *item =
      gtk_check_menu_item_new_with_label  (_("Prefer variable labels"));

    dict_view->override_button = item;
    gtk_check_menu_item_set_inconsistent (GTK_CHECK_MENU_ITEM (item),
					  TRUE);

    g_signal_connect (item, "toggled",
		      G_CALLBACK (toggle_label_preference), dict_view);

    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);

    item = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);

    item = gtk_radio_menu_item_new_with_label (group, _("Default sort order"));
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);
    g_signal_connect (item, "toggled", G_CALLBACK (set_sort_criteria_default), dict_view);

    item = gtk_radio_menu_item_new_with_label (group, _("Unsorted (dictionary order)"));
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);
    g_signal_connect (item, "toggled", G_CALLBACK (set_sort_criteria_unsorted), dict_view);

    item = gtk_radio_menu_item_new_with_label (group, _("Sort by name"));
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);
    g_signal_connect (item, "toggled", G_CALLBACK (set_sort_criteria_name), dict_view);

    item = gtk_radio_menu_item_new_with_label (group, _("Sort by label"));
    group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);
    g_signal_connect (item, "toggled", G_CALLBACK (set_sort_criteria_label), dict_view);

    item = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);

    item = gtk_check_menu_item_new_with_label (_("Variable Information"));
    dict_view->var_info_check = item;
    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), item);
    g_signal_connect (item, "toggled", G_CALLBACK (pop_up_down_variable_information), dict_view);
  }

  gtk_widget_show_all (dict_view->menu);

  g_signal_connect (dict_view, "button-press-event",
		    G_CALLBACK (show_menu), NULL);
}

struct get_vars_aux
  {
    PsppireDictView *dict_view;
    struct variable **vars;
    size_t idx;
  };

static void
get_vars_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
             gpointer data)
{
  struct get_vars_aux *aux = data;
  struct variable *var = psppire_dict_view_iter_to_var (aux->dict_view, iter);

  g_return_if_fail (var != NULL);
  aux->vars[aux->idx++] = var;
}

void
psppire_dict_view_get_selected_variables (PsppireDictView *dict_view,
                                          struct variable ***vars,
                                          size_t *n_varsp)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (dict_view);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
  gint n_vars = gtk_tree_selection_count_selected_rows (selection);
  struct get_vars_aux aux;

  *vars = g_malloc_n (n_vars, sizeof **vars);

  aux.dict_view = dict_view;
  aux.vars = *vars;
  aux.idx = 0;
  gtk_tree_selection_selected_foreach (selection, get_vars_cb, &aux);

  *n_varsp = aux.idx;
  g_return_if_fail (aux.idx >= n_vars);
}

struct variable *
psppire_dict_view_get_selected_variable (PsppireDictView *dict_view)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (dict_view);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    return psppire_dict_view_iter_to_var (dict_view, &iter);
  else
    return NULL;
}
