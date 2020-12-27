/* PSPP - a program for statistical analysis.
   Copyright (C) 2009-2014 Free Software Foundation, Inc.

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

/* A driver for creating OpenDocument Format text files from PSPP's output */

#include <errno.h>
#include <libgen.h>
#include <libxml/xmlwriter.h>
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/zip-writer.h"
#include "data/file-handle-def.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define _xml(X) (CHAR_CAST (const xmlChar *, X))

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

struct odt_driver
{
  struct output_driver driver;

  struct zip_writer *zip;     /* ZIP file writer. */
  struct file_handle *handle; /* Handle for 'file_name'. */
  char *file_name;            /* Output file name. */

  /* content.xml */
  xmlTextWriterPtr content_wtr; /* XML writer. */
  FILE *content_file;           /* Temporary file. */

  /* manifest.xml */
  xmlTextWriterPtr manifest_wtr; /* XML writer. */
  FILE *manifest_file;           /* Temporary file. */

  /* Number of tables so far. */
  int table_num;
};

static const struct output_driver_class odt_driver_class;

static struct odt_driver *
odt_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &odt_driver_class);
  return UP_CAST (driver, struct odt_driver, driver);
}

/* Creates a new temporary file and stores it in *FILE, then creates an XML
   writer for it and stores it in *W. */
static void
create_writer (FILE **file, xmlTextWriterPtr *w)
{
  /* XXX this can fail */
  *file = create_temp_file ();
  *w = xmlNewTextWriter (xmlOutputBufferCreateFile (*file, NULL));

  xmlTextWriterStartDocument (*w, NULL, "UTF-8", NULL);
}


static void
register_file (struct odt_driver *odt, const char *filename)
{
  assert (odt->manifest_wtr);
  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:media-type"),  _xml("text/xml"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:full-path"),  _xml (filename));
  xmlTextWriterEndElement (odt->manifest_wtr);
}

