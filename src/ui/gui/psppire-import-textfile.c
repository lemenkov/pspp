/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2015, 2016, 2017, 2018, 2020  Free Software Foundation

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

#include "psppire-import-textfile.h"
#include <gtk/gtk.h>

#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "libpspp/hmap.h"
#include "libpspp/hash-functions.h"
#include "libpspp/str.h"
#include "libpspp/misc.h"

#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "data/data-in.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"

#include "builder-wrapper.h"

#include "psppire-data-store.h"
#include "psppire-scanf.h"

#include "ui/syntax-gen.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Chooses a name for each column on the separators page */
static void choose_column_names (PsppireImportAssistant *ia);

/* Revises the contents of the fields tree view based on the
   currently chosen set of separators. */
static void
revise_fields_preview (PsppireImportAssistant *ia)
{
  choose_column_names (ia);
}


struct separator_count_node
{
  struct hmap_node node;
  int occurance; /* The number of times the separator occurs in a line */
  int quantity;  /* The number of lines with this occurance */
};


/* Picks the most likely separator and quote characters based on
   IA's file data. */
static void
choose_likely_separators (PsppireImportAssistant *ia)
{
  gint first_line = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_line, NULL);

  gboolean valid;
  GtkTreeIter iter;

  struct hmap count_map[SEPARATOR_CNT];
  for (int j = 0; j < SEPARATOR_CNT; ++j)
    hmap_init (count_map + j);

  GtkTreePath *p = gtk_tree_path_new_from_indices (first_line, -1);

  for (valid = gtk_tree_model_get_iter (GTK_TREE_MODEL (ia->text_file), &iter, p);
       valid;
       valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->text_file), &iter))
    {
      gchar *line_text = NULL;
      gtk_tree_model_get (GTK_TREE_MODEL (ia->text_file), &iter, 1, &line_text, -1);

      gint *counts = xzalloc (sizeof *counts * SEPARATOR_CNT);

      struct substring cs = ss_cstr (line_text);
      for (;
	   UINT32_MAX != ss_first_mb (cs);
	   ss_get_mb (&cs))
	{
	  ucs4_t character = ss_first_mb (cs);

	  int s;
	  for (s = 0; s < SEPARATOR_CNT; ++s)
	    {
	      if (character == separators[s].c)
		counts[s]++;
	    }
	}

      int j;
      for (j = 0; j < SEPARATOR_CNT; ++j)
	{
	  if (counts[j] > 0)
	    {
	      struct separator_count_node *cn = NULL;
	      unsigned int hash = hash_int (counts[j], 0);
	      HMAP_FOR_EACH_WITH_HASH (cn, struct separator_count_node, node, hash, &count_map[j])
		{
		  if (cn->occurance == counts[j])
		    break;
		}

	      if (cn == NULL)
		{
		  struct separator_count_node *new_cn = xzalloc (sizeof *new_cn);
		  new_cn->occurance = counts[j];
		  new_cn->quantity = 1;
		  hmap_insert (&count_map[j], &new_cn->node, hash);
		}
	      else
		cn->quantity++;
	    }
	}

      free (line_text);
      free (counts);
    }
  gtk_tree_path_free (p);

  if (hmap_count (count_map) > 0)
    {
      int most_frequent = -1;
      int largest = 0;
      for (int j = 0; j < SEPARATOR_CNT; ++j)
        {
          struct separator_count_node *cn;
          struct separator_count_node *next;
          HMAP_FOR_EACH_SAFE (cn, next, struct separator_count_node, node, &count_map[j])
            {
              if (largest < cn->quantity)
                {
                  largest = cn->quantity;
                  most_frequent = j;
                }
              free (cn);
            }
          hmap_destroy (&count_map[j]);
        }

      g_return_if_fail (most_frequent >= 0);

      GtkWidget *toggle =
        get_widget_assert (ia->text_builder, separators[most_frequent].name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
    }
}

