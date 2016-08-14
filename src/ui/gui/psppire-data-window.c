/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2016  Free Software Foundation

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
#include <stdlib.h>

#include "data/dataset.h"
#include "data/session.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/entry-dialog.h"
#include "ui/gui/executor.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/helper.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-import-assistant.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog-action.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-window.h"
#include "ui/gui/psppire-data-sheet.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/windows-menu.h"
#include "ui/gui/goto-case-dialog.h"
#include "ui/gui/psppire.h"
#include "ui/syntax-gen.h"

#include "gl/c-strcase.h"
#include "gl/c-strcasestr.h"
#include "gl/xvasprintf.h"

#include "find-dialog.h"
#include "psppire-dialog-action-1sks.h"
#include "psppire-dialog-action-aggregate.h"
#include "psppire-dialog-action-autorecode.h"
#include "psppire-dialog-action-barchart.h"
#include "psppire-dialog-action-binomial.h"
#include "psppire-dialog-action-chisquare.h"
#include "psppire-dialog-action-comments.h"
#include "psppire-dialog-action-compute.h"
#include "psppire-dialog-action-correlation.h"
#include "psppire-dialog-action-count.h"
#include "psppire-dialog-action-crosstabs.h"
#include "psppire-dialog-action-descriptives.h"
#include "psppire-dialog-action-examine.h"
#include "psppire-dialog-action-factor.h"
#include "psppire-dialog-action-flip.h"
#include "psppire-dialog-action-frequencies.h"
#include "psppire-dialog-action-histogram.h"
#include "psppire-dialog-action-indep-samps.h"
#include "psppire-dialog-action-k-related.h"
#include "psppire-dialog-action-kmeans.h"
#include "psppire-dialog-action-logistic.h"
#include "psppire-dialog-action-means.h"
#include "psppire-dialog-action-oneway.h"
#include "psppire-dialog-action-paired.h"
#include "psppire-dialog-action-rank.h"
#include "psppire-dialog-action-recode-same.h"
#include "psppire-dialog-action-recode-different.h"
#include "psppire-dialog-action-regression.h"
#include "psppire-dialog-action-reliability.h"
#include "psppire-dialog-action-roc.h"
#include "psppire-dialog-action-runs.h"
#include "psppire-dialog-action-scatterplot.h"
#include "psppire-dialog-action-select.h"
#include "psppire-dialog-action-sort.h"
#include "psppire-dialog-action-split.h"
#include "psppire-dialog-action-tt1s.h"
#include "psppire-dialog-action-two-sample.h"
#include "psppire-dialog-action-univariate.h"
#include "psppire-dialog-action-var-info.h"
#include "psppire-dialog-action-weight.h"


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct session *the_session;
struct ll_list all_data_windows = LL_INITIALIZER (all_data_windows);

static void psppire_data_window_class_init    (PsppireDataWindowClass *class);
static void psppire_data_window_init          (PsppireDataWindow      *data_editor);


static void psppire_data_window_iface_init (PsppireWindowIface *iface);

static void psppire_data_window_dispose (GObject *object);
static void psppire_data_window_finalize (GObject *object);
static void psppire_data_window_set_property (GObject         *object,
                                              guint            prop_id,
                                              const GValue    *value,
                                              GParamSpec      *pspec);
static void psppire_data_window_get_property (GObject         *object,
                                              guint            prop_id,
                                              GValue          *value,
                                              GParamSpec      *pspec);

GType
psppire_data_window_get_type (void)
{
  static GType psppire_data_window_type = 0;

  if (!psppire_data_window_type)
    {
      static const GTypeInfo psppire_data_window_info =
	{
	  sizeof (PsppireDataWindowClass),
	  NULL,
	  NULL,
	  (GClassInitFunc)psppire_data_window_class_init,
	  (GClassFinalizeFunc) NULL,
	  NULL,
	  sizeof (PsppireDataWindow),
	  0,
	  (GInstanceInitFunc) psppire_data_window_init,
	};

      static const GInterfaceInfo window_interface_info =
	{
	  (GInterfaceInitFunc) psppire_data_window_iface_init,
	  NULL,
	  NULL
	};

      psppire_data_window_type =
	g_type_register_static (PSPPIRE_TYPE_WINDOW, "PsppireDataWindow",
				&psppire_data_window_info, 0);


      g_type_add_interface_static (psppire_data_window_type,
				   PSPPIRE_TYPE_WINDOW_MODEL,
				   &window_interface_info);
    }

  return psppire_data_window_type;
}

static GObjectClass *parent_class ;

enum {
  PROP_DATASET = 1
};

static void
psppire_data_window_class_init (PsppireDataWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->dispose = psppire_data_window_dispose;
  object_class->finalize = psppire_data_window_finalize;
  object_class->set_property = psppire_data_window_set_property;
  object_class->get_property = psppire_data_window_get_property;

  g_object_class_install_property (
				   object_class, PROP_DATASET,
				   g_param_spec_pointer ("dataset", "Dataset",
							 "'struct datset *' represented by the window",
							 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}



/* Run the EXECUTE command. */
static void
execute (PsppireDataWindow *dw)
{
  execute_const_syntax_string (dw, "EXECUTE.");
}

static void
transformation_change_callback (bool transformations_pending,
				gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *status_label  =
    get_widget_assert (de->builder, "case-counter-area");

  {
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (de),
						  "transform-pending");

    g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
				 transformations_pending); 
  }

  if ( transformations_pending)
    gtk_label_set_text (GTK_LABEL (status_label),
			_("Transformations Pending"));
  else
    gtk_label_set_text (GTK_LABEL (status_label), "");
}