static void
write_style_data (struct odt_driver *odt)
{
  xmlTextWriterPtr w;
  FILE *file;

  create_writer (&file, &w);
  register_file (odt, "styles.xml");

  xmlTextWriterStartElement (w, _xml ("office:document-styles"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:office"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));

  xmlTextWriterWriteAttribute (w, _xml ("xmlns:style"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:style:1.0"));

  xmlTextWriterWriteAttribute (w, _xml ("xmlns:fo"),
			       _xml ("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"));

  xmlTextWriterWriteAttribute (w, _xml ("office:version"),  _xml ("1.1"));



  xmlTextWriterStartElement (w, _xml ("office:styles"));


  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Standard"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("text"));

    xmlTextWriterEndElement (w); /* style:style */
  }

  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Table_20_Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:display-name"),
				 _xml ("Table Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:parent-style-name"),
				 _xml ("Standard"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("extra"));

    xmlTextWriterEndElement (w); /* style:style */
  }

  {
    xmlTextWriterStartElement (w, _xml ("style:style"));
    xmlTextWriterWriteAttribute (w, _xml ("style:name"),
				 _xml ("Table_20_Heading"));

    xmlTextWriterWriteAttribute (w, _xml ("style:display-name"),
				 _xml ("Table Heading"));

    xmlTextWriterWriteAttribute (w, _xml ("style:family"),
				 _xml ("paragraph"));

    xmlTextWriterWriteAttribute (w, _xml ("style:parent-style-name"),
				 _xml ("Table_20_Contents"));

    xmlTextWriterWriteAttribute (w, _xml ("style:class"),
				 _xml ("extra"));


    xmlTextWriterStartElement (w, _xml ("style:text-properties"));
    xmlTextWriterWriteAttribute (w, _xml ("fo:font-weight"), _xml ("bold"));
    xmlTextWriterWriteAttribute (w, _xml ("style:font-weight-asian"), _xml ("bold"));
    xmlTextWriterWriteAttribute (w, _xml ("style:font-weight-complex"), _xml ("bold"));
    xmlTextWriterEndElement (w); /* style:text-properties */

    xmlTextWriterEndElement (w); /* style:style */
  }


  xmlTextWriterEndElement (w); /* office:styles */
  xmlTextWriterEndElement (w); /* office:document-styles */

  xmlTextWriterEndDocument (w);
  xmlFreeTextWriter (w);
  zip_writer_add (odt->zip, file, "styles.xml");
  close_temp_file (file);
}

static void
write_meta_data (struct odt_driver *odt)
{
  xmlTextWriterPtr w;
  FILE *file;

  create_writer (&file, &w);
  register_file (odt, "meta.xml");

  xmlTextWriterStartElement (w, _xml ("office:document-meta"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:office"), _xml ("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:dc"),  _xml ("http://purl.org/dc/elements/1.1/"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:meta"), _xml ("urn:oasis:names:tc:opendocument:xmlns:meta:1.0"));
  xmlTextWriterWriteAttribute (w, _xml ("xmlns:ooo"), _xml("http://openoffice.org/2004/office"));
  xmlTextWriterWriteAttribute (w, _xml ("office:version"),  _xml("1.1"));

  xmlTextWriterStartElement (w, _xml ("office:meta"));
  {
    xmlTextWriterStartElement (w, _xml ("meta:generator"));
    xmlTextWriterWriteString (w, _xml (version));
    xmlTextWriterEndElement (w);
  }


  {
    char buf[30];
    time_t t = time (NULL);
    struct tm *tm =  localtime (&t);

    strftime (buf, 30, "%Y-%m-%dT%H:%M:%S", tm);

    xmlTextWriterStartElement (w, _xml ("meta:creation-date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);

    xmlTextWriterStartElement (w, _xml ("dc:date"));
    xmlTextWriterWriteString (w, _xml (buf));
    xmlTextWriterEndElement (w);
  }

#ifdef HAVE_PWD_H
  {
    struct passwd *pw = getpwuid (getuid ());
    if (pw != NULL)
      {
        xmlTextWriterStartElement (w, _xml ("meta:initial-creator"));
        xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));
        xmlTextWriterEndElement (w);

        xmlTextWriterStartElement (w, _xml ("dc:creator"));
        xmlTextWriterWriteString (w, _xml (strtok (pw->pw_gecos, ",")));
        xmlTextWriterEndElement (w);
      }
  }
#endif

  xmlTextWriterEndElement (w);
  xmlTextWriterEndElement (w);
  xmlTextWriterEndDocument (w);
  xmlFreeTextWriter (w);
  zip_writer_add (odt->zip, file, "meta.xml");
  close_temp_file (file);
}

static struct output_driver *
odt_create (struct file_handle *fh, enum settings_output_devices device_type,
            struct string_map *o UNUSED)
{
  struct output_driver *d;
  struct odt_driver *odt;
  struct zip_writer *zip;
  const char *file_name = fh_get_file_name (fh);

  zip = zip_writer_create (file_name);
  if (zip == NULL)
    return NULL;

  odt = xzalloc (sizeof *odt);
  d = &odt->driver;

  output_driver_init (d, &odt_driver_class, file_name, device_type);

  odt->zip = zip;
  odt->handle = fh;
  odt->file_name = xstrdup (file_name);

  zip_writer_add_string (zip, "mimetype",
                         "application/vnd.oasis.opendocument.text");

  /* Create the manifest */
  create_writer (&odt->manifest_file, &odt->manifest_wtr);

  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:manifest"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("xmlns:manifest"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"));


  /* Add a manifest entry for the document as a whole */
  xmlTextWriterStartElement (odt->manifest_wtr, _xml("manifest:file-entry"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:media-type"),  _xml("application/vnd.oasis.opendocument.text"));
  xmlTextWriterWriteAttribute (odt->manifest_wtr, _xml("manifest:full-path"),  _xml("/"));
  xmlTextWriterEndElement (odt->manifest_wtr);


  write_meta_data (odt);
  write_style_data (odt);

  create_writer (&odt->content_file, &odt->content_wtr);
  register_file (odt, "content.xml");


  /* Some necessary junk at the start */
  xmlTextWriterStartElement (odt->content_wtr, _xml("office:document-content"));
  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:office"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:office:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:text"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:text:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("xmlns:table"),
			       _xml("urn:oasis:names:tc:opendocument:xmlns:table:1.0"));

  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("office:version"), _xml("1.1"));

  xmlTextWriterStartElement (odt->content_wtr, _xml("office:body"));
  xmlTextWriterStartElement (odt->content_wtr, _xml("office:text"));



  /* Close the manifest */
  xmlTextWriterEndElement (odt->manifest_wtr);
  xmlTextWriterEndDocument (odt->manifest_wtr);
  xmlFreeTextWriter (odt->manifest_wtr);
  zip_writer_add (odt->zip, odt->manifest_file, "META-INF/manifest.xml");
  close_temp_file (odt->manifest_file);

  return d;
}

static void
odt_destroy (struct output_driver *driver)
{
  struct odt_driver *odt = odt_driver_cast (driver);

  if (odt->content_wtr != NULL)
    {
      xmlTextWriterEndElement (odt->content_wtr); /* office:text */
      xmlTextWriterEndElement (odt->content_wtr); /* office:body */
      xmlTextWriterEndElement (odt->content_wtr); /* office:document-content */

      xmlTextWriterEndDocument (odt->content_wtr);
      xmlFreeTextWriter (odt->content_wtr);
      zip_writer_add (odt->zip, odt->content_file, "content.xml");
      close_temp_file (odt->content_file);

      zip_writer_close (odt->zip);
    }

  fh_unref (odt->handle);
  free (odt->file_name);
  free (odt);
}

static void
write_xml_with_line_breaks (struct odt_driver *odt, const char *line_)
{
  xmlTextWriterPtr writer = odt->content_wtr;

  if (!strchr (line_, '\n'))
    xmlTextWriterWriteString (writer, _xml(line_));
  else
    {
      char *line = xstrdup (line_);
      char *newline;
      char *p;

      for (p = line; *p; p = newline + 1)
        {
          newline = strchr (p, '\n');

          if (!newline)
            {
              xmlTextWriterWriteString (writer, _xml(p));
              free (line);
              return;
            }

          if (newline > p && newline[-1] == '\r')
            newline[-1] = '\0';
          else
            *newline = '\0';
          xmlTextWriterWriteString (writer, _xml(p));
          xmlTextWriterWriteElement (writer, _xml("text:line-break"), _xml(""));
        }
    }
}

static void
write_footnote (struct odt_driver *odt, const struct footnote *f)
{
  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note"));
  xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:note-class"),
                               _xml("footnote"));

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note-citation"));
  if (strlen (f->marker) > 1)
    xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("text:label"),
                                       "(%s)", f->marker);
  else
    xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:label"),
                                 _xml(f->marker));
  xmlTextWriterEndElement (odt->content_wtr);

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:note-body"));
  xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));
  write_xml_with_line_breaks (odt, f->content);
  xmlTextWriterEndElement (odt->content_wtr);
  xmlTextWriterEndElement (odt->content_wtr);

  xmlTextWriterEndElement (odt->content_wtr);
}