static void
repopulate_delimiter_columns (PsppireImportAssistant *ia)
{
  /* Remove all the columns */
  while (gtk_tree_view_get_n_columns (GTK_TREE_VIEW (ia->fields_tree_view)) > 0)
    {
      GtkTreeViewColumn *tvc = gtk_tree_view_get_column (GTK_TREE_VIEW (ia->fields_tree_view), 0);
      gtk_tree_view_remove_column (GTK_TREE_VIEW (ia->fields_tree_view), tvc);
    }

  gint n_fields =
    gtk_tree_model_get_n_columns (GTK_TREE_MODEL (ia->delimiters_model));

  /* ... and put them back again. */
  gint f;
  for (f = gtk_tree_view_get_n_columns (GTK_TREE_VIEW (ia->fields_tree_view));
       f < n_fields; f++)
    {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

      const gchar *title = NULL;

      if (f == 0)
	title = _("line");
      else
	{
	  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)))
	    {
	      title =
		psppire_delimited_text_get_header_title
		(PSPPIRE_DELIMITED_TEXT (ia->delimiters_model), f - 1);
	    }
	  if (title == NULL)
	    title = _("var");
	}

      GtkTreeViewColumn *column =
	gtk_tree_view_column_new_with_attributes (title,
						  renderer,
						  "text", f,
						  NULL);
      g_object_set (column,
		    "resizable", TRUE,
		    "sizing", GTK_TREE_VIEW_COLUMN_AUTOSIZE,
		    NULL);

      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->fields_tree_view), column);
    }
}

static void
reset_tree_view_model (PsppireImportAssistant *ia)
{
  GtkTreeModel *tm = gtk_tree_view_get_model (GTK_TREE_VIEW (ia->fields_tree_view));
  g_object_ref (tm);
  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view), NULL);


  repopulate_delimiter_columns (ia);

  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view), tm);
  //  gtk_tree_view_columns_autosize (GTK_TREE_VIEW (ia->fields_tree_view));

  g_object_unref (tm);
}

/* Resets IA's intro page to its initial state. */
static void
reset_intro_page (PsppireImportAssistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->n_cases_button),
                                TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->percent_button),
                                TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->all_cases_button),
                                TRUE);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (ia->n_cases_spin), 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (ia->percent_spin), 0);
}

/* Called when one of the radio buttons is clicked. */
static void
on_intro_amount_changed (PsppireImportAssistant *ia)
{
  gtk_widget_set_sensitive (ia->n_cases_spin,
			    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)));

  gtk_widget_set_sensitive (ia->percent_spin,
			    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->percent_button)));
}

static void
on_treeview_selection_change (PsppireImportAssistant *ia)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (ia->first_line_tree_view));
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gint max_lines;
      int n;
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      gint *index = gtk_tree_path_get_indices (path);
      n = *index;
      gtk_tree_path_free (path);
      g_object_get (model, "maximum-lines", &max_lines, NULL);
      gtk_widget_set_sensitive (ia->variable_names_cb,
				(n > 0 && n < max_lines));
      ia->delimiters_model =
	psppire_delimited_text_new (GTK_TREE_MODEL (ia->text_file));
      g_object_set (ia->delimiters_model, "first-line", n, NULL);
    }
}

static void
render_text_preview_line (GtkTreeViewColumn *tree_column,
		GtkCellRenderer *cell,
		GtkTreeModel *tree_model,
		GtkTreeIter *iter,
		gpointer data)
{
  /*
     Set the text  to a "insensitive" state if the row
     is greater than what the user declared to be the maximum.
  */
  GtkTreePath *path = gtk_tree_model_get_path (tree_model, iter);
  gint *ii = gtk_tree_path_get_indices (path);
  gint max_lines;
  g_object_get (tree_model, "maximum-lines", &max_lines, NULL);
  g_object_set (cell, "sensitive", (*ii < max_lines), NULL);
  gtk_tree_path_free (path);
}