/* Callback for when the dictionary changes its filter variable */
static void
on_filter_change (GObject *o, gint filter_index, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *filter_status_area =
    get_widget_assert (de->builder, "filter-use-status-area");

  if ( filter_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (filter_status_area), _("Filter off"));
    }
  else
    {
      PsppireDict *dict = NULL;
      struct variable *var ;
      gchar *text ;

      g_object_get (de->data_editor, "dictionary", &dict, NULL);

      var = psppire_dict_get_variable (dict, filter_index);

      text = g_strdup_printf (_("Filter by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (filter_status_area), text);

      g_free (text);
    }
}

/* Callback for when the dictionary changes its split variables */
static void
on_split_change (PsppireDict *dict, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  size_t n_split_vars = dict_get_split_cnt (dict->dict);

  GtkWidget *split_status_area =
    get_widget_assert (de->builder, "split-file-status-area");

  if ( n_split_vars == 0 )
    {
      gtk_label_set_text (GTK_LABEL (split_status_area), _("No Split"));
    }
  else
    {
      gint i;
      GString *text;
      const struct variable *const * split_vars =
	dict_get_split_vars (dict->dict);

      text = g_string_new (_("Split by "));

      for (i = 0 ; i < n_split_vars - 1; ++i )
	{
	  g_string_append_printf (text, "%s, ", var_get_name (split_vars[i]));
	}
      g_string_append (text, var_get_name (split_vars[i]));

      gtk_label_set_text (GTK_LABEL (split_status_area), text->str);

      g_string_free (text, TRUE);
    }
}




/* Callback for when the dictionary changes its weights */
static void
on_weight_change (GObject *o, gint weight_index, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *weight_status_area =
    get_widget_assert (de->builder, "weight-status-area");

  if ( weight_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (weight_status_area), _("Weights off"));
    }
  else
    {
      struct variable *var ;
      PsppireDict *dict = NULL;
      gchar *text;

      g_object_get (de->data_editor, "dictionary", &dict, NULL);

      var = psppire_dict_get_variable (dict, weight_index);

      text = g_strdup_printf (_("Weight by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (weight_status_area), text);

      g_free (text);
    }
}

#if 0
static void
dump_rm (GtkRecentManager *rm)
{
  GList *items = gtk_recent_manager_get_items (rm);

  GList *i;

  g_print ("Recent Items:\n");
  for (i = items; i; i = i->next)
    {
      GtkRecentInfo *ri = i->data;

      g_print ("Item: %s (Mime: %s) (Desc: %s) (URI: %s)\n",
	       gtk_recent_info_get_short_name (ri),
	       gtk_recent_info_get_mime_type (ri),
	       gtk_recent_info_get_description (ri),
	       gtk_recent_info_get_uri (ri)
	       );


      gtk_recent_info_unref (ri);
    }

  g_list_free (items);
}
#endif

static gboolean
has_suffix (const gchar *name, const gchar *suffix)
{
  size_t name_len = strlen (name);
  size_t suffix_len = strlen (suffix);
  return (name_len > suffix_len
          && !c_strcasecmp (&name[name_len - suffix_len], suffix));
}

static gboolean
name_has_por_suffix (const gchar *name)
{
  return has_suffix (name, ".por");
}

static gboolean
name_has_sav_suffix (const gchar *name)
{
  return has_suffix (name, ".sav") || has_suffix (name, ".zsav");
}

/* Returns true if NAME has a suffix which might denote a PSPP file */
static gboolean
name_has_suffix (const gchar *name)
{
  return name_has_por_suffix (name) || name_has_sav_suffix (name);
}

static gboolean
load_file (PsppireWindow *de, const gchar *file_name, const char *encoding,
           gpointer syn)
{
  const char *mime_type = NULL;
  gchar *syntax = NULL;
  bool ok;

  if (syn == NULL)
    {
      gchar *utf8_file_name;
      struct string filename;
      
      utf8_file_name = g_filename_to_utf8 (file_name, -1, NULL, NULL, NULL);

      if (NULL == utf8_file_name)
	return FALSE;

      ds_init_empty (&filename);    
      syntax_gen_string (&filename, ss_cstr (utf8_file_name));
      
      g_free (utf8_file_name);

      if (encoding && encoding[0])
        syntax = g_strdup_printf ("GET FILE=%s ENCODING='%s'.",
                                  ds_cstr (&filename), encoding);
      else
        syntax = g_strdup_printf ("GET FILE=%s.", ds_cstr (&filename));
      ds_destroy (&filename);
    }
  else
    {
      syntax = syn;
    }

  ok = execute_syntax (PSPPIRE_DATA_WINDOW (de),
                       lex_reader_for_string (syntax, "UTF-8"));
  g_free (syntax);

  if (ok && syn == NULL)
    {
      if (name_has_por_suffix (file_name))
	mime_type = "application/x-spss-por";
      else if (name_has_sav_suffix (file_name))
	mime_type = "application/x-spss-sav";
      
      add_most_recent (file_name, mime_type, encoding);
    }

  return ok;
}

static const char *
psppire_data_window_format_to_string (enum PsppireDataWindowFormat format)
{
  if (format == PSPPIRE_DATA_WINDOW_SAV)
    return ".sav";
  else if (format == PSPPIRE_DATA_WINDOW_ZSAV)
    return ".zsav";
  else
    return ".por";
}

/* Save DE to file */
static void
save_file (PsppireWindow *w)
{
  const gchar *file_name = NULL;
  gchar *utf8_file_name = NULL;
  GString *fnx;
  struct string filename ;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (w);
  gchar *syntax;

  file_name = psppire_window_get_filename (w);

  fnx = g_string_new (file_name);

  if ( ! name_has_suffix (fnx->str))
    g_string_append (fnx, psppire_data_window_format_to_string (de->format));

  ds_init_empty (&filename);

  utf8_file_name = g_filename_to_utf8 (fnx->str, -1, NULL, NULL, NULL);

  g_string_free (fnx, TRUE);

  syntax_gen_string (&filename, ss_cstr (utf8_file_name));
  g_free (utf8_file_name);

  if (de->format == PSPPIRE_DATA_WINDOW_SAV)
    syntax = g_strdup_printf ("SAVE OUTFILE=%s.", ds_cstr (&filename));
  else if (de->format == PSPPIRE_DATA_WINDOW_ZSAV)
    syntax = g_strdup_printf ("SAVE /ZCOMPRESSED /OUTFILE=%s.",
                              ds_cstr (&filename));
  else
    syntax = g_strdup_printf ("EXPORT OUTFILE=%s.", ds_cstr (&filename));

  ds_destroy (&filename);

  g_free (execute_syntax_string (de, syntax));
}


static void
display_dict (PsppireDataWindow *de)
{
  execute_const_syntax_string (de, "DISPLAY DICTIONARY.");
}

static void
sysfile_info (PsppireDataWindow *de)
{
  GtkWidget *dialog = psppire_window_file_chooser_dialog (PSPPIRE_WINDOW (de));

  if  ( GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog)))
    {
      struct string filename;
      gchar *file_name =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      gchar *utf8_file_name = g_filename_to_utf8 (file_name, -1, NULL, NULL,
                                                  NULL);

      const gchar *encoding = psppire_encoding_selector_get_encoding (
								      gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));

      gchar *syntax;

      ds_init_empty (&filename);

      syntax_gen_string (&filename, ss_cstr (utf8_file_name));

      g_free (utf8_file_name);

      if (encoding)
        syntax = g_strdup_printf ("SYSFILE INFO %s ENCODING='%s'.",
                                  ds_cstr (&filename), encoding);
      else
        syntax = g_strdup_printf ("SYSFILE INFO %s.", ds_cstr (&filename));
      g_free (execute_syntax_string (de, syntax));
    }

  gtk_widget_destroy (dialog);
}


/* PsppireWindow 'pick_filename' callback: prompt for a filename to save as. */
static void
data_pick_filename (PsppireWindow *window)
{
  GtkListStore *list_store;
  GtkWidget *combo_box;

  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (window);
  GtkFileFilter *filter;
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save"),
				 GTK_WINDOW (de),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 _("Cancel"), GTK_RESPONSE_CANCEL,
				 _("Save"), GTK_RESPONSE_ACCEPT,
				 NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("System Files (*.sav)"));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-sav");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Compressed System Files (*.zsav)"));
  gtk_file_filter_add_pattern (filter, "*.zsav");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Portable Files (*.por) "));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-por");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

  {
    GtkCellRenderer *cell;
    GtkWidget *label;
    GtkTreeIter iter;
    GtkWidget *hbox;

    list_store = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
    combo_box = gtk_combo_box_new_with_model (GTK_TREE_MODEL (list_store));

    gtk_list_store_append (list_store, &iter);
    gtk_list_store_set (list_store, &iter,
                        0, PSPPIRE_DATA_WINDOW_SAV,
                        1, _("System File"),
                        -1);
    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo_box), &iter);

    gtk_list_store_append (list_store, &iter);
    gtk_list_store_set (list_store, &iter,
                        0, PSPPIRE_DATA_WINDOW_ZSAV,
                        1, _("Compressed System File"),
                        -1);

    gtk_list_store_append (list_store, &iter);
    gtk_list_store_set (list_store, &iter,
                        0, PSPPIRE_DATA_WINDOW_POR,
                        1, _("Portable File"),
                        -1);

    label = gtk_label_new (_("Format:"));

    cell = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), cell, FALSE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo_box), cell,
                                   "text", 1);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), combo_box, FALSE, FALSE, 0);
    gtk_widget_show_all (hbox);

    gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), hbox);
  }

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog),
                                                  TRUE);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	GString *filename =
	  g_string_new
	  (
	   gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog))
	   );

        GtkTreeIter iter;
        int format;

        gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combo_box), &iter);
        gtk_tree_model_get (GTK_TREE_MODEL (list_store), &iter,
                            0, &format,
                            -1);
	de->format = format;

	if ( ! name_has_suffix (filename->str))
          g_string_append (filename,
                           psppire_data_window_format_to_string (format));

	psppire_window_set_filename (PSPPIRE_WINDOW (de), filename->str);

	g_string_free (filename, TRUE);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}

