/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017, 2020 Free Software Foundation

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

#include "psppire-text-file.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/i18n.h"

#include <gtk/gtk.h>

/* Properties */
enum
  {
    PROP_0,
    PROP_FILE_NAME,
    PROP_ENCODING,
    PROP_MAXIMUM_LINES,
    PROP_LINE_COUNT
  };

enum {MAX_LINE_LEN = 16384};  /* Max length of an acceptable line. */

static void
read_lines (PsppireTextFile *tf)
{
  if (tf->file_name && 0 != g_strcmp0 ("unset", tf->encoding))
    {
      struct line_reader *reader = line_reader_for_file (tf->encoding, tf->file_name, O_RDONLY);

      if (reader == NULL)
	{
	  msg_error (errno, _("Could not open `%s'"),  tf->file_name);
	  return;
	}

      struct string input;
      ds_init_empty (&input);
      for (tf->line_cnt = 0; tf->line_cnt < MAX_PREVIEW_LINES; tf->line_cnt++)
	{
	  ds_clear (&input);
	  if (!line_reader_read (reader, &input, MAX_LINE_LEN + 1)
	      || ds_length (&input) > MAX_LINE_LEN)
	    {
	      int i;
	      if (line_reader_eof (reader))
		break;
	      else if (line_reader_error (reader))
		msg (ME, _("Error reading `%s': %s"),
		     tf->file_name, strerror (line_reader_error (reader)));
	      else
		msg (ME, _("Failed to read `%s', because it contains a line "
			   "over %d bytes long and therefore appears not to be "
			   "a text file."),
		     tf->file_name, MAX_LINE_LEN);
	      line_reader_close (reader);
	      for (i = 0; i < tf->line_cnt; i++)
                g_free (tf->lines[i].string);
	      tf->line_cnt = 0;
	      ds_destroy (&input);
	      return;
	    }

	  tf->lines[tf->line_cnt]
	    = recode_substring_pool ("UTF-8",
				     line_reader_get_encoding (reader),
				     input.ss, NULL);
        }
      ds_destroy (&input);

      if (tf->line_cnt == 0)
	{
	  int i;
	  msg (ME, _("`%s' is empty."), tf->file_name);
	  line_reader_close (reader);
	  for (i = 0; i < tf->line_cnt; i++)
	    g_free (tf->lines[i].string);
	  tf->line_cnt = 0;
	  goto done;
	}

      if (tf->line_cnt < MAX_PREVIEW_LINES)
	{
	  tf->total_lines = tf->line_cnt;
	  tf->total_is_exact = true;
	}
      else
	{
	  /* Estimate the number of lines in the file. */
	  struct stat s;
	  off_t position = line_reader_tell (reader);
	  if (fstat (line_reader_fileno (reader), &s) == 0 && position > 0)
	    {
	      tf->total_lines = (double) tf->line_cnt / position * s.st_size;
	      tf->total_is_exact = false;
	    }
	  else
	    {
	      tf->total_lines = 0;
	      tf->total_is_exact = true;
	    }
	}
    done:
      line_reader_close (reader);
    }
}

static void
psppire_text_file_set_property (GObject         *object,
				guint            prop_id,
				const GValue    *value,
				GParamSpec      *pspec)
{
  PsppireTextFile *tf = PSPPIRE_TEXT_FILE (object);

  switch (prop_id)
    {
    case PROP_MAXIMUM_LINES:
      tf->maximum_lines = g_value_get_int (value);
      break;
    case PROP_FILE_NAME:
      tf->file_name = g_value_dup_string (value);
      read_lines (tf);
      break;
    case PROP_ENCODING:
      g_free (tf->encoding);
      tf->encoding = g_value_dup_string (value);
      read_lines (tf);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };

}

static void
psppire_text_file_get_property (GObject         *object,
				guint            prop_id,
				GValue          *value,
				GParamSpec      *pspec)
{
  PsppireTextFile *text_file = PSPPIRE_TEXT_FILE (object);

  switch (prop_id)
    {
    case PROP_MAXIMUM_LINES:
      g_value_set_int (value, text_file->maximum_lines);
      break;
    case PROP_LINE_COUNT:
      g_value_set_int (value, text_file->line_cnt);
      break;
    case PROP_FILE_NAME:
      g_value_set_string (value, text_file->file_name);
      break;
    case PROP_ENCODING:
      g_value_set_string (value, text_file->encoding);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void psppire_text_file_finalize        (GObject           *object);
static void psppire_text_file_dispose        (GObject           *object);

static GObjectClass *parent_class = NULL;

static gboolean
__tree_get_iter (GtkTreeModel *tree_model,
		 GtkTreeIter *iter,
		 GtkTreePath *path)
{
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);

  if (path == NULL)
    return FALSE;

  gint *indices = gtk_tree_path_get_indices (path);

  gint n = *indices;

  if (n >= file->line_cnt)
    return FALSE;

  iter->user_data = GINT_TO_POINTER (n);
  iter->stamp = file->stamp;

  return TRUE;
}


static gboolean
__tree_iter_next (GtkTreeModel *tree_model,
		  GtkTreeIter *iter)
{
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);
  g_return_val_if_fail (file->stamp == iter->stamp, FALSE);

  gint n = GPOINTER_TO_INT (iter->user_data) + 1;

  if (n >= file->line_cnt)
    return FALSE;

  iter->user_data = GINT_TO_POINTER (n);

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
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);
  g_return_val_if_fail (file->stamp == iter->stamp, FALSE);

  gint n = GPOINTER_TO_INT (iter->user_data);

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
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);
  g_assert (iter == NULL);
  return file->line_cnt;
}