/* Resets IA's "first line" page to its initial state. */
static void
reset_first_line_page (PsppireImportAssistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb), FALSE);

  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (ia->first_line_tree_view));

  gtk_tree_selection_unselect_all (selection);
}

/* Initializes IA's first_line substructure. */
void
first_line_page_create (PsppireImportAssistant *ia)
{
  GtkWidget *w =  get_widget_assert (ia->text_builder, "FirstLine");

  g_object_set_data (G_OBJECT (w), "on-entering", on_treeview_selection_change);
  g_object_set_data (G_OBJECT (w), "on-reset", reset_first_line_page);

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONTENT, _("Select the First Line"));

  GtkWidget *scrolled_window = get_widget_assert (ia->text_builder, "first-line-scroller");

  if (ia->first_line_tree_view == NULL)
    {
      ia->first_line_tree_view = gtk_tree_view_new ();
      g_object_set (ia->first_line_tree_view, "enable-search", FALSE, NULL);

      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (ia->first_line_tree_view), TRUE);

      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (_("Line"), renderer,
									    "text", 0,
									    NULL);

      gtk_tree_view_column_set_cell_data_func (column, renderer, render_text_preview_line, ia, 0);
      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      renderer = gtk_cell_renderer_text_new ();
      column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer, "text", 1, NULL);
      gtk_tree_view_column_set_cell_data_func (column, renderer, render_text_preview_line, ia, 0);

      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      g_signal_connect_swapped (ia->first_line_tree_view, "cursor-changed",
				G_CALLBACK (on_treeview_selection_change), ia);
      gtk_container_add (GTK_CONTAINER (scrolled_window), ia->first_line_tree_view);
    }

  gtk_widget_show_all (scrolled_window);

  ia->variable_names_cb = get_widget_assert (ia->text_builder, "variable-names");

  reset_first_line_page (ia);
}

static void
intro_on_leave (PsppireImportAssistant *ia, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
  if (dir != IMPORT_ASSISTANT_FORWARDS)
    return;

  gint lc = 0;
  g_object_get (ia->text_file, "line-count", &lc, NULL);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)))
    {
      gint max_lines = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin));
      g_object_set (ia->text_file, "maximum-lines", max_lines, NULL);
    }
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->percent_button)))
    {
      gdouble percent = gtk_spin_button_get_value (GTK_SPIN_BUTTON (ia->percent_spin));
      g_object_set (ia->text_file, "maximum-lines", (gint) (lc * percent / 100.0), NULL);
    }
  else
    {
      g_object_set (ia->text_file, "maximum-lines", lc, NULL);
    }
}