static bool
confirm_delete_dataset (PsppireDataWindow *de,
                        const char *old_dataset,
                        const char *new_dataset,
                        const char *existing_dataset)
{
  GtkWidget *dialog;
  int result;

  dialog = gtk_message_dialog_new (
				   GTK_WINDOW (de), 0, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
				   _("Delete Existing Dataset?"));

  gtk_message_dialog_format_secondary_text (
					    GTK_MESSAGE_DIALOG (dialog),
					    _("Renaming \"%s\" to \"%s\" will destroy the existing "
					      "dataset named \"%s\".  Are you sure that you want to do this?"),
					    old_dataset, new_dataset, existing_dataset);

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                          _("Cancel"), GTK_RESPONSE_CANCEL,
                          _("Delete"), GTK_RESPONSE_OK,
                          NULL);

  g_object_set (dialog, "icon-name", "pspp", NULL);

  result = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return result == GTK_RESPONSE_OK;
}

static void
on_rename_dataset (PsppireDataWindow *de)
{
  struct dataset *ds = de->dataset;
  struct session *session = dataset_session (ds);
  const char *old_name = dataset_name (ds);
  struct dataset *existing_dataset;
  char *new_name;
  char *prompt;

  prompt = xasprintf (_("Please enter a new name for dataset \"%s\":"),
                      old_name);
  new_name = entry_dialog_run (GTK_WINDOW (de), _("Rename Dataset"), prompt,
                               old_name);
  free (prompt);

  if (new_name == NULL)
    return;

  existing_dataset = session_lookup_dataset (session, new_name);
  if (existing_dataset == NULL || existing_dataset == ds
      || confirm_delete_dataset (de, old_name, new_name,
                                 dataset_name (existing_dataset)))
    g_free (execute_syntax_string (de, g_strdup_printf ("DATASET NAME %s.",
                                                        new_name)));

  free (new_name);
}


static void
status_bar_activate (GAction *action, GVariant *param,  PsppireDataWindow  *de)
{
  GtkWidget *statusbar = get_widget_assert (de->builder, "status-bar");
  
  GVariant *state = g_action_get_state (action);
  const gboolean visible = g_variant_get_boolean (state);
  g_action_change_state (action, g_variant_new_boolean (!visible));

  gtk_widget_set_visible (statusbar, !visible);
}


static void
grid_lines_activate (GAction *action, GVariant *param,  PsppireDataWindow  *de)
{
  GVariant *state = g_action_get_state (action);
  const gboolean grid_visible = g_variant_get_boolean (state);
  g_action_change_state (action, g_variant_new_boolean (!grid_visible));

  psppire_data_editor_show_grid (de->data_editor, !grid_visible);
}


static void
on_switch_page (GtkNotebook *notebook, GtkWidget *page, guint pn, gpointer ud)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (ud);

  GAction *action = g_action_map_lookup_action (G_ACTION_MAP (de), "view_dv");

  switch (pn)
    {
    case 0:
      g_action_change_state (action, g_variant_new_string ("DATA"));
      gtk_widget_show (GTK_WIDGET (de->ti_insert_case));
      gtk_widget_show (GTK_WIDGET (de->ti_jump_to_case));
      gtk_widget_show (GTK_WIDGET (de->ti_find));

      gtk_widget_show (GTK_WIDGET (de->mi_go_to_case));
      gtk_widget_show (GTK_WIDGET (de->mi_insert_case));
      gtk_widget_show (GTK_WIDGET (de->mi_find));
      gtk_widget_show (GTK_WIDGET (de->mi_find_separator));
      gtk_widget_show (GTK_WIDGET (de->mi_clear_cases));

      break;
      
    case 1:
      g_action_change_state (action, g_variant_new_string ("VARS"));
      gtk_widget_hide (GTK_WIDGET (de->ti_insert_case));
      gtk_widget_hide (GTK_WIDGET (de->ti_jump_to_case));
      gtk_widget_hide (GTK_WIDGET (de->ti_find));

      gtk_widget_hide (GTK_WIDGET (de->mi_go_to_case));
      gtk_widget_hide (GTK_WIDGET (de->mi_insert_case));
      gtk_widget_hide (GTK_WIDGET (de->mi_find));
      gtk_widget_hide (GTK_WIDGET (de->mi_find_separator));
      gtk_widget_hide (GTK_WIDGET (de->mi_clear_cases));
      
      break;
    }      
}


static void
activate_change_view (GAction *action, GVariant *param, PsppireDataWindow  *de)
{
  g_action_change_state (action, param);
  GVariant *new_state = g_action_get_state (action);

  const gchar *what = g_variant_get_string (new_state, NULL);
  if (0 == g_strcmp0 (what, "DATA"))
    {
      gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);
    }
  else if (0 == g_strcmp0 (what, "VARS"))
    {
      gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
    }
}



static void
fonts_activate (PsppireDataWindow  *de)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (de));
  GtkWidget *dialog =  gtk_font_chooser_dialog_new (NULL, GTK_WINDOW (toplevel));
  GtkStyleContext *style = gtk_widget_get_style_context (GTK_WIDGET(de->data_editor));
  const PangoFontDescription *current_font ;
  
  gtk_style_context_get (style, GTK_STATE_FLAG_NORMAL, "font", &current_font, NULL);

  gtk_font_chooser_set_font_desc (GTK_FONT_CHOOSER (dialog), current_font);

  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (toplevel));

  if ( GTK_RESPONSE_OK == gtk_dialog_run (GTK_DIALOG (dialog)) )
    {
      PangoFontDescription* font_desc = gtk_font_chooser_get_font_desc (GTK_FONT_CHOOSER (dialog));

      psppire_data_editor_set_font (de->data_editor, font_desc);
    }

  gtk_widget_hide (dialog);
}



/* Callback for the value labels action */

static void
value_labels_activate (GAction *action, GVariant *param,  PsppireDataWindow  *de)
{
  GVariant *v = g_action_get_state (action);
  gboolean labels_active = g_variant_get_boolean (v);
  g_action_change_state (action, g_variant_new_boolean (!labels_active));

  GVariant *new_state  = g_action_get_state (action);
  labels_active = g_variant_get_boolean (new_state);
  g_object_set (de->data_editor, "value-labels", labels_active, NULL);
  
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (de->ti_value_labels_button),
				     labels_active);
}

