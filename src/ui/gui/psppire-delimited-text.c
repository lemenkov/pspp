/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017 Free Software Foundation

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
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define P_(msgid) msgid

#include "psppire-delimited-text.h"
#include "psppire-text-file.h"
#include "language/commands/data-parser.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/i18n.h"

#include <gtk/gtk.h>

/* Properties */
enum
  {
    PROP_0,
    PROP_CHILD,
    PROP_DELIMITERS,
    PROP_QUOTE,
    PROP_FIRST_LINE
  };

static struct data_parser *
make_data_parser (PsppireDelimitedText *tf)
{
  struct data_parser *parser = data_parser_create ();
  data_parser_set_type (parser, DP_DELIMITED);
  data_parser_set_span (parser, false);
  data_parser_set_quotes (parser, ss_empty ());
  data_parser_set_quote_escape (parser, true);
  data_parser_set_empty_line_has_field (parser, true);

  bool space = false;
  struct string hard_delimiters = DS_EMPTY_INITIALIZER;
  GSList *del;
  for (del = tf->delimiters; del; del = g_slist_next (del))
    {
      gunichar c = GPOINTER_TO_INT (del->data);
      if (c == ' ')
        space = true;
      else
        ds_put_unichar (&hard_delimiters, c);
    }
  data_parser_set_soft_delimiters (parser, ss_cstr (space ? " " : ""));
  data_parser_set_hard_delimiters (parser, ds_ss (&hard_delimiters));
  ds_destroy (&hard_delimiters);

  if (tf->quote)
    {
      struct string quote = DS_EMPTY_INITIALIZER;
      ds_put_unichar (&quote, tf->quote);
      data_parser_set_quotes (parser, ds_ss (&quote));
      ds_destroy (&quote);
    }
  return parser;
}

static void
count_delims (PsppireDelimitedText *tf)
{
  if (tf->child == NULL)
    return;

  struct data_parser *parser = make_data_parser (tf);

  tf->max_fields = 0;
  GtkTreeIter iter;
  gboolean valid;
  for (valid = gtk_tree_model_get_iter_first (tf->child, &iter);
       valid;
       valid = gtk_tree_model_iter_next (tf->child, &iter))
    {
      gchar *line = NULL;
      gtk_tree_model_get (tf->child, &iter, 1, &line, -1);
      size_t n_fields = data_parser_split (parser, ss_cstr (line), NULL);
      if (n_fields > tf->max_fields)
        tf->max_fields = n_fields;
      g_free (line);
    }

  data_parser_destroy (parser);
}

static void
cache_invalidate (PsppireDelimitedText *tf)
{
  tf->cache_row = -1;
  data_parser_destroy (tf->parser);
  tf->parser = make_data_parser (tf);
}

static void
psppire_delimited_text_set_property (GObject         *object,
                                guint            prop_id,
                                const GValue    *value,
                                GParamSpec      *pspec)
{
  PsppireDelimitedText *tf = PSPPIRE_DELIMITED_TEXT (object);

  switch (prop_id)
    {
    case PROP_FIRST_LINE:
      tf->first_line = g_value_get_int (value);
      break;
    case PROP_CHILD:
      tf->child = g_value_get_object (value);
      g_return_if_fail (PSPPIRE_IS_TEXT_FILE (tf->child));
      break;
    case PROP_DELIMITERS:
      g_slist_free (tf->delimiters);
      tf->delimiters =  g_slist_copy (g_value_get_pointer (value));
      break;
    case PROP_QUOTE:
      tf->quote = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };

  cache_invalidate (tf);
  count_delims (tf);
}