static void
intro_on_enter (PsppireImportAssistant *ia, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
  GtkBuilder *builder = ia->text_builder;
  GtkWidget *table  = get_widget_assert (builder, "button-table");

  struct string s;

  ds_init_empty (&s);
  ds_put_cstr (&s, _("This assistant will guide you through the process of "
                     "importing data into PSPP from a text file with one line "
                     "per case,  in which fields are separated by tabs, "
                     "commas, or other delimiters.\n\n"));

  if (ia->text_file)
    {
      if (ia->text_file->total_is_exact)
	{
	  ds_put_format (
			 &s, ngettext ("The selected file contains %'lu line of text.  ",
				       "The selected file contains %'lu lines of text.  ",
				       ia->text_file->total_lines),
			 ia->text_file->total_lines);
	}
      else if (ia->text_file->total_lines > 0)
	{
	  ds_put_format (
			 &s, ngettext (
				       "The selected file contains approximately %'lu line of text.  ",
				       "The selected file contains approximately %'lu lines of text.  ",
				       ia->text_file->total_lines),
			 ia->text_file->total_lines);
	  ds_put_format (
			 &s, ngettext (
				       "Only the first %zu line of the file will be shown for "
				       "preview purposes in the following screens.  ",
				       "Only the first %zu lines of the file will be shown for "
				       "preview purposes in the following screens.  ",
				       ia->text_file->line_cnt),
			 ia->text_file->line_cnt);
	}
    }

  ds_put_cstr (&s, _("You may choose below how much of the file should "
                     "actually be imported."));

  gtk_label_set_text (GTK_LABEL (get_widget_assert (builder, "intro-label")),
                      ds_cstr (&s));
  ds_destroy (&s);

  if (gtk_grid_get_child_at (GTK_GRID (table), 1, 1) == NULL)
    {
      GtkWidget *hbox_n_cases = psppire_scanf_new (_("Only the first %4d cases"), &ia->n_cases_spin);
      gtk_grid_attach (GTK_GRID (table), hbox_n_cases,
		       1, 1,
		       1, 1);
    }

  GtkAdjustment *adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (ia->n_cases_spin));
  gtk_adjustment_set_lower (adj, 1.0);

  if (gtk_grid_get_child_at (GTK_GRID (table), 1, 2) == NULL)
    {
      GtkWidget *hbox_percent = psppire_scanf_new (_("Only the first %3d %% of file (approximately)"),
        					   &ia->percent_spin);

      gtk_grid_attach (GTK_GRID (table), hbox_percent,
		       1, 2,
		       1, 1);
    }

  gtk_widget_show_all (table);


  if (dir != IMPORT_ASSISTANT_FORWARDS)
    return;

  reset_intro_page (ia);
  on_intro_amount_changed (ia);
}

/* Initializes IA's intro substructure. */
void
intro_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->text_builder;

  GtkWidget *w = get_widget_assert (builder, "Intro");

  ia->percent_spin = gtk_spin_button_new_with_range (0, 100, 1);

  add_page_to_assistant (ia, w,  GTK_ASSISTANT_PAGE_CONTENT, _("Select the Lines to Import"));

  ia->all_cases_button = get_widget_assert (builder, "import-all-cases");
  ia->n_cases_button = get_widget_assert (builder, "import-n-cases");
  ia->percent_button = get_widget_assert (builder, "import-percent");

  g_signal_connect_swapped (ia->all_cases_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (ia->n_cases_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (ia->percent_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);

  g_object_set_data (G_OBJECT (w), "on-leaving", intro_on_leave);
  g_object_set_data (G_OBJECT (w), "on-entering", intro_on_enter);
  g_object_set_data (G_OBJECT (w), "on-reset", reset_intro_page);
}




/* Chooses a name for each column on the separators page */
static void
choose_column_names (PsppireImportAssistant *ia)
{
  int i;
  unsigned long int generated_name_count = 0;
  char *encoding = NULL;
  g_object_get (ia->text_file, "encoding", &encoding, NULL);
  if (ia->dict)
    dict_unref (ia->dict);
  ia->dict = dict_create (encoding ? encoding : UTF8);
  g_free (encoding);

  for (i = 0;
       i < gtk_tree_model_get_n_columns (GTK_TREE_MODEL (ia->delimiters_model)) - 1;
       ++i)
    {
      const gchar *candidate_name = NULL;

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)))
	{
	  candidate_name = psppire_delimited_text_get_header_title (PSPPIRE_DELIMITED_TEXT (ia->delimiters_model), i);
	}

      char *name = dict_make_unique_var_name (ia->dict,
					      candidate_name,
					      &generated_name_count);

      dict_create_var_assert (ia->dict, name, 0);
      free (name);
    }
}

/* Called when the user toggles one of the separators
   checkboxes. */
static void
on_separator_toggle (GtkToggleButton *toggle UNUSED,
                     PsppireImportAssistant *ia)
{
  int i;
  GSList *delimiters = NULL;
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->text_builder, s->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
	{
	  delimiters = g_slist_prepend (delimiters,  GINT_TO_POINTER (s->c));
	}
    }

  g_object_set (ia->delimiters_model, "delimiters", delimiters, NULL);

  revise_fields_preview (ia);
}