static void
on_labels_button_toggle (GtkToggleToolButton *ttb, PsppireDataWindow *de)
{
  GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de), "value_labels");
  g_assert (a);
  gboolean labels_active = gtk_toggle_tool_button_get_active (ttb);

  g_action_change_state (a, g_variant_new_boolean (labels_active));

  GVariant *new_state  = g_action_get_state (a);
  labels_active = g_variant_get_boolean (new_state);
  g_object_set (de->data_editor, "value-labels", labels_active, NULL);
}

static void
on_recent_data_select (GtkMenuShell *menushell,
		       PsppireWindow *window)
{
  gchar *file;

  gchar *uri =
    gtk_recent_chooser_get_current_uri (GTK_RECENT_CHOOSER (menushell));

  file = g_filename_from_uri (uri, NULL, NULL);

  g_free (uri);

  open_data_window (window, file, NULL, NULL);

  g_free (file);
}

static char *
charset_from_mime_type (const char *mime_type)
{
  const char *charset;
  struct string s;
  const char *p;

  if (mime_type == NULL)
    return NULL;

  charset = c_strcasestr (mime_type, "charset=");
  if (charset == NULL)
    return NULL;

  ds_init_empty (&s);
  p = charset + 8;
  if (*p == '"')
    {
      /* Parse a "quoted-string" as defined by RFC 822. */
      for (p++; *p != '\0' && *p != '"'; p++)
        {
          if (*p != '\\')
            ds_put_byte (&s, *p);
          else if (*++p != '\0')
            ds_put_byte (&s, *p);
        }
    }
  else
    {
      /* Parse a "token" as defined by RFC 2045. */
      while (*p > 32 && *p < 127 && strchr ("()<>@,;:\\\"/[]?=", *p) == NULL)
        ds_put_byte (&s, *p++);
    }
  if (!ds_is_empty (&s))
    return ds_steal_cstr (&s);

  ds_destroy (&s);
  return NULL;
}

static void
on_recent_files_select (GtkMenuShell *menushell,   gpointer user_data)
{
  GtkRecentInfo *item;
  char *encoding;
  GtkWidget *se;
  gchar *file;

  /* Get the file name and its encoding. */
  item = gtk_recent_chooser_get_current_item (GTK_RECENT_CHOOSER (menushell));
  file = g_filename_from_uri (gtk_recent_info_get_uri (item), NULL, NULL);
  encoding = charset_from_mime_type (gtk_recent_info_get_mime_type (item));
  gtk_recent_info_unref (item);

  se = psppire_syntax_window_new (encoding);

  free (encoding);

  if ( psppire_window_load (PSPPIRE_WINDOW (se), file, encoding, NULL) ) 
    gtk_widget_show (se);
  else
    gtk_widget_destroy (se);

  g_free (file);
}

static void
set_unsaved (gpointer w)
{
  psppire_window_set_unsaved (PSPPIRE_WINDOW (w));
}


/* Only a data file with at least one variable can be saved. */
static void
enable_save (PsppireDataWindow *dw)
{
  gboolean enable = psppire_dict_get_var_cnt (dw->dict) > 0;

  GAction *save_as = g_action_map_lookup_action (G_ACTION_MAP (dw), "save-as");
  GAction *save = g_action_map_lookup_action (G_ACTION_MAP (dw), "save");

  if (save)
    g_object_set (save, "enabled", enable, NULL);

  if (save_as)
    g_object_set (save_as, "enabled", enable, NULL);
}

/* Initializes as much of a PsppireDataWindow as we can and must before the
   dataset has been set.

   In particular, the 'menu' member is required in case the "filename" property
   is set before the "dataset" property: otherwise PsppireWindow will try to
   modify the menu as part of the "filename" property_set() function and end up
   with a Gtk-CRITICAL since 'menu' is NULL.  */
static void
psppire_data_window_init (PsppireDataWindow *de)
{
  de->builder = builder_new ("data-editor.ui");
}

static void
file_import (PsppireDataWindow *dw)
{
  GtkWidget *w = psppire_import_assistant_new (GTK_WINDOW (dw));
  PsppireImportAssistant *asst = PSPPIRE_IMPORT_ASSISTANT (w);
  gtk_widget_show_all (w);
  
  asst->main_loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (asst->main_loop);
  g_main_loop_unref (asst->main_loop);

  if (!asst->file_name)
    goto end;
  
  switch (asst->response)
    {
    case GTK_RESPONSE_APPLY:
      {
	gchar *fn = g_path_get_basename (asst->file_name);
	open_data_window (PSPPIRE_WINDOW (dw), fn, NULL, psppire_import_assistant_generate_syntax (asst));
	g_free (fn);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      free (paste_syntax_to_window (psppire_import_assistant_generate_syntax (asst)));
      break;
    default:
      break;
    }
    
 end:  
  gtk_widget_destroy (GTK_WIDGET (asst));
}



static void
connect_dialog_action (GType type, PsppireDataWindow *de)
{
  GAction *act = g_object_new (type,
			       "top-level", de,
			       NULL);
  
  g_action_map_add_action (G_ACTION_MAP (de), act);
}

static void
g_action_activate_null (GAction *a)
{
  g_action_activate (a, NULL);
}

static void
connect_action_to_menuitem (GActionMap *map, const gchar *action_name, GtkWidget *w, const gchar *accel)
{
  GAction *a = g_action_map_lookup_action (map, action_name);
  
  if (NULL == a)
    g_error ("Action \"%s\" not found in map", action_name);

  if (accel)
    {
      GtkApplication *app = GTK_APPLICATION (g_application_get_default());

      /* First set the label for the accellerator so that it appears
	 on the menuitem */
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (w));
      guint key;
      GdkModifierType modifier;
      gtk_accelerator_parse (accel, &key, &modifier);
      gtk_accel_label_set_accel (GTK_ACCEL_LABEL (child), key, modifier);

      /* Now tell the application that it must do something when that
	 key combination is pressed */
      const gchar *accels[2];
      accels[0] = accel;
      accels[1] = NULL;

      gchar *detailed_action_name = NULL;
      if (GTK_IS_WINDOW (map))
	detailed_action_name = g_strdup_printf ("win.%s", action_name);
      else if (GTK_IS_APPLICATION (map))
	detailed_action_name = g_strdup_printf ("app.%s", action_name);
      
      gtk_application_set_accels_for_action (app,
					     detailed_action_name,
					     accels);
      free (detailed_action_name);
    }
  
  g_signal_connect_swapped (w, "activate", G_CALLBACK (g_action_activate_null), a);
 }


static void
set_data_page (PsppireDataWindow *dw)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (dw->data_editor), 1);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (dw->data_editor), 0);
}


static void
on_cut (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_edit_cut (ds);
    }
}

static void
on_copy (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_edit_copy (ds);
    }
}

static void
on_paste (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_edit_paste (ds);
    }
}


static void
on_clear_cases (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_edit_clear_cases (ds);
    }
}

static void
on_clear_variables (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_edit_clear_variables (ds);
    }
  else
    {
      psppire_var_sheet_clear_variables (PSPPIRE_VAR_SHEET (dw->data_editor->var_sheet));
    }
}


static void
insert_variable (PsppireDataWindow *dw)
{
  int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (dw->data_editor));
  if (p == 0)
    {
      PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);
      psppire_data_sheet_insert_variable (ds);
    }
  else
    {
      psppire_var_sheet_insert_variable (PSPPIRE_VAR_SHEET (dw->data_editor->var_sheet));
    }
}


static void
insert_case_at_row (PsppireDataWindow *dw)
{
  PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);

  psppire_data_sheet_insert_case (ds);
}