static void
write_table_item_text (struct odt_driver *odt,
                       const struct table_item_text *text)
{
  if (!text)
    return;

  xmlTextWriterStartElement (odt->content_wtr, _xml("text:h"));
  xmlTextWriterWriteFormatAttribute (odt->content_wtr,
                                     _xml("text:outline-level"), "%d", 2);
  xmlTextWriterWriteString (odt->content_wtr, _xml (text->content));
  for (size_t i = 0; i < text->n_footnotes; i++)
    write_footnote (odt, text->footnotes[i]);
  xmlTextWriterEndElement (odt->content_wtr);
}

static void
write_table_item_layers (struct odt_driver *odt,
                         const struct table_item_layers *layers)
{
  if (!layers)
    return;

  for (size_t i = 0; i < layers->n_layers; i++)
    {
      const struct table_item_layer *layer = &layers->layers[i];
      xmlTextWriterStartElement (odt->content_wtr, _xml("text:h"));
      xmlTextWriterWriteFormatAttribute (odt->content_wtr,
                                         _xml("text:outline-level"), "%d", 2);
      xmlTextWriterWriteString (odt->content_wtr, _xml (layer->content));
      for (size_t i = 0; i < layer->n_footnotes; i++)
        write_footnote (odt, layer->footnotes[i]);
      xmlTextWriterEndElement (odt->content_wtr);
    }
}