static void
psppire_delimited_text_get_property (GObject         *object,
                                guint            prop_id,
                                GValue          *value,
                                GParamSpec      *pspec)
{
  PsppireDelimitedText *text_file = PSPPIRE_DELIMITED_TEXT (object);

  switch (prop_id)
    {
    case PROP_FIRST_LINE:
      g_value_set_int (value, text_file->first_line);
      break;
    case PROP_DELIMITERS:
      g_value_set_pointer (value, text_file->delimiters);
      break;
    case PROP_QUOTE:
      g_value_set_uint (value, text_file->quote);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void psppire_delimited_text_finalize        (GObject           *object);
static void psppire_delimited_text_dispose        (GObject           *object);

static GObjectClass *parent_class = NULL;

static gint
n_lines (PsppireDelimitedText *file)
{
  PsppireTextFile *child = PSPPIRE_TEXT_FILE (file->child);

  return child->maximum_lines;
}

static gboolean
__tree_get_iter (GtkTreeModel *tree_model,
                 GtkTreeIter *iter,
                 GtkTreePath *path)
{
  PsppireDelimitedText *file = PSPPIRE_DELIMITED_TEXT (tree_model);
  if (path == NULL)
    return FALSE;


  gint *indices = gtk_tree_path_get_indices (path);

  if (!indices)
    return FALSE;

  gint n = *indices;

  gint children = n_lines (file);

  if (n >= children - file->first_line)
    return FALSE;


  iter->user_data = GINT_TO_POINTER (n);
  iter->stamp = file->stamp;

  return TRUE;
}


static gboolean
__tree_iter_next (GtkTreeModel *tree_model,
                  GtkTreeIter *iter)
{
  PsppireDelimitedText *file  = PSPPIRE_DELIMITED_TEXT (tree_model);
  g_return_val_if_fail (file->stamp == iter->stamp, FALSE);

  gint n = GPOINTER_TO_INT (iter->user_data);


  gint children = n_lines (file);

  if (n + 1 >= children - file->first_line)
    return FALSE;

  iter->user_data = GINT_TO_POINTER (n + 1);

  return TRUE;
}


static GType
__tree_get_column_type (GtkTreeModel *tree_model,
                        gint          index)
{
  if (index == 0)
    return G_TYPE_INT;

  return G_TYPE_STRING;
}

static gboolean
__iter_has_child (GtkTreeModel *tree_model,
                  GtkTreeIter  *iter)
{
  return 0;
}


static gboolean
__iter_parent     (GtkTreeModel *tree_model,
                   GtkTreeIter  *iter,
                   GtkTreeIter  *child)
{
  return 0;
}

static GtkTreePath *
__tree_get_path (GtkTreeModel *tree_model,
                 GtkTreeIter  *iter)
{
  PsppireDelimitedText *file  = PSPPIRE_DELIMITED_TEXT (tree_model);
  g_return_val_if_fail (file->stamp == iter->stamp, FALSE);

  gint n = GPOINTER_TO_INT (iter->user_data);

  gint children = n_lines (file);

  if (n >= children - file->first_line)
    return NULL;

  return gtk_tree_path_new_from_indices (n, -1);
}


static gboolean
__iter_children (GtkTreeModel *tree_model,
                              GtkTreeIter *iter,
                              GtkTreeIter *parent)
{
  return 0;
}


static gint
__tree_model_iter_n_children (GtkTreeModel *tree_model,
                              GtkTreeIter *iter)
{
  PsppireDelimitedText *file  = PSPPIRE_DELIMITED_TEXT (tree_model);
  g_assert (iter == NULL);

  gint children = n_lines (file);

  return children - file->first_line;
}

static GtkTreeModelFlags
__tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_DELIMITED_TEXT (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
__tree_model_get_n_columns (GtkTreeModel *tree_model)
{
  PsppireDelimitedText *tf  = PSPPIRE_DELIMITED_TEXT (tree_model);

  /* +1 for the leading line number column */
  return tf->max_fields + 1;
}


static gboolean
__iter_nth_child (GtkTreeModel *tree_model,
                  GtkTreeIter *iter,
                  GtkTreeIter *parent,
                  gint n)
{
  PsppireDelimitedText *file  = PSPPIRE_DELIMITED_TEXT (tree_model);

  g_assert (parent == NULL);

  g_return_val_if_fail (file, FALSE);

  gint children = gtk_tree_model_iter_n_children (file->child, NULL);

  if (n >= children - file->first_line)
    {
      iter->stamp = -1;
      iter->user_data = NULL;
      return FALSE;
    }

  iter->user_data = GINT_TO_POINTER (n);
  iter->stamp = file->stamp;

  return TRUE;
}

/* Split row N into it's delimited fields (if it is not already cached)
   and set this row as the current cache. */
static void
split_row_into_fields (PsppireDelimitedText *file, gint n)
{
  if (n == file->cache_row)  /* Cache hit */
    return;
  if (!file->parser)
    file->parser = make_data_parser (file);

  string_array_clear (&file->cache);
  data_parser_split (file->parser, PSPPIRE_TEXT_FILE (file->child)->lines[n],
                     &file->cache);
  file->cache_row = n;
}

const gchar *
psppire_delimited_text_get_header_title (PsppireDelimitedText *file, gint column)
{
  if (file->first_line <= 0)
    return NULL;

  split_row_into_fields (file, file->first_line - 1);

  return column < file->cache.n ? file->cache.strings[column] : "";
}

static void
__get_value (GtkTreeModel *tree_model,
             GtkTreeIter *iter,
             gint column,
             GValue *value)
{
  PsppireDelimitedText *file  = PSPPIRE_DELIMITED_TEXT (tree_model);

  g_return_if_fail (iter->stamp == file->stamp);

  gint n = GPOINTER_TO_INT (iter->user_data) + file->first_line;


  if (column == 0)
    {
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, n + 1);
      return;
    }

  g_value_init (value, G_TYPE_STRING);

  split_row_into_fields (file, n);

  size_t idx = column - 1;
  const char *s = idx < file->cache.n ? file->cache.strings[idx] : "";
  g_value_set_string (value, s);
}


static void
__tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags       = __tree_model_get_flags;
  iface->get_n_columns   = __tree_model_get_n_columns ;
  iface->get_column_type = __tree_get_column_type;
  iface->get_iter        = __tree_get_iter;
  iface->iter_next       = __tree_iter_next;
  iface->get_path        = __tree_get_path;
  iface->get_value       = __get_value;

  iface->iter_children   = __iter_children;
  iface->iter_has_child  = __iter_has_child;
  iface->iter_n_children = __tree_model_iter_n_children;
  iface->iter_nth_child  = __iter_nth_child;
  iface->iter_parent     = __iter_parent;
}

G_DEFINE_TYPE_WITH_CODE (PsppireDelimitedText, psppire_delimited_text, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                __tree_model_init))