static void
goto_case (PsppireDataWindow *dw)
{
  PsppireDataSheet *ds = psppire_data_editor_get_active_data_sheet (dw->data_editor);

  goto_case_dialog (ds);
}



static GtkWidget *
create_file_menu (PsppireDataWindow *dw)
{
  GtkWidget *menuitem = gtk_menu_item_new_with_mnemonic (_("_File"));
  GtkWidget *menu = gtk_menu_new ();

  {
    GtkWidget *new = gtk_menu_item_new_with_mnemonic (_("_New"));
    gtk_menu_attach (GTK_MENU (menu), new,        0, 1, 0, 1);

    GtkWidget *new_menu = gtk_menu_new ();

    g_object_set (new, "submenu", new_menu, NULL);
	
    GtkWidget *syntax  = gtk_menu_item_new_with_mnemonic (_("_Syntax"));
    connect_action_to_menuitem (G_ACTION_MAP (g_application_get_default ()), "new-syntax", syntax, 0);
    
    GtkWidget *data = gtk_menu_item_new_with_mnemonic (_("_Data"));
    connect_action_to_menuitem (G_ACTION_MAP (g_application_get_default ()), "new-data", data, 0);

    gtk_menu_attach (GTK_MENU (new_menu), syntax,    0, 1, 0, 1);
    gtk_menu_attach (GTK_MENU (new_menu), data,      0, 1, 1, 2);
  }
  
  GtkWidget *open = gtk_menu_item_new_with_mnemonic (_("_Open"));
  connect_action_to_menuitem (G_ACTION_MAP (dw), "open", open, "<Ctrl>O");
  
  GtkWidget *import = gtk_menu_item_new_with_mnemonic (_("_Import Data..."));
  connect_action_to_menuitem (G_ACTION_MAP (dw), "file-import", import, 0);
  
  gtk_menu_attach (GTK_MENU (menu), open,       0, 1, 1, 2);
  gtk_menu_attach (GTK_MENU (menu), import,     0, 1, 2, 3);

  gtk_menu_attach (GTK_MENU (menu), gtk_separator_menu_item_new (), 0, 1, 3, 4);

  GtkWidget *save = gtk_menu_item_new_with_mnemonic (_("_Save..."));
  connect_action_to_menuitem (G_ACTION_MAP (dw), "save", save, "<Ctrl>S");
  
  GtkWidget *save_as = gtk_menu_item_new_with_mnemonic (_("Save _As..."));
  connect_action_to_menuitem (G_ACTION_MAP (dw), "save-as", save_as, "<Shift><Ctrl>S");
  
  GtkWidget *rename_dataset = gtk_menu_item_new_with_mnemonic (_("_Rename Dataset..."));
  connect_action_to_menuitem (G_ACTION_MAP (dw), "rename-dataset", rename_dataset, 0);

  
  gtk_menu_attach (GTK_MENU (menu), save,        0, 1, 4, 5);
  gtk_menu_attach (GTK_MENU (menu), save_as,     0, 1, 5, 6);
  gtk_menu_attach (GTK_MENU (menu), rename_dataset,     0, 1, 6, 7);

  gtk_menu_attach (GTK_MENU (menu), gtk_separator_menu_item_new (), 0, 1, 7, 8);

  {
    GtkWidget *display_data = gtk_menu_item_new_with_mnemonic (_("_Display Data File Information"));
    gtk_menu_attach (GTK_MENU (menu), display_data,     0, 1, 8, 9);

    GtkWidget *dd_menu = gtk_menu_new ();

    g_object_set (display_data, "submenu", dd_menu, NULL);
    
    GtkWidget *working_file  = gtk_menu_item_new_with_mnemonic (_("Working File"));
    connect_action_to_menuitem (G_ACTION_MAP (dw), "info-working", working_file, 0);
    GtkWidget *external_file = gtk_menu_item_new_with_mnemonic (_("_External File..."));
    connect_action_to_menuitem (G_ACTION_MAP (dw), "info-external", external_file, 0);

    gtk_menu_attach (GTK_MENU (dd_menu), working_file,    0, 1, 0, 1);
    gtk_menu_attach (GTK_MENU (dd_menu), external_file,   0, 1, 1, 2);
  }
  
  gtk_menu_attach (GTK_MENU (menu), gtk_separator_menu_item_new (), 0, 1, 9, 10);

  {
    GtkWidget *mi_data = gtk_menu_item_new_with_mnemonic (_("_Recently Used Data"));
    GtkWidget *mi_files = gtk_menu_item_new_with_mnemonic (_("Recently Used _Files"));

    GtkWidget *menu_data = gtk_recent_chooser_menu_new_for_manager (
      gtk_recent_manager_get_default ());

    GtkWidget *menu_files = gtk_recent_chooser_menu_new_for_manager (
      gtk_recent_manager_get_default ());

    gtk_menu_attach (GTK_MENU (menu), mi_data,       0, 1, 10, 11);
    gtk_menu_attach (GTK_MENU (menu), mi_files,      0, 1, 11, 12);
    
    g_object_set (menu_data, "show-tips",  TRUE, NULL);
    g_object_set (menu_files, "show-tips",  TRUE, NULL);

    g_object_set (mi_data, "submenu",  menu_data, NULL);
    g_object_set (mi_files, "submenu", menu_files, NULL);
    
    {
      GtkRecentFilter *filter = gtk_recent_filter_new ();

      gtk_recent_filter_add_mime_type (filter, "application/x-spss-sav");
      gtk_recent_filter_add_mime_type (filter, "application/x-spss-por");

      gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (menu_data), GTK_RECENT_SORT_MRU);

      gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu_data), filter);
    }

    g_signal_connect (menu_data, "selection-done", G_CALLBACK (on_recent_data_select), dw);

    {
      GtkRecentFilter *filter = gtk_recent_filter_new ();

      gtk_recent_filter_add_pattern (filter, "*.sps");
      gtk_recent_filter_add_pattern (filter, "*.SPS");

      gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (menu_files), GTK_RECENT_SORT_MRU);

      gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu_files), filter);
    }

    g_signal_connect (menu_files, "selection-done", G_CALLBACK (on_recent_files_select), dw);
  }

  gtk_menu_attach (GTK_MENU (menu), gtk_separator_menu_item_new (), 0, 1, 12, 13);

  {
    GtkWidget *quit = gtk_menu_item_new_with_mnemonic (_("_Quit"));
    gtk_menu_attach (GTK_MENU (menu), quit,     0, 1, 13, 14);

    connect_action_to_menuitem (G_ACTION_MAP (g_application_get_default ()),
				"quit", quit, "<Ctrl>Q");
  }
  
  g_object_set (menuitem, "submenu", menu, NULL);
  gtk_widget_show_all (menuitem);
  
  return menuitem;
}

