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
#include "libpspp/str.h"
#include "libpspp/i18n.h"

#include <gtk/gtk.h>

/* Properties */
enum
  {
    PROP_0,
    PROP_CHILD,
    PROP_DELIMITERS,
    PROP_FIRST_LINE
  };

struct enclosure
{
  gunichar opening;
  gunichar closing;
};

static const struct enclosure enclosures[3] =
  {
    {'(',   ')'},
    {'"',   '"'},
    {'\'',  '\''}
  };

static void
count_delims (PsppireDelimitedText *tf)
{
  if (tf->child == NULL)
    return;

  tf->max_delimiters = 0;
  GtkTreeIter iter;
  gboolean valid;
  for (valid = gtk_tree_model_get_iter_first (tf->child, &iter);
       valid;
       valid = gtk_tree_model_iter_next (tf->child, &iter))
    {
      gint enc = -1;
      // FIXME: Box these lines to avoid constant allocation/deallocation
      gchar *line = NULL;
      gtk_tree_model_get (tf->child, &iter, 1, &line, -1);
      {
	char *p;
	gint count = 0;
	for (p = line; ; p = g_utf8_find_next_char (p, NULL))
	  {
	    const gunichar c = g_utf8_get_char (p);
	    if (c == 0)
	      break;
	    if (enc == -1)
	      {
		gint i;
		for (i = 0; i < 3; ++i)
		  {
		    if (c == enclosures[i].opening)
		      {
			enc = i;
			break;
		      }
		  }
	      }
	    else if (c == enclosures[enc].closing)
	      {
		enc = -1;
	      }
	    if (enc == -1)
	      {
		GSList *del;
		for (del = tf->delimiters; del; del = g_slist_next (del))
		  {
		    if (c == GPOINTER_TO_INT (del->data))
		      count++;
		  }
	      }
	  }
	tf->max_delimiters = MAX (tf->max_delimiters, count);
      }
      g_free (line);
    }
}

static void
cache_invalidate (PsppireDelimitedText *tf)
{
  memset (tf->cache_starts, 0, sizeof tf->cache_starts);
  if (tf->const_cache.string)
    {
      ss_dealloc (&tf->const_cache);
      tf->const_cache.string = NULL;
      tf->cache_row = -1;
    }
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

  /* + 1 for the trailing field and +1 for the leading line number column */
  return tf->max_delimiters + 1 + 1;
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


static void
nullify_char (struct substring cs)
{
  int char_len = ss_first_mblen (cs);
  while (char_len > 0)
    {
      cs.string[char_len - 1] = '\0';
      char_len--;
    }
}


/* Split row N into it's delimited fields (if it is not already cached)
   and set this row as the current cache. */
static void
split_row_into_fields (PsppireDelimitedText *file, gint n)
{
  if (n == file->cache_row)  /* Cache hit */
    {
      return;
    }

  memset (file->cache_starts, 0, sizeof file->cache_starts);
  /* Cache miss */
  if (file->const_cache.string)
    {
      ss_dealloc (&file->const_cache);
    }
  ss_alloc_substring_pool (&file->const_cache,
			   PSPPIRE_TEXT_FILE (file->child)->lines[n], NULL);
  struct substring cs = file->const_cache;
  int field = 0;
  file->cache_starts[0] = cs.string;
  gint enc = -1;
  for (;
       UINT32_MAX != ss_first_mb (cs);
       ss_get_mb (&cs))
    {
      ucs4_t character = ss_first_mb (cs);
      gboolean char_is_quote = FALSE;
      if (enc == -1)
	{
	  gint i;
	  for (i = 0; i < 3; ++i)
	    {
	      if (character == enclosures[i].opening)
		{
		  enc = i;
		  char_is_quote = TRUE;
		  file->cache_starts[field] += ss_first_mblen (cs);
		  break;
		}
	    }
	}
      else if (character == enclosures[enc].closing)
	{
	  char_is_quote = TRUE;
	  nullify_char (cs);
	  enc = -1;
	}

      if (enc == -1 && char_is_quote == FALSE)
	{
	  GSList *del;
	  for (del = file->delimiters; del; del = g_slist_next (del))
	    {
	      if (character == GPOINTER_TO_INT (del->data))
		{
		  field++;
		  int char_len = ss_first_mblen (cs);
		  file->cache_starts[field] = cs.string + char_len;
		  nullify_char (cs);
		  break;
		}
	    }
	}
    }

  file->cache_row = n;
}

const gchar *
psppire_delimited_text_get_header_title (PsppireDelimitedText *file, gint column)
{
  if (file->first_line <= 0)
    return NULL;

  split_row_into_fields (file, file->first_line - 1);

  return file->cache_starts [column];
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

  g_value_set_string (value, file->cache_starts [column - 1]);
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

  text_file->const_cache.string = NULL;
  text_file->const_cache.length = 0;
  text_file->cache_row = -1;
  memset (text_file->cache_starts, 0, sizeof text_file->cache_starts);

  text_file->max_delimiters = 0;

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

  ss_dealloc (&tf->const_cache);

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