static void
psppire_delimited_text_class_init (PsppireDelimitedTextClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = G_OBJECT_CLASS (class);

  GParamSpec *first_line_spec =
    g_param_spec_int ("first-line",
                      "First Line",
                      P_("The first line to be considered."),
                      0, 1000, 0,
                      G_PARAM_READWRITE);

  GParamSpec *delimiters_spec =
    g_param_spec_pointer ("delimiters",
                          "Field Delimiters",
                          P_("A GSList of gunichars which delimit the fields."),
                          G_PARAM_READWRITE);

  GParamSpec *quote_spec =
    g_param_spec_unichar ("quote",
                         "Quote Character",
                         P_("A character that quotes the field, or 0 to disable quoting."),
                         0,
                         G_PARAM_READWRITE);

  GParamSpec *child_spec =
    g_param_spec_object ("child",
                         "Child Model",
                         P_("The GtkTextModel which this object wraps."),
                         GTK_TYPE_TREE_MODEL,
                         G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  object_class->set_property = psppire_delimited_text_set_property;
  object_class->get_property = psppire_delimited_text_get_property;

  g_object_class_install_property (object_class,
                                   PROP_CHILD,
                                   child_spec);

  g_object_class_install_property (object_class,
                                   PROP_DELIMITERS,
                                   delimiters_spec);

  g_object_class_install_property (object_class,
                                   PROP_QUOTE,
                                   quote_spec);

  g_object_class_install_property (object_class,
                                   PROP_FIRST_LINE,
                                   first_line_spec);

  object_class->finalize = psppire_delimited_text_finalize;
  object_class->dispose = psppire_delimited_text_dispose;
}


static void
psppire_delimited_text_init (PsppireDelimitedText *text_file)
{
  text_file->child = NULL;
  text_file->first_line = 0;
  text_file->delimiters = g_slist_prepend (NULL, GINT_TO_POINTER (':'));

  text_file->cache_row = -1;
  string_array_init (&text_file->cache);
  text_file->parser = NULL;

  text_file->max_fields = 0;

  text_file->quote = 0;

  text_file->dispose_has_run = FALSE;
  text_file->stamp = g_random_int ();
}


PsppireDelimitedText *
psppire_delimited_text_new (GtkTreeModel *child)
{
  return
    g_object_new (PSPPIRE_TYPE_DELIMITED_TEXT,
                  "child", child,
                  NULL);
}

static void
psppire_delimited_text_finalize (GObject *object)
{
  PsppireDelimitedText *tf = PSPPIRE_DELIMITED_TEXT (object);

  g_slist_free (tf->delimiters);
  string_array_destroy (&tf->cache);
  data_parser_destroy (tf->parser);

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_delimited_text_dispose (GObject *object)
{
  PsppireDelimitedText *ds = PSPPIRE_DELIMITED_TEXT (object);

  if (ds->dispose_has_run)
    return;

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}