static GtkWidget *
create_edit_menu (PsppireDataWindow *dw)
{
  int i = 0;
  GtkWidget *menuitem = gtk_menu_item_new_with_mnemonic (_("_Edit"));

  GtkWidget *menu = gtk_menu_new ();

  dw->mi_insert_var = gtk_menu_item_new_with_mnemonic (_("_Insert Variable"));
  dw->mi_insert_case = gtk_menu_item_new_with_mnemonic (_("_Insert Case"));
  GtkWidget *go_to_variable = gtk_menu_item_new_with_mnemonic (_("_Go To Variable..."));
  dw->mi_go_to_case = gtk_menu_item_new_with_mnemonic (_("_Go To Case..."));

  gtk_menu_attach (GTK_MENU (menu), dw->mi_insert_var,        0, 1, i, i + 1); ++i;
  gtk_menu_attach (GTK_MENU (menu), dw->mi_insert_case,     0, 1, i, i + 1); ++i;

  g_signal_connect_swapped (dw->mi_insert_case, "activate", G_CALLBACK (insert_case_at_row), dw);
  g_signal_connect_swapped (dw->mi_go_to_case, "activate", G_CALLBACK (goto_case), dw);
  g_signal_connect_swapped (dw->mi_insert_var, "activate", G_CALLBACK (insert_variable), dw);

  GAction *a = g_action_map_lookup_action (G_ACTION_MAP (dw),  "PsppireDialogActionVarInfo");
  g_assert (a);
  g_signal_connect_swapped (go_to_variable, "activate", G_CALLBACK (psppire_dialog_action_activate_null), a);
  
  gtk_menu_attach (GTK_MENU (menu), go_to_variable,         0, 1, i, i + 1); ++i;
  gtk_menu_attach (GTK_MENU (menu), dw->mi_go_to_case,      0, 1, i, i + 1); ++i;

  {
    GtkAccelGroup *ag = gtk_accel_group_new ();
    
    dw->mi_edit_separator = gtk_separator_menu_item_new ();
    gtk_menu_attach (GTK_MENU (menu), dw->mi_edit_separator, 0, 1, i, i + 1); ++i;

    dw->mi_cut = gtk_menu_item_new_with_mnemonic (_("Cu_t"));
    gtk_menu_attach (GTK_MENU (menu), dw->mi_cut,     0, 1, i, i + 1); ++i;
    g_signal_connect_swapped (dw->mi_cut, "activate", G_CALLBACK (on_cut), dw);

    gtk_window_add_accel_group (GTK_WINDOW (dw), ag);
    gtk_widget_add_accelerator (dw->mi_cut, "activate", ag,
				'X', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    
    dw->mi_copy = gtk_menu_item_new_with_mnemonic (_("_Copy"));
    gtk_menu_attach (GTK_MENU (menu), dw->mi_copy,     0, 1, i, i + 1); ++i;
    g_signal_connect_swapped (dw->mi_copy, "activate", G_CALLBACK (on_copy), dw);
    gtk_widget_add_accelerator (dw->mi_copy, "activate", ag,
				'C', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	
    dw->mi_paste = gtk_menu_item_new_with_mnemonic (_("_Paste"));
    gtk_menu_attach (GTK_MENU (menu), dw->mi_paste,     0, 1, i, i + 1); ++i;
    g_signal_connect_swapped (dw->mi_paste, "activate", G_CALLBACK (on_paste), dw);
    gtk_widget_add_accelerator (dw->mi_paste, "activate", ag,
				'V', GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    dw->mi_clear_variables = gtk_menu_item_new_with_mnemonic (_("Clear _Variables"));
    gtk_menu_attach (GTK_MENU (menu), dw->mi_clear_variables,     0, 1, i, i + 1); ++i;
    g_signal_connect_swapped (dw->mi_clear_variables, "activate", G_CALLBACK (on_clear_variables), dw);
    
    dw->mi_clear_cases = gtk_menu_item_new_with_mnemonic (_("Cl_ear Cases"));
    gtk_menu_attach (GTK_MENU (menu), dw->mi_clear_cases,     0, 1, i, i + 1); ++i;
    g_signal_connect_swapped (dw->mi_clear_cases, "activate", G_CALLBACK (on_clear_cases), dw);
  }
  
  {
    dw->mi_find_separator = gtk_separator_menu_item_new ();
    gtk_menu_attach (GTK_MENU (menu), dw->mi_find_separator, 0, 1, i, i + 1); ++i;
  
    dw->mi_find = gtk_menu_item_new_with_mnemonic (_("_Find..."));
    g_signal_connect_swapped (dw->mi_find, "activate", G_CALLBACK (find_dialog), dw);
    gtk_menu_attach (GTK_MENU (menu), dw->mi_find,      0, 1,  i, i + 1); ++i;
  }
  
  g_object_set (menuitem, "submenu", menu, NULL);
  
  gtk_widget_show_all (menuitem);
  
  return menuitem;
}


static void
psppire_data_window_finish_init (PsppireDataWindow *de,
                                 struct dataset *ds)
{
  static const struct dataset_callbacks cbs =
    {
      set_unsaved,                    /* changed */
      transformation_change_callback, /* transformations_changed */
    };

  GtkWidget *menubar;
  GtkWidget *hb ;
  GtkWidget *sb ;

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  de->dataset = ds;
  de->dict = psppire_dict_new_from_dict (dataset_dict (ds));
  de->data_store = psppire_data_store_new (de->dict);
  psppire_data_store_set_reader (de->data_store, NULL);

  GObject *menu = get_object_assert (de->builder, "data-editor-menu", G_TYPE_MENU);
  menubar = gtk_menu_bar_new_from_model (G_MENU_MODEL (menu));
  gtk_widget_show (menubar);

  hb = gtk_toolbar_new ();
  sb = get_widget_assert (de->builder, "status-bar");

  de->data_editor =
    PSPPIRE_DATA_EDITOR (psppire_data_editor_new (de->dict, de->data_store));
  
  g_signal_connect (de, "realize",
                    G_CALLBACK (set_data_page), de);

  g_signal_connect_swapped (de->data_store, "case-changed",
			    G_CALLBACK (set_unsaved), de);

  g_signal_connect_swapped (de->data_store, "case-inserted",
			    G_CALLBACK (set_unsaved), de);

  g_signal_connect_swapped (de->data_store, "cases-deleted",
			    G_CALLBACK (set_unsaved), de);

  dataset_set_callbacks (de->dataset, &cbs, de);

  connect_help (de->builder);

  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), hb, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (de->data_editor), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sb, FALSE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (de), box);

  g_signal_connect (de->dict, "weight-changed",
		    G_CALLBACK (on_weight_change),
		    de);

  g_signal_connect (de->dict, "filter-changed",
		    G_CALLBACK (on_filter_change),
		    de);

  g_signal_connect (de->dict, "split-changed",
		    G_CALLBACK (on_split_change),
		    de);

  g_signal_connect_swapped (de->dict, "backend-changed",
                            G_CALLBACK (enable_save), de);
  g_signal_connect_swapped (de->dict, "variable-inserted",
                            G_CALLBACK (enable_save), de);
  g_signal_connect_swapped (de->dict, "variable-deleted",
                            G_CALLBACK (enable_save), de);
  enable_save (de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_SORT,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_SPLIT,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_FLIP,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_WEIGHT,  de);
  
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_COMPUTE,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_COUNT,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_AUTORECODE,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_RANK,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_SELECT,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_RECODE_SAME,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT,  de);

    
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_DESCRIPTIVES,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_FREQUENCIES,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS,  de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_INDEP_SAMPS,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_PAIRED,  de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_MEANS,  de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_TT1S,  de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_ONEWAY, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_UNIVARIATE, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_KMEANS, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_FACTOR, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_CORRELATION, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_RELIABILITY, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_REGRESSION, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_LOGISTIC, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_ROC, de);
  
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_COMMENTS, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_VAR_INFO, de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_BARCHART, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_SCATTERPLOT, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_HISTOGRAM, de);

  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_CHISQUARE, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_BINOMIAL, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_RUNS, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_1SKS, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_TWO_SAMPLE, de);
  connect_dialog_action (PSPPIRE_TYPE_DIALOG_ACTION_K_RELATED, de);

  {
    GSimpleAction *file_import_action = g_simple_action_new ("file-import", NULL);
    g_signal_connect_swapped (file_import_action, "activate", G_CALLBACK (file_import), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (file_import_action));
  }
  
  {
    GSimpleAction *save = g_simple_action_new ("save", NULL);
    g_signal_connect_swapped (save, "activate", G_CALLBACK (psppire_window_save), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (save));
  }

  {
    GSimpleAction *open = g_simple_action_new ("open", NULL);
    g_signal_connect_swapped (open, "activate", G_CALLBACK (psppire_window_open), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (open));
  }

  {
    GSimpleAction *save_as = g_simple_action_new ("save-as", NULL);
    g_signal_connect_swapped (save_as, "activate", G_CALLBACK (psppire_window_save_as), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (save_as));
  }

  {
    GSimpleAction *rename_dataset_act = g_simple_action_new ("rename-dataset", NULL);
    g_signal_connect_swapped (rename_dataset_act, "activate",
			      G_CALLBACK (on_rename_dataset), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (rename_dataset_act));
  }

  {
    GSimpleAction *info_working = g_simple_action_new ("info-working", NULL);
    g_signal_connect_swapped (info_working, "activate", G_CALLBACK (display_dict), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (info_working));
  }
  {
    GSimpleAction *info_external = g_simple_action_new ("info-external", NULL);
    g_signal_connect_swapped (info_external, "activate", G_CALLBACK (sysfile_info), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (info_external));
  }

  {
    GSimpleAction *act_statusbar = g_simple_action_new_stateful ("statusbar", NULL, g_variant_new_boolean (TRUE));
    g_signal_connect (act_statusbar, "activate", G_CALLBACK (status_bar_activate), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_statusbar));
  }

  {
    GSimpleAction *act_gridlines = g_simple_action_new_stateful ("gridlines", NULL, g_variant_new_boolean (TRUE));
    g_signal_connect (act_gridlines, "activate", G_CALLBACK (grid_lines_activate), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_gridlines));
  }

  
  {
    GSimpleAction *act_view_data = g_simple_action_new_stateful ("view_dv", G_VARIANT_TYPE_STRING,
								 g_variant_new_string ("DATA"));
    g_signal_connect (act_view_data, "activate", G_CALLBACK (activate_change_view), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_view_data));
  }

  {
    GSimpleAction *act_fonts = g_simple_action_new ("fonts", NULL);
    g_signal_connect_swapped (act_fonts, "activate", G_CALLBACK (fonts_activate), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_fonts));
  }

  {
    GSimpleAction *act_value_labels =
      g_simple_action_new_stateful ("value_labels", NULL,
				    g_variant_new_boolean (FALSE));
    g_signal_connect (act_value_labels, "activate", G_CALLBACK (value_labels_activate), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_value_labels));
  }

  {
    GSimpleAction *act_transform_pending = g_simple_action_new ("transform-pending", NULL);
    g_signal_connect_swapped (act_transform_pending, "activate", G_CALLBACK (execute), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_transform_pending));
  }

  {
    GSimpleAction *act_jump_to_variable = g_simple_action_new ("jump-to-variable", NULL);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_jump_to_variable));
  }

  {
    GSimpleAction *act_insert_variable = g_simple_action_new ("insert-variable", NULL);
    g_signal_connect_swapped (act_insert_variable, "activate", G_CALLBACK (insert_variable), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_insert_variable));
  }

  {
    GSimpleAction *act_jump_to_case = g_simple_action_new ("jump-to-case", NULL);
    g_signal_connect_swapped (act_jump_to_case, "activate", G_CALLBACK (goto_case), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_jump_to_case));
  }

  {
    GSimpleAction *act_insert_case = g_simple_action_new ("insert-case", NULL);
    g_signal_connect_swapped (act_insert_case, "activate", G_CALLBACK (insert_case_at_row), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (act_insert_case));
  }

  {
    GSimpleAction *find = g_simple_action_new ("find", NULL);
    g_signal_connect_swapped (find, "activate", G_CALLBACK (find_dialog), de);
    g_action_map_add_action (G_ACTION_MAP (de), G_ACTION (find));
  }
  
  {
    int idx = 0;
    {
      GtkToolItem *ti = gtk_tool_button_new (NULL, "Open");
      g_signal_connect_swapped (ti, "clicked", G_CALLBACK (psppire_window_open), de);
      gtk_toolbar_insert (GTK_TOOLBAR (hb), ti, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (ti), "file-open-data");
    }

    {
      GtkToolItem *ti = gtk_tool_button_new (NULL, "Save");
      g_signal_connect_swapped (ti, "clicked", G_CALLBACK (psppire_window_save), de);
      gtk_toolbar_insert (GTK_TOOLBAR (hb), ti, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (ti), "file-save-data");
    }

    gtk_toolbar_insert (GTK_TOOLBAR (hb), gtk_separator_tool_item_new (), idx++);

    {
      de->ti_jump_to_variable = gtk_tool_button_new (NULL, "Goto Var");

      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),  "PsppireDialogActionVarInfo");
      g_assert (a);
      g_signal_connect_swapped (de->ti_jump_to_variable, "clicked",
				G_CALLBACK (psppire_dialog_action_activate_null), a);

      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_jump_to_variable, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_jump_to_variable), "edit-go-to-variable");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_jump_to_variable), _("Jump to variable"));
    }

    {
      de->ti_jump_to_case = gtk_tool_button_new (NULL, "Jump to Case");
      
      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),  "jump-to-case");
      g_assert (a);
      g_signal_connect_swapped (de->ti_jump_to_case, "clicked",
				G_CALLBACK (g_action_activate_null), a);
      
      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_jump_to_case, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_jump_to_case), "edit-go-to-case");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_jump_to_case), _("Jump to a case in the data sheet"));
    }

    {
      de->ti_find = gtk_tool_button_new (NULL, "Find");

      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),  "find");
      g_assert (a);
      g_signal_connect_swapped (de->ti_find, "clicked",
				G_CALLBACK (g_action_activate_null), a);

      
      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_find, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_find), "edit-find");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_find), _("Search for values in the data"));
    }

    {
      de->ti_insert_case = gtk_tool_button_new (NULL, "Create Case");
      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),  "insert-case");
      g_assert (a);
      g_signal_connect_swapped (de->ti_insert_case, "clicked",
				G_CALLBACK (g_action_activate_null), a);

      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_insert_case, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_insert_case), "edit-insert-case");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_insert_case), _("Create a new case at the current position"));
    }

    {
      de->ti_insert_variable = gtk_tool_button_new (NULL, "Create Variable");
      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),  "insert-variable");
      g_assert (a);
      g_signal_connect_swapped (de->ti_insert_variable, "clicked",
				G_CALLBACK (g_action_activate_null), a);

      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_insert_variable, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_insert_variable), "edit-insert-variable");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_insert_variable), _("Create a new variable at the current position"));
    }

    gtk_toolbar_insert (GTK_TOOLBAR (hb), gtk_separator_tool_item_new (), idx++);

    {
      GtkToolItem *ti = gtk_tool_button_new (NULL, "Split");
      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),
					       "PsppireDialogActionSplit");
      g_assert (a);
      g_signal_connect_swapped (ti, "clicked",
				G_CALLBACK (psppire_dialog_action_activate_null), a);
      gtk_toolbar_insert (GTK_TOOLBAR (hb), ti, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (ti), "data-split-file");
      gtk_widget_set_tooltip_text (GTK_WIDGET (ti), _("Split the active dataset"));
    }

    {
      GtkToolItem *ti = gtk_tool_button_new (NULL, "Weight");
      GAction *a = g_action_map_lookup_action (G_ACTION_MAP (de),
					       "PsppireDialogActionWeight");
      g_assert (a);
      g_signal_connect_swapped (ti, "clicked",
				G_CALLBACK (psppire_dialog_action_activate_null), a);
      gtk_toolbar_insert (GTK_TOOLBAR (hb), ti, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (ti), "data-weight-cases");
      gtk_widget_set_tooltip_text (GTK_WIDGET (ti), _("Weight cases by variable"));
    }

    {
      de->ti_value_labels_button = gtk_toggle_tool_button_new ();
      gtk_tool_button_set_label (GTK_TOOL_BUTTON (de->ti_value_labels_button),
				 "Value Labels");
      g_signal_connect (de->ti_value_labels_button, "toggled",
			G_CALLBACK (on_labels_button_toggle), de);
      gtk_toolbar_insert (GTK_TOOLBAR (hb), de->ti_value_labels_button, idx++);
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (de->ti_value_labels_button), "view-value-labels");
      gtk_widget_set_tooltip_text (GTK_WIDGET (de->ti_value_labels_button), _("Show/hide value labels"));
    }
  }


  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);

  gtk_menu_shell_insert (GTK_MENU_SHELL (menubar),  create_file_menu (de), 0);
  gtk_menu_shell_insert (GTK_MENU_SHELL (menubar),  create_edit_menu (de), 1);
  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),  create_windows_menu (GTK_WINDOW (de)));
  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),  create_help_menu (GTK_WINDOW (de)));

  g_signal_connect (de->data_editor, "switch-page",
                    G_CALLBACK (on_switch_page), de);

  gtk_widget_show (GTK_WIDGET (de->data_editor));
  gtk_widget_show_all (box);

  ll_push_head (&all_data_windows, &de->ll);
}