/* Called when the user changes the entry field for custom
   separators. */
static void
on_separators_custom_entry_notify (GObject *gobject UNUSED,
                                   GParamSpec *arg1 UNUSED,
                                   PsppireImportAssistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables custom
   separators. */
static void
on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                PsppireImportAssistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (custom_cb);
  gtk_widget_set_sensitive (ia->custom_entry, is_active);
  revise_fields_preview (ia);
}

/* Called when the user changes the selection in the combo box
   that selects a quote character. */
static void
on_quote_combo_change (GtkComboBox *combo, PsppireImportAssistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables
   quoting. */
static void
on_quote_cb_toggle (GtkToggleButton *quote_cb, PsppireImportAssistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (quote_cb);
  gtk_widget_set_sensitive (ia->quote_combo, is_active);
  revise_fields_preview (ia);
}


/* Called when the Reset button is clicked. */
static void
reset_separators_page (PsppireImportAssistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->custom_cb), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->quote_cb), FALSE);
  gtk_entry_set_text (GTK_ENTRY (ia->custom_entry), "");

  for (gint i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->text_builder, s->name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
    }

  repopulate_delimiter_columns (ia);

  revise_fields_preview (ia);
  choose_likely_separators (ia);
}

/* Called just before the separators page becomes visible in the
   assistant. */
static void
prepare_separators_page (PsppireImportAssistant *ia, GtkWidget *new_page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
  if (dir != IMPORT_ASSISTANT_FORWARDS)
    return;

  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view),
			   GTK_TREE_MODEL (ia->delimiters_model));

  g_signal_connect_swapped (GTK_TREE_MODEL (ia->delimiters_model), "notify::delimiters",
  			G_CALLBACK (reset_tree_view_model), ia);


  reset_separators_page (ia);
}


/* Initializes IA's separators substructure. */
void
separators_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->text_builder;

  size_t i;

  GtkWidget *w = get_widget_assert (builder, "Separators");

  g_object_set_data (G_OBJECT (w), "on-entering", prepare_separators_page);
  g_object_set_data (G_OBJECT (w), "on-reset", reset_separators_page);

  add_page_to_assistant (ia, w,   GTK_ASSISTANT_PAGE_CONTENT, _("Choose Separators"));

  ia->custom_cb = get_widget_assert (builder, "custom-cb");
  ia->custom_entry = get_widget_assert (builder, "custom-entry");
  ia->quote_combo = get_widget_assert (builder, "quote-combo");
  ia->quote_cb = get_widget_assert (builder, "quote-cb");

  gtk_widget_set_sensitive (ia->custom_entry,
			    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->custom_cb)));

  gtk_combo_box_set_active (GTK_COMBO_BOX (ia->quote_combo), 0);

  if (ia->fields_tree_view == NULL)
    {
      GtkWidget *scroller = get_widget_assert (ia->text_builder, "fields-scroller");
      ia->fields_tree_view = gtk_tree_view_new ();
      g_object_set (ia->fields_tree_view, "enable-search", FALSE, NULL);
      gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (ia->fields_tree_view));
      gtk_widget_show_all (scroller);
    }

  g_signal_connect (ia->quote_combo, "changed",
                    G_CALLBACK (on_quote_combo_change), ia);
  g_signal_connect (ia->quote_cb, "toggled",
                    G_CALLBACK (on_quote_cb_toggle), ia);
  g_signal_connect (ia->custom_entry, "notify::text",
                    G_CALLBACK (on_separators_custom_entry_notify), ia);
  g_signal_connect (ia->custom_cb, "toggled",
                    G_CALLBACK (on_separators_custom_cb_toggle), ia);
  for (i = 0; i < SEPARATOR_CNT; i++)
    g_signal_connect (get_widget_assert (builder, separators[i].name),
                      "toggled", G_CALLBACK (on_separator_toggle), ia);

  reset_separators_page (ia);
}



