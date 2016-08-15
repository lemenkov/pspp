/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C)  2007, 2010, 2011, 2012, 2013, 2015  Free Software Foundation

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

#include "psppire-dialog-action-comments.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"
#include "dict-display.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include <ui/syntax-gen.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_comments_init            (PsppireDialogActionComments      *act);
static void psppire_dialog_action_comments_class_init      (PsppireDialogActionCommentsClass *class);

G_DEFINE_TYPE (PsppireDialogActionComments, psppire_dialog_action_comments, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (const PsppireDialogAction *pda)
{
  PsppireDialogActionComments *cd = PSPPIRE_DIALOG_ACTION_COMMENTS (pda);
  gint i;

  GString *str;
  gchar *text;
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (cd->textview));

  str = g_string_new ("\n* Data File Comments.\n\n");

  if (dict_get_documents (pda->dict->dict) != NULL)
    g_string_append (str, "DROP DOCUMENTS.\n");

  g_string_append (str, "ADD DOCUMENT\n");

  for (i = 0 ; i < gtk_text_buffer_get_line_count (buffer) ; ++i )
    {
      struct string tmp;
      GtkTextIter start;
      char *line;

      gtk_text_buffer_get_iter_at_line (buffer, &start, i);
      if (gtk_text_iter_ends_line (&start))
	line = g_strdup ("");
      else
        {
          GtkTextIter end = start;
          gtk_text_iter_forward_to_line_end (&end);
          line = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        }

      ds_init_empty (&tmp);
      syntax_gen_string (&tmp, ss_cstr (line));
      g_free (line);

      g_string_append_printf (str, " %s\n", ds_cstr (&tmp));

      ds_destroy (&tmp);
    }
  g_string_append (str, " .\n");


  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->check)))
    g_string_append (str, "DISPLAY DOCUMENTS.\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  return TRUE;
}

static void
add_line_to_buffer (GtkTextBuffer *buffer, const char *line)
{
  gtk_text_buffer_insert_at_cursor (buffer, line, -1);

  gtk_text_buffer_insert_at_cursor (buffer, "\n", 1);
}

static void
retrieve_comments (PsppireDialogAction *pda)
{
  PsppireDialogActionComments *wcd = PSPPIRE_DIALOG_ACTION_COMMENTS (pda);
  gint i;

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (wcd->textview));

  gtk_text_buffer_set_text (buffer, "", 0);

  for ( i = 0 ; i < dict_get_document_line_cnt (pda->dict->dict); ++i )
    add_line_to_buffer (buffer, dict_get_document_line (pda->dict->dict, i));
}


static void
refresh (PsppireDialogAction *pda)
{
  PsppireDialogActionComments *act = PSPPIRE_DIALOG_ACTION_COMMENTS (pda);

  retrieve_comments (pda);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->check), FALSE);
}


static void
set_column_number (GtkTextBuffer *textbuffer,
     GtkTextIter   *iter,
     GtkTextMark   *mark,
     gpointer       data)
{
  GtkLabel *label = data;
  gchar *text ;

  text = g_strdup_printf ( _("Column Number: %d"),
			   1 + gtk_text_iter_get_line_offset (iter));

  gtk_label_set_text (label, text);

  g_free (text);
}

static void
wrap_line (GtkTextBuffer *buffer,
     GtkTextIter   *iter,
     gchar         *text,
     gint           count,
     gpointer       data)
{
  gint chars = gtk_text_iter_get_chars_in_line (iter);

  if ( chars > DOC_LINE_LENGTH )
    {
      GtkTextIter line_fold = *iter;

      gtk_text_iter_set_line_offset (&line_fold, DOC_LINE_LENGTH);

      gtk_text_buffer_insert (buffer, &line_fold, "\r\n", 2);
    }
}


static void
psppire_dialog_action_comments_activate (PsppireDialogAction *pda)
{
  PsppireDialogActionComments *act = PSPPIRE_DIALOG_ACTION_COMMENTS (pda);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, pda);
  if (!xml)
    {
      GtkTextIter iter;

      xml = builder_new ("comments.ui");
      g_hash_table_insert (thing, pda, xml);

      pda->dialog = get_widget_assert (xml, "comments-dialog");
      act->textview = get_widget_assert (xml, "comments-textview1");
      GtkWidget *label = get_widget_assert (xml, "column-number-label");
      GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (act->textview));
      act->check = get_widget_assert (xml, "comments-checkbutton1");

      g_signal_connect_swapped (pda->dialog, "show", G_CALLBACK (retrieve_comments), pda);
      
      {
	PangoContext * context ;
	PangoLayout *  layout ;
	PangoRectangle rect;

	
	/* Since we're going to truncate lines to 80 chars,
	   we need a monospaced font otherwise it'll look silly */
	PangoFontDescription *font_desc =
	  pango_font_description_from_string ("monospace");
	{
	  GtkStyleContext *style = gtk_widget_get_style_context (GTK_WIDGET (act->textview));
	  GtkCssProvider *cssp = gtk_css_provider_new ();

	  gchar *str = pango_font_description_to_string (font_desc);
	  gchar *css =
	    g_strdup_printf ("* {font: %s}", str);
	  g_free (str);

	  GError *err = NULL;
	  gtk_css_provider_load_from_data (cssp, css, -1, &err);
	  if (err)
	    {
	      g_warning ("Failed to load font css \"%s\": %s", css, err->message);
	      g_error_free (err);
	    }
	  g_free (css);

	  gtk_style_context_add_provider (style,
					  GTK_STYLE_PROVIDER (cssp),
					  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	  g_object_unref (cssp);
	}
	
	/* And let's just make sure that a complete line fits into the
	   widget's width */
	context = gtk_widget_create_pango_context (act->textview);
	layout = pango_layout_new (context);

	pango_layout_set_text (layout, "M", 1);

	pango_layout_set_font_description (layout, font_desc);

	pango_layout_get_extents (layout, NULL, &rect);

	g_object_set (act->textview, "width-request",
		      PANGO_PIXELS (rect.width) * DOC_LINE_LENGTH + 20, NULL);

	g_object_unref (G_OBJECT (layout));
	g_object_unref (G_OBJECT (context));

	pango_font_description_free (font_desc);
      }
      
      g_signal_connect (buffer, "mark-set",
			G_CALLBACK (set_column_number), label);

      g_signal_connect_after (buffer, "insert-text",
			      G_CALLBACK (wrap_line), NULL);

      gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);
      gtk_text_buffer_place_cursor (buffer, &iter);
    }
  
  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);
}

static void
psppire_dialog_action_comments_class_init (PsppireDialogActionCommentsClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_comments_activate);
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_comments_init (PsppireDialogActionComments *act)
{
}