static void
psppire_data_window_dispose (GObject *object)
{
  PsppireDataWindow *dw = PSPPIRE_DATA_WINDOW (object);

  if (dw->builder != NULL)
    {
      g_object_unref (dw->builder);
      dw->builder = NULL;
    }

  if (dw->dict)
    {
      g_signal_handlers_disconnect_by_func (dw->dict,
                                            G_CALLBACK (enable_save), dw);
      g_signal_handlers_disconnect_by_func (dw->dict,
                                            G_CALLBACK (on_weight_change), dw);
      g_signal_handlers_disconnect_by_func (dw->dict,
                                            G_CALLBACK (on_filter_change), dw);
      g_signal_handlers_disconnect_by_func (dw->dict,
                                            G_CALLBACK (on_split_change), dw);

      g_object_unref (dw->dict);
      dw->dict = NULL;
    }

  if (dw->data_store)
    {
      g_object_unref (dw->data_store);
      dw->data_store = NULL;
    }

  if (dw->ll.next != NULL)
    {
      ll_remove (&dw->ll);
      dw->ll.next = NULL;
    }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
psppire_data_window_finalize (GObject *object)
{
  PsppireDataWindow *dw = PSPPIRE_DATA_WINDOW (object);

  if (dw->dataset)
    {
      struct dataset *dataset = dw->dataset;
      struct session *session = dataset_session (dataset);

      dw->dataset = NULL;

      dataset_set_callbacks (dataset, NULL, NULL);
      session_set_active_dataset (session, NULL);
      dataset_destroy (dataset);
    }

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_data_window_set_property (GObject         *object,
                                  guint            prop_id,
                                  const GValue    *value,
                                  GParamSpec      *pspec)
{
  PsppireDataWindow *window = PSPPIRE_DATA_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DATASET:
      psppire_data_window_finish_init (window, g_value_get_pointer (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_data_window_get_property (GObject         *object,
                                  guint            prop_id,
                                  GValue          *value,
                                  GParamSpec      *pspec)
{
  PsppireDataWindow *window = PSPPIRE_DATA_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DATASET:
      g_value_set_pointer (value, window->dataset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


GtkWidget*
psppire_data_window_new (struct dataset *ds)
{
  GtkWidget *dw;

  if (the_session == NULL)
    the_session = session_create (NULL);

  if (ds == NULL)
    {
      char *dataset_name = session_generate_dataset_name (the_session);
      ds = dataset_create (the_session, dataset_name);
      free (dataset_name);
    }
  assert (dataset_session (ds) == the_session);

  dw = GTK_WIDGET (
		   g_object_new (
				 psppire_data_window_get_type (),
				 "description", _("Data Editor"),
				 "dataset", ds,
				 NULL));

  if (dataset_name (ds) != NULL)
    g_object_set (dw, "id", dataset_name (ds), (void *) NULL);


  GApplication *app = g_application_get_default ();
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (dw));
  
  return dw;
}

bool
psppire_data_window_is_empty (PsppireDataWindow *dw)
{
  return psppire_dict_get_var_cnt (dw->dict) == 0;
}

static void
psppire_data_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = save_file;
  iface->pick_filename = data_pick_filename;
  iface->load = load_file;
}



PsppireDataWindow *
psppire_default_data_window (void)
{
  if (ll_is_empty (&all_data_windows))
    create_data_window ();
  return ll_data (ll_head (&all_data_windows), PsppireDataWindow, ll);
}

void
psppire_data_window_set_default (PsppireDataWindow *pdw)
{
  ll_remove (&pdw->ll);
  ll_push_head (&all_data_windows, &pdw->ll);
}

void
psppire_data_window_undefault (PsppireDataWindow *pdw)
{
  ll_remove (&pdw->ll);
  ll_push_tail (&all_data_windows, &pdw->ll);
}

PsppireDataWindow *
psppire_data_window_for_dataset (struct dataset *ds)
{
  PsppireDataWindow *pdw;

  ll_for_each (pdw, PsppireDataWindow, ll, &all_data_windows)
    if (pdw->dataset == ds)
      return pdw;

  return NULL;
}

PsppireDataWindow *
psppire_data_window_for_data_store (PsppireDataStore *data_store)
{
  PsppireDataWindow *pdw;

  ll_for_each (pdw, PsppireDataWindow, ll, &all_data_windows)
    if (pdw->data_store == data_store)
      return pdw;

  return NULL;
}

GtkWindow *
create_data_window (void)
{
  GtkWidget *w = psppire_data_window_new (NULL);

  gtk_widget_show (w);
  
  return GTK_WINDOW (w);
}

GtkWindow *
open_data_window (PsppireWindow *victim, const char *file_name,
                  const char *encoding, gpointer hint)
{
  GtkWidget *window;

  if (PSPPIRE_IS_DATA_WINDOW (victim)
      && psppire_data_window_is_empty (PSPPIRE_DATA_WINDOW (victim)))
    {
      window = GTK_WIDGET (victim);
      gtk_widget_hide (GTK_WIDGET (PSPPIRE_DATA_WINDOW (window)->data_editor));
    }
  else
    window = psppire_data_window_new (NULL);

  psppire_window_load (PSPPIRE_WINDOW (window), file_name, encoding, hint);
  gtk_widget_show_all (window);
  return GTK_WINDOW (window);
}