static struct casereader_random_class my_casereader_class;

static struct ccase *
my_read (struct casereader *reader, void *aux, casenumber idx)
{
  PsppireImportAssistant *ia = PSPPIRE_IMPORT_ASSISTANT (aux);
  GtkTreeModel *tm = GTK_TREE_MODEL (ia->delimiters_model);

  GtkTreePath *tp = gtk_tree_path_new_from_indices (idx, -1);

  const struct caseproto *proto = casereader_get_proto (reader);

  GtkTreeIter iter;
  struct ccase *c = NULL;
  if (gtk_tree_model_get_iter (tm, &iter, tp))
    {
      c = case_create (proto);
      int i;
      for (i = 0 ; i < caseproto_get_n_widths (proto); ++i)
	{
	  GValue value = {0};
	  gtk_tree_model_get_value (tm, &iter, i + 1, &value);

	  const struct variable *var = dict_get_var (ia->casereader_dict, i);

	  const gchar *ss = g_value_get_string (&value);
	  if (ss)
	    {
	      union value *v = case_data_rw (c, var);
	      /* In this reader we derive the union value from the
		 string in the tree_model. We retrieve the width and format
		 from a dictionary which is stored directly after
		 the reader creation. Changes in ia->dict in the
		 variable window are not reflected here and therefore
		 this is always compatible with the width in the
		 caseproto. See bug #58298 */
	      char *xx = data_in (ss_cstr (ss),
				  "UTF-8",
				  var_get_write_format (var)->type,
				  v, var_get_width (var), "UTF-8");

	      free (xx);
	    }
	  g_value_unset (&value);
	}
    }

  gtk_tree_path_free (tp);

  return c;
}

static void
my_destroy (struct casereader *reader, void *aux)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, reader);
}

static void
my_advance (struct casereader *reader, void *aux, casenumber cnt)
{
  g_print ("%s:%d\n", __FILE__, __LINE__);
}

static struct casereader *
textfile_create_reader (PsppireImportAssistant *ia)
{
  int n_vars = dict_get_var_cnt (ia->dict);

  int i;

  struct fmt_guesser **fg = XCALLOC (n_vars,  struct fmt_guesser *);
  for (i = 0 ; i < n_vars; ++i)
    {
      fg[i] = fmt_guesser_create ();
    }

  gint n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (ia->delimiters_model), NULL);

  GtkTreeIter iter;
  gboolean ok;
  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (ia->delimiters_model), &iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->delimiters_model), &iter))
    {
      for (i = 0 ; i < n_vars; ++i)
	{
	  gchar *s = NULL;
	  gtk_tree_model_get (GTK_TREE_MODEL (ia->delimiters_model), &iter, i+1, &s, -1);
	  if (s)
	    fmt_guesser_add (fg[i], ss_cstr (s));
	  free (s);
	}
    }

  struct caseproto *proto = caseproto_create ();
  for (i = 0 ; i < n_vars; ++i)
    {
      struct fmt_spec fs;
      fmt_guesser_guess (fg[i], &fs);

      fmt_fix (&fs, FMT_FOR_INPUT);

      struct variable *var = dict_get_var (ia->dict, i);

      int width = fmt_var_width (&fs);

      var_set_width_and_formats (var, width,
				 &fs, &fs);

      proto = caseproto_add_width (proto, width);
      fmt_guesser_destroy (fg[i]);
    }

  free (fg);

  struct casereader *cr = casereader_create_random (proto, n_rows, &my_casereader_class,  ia);
  /* Store the dictionary at this point when the casereader is created.
     my_read depends on the dictionary to interpret the strings in the treeview.
     This guarantees that the union value is produced according to the
     caseproto in the reader. */
  ia->casereader_dict = dict_clone (ia->dict);
  caseproto_unref (proto);
  return cr;
}