static void
write_table (struct odt_driver *odt, const struct table_item *item)
{
  const struct table *tab = table_item_get_table (item);
  int r, c;

  /* Write a heading for the table */
  write_table_item_text (odt, table_item_get_title (item));
  write_table_item_layers (odt, table_item_get_layers (item));

  /* Start table */
  xmlTextWriterStartElement (odt->content_wtr, _xml("table:table"));
  xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("table:name"),
				     "TABLE-%d", odt->table_num++);


  /* Start column definitions */
  xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-column"));
  xmlTextWriterWriteFormatAttribute (odt->content_wtr, _xml("table:number-columns-repeated"), "%d", tab->n[H]);
  xmlTextWriterEndElement (odt->content_wtr);


  /* Deal with row headers */
  if (tab->h[V][0] > 0)
    xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-header-rows"));


  /* Write all the rows */
  for (r = 0 ; r < tab->n[V]; ++r)
    {
      /* Start row definition */
      xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-row"));

      /* Write all the columns */
      for (c = 0 ; c < tab->n[H] ; ++c)
	{
          struct table_cell cell;

          table_get_cell (tab, c, r, &cell);

          if (c == cell.d[H][0] && r == cell.d[V][0])
            {
              int colspan = table_cell_colspan (&cell);
              int rowspan = table_cell_rowspan (&cell);

              xmlTextWriterStartElement (odt->content_wtr, _xml("table:table-cell"));
              xmlTextWriterWriteAttribute (odt->content_wtr, _xml("office:value-type"), _xml("string"));

              if (colspan > 1)
                xmlTextWriterWriteFormatAttribute (
                  odt->content_wtr, _xml("table:number-columns-spanned"),
                  "%d", colspan);

              if (rowspan > 1)
                xmlTextWriterWriteFormatAttribute (
                  odt->content_wtr, _xml("table:number-rows-spanned"),
                  "%d", rowspan);

              xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));

              if (r < tab->h[V][0] || c < tab->h[H][0])
                xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Heading"));
              else
                xmlTextWriterWriteAttribute (odt->content_wtr, _xml("text:style-name"), _xml("Table_20_Contents"));

              if (cell.options & TAB_MARKUP)
                {
                  /* XXX */
                  char *s = output_get_text_from_markup (cell.text);
                  write_xml_with_line_breaks (odt, s);
                  free (s);
                }
              else
                write_xml_with_line_breaks (odt, cell.text);

              for (int i = 0; i < cell.n_footnotes; i++)
                write_footnote (odt, cell.footnotes[i]);

              xmlTextWriterEndElement (odt->content_wtr); /* text:p */
              xmlTextWriterEndElement (odt->content_wtr); /* table:table-cell */
	    }
	  else
	    {
	      xmlTextWriterStartElement (odt->content_wtr, _xml("table:covered-table-cell"));
	      xmlTextWriterEndElement (odt->content_wtr);
	    }
	}

      xmlTextWriterEndElement (odt->content_wtr); /* row */

      int ht = tab->h[V][0];
      if (ht > 0 && r == ht - 1)
	xmlTextWriterEndElement (odt->content_wtr); /* table-header-rows */
    }

  xmlTextWriterEndElement (odt->content_wtr); /* table */

  /* Write a caption for the table */
  write_table_item_text (odt, table_item_get_caption (item));
}

static void
odt_output_text (struct odt_driver *odt, const char *text)
{
  xmlTextWriterStartElement (odt->content_wtr, _xml("text:p"));
  xmlTextWriterWriteString (odt->content_wtr, _xml(text));
  xmlTextWriterEndElement (odt->content_wtr);
}

/* Submit a table to the ODT driver */
static void
odt_submit (struct output_driver *driver,
            const struct output_item *output_item)
{
  struct odt_driver *odt = odt_driver_cast (driver);

  if (is_table_item (output_item))
    write_table (odt, to_table_item (output_item));
  else if (is_text_item (output_item))
    odt_output_text (odt, text_item_get_text (to_text_item (output_item)));
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      char *s = msg_to_string (message_item_get_msg (message_item));
      odt_output_text (odt, s);
      free (s);
    }
}

struct output_driver_factory odt_driver_factory =
  { "odt", "pspp.odf", odt_create };

static const struct output_driver_class odt_driver_class =
{
  "odf",
  odt_destroy,
  odt_submit,
  NULL,
};