static GtkTreeModelFlags
__tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_TEXT_FILE (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
__tree_model_get_n_columns (GtkTreeModel *tree_model)
{
  return 2;
}


static gboolean
__iter_nth_child (GtkTreeModel *tree_model,
		  GtkTreeIter *iter,
		  GtkTreeIter *parent,
		  gint n)
{
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);

  g_assert (parent == NULL);

  g_return_val_if_fail (file, FALSE);

  if (n >= file->line_cnt)
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
__get_value (GtkTreeModel *tree_model,
	     GtkTreeIter *iter,
	     gint column,
	     GValue *value)
{
  PsppireTextFile *file  = PSPPIRE_TEXT_FILE (tree_model);

  g_return_if_fail (iter->stamp == file->stamp);

  gint n = GPOINTER_TO_INT (iter->user_data);

  g_return_if_fail (n < file->line_cnt);

  if (column == 0)
    {
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, n + 1);
      return;
    }

  g_value_init (value, G_TYPE_STRING);

  if (column == 1)
    {
      char *s = ss_xstrdup (file->lines[n]);
      g_value_set_string (value, s);
      free (s);
      return;
    }

  g_assert_not_reached ();
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

G_DEFINE_TYPE_WITH_CODE (PsppireTextFile, psppire_text_file, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
						__tree_model_init))

static void
psppire_text_file_class_init (PsppireTextFileClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = G_OBJECT_CLASS (class);

  GParamSpec *maximum_lines_spec =
    g_param_spec_int ("maximum-lines",
		      "Maximum Lines",
		      P_("An upper limit on the number of lines to consider"),
		      0, G_MAXINT, G_MAXINT,
		      G_PARAM_READWRITE);

  GParamSpec *line_count_spec =
    g_param_spec_int ("line-count",
		      "Line Count",
		      P_("The number of lines in the file"),
		      0, G_MAXINT, G_MAXINT,
		      G_PARAM_READABLE);

  GParamSpec *file_name_spec =
    g_param_spec_string ("file-name",
			 "File Name",
			 P_("The name of the file from which this object was constructed"),
			 NULL,
			 G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  GParamSpec *encoding_spec =
    g_param_spec_string ("encoding",
			 "Character Encoding",
			 P_("The character encoding of the file from which this object was constructed"),
			 "unset",
			 G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  object_class->set_property = psppire_text_file_set_property;
  object_class->get_property = psppire_text_file_get_property;

  g_object_class_install_property (object_class,
                                   PROP_MAXIMUM_LINES,
                                   maximum_lines_spec);

  g_object_class_install_property (object_class,
                                   PROP_LINE_COUNT,
                                   line_count_spec);

  g_object_class_install_property (object_class,
                                   PROP_FILE_NAME,
                                   file_name_spec);

  g_object_class_install_property (object_class,
                                   PROP_ENCODING,
                                   encoding_spec);

  object_class->finalize = psppire_text_file_finalize;
  object_class->dispose = psppire_text_file_dispose;
}

static void
psppire_text_file_init (PsppireTextFile *text_file)
{
  text_file->encoding = g_strdup ("unset");
  text_file->file_name = NULL;

  text_file->dispose_has_run = FALSE;
  text_file->stamp = g_random_int ();
}


PsppireTextFile *
psppire_text_file_new (const gchar *file_name, const gchar *encoding)
{
  PsppireTextFile *retval =
    g_object_new (PSPPIRE_TYPE_TEXT_FILE,
		  "file-name", file_name,
		  "encoding", encoding,
		  NULL);

  return retval;
}

static void
psppire_text_file_finalize (GObject *object)
{
  PsppireTextFile *tf = PSPPIRE_TEXT_FILE (object);

  for (int i = 0; i < tf->line_cnt; i++)
    g_free (tf->lines[i].string);

  g_free (tf->encoding);
  g_free (tf->file_name);

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_text_file_dispose (GObject *object)
{
  PsppireTextFile *ds = PSPPIRE_TEXT_FILE (object);

  if (ds->dispose_has_run)
    return;

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}

gboolean
psppire_text_file_get_total_exact (PsppireTextFile *tf)
{
  return tf->total_is_exact;
}

gulong
psppire_text_file_get_n_lines (PsppireTextFile *tf)
{
  return tf->total_lines;
}