/* When during import the variable type is changed, the reader is reinitialized
   based on the new dictionary with a fresh caseprototype. The default behaviour
   when a variable type is changed and the column is resized is that the union
   value is interpreted with new variable type and an overlay for that column
   is generated. Here we reinit to the original reader based on strings.
   As a result you can switch from string to numeric to string without loosing
   the string information. */
static void
ia_variable_changed_cb (GObject *obj, gint var_num, guint what,
			const struct variable *oldvar, gpointer data)
{
  PsppireImportAssistant *ia  = PSPPIRE_IMPORT_ASSISTANT (data);

  struct caseproto *proto = caseproto_create();
  for (int i = 0; i < dict_get_var_cnt (ia->dict); i++)
    {
      const struct variable *var = dict_get_var (ia->dict, i);
      int width = var_get_width (var);
      proto = caseproto_add_width (proto, width);
    }

  gint n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (ia->delimiters_model), NULL);

  PsppireDataStore *store = NULL;
  g_object_get (ia->data_sheet, "data-model", &store, NULL);

  struct casereader *cr = casereader_create_random (proto, n_rows,
						    &my_casereader_class, ia);
  psppire_data_store_set_reader (store, cr);
  dict_unref (ia->casereader_dict);
  ia->casereader_dict = dict_clone (ia->dict);
}


/* Set the data model for both the data sheet and the variable sheet.  */
void
textfile_set_data_models (PsppireImportAssistant *ia)
{
  my_casereader_class.read = my_read;
  my_casereader_class.destroy = my_destroy;
  my_casereader_class.advance = my_advance;

  struct casereader *reader = textfile_create_reader (ia);

  PsppireDict *dict = psppire_dict_new_from_dict (ia->dict);
  PsppireDataStore *store = psppire_data_store_new (dict);
  psppire_data_store_set_reader (store, reader);
  g_signal_connect (dict, "variable-changed",
                    G_CALLBACK (ia_variable_changed_cb),
                    ia);

  g_object_set (ia->data_sheet, "data-model", store, NULL);
  g_object_set (ia->var_sheet, "data-model", dict, NULL);
}

static void
first_line_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  gint first_case = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_case, NULL);

  if (first_case > 0)
    ds_put_format (s, "  /FIRSTCASE=%d\n", first_case + 1);
}

/* Emits PSPP syntax to S that applies the dictionary attributes
   (such as missing values and value labels) of the variables in
   DICT.  */
static void
apply_dict (const struct dictionary *dict, struct string *s)
{
  size_t var_cnt = dict_get_var_cnt (dict);

  for (size_t i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const char *name = var_get_name (var);
      enum val_type type = var_get_type (var);
      int width = var_get_width (var);
      enum measure measure = var_get_measure (var);
      enum var_role role = var_get_role (var);
      enum alignment alignment = var_get_alignment (var);
      const struct fmt_spec *format = var_get_print_format (var);

      if (var_has_missing_values (var))
        {
          const struct missing_values *mv = var_get_missing_values (var);
          size_t j;

          syntax_gen_pspp (s, "MISSING VALUES %ss (", name);
          for (j = 0; j < mv_n_values (mv); j++)
            {
              if (j)
                ds_put_cstr (s, ", ");
              syntax_gen_value (s, mv_get_value (mv, j), width, format);
            }

          if (mv_has_range (mv))
            {
              double low, high;
              if (mv_has_value (mv))
                ds_put_cstr (s, ", ");
              mv_get_range (mv, &low, &high);
              syntax_gen_num_range (s, low, high, format);
            }
          ds_put_cstr (s, ").\n");
        }
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          size_t n_labels = val_labs_count (vls);

          syntax_gen_pspp (s, "VALUE LABELS %ss", name);
          for (size_t j = 0; j < n_labels; j++)
            {
              const struct val_lab *vl = labels[j];
              ds_put_cstr (s, "\n  ");
              syntax_gen_value (s, &vl->value, width, format);
              ds_put_byte (s, ' ');
              syntax_gen_string (s, ss_cstr (val_lab_get_escaped_label (vl)));
            }
          free (labels);
          ds_put_cstr (s, ".\n");
        }
      if (var_has_label (var))
        syntax_gen_pspp (s, "VARIABLE LABELS %ss %sq.\n",
                         name, var_get_label (var));
      if (measure != var_default_measure (type))
        syntax_gen_pspp (s, "VARIABLE LEVEL %ss (%ss).\n",
                         name, measure_to_syntax (measure));
      if (role != ROLE_INPUT)
        syntax_gen_pspp (s, "VARIABLE ROLE /%ss %ss.\n",
                         var_role_to_syntax (role), name);
      if (alignment != var_default_alignment (type))
        syntax_gen_pspp (s, "VARIABLE ALIGNMENT %ss (%ss).\n",
                         name, alignment_to_syntax (alignment));
      if (var_get_display_width (var) != var_default_display_width (width))
        syntax_gen_pspp (s, "VARIABLE WIDTH %ss (%d).\n",
                         name, var_get_display_width (var));
    }
}


static void
intro_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  gint first_line = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_line, NULL);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)))
    ds_put_format (s, "SELECT IF ($CASENUM <= %d).\n",
		   gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin)) - first_line);
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->percent_button)))
    ds_put_format (s, "SAMPLE %.4g.\n",
		   gtk_spin_button_get_value (GTK_SPIN_BUTTON (ia->percent_spin)) / 100.0);
}


static void
formats_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  int i;
  int var_cnt;

  g_return_if_fail (ia->dict);

  ds_put_cstr (s, "  /VARIABLES=\n");

  var_cnt = dict_get_var_cnt (ia->dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (ia->dict, i);
      char format_string[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (var_get_print_format (var), format_string);
      ds_put_format (s, "    %s %s%s\n",
		     var_get_name (var), format_string,
		     i == var_cnt - 1 ? "." : "");
    }
}

static void
separators_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  int i;

  ds_put_cstr (s, "  /DELIMITERS=\"");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (get_widget_assert (ia->text_builder, "tab"))))
    ds_put_cstr (s, "\\t");
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *seps = &separators[i];
      GtkWidget *button = get_widget_assert (ia->text_builder, seps->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
	{
	  if (seps->c == '\t')
	    continue;

	  ds_put_byte (s, seps->c);
	}
    }
  ds_put_cstr (s, "\"\n");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->quote_cb)))
    {
      GtkComboBoxText *cbt = GTK_COMBO_BOX_TEXT (ia->quote_combo);
      gchar *quotes = gtk_combo_box_text_get_active_text (cbt);
      if (quotes && *quotes)
        syntax_gen_pspp (s, "  /QUALIFIER=%sq\n", quotes);
      free (quotes);
    }
}


void
text_spec_gen_syntax (PsppireImportAssistant *ia, struct string *s)
{
  gchar *file_name = NULL;
  gchar *encoding = NULL;
  g_object_get (ia->text_file,
                "file-name", &file_name,
                "encoding", &encoding,
                NULL);

  if (file_name == NULL)
    return;

  syntax_gen_pspp (s,
                   "GET DATA"
                   "\n  /TYPE=TXT"
                   "\n  /FILE=%sq\n",
                   file_name);
  if (encoding && strcmp (encoding, "Auto"))
    syntax_gen_pspp (s, "  /ENCODING=%sq\n", encoding);

  ds_put_cstr (s,
               "  /ARRANGEMENT=DELIMITED\n"
               "  /DELCASE=LINE\n");

  first_line_append_syntax (ia, s);
  separators_append_syntax (ia, s);

  formats_append_syntax (ia, s);
  apply_dict (ia->dict, s);
  intro_append_syntax (ia, s);
}
