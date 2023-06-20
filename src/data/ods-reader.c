/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013, 2016, 2020, 2021 Free Software Foundation, Inc.

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

#include "ods-reader.h"
#include "spreadsheet-reader.h"

#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <zlib.h>

#include "data/case.h"
#include "data/casereader-provider.h"
#include "data/data-in.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/identifier.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/zip-reader.h"
#include "libpspp/hmap.h"
#include "libpspp/hash-functions.h"


#include "gl/c-strtod.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static void ods_file_casereader_destroy (struct casereader *, void *);
static struct ccase *ods_file_casereader_read (struct casereader *, void *);

static const struct casereader_class ods_file_casereader_class =
  {
    ods_file_casereader_read,
    ods_file_casereader_destroy,
    NULL,
    NULL,
  };

enum reader_state
  {
    STATE_INIT = 0,        /* Initial state */
    STATE_SPREADSHEET,     /* Found the start of the spreadsheet doc */
    STATE_TABLE,           /* Found the sheet that we actually want */
    STATE_ROW,             /* Found the start of the cell array */
    STATE_CELL,            /* Found a cell */
  };

struct state_data
{
  xmlTextReaderPtr xtr;
  struct zip_member *zm;
  int node_type;
  enum reader_state state;
  int row;
  int col;
  int current_sheet;
  xmlChar *current_sheet_name;

  int col_span;
};

static void
state_data_destroy (struct state_data *sd)
{
  xmlFree (sd->current_sheet_name);
  sd->current_sheet_name = NULL;

  xmlFreeTextReader (sd->xtr);
  sd->xtr = NULL;

  zip_member_finish (sd->zm);
  sd->zm = NULL;
}

struct ods_reader
{
  struct spreadsheet spreadsheet;
  struct zip_reader *zreader;

  int target_sheet_index;
  xmlChar *target_sheet_name;

  int n_allocated_sheets;

  /* The total number of sheets in the "workbook" */
  int n_sheets;

  /* State data for the reader */
  struct state_data rsd;

  struct string ods_errs;

  struct hmap cache;
};

/* A value to be kept in the hash table for cache purposes.  */
struct cache_datum
{
  struct hmap_node node;

  /* The number of the sheet.  */
  int sheet;

  /* The cell's row.  */
  int row;

  /* The cell's column.  */
  int col;

  /* The value of the cell.  */
  char *value;
};

static int
xml_reader_for_zip_member (void *zm_, char *buffer, int len)
{
  struct zip_member *zm = zm_;
  return zip_member_read (zm, buffer, len);
}

static void
ods_destroy (struct spreadsheet *s)
{
  struct ods_reader *r = (struct ods_reader *) s;

  int i;

  for (i = 0; i < r->n_allocated_sheets; ++i)
    {
      xmlFree (r->spreadsheet.sheets[i].name);
    }

  dict_unref (r->spreadsheet.dict);

  zip_reader_unref (r->zreader);
  free (r->spreadsheet.sheets);
  free (s->file_name);

  struct cache_datum *cell;
  struct cache_datum *next;
  HMAP_FOR_EACH_SAFE (cell, next, struct cache_datum, node, &r->cache)
    {
      free (cell->value);
      free (cell);
    }

  hmap_destroy (&r->cache);

  free (r);
}

static bool
reading_target_sheet (const struct ods_reader *r, const struct state_data *sd)
{
  if (r->target_sheet_name != NULL)
    {
      if (0 == xmlStrcmp (r->target_sheet_name, sd->current_sheet_name))
        return true;
    }

  if (r->target_sheet_index == sd->current_sheet + 1)
    return true;

  return false;
}


static bool process_node (struct ods_reader *or, struct state_data *r);


/* Initialise SD using R */
static bool
state_data_init (const struct ods_reader *r, struct state_data *sd)
{
  memset (sd, 0, sizeof (*sd));

  char *error = zip_member_open (r->zreader, "content.xml", &sd->zm);
  if (error)
    {
      free (error);
      return false;
    }

  sd->xtr =
    xmlReaderForIO (xml_reader_for_zip_member, NULL, sd->zm, NULL, NULL,
                    0);

  if (sd->xtr == NULL)
    return NULL;

  sd->state = STATE_INIT;
  return true;
}


static const char *
ods_get_sheet_name (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data sd;
  state_data_init (r, &sd);

  while ((r->n_allocated_sheets <= n
          || sd.state != STATE_SPREADSHEET)
         && process_node (r, &sd))
    continue;
  state_data_destroy (&sd);

  return r->spreadsheet.sheets[n].name;
}

static char *
ods_get_sheet_range (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data sd;
  state_data_init (r, &sd);

  while ((r->n_allocated_sheets <= n
          || r->spreadsheet.sheets[n].last_row == -1
          || sd.state != STATE_SPREADSHEET)
         && process_node (r, &sd))
    continue;
  state_data_destroy (&sd);

  return create_cell_range (
                          r->spreadsheet.sheets[n].first_col,
                          r->spreadsheet.sheets[n].first_row,
                          r->spreadsheet.sheets[n].last_col,
                          r->spreadsheet.sheets[n].last_row);
}

static unsigned int
ods_get_sheet_n_rows (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data sd;

  if (r->n_allocated_sheets > n && r->spreadsheet.sheets[n].last_row != -1)
    {
      return r->spreadsheet.sheets[n].last_row + 1;
    }

  state_data_init (r, &sd);
  while (process_node (r, &sd))
    continue;
  state_data_destroy (&sd);

  return r->spreadsheet.sheets[n].last_row + 1;
}

static unsigned int
ods_get_sheet_n_columns (struct spreadsheet *s, int n)
{
  struct ods_reader *r = (struct ods_reader *) s;
  struct state_data sd;

  if (r->n_allocated_sheets > n && r->spreadsheet.sheets[n].last_col != -1)
    return r->spreadsheet.sheets[n].last_col + 1;

  state_data_init (r, &sd);
  while (process_node (r, &sd))
    continue;
  state_data_destroy (&sd);

  return r->spreadsheet.sheets[n].last_col + 1;
}

static unsigned int
cell_hash (int n, int row, int column)
{
  return hash_int (column, hash_int (row, hash_int (n, 0)));
}

static struct cache_datum *
cache_lookup (struct ods_reader *r, int n, int row, int column)
{
  unsigned int hash = cell_hash (n, row, column);

  struct cache_datum *d;
  HMAP_FOR_EACH_WITH_HASH (d, struct cache_datum, node, hash, &r->cache)
    if (d->row == row && d->col == column && d->sheet == n)
      return d;
  return NULL;
}

static void
populate_cache (struct ods_reader *r)
{
  struct state_data sd;

  state_data_init (r, &sd);

  while (process_node (r, &sd))
    {
      if (sd.state == STATE_CELL)
        {
          /* When cell contents are encountered, copy and save it, discarding
             any older content.  */
          char *cell_content = CHAR_CAST (char *, xmlNodeGetContent (
                                            xmlTextReaderExpand (sd.xtr)));

          for (int c = sd.col - sd.col_span; c < sd.col; ++c)
            {
              if (cache_lookup (r, sd.current_sheet, sd.row, c))
                continue;

              struct cache_datum *cell_data = xmalloc (sizeof *cell_data);
              *cell_data = (struct cache_datum) {
                .row = sd.row - 1,
                .col = c,
                .sheet = sd.current_sheet,
                .value = xstrdup_if_nonnull (cell_content),
              };
              hmap_insert (&r->cache, &cell_data->node,
                           cell_hash (sd.current_sheet, sd.row - 1, c));
            }
          free (cell_content);
        }
    }

  state_data_destroy (&sd);
}

static char *
ods_get_sheet_cell (struct spreadsheet *s, int n, int row, int column)
{
  struct ods_reader *r = (struct ods_reader *) s;

  if (hmap_is_empty (&r->cache))
    populate_cache (r);

  const struct cache_datum *datum = cache_lookup (r, n, row, column);
  return datum ? xstrdup_if_nonnull (datum->value) : NULL;
}

static void
ods_file_casereader_destroy (struct casereader *reader UNUSED, void *r_)
{
  struct ods_reader *r = r_;
  if (r == NULL)
    return ;

  state_data_destroy (&r->rsd);

  if (! ds_is_empty (&r->ods_errs))
    msg (ME, "%s", ds_cstr (&r->ods_errs));

  ds_destroy (&r->ods_errs);

  if (r->spreadsheet.first_case && ! r->spreadsheet.used_first_case)
    case_unref (r->spreadsheet.first_case);

  caseproto_unref (r->spreadsheet.proto);
  r->spreadsheet.proto = NULL;

  xmlFree (r->target_sheet_name);
  r->target_sheet_name = NULL;

  spreadsheet_unref (&r->spreadsheet);
}

static bool
process_node (struct ods_reader *or, struct state_data *r)
{
  int ret = (r->state == STATE_CELL
             ? xmlTextReaderNext (r->xtr)
             : xmlTextReaderRead (r->xtr));
  if (ret != 1)
    return false;

  xmlChar *name = xmlTextReaderName (r->xtr);
  if (name == NULL)
    name = xmlStrdup (_xml ("--"));


  r->node_type = xmlTextReaderNodeType (r->xtr);

  switch (r->state)
    {
    case STATE_INIT:
      if (0 == xmlStrcasecmp (name, _xml("office:spreadsheet")) &&
          XML_READER_TYPE_ELEMENT  == r->node_type)
        {
          r->state = STATE_SPREADSHEET;
          r->current_sheet = -1;
          r->current_sheet_name = NULL;
        }
      break;
    case STATE_SPREADSHEET:
      if (0 == xmlStrcasecmp (name, _xml("table:table"))
          &&
          (XML_READER_TYPE_ELEMENT == r->node_type))
        {
          xmlFree (r->current_sheet_name);
          r->current_sheet_name = xmlTextReaderGetAttribute (r->xtr, _xml ("table:name"));

          ++r->current_sheet;

          if (r->current_sheet >= or->n_allocated_sheets)
            {
              assert (r->current_sheet == or->n_allocated_sheets);
              or->spreadsheet.sheets = xrealloc (or->spreadsheet.sheets, sizeof (*or->spreadsheet.sheets) * ++or->n_allocated_sheets);
              or->spreadsheet.sheets[or->n_allocated_sheets - 1].first_col = -1;
              or->spreadsheet.sheets[or->n_allocated_sheets - 1].last_col = -1;
              or->spreadsheet.sheets[or->n_allocated_sheets - 1].first_row = -1;
              or->spreadsheet.sheets[or->n_allocated_sheets - 1].last_row = -1;
              or->spreadsheet.sheets[or->n_allocated_sheets - 1].name = CHAR_CAST (char *, xmlStrdup (r->current_sheet_name));
            }
          if (or->n_allocated_sheets > or->n_sheets)
            or->n_sheets = or->n_allocated_sheets;

          r->col = 0;
          r->row = 0;

          r->state = STATE_TABLE;
        }
      else if (0 == xmlStrcasecmp (name, _xml("office:spreadsheet")) &&
               XML_READER_TYPE_ELEMENT  == r->node_type)
        {
          r->state = STATE_INIT;
        }
      break;
    case STATE_TABLE:
      if (0 == xmlStrcasecmp (name, _xml("table:table-row")) &&
          (XML_READER_TYPE_ELEMENT  == r->node_type))
        {
          xmlChar *value =
            xmlTextReaderGetAttribute (r->xtr,
                                       _xml ("table:number-rows-repeated"));

          int row_span = value ? _xmlchar_to_int (value) : 1;

          r->row += row_span;
          r->col = 0;

          if (! xmlTextReaderIsEmptyElement (r->xtr))
            r->state = STATE_ROW;

          xmlFree (value);
        }
      else if (0 == xmlStrcasecmp (name, _xml("table:table")) &&
               (XML_READER_TYPE_END_ELEMENT  == r->node_type))
        {
          r->state = STATE_SPREADSHEET;
        }
      break;

    case STATE_CELL:
      r->state = STATE_ROW;
      /* Fall through. */
    case STATE_ROW:
      if ((0 == xmlStrcasecmp (name, _xml ("table:table-cell")))
           &&
           (XML_READER_TYPE_ELEMENT  == r->node_type))
        {
          xmlChar *value =
            xmlTextReaderGetAttribute (r->xtr,
                                       _xml ("table:number-columns-repeated"));

          r->col_span = value ? _xmlchar_to_int (value) : 1;
          r->col += r->col_span;
          xmlFree (value);

          if (! xmlTextReaderIsEmptyElement (r->xtr))
            {
              assert (r->current_sheet >= 0);
              assert (r->current_sheet < or->n_allocated_sheets);

              if (or->spreadsheet.sheets[r->current_sheet].first_row == -1)
                or->spreadsheet.sheets[r->current_sheet].first_row = r->row - 1;

              if (
                (or->spreadsheet.sheets[r->current_sheet].first_col == -1)
                ||
                (or->spreadsheet.sheets[r->current_sheet].first_col >= r->col - 1)
                  )
                or->spreadsheet.sheets[r->current_sheet].first_col = r->col - 1;

              if (or->spreadsheet.sheets[r->current_sheet].last_row < r->row - 1)
                or->spreadsheet.sheets[r->current_sheet].last_row = r->row - 1;

              if (or->spreadsheet.sheets[r->current_sheet].last_col < r->col - 1)
                or->spreadsheet.sheets[r->current_sheet].last_col = r->col - 1;

              r->state = STATE_CELL;
            }
        }
      else if ((0 == xmlStrcasecmp (name, _xml ("table:table-row")))
                &&
                (XML_READER_TYPE_END_ELEMENT  == r->node_type))
        {
          r->state = STATE_TABLE;
        }
      break;
    default:
      NOT_REACHED ();
      break;
    };

  xmlFree (name);

  return true;
}

/*
   A struct containing the parameters of a cell's value
   parsed from the xml
*/
struct xml_value
{
  xmlChar *type;
  xmlChar *value;
  xmlChar *text;
};

struct var_spec
{
  char *name;
  struct xml_value firstval;
};


/* Determine the width that a xmv should probably have */
static int
xmv_to_width (const struct xml_value *xmv, int fallback)
{
  int width = SPREADSHEET_DEFAULT_WIDTH;

  /* Non-strings always have zero width */
  if (xmv->type != NULL && 0 != xmlStrcmp (xmv->type, _xml("string")))
    return 0;

  if (fallback != -1)
    return fallback;

  if (xmv->value)
    width = ROUND_UP (xmlStrlen (xmv->value),
                      SPREADSHEET_DEFAULT_WIDTH);
  else if (xmv->text)
    width = ROUND_UP (xmlStrlen (xmv->text),
                      SPREADSHEET_DEFAULT_WIDTH);

  return width;
}

/*
   Sets the VAR of case C, to the value corresponding to the xml data
 */
static void
convert_xml_to_value (struct ccase *c, const struct variable *var,
                      const struct xml_value *xmv, int col, int row)
{
  union value *v = case_data_rw (c, var);

  if (xmv->value == NULL && xmv->text == NULL)
    value_set_missing (v, var_get_width (var));
  else if (var_is_alpha (var))
    /* Use the text field, because it seems that there is no
       value field for strings */
    value_copy_str_rpad (v, var_get_width (var), xmv->text, ' ');
  else
    {
      const struct fmt_spec fmt = var_get_write_format (var);
      enum fmt_category fc  = fmt_get_category (fmt.type);

      assert (fc != FMT_CAT_STRING);

      if (0 == xmlStrcmp (xmv->type, _xml("float")))
        {
          v->f = c_strtod (CHAR_CAST (const char *, xmv->value), NULL);
        }
      else
        {
          const char *text = xmv->value ?
            CHAR_CAST (const char *, xmv->value) : CHAR_CAST (const char *, xmv->text);

          char *m = data_in (ss_cstr (text), "UTF-8", fmt.type,
                             settings_get_fmt_settings (), v,
                             var_get_width (var), "UTF-8");

          if (m)
            {
              char buf [FMT_STRING_LEN_MAX + 1];
              char *cell = create_cell_ref (col, row);

              msg (MW, _("Cannot convert the value in the spreadsheet cell %s to format (%s): %s"),
                   cell, fmt_to_string (fmt, buf), m);
              free (cell);
            }
          free (m);
        }
    }
}

/* Try to find out how many sheets there are in the "workbook" */
static int
get_sheet_count (struct zip_reader *zreader)
{
  xmlTextReaderPtr mxtr;
  struct zip_member *meta = NULL;
  char *error = zip_member_open (zreader, "meta.xml", &meta);
  if (error)
    {
      free (error);
      return -1;
    }

  mxtr = xmlReaderForIO (xml_reader_for_zip_member, NULL, meta, NULL, NULL, 0);

  while (1 == xmlTextReaderRead (mxtr))
    {
      xmlChar *name = xmlTextReaderName (mxtr);
      if (0 == xmlStrcmp (name, _xml("meta:document-statistic")))
        {
          xmlChar *attr = xmlTextReaderGetAttribute (mxtr, _xml ("meta:table-count"));

          if (attr != NULL)
            {
              int s = _xmlchar_to_int (attr);
              xmlFreeTextReader (mxtr);
              zip_member_finish (meta);
              xmlFree (name);
              xmlFree (attr);
              return s;
            }
          xmlFree (attr);
        }
      xmlFree (name);
    }

  xmlFreeTextReader (mxtr);
  zip_member_finish (meta);
  return -1;
}

static int
ods_get_sheet_n_sheets (struct spreadsheet *s)
{
  struct ods_reader *r = (struct ods_reader *) s;

  if (r->n_sheets >= 0)
    return r->n_sheets;

  r->n_sheets = get_sheet_count (r->zreader);

  return r->n_sheets;
}


static void
ods_error_handler (void *ctx, const char *mesg,
                   xmlParserSeverities sev UNUSED,
                   xmlTextReaderLocatorPtr loc)
{
  struct ods_reader *r = ctx;

  msg (MW, _("There was a problem whilst reading the %s file `%s' (near line %d): `%s'"),
       "ODF",
       r->spreadsheet.file_name,
       xmlTextReaderLocatorLineNumber (loc),
       mesg);
}


static bool init_reader (struct ods_reader *r, bool report_errors, struct state_data *state);

static struct casereader *
ods_make_reader (struct spreadsheet *spreadsheet,
                 const struct spreadsheet_read_options *opts)
{
  xmlChar *type = NULL;
  casenumber n_cases = CASENUMBER_MAX;
  int i;
  struct var_spec *var_spec = NULL;
  int n_var_specs = 0;

  struct ods_reader *r = (struct ods_reader *) spreadsheet;
  xmlChar *val_string = NULL;

  assert (r);
  ds_init_empty (&r->ods_errs);
  r = (struct ods_reader *) spreadsheet_ref (SPREADSHEET_CAST (r));

  if (!init_reader (r, true, &r->rsd))
    goto error;

  r->spreadsheet.used_first_case = false;
  r->spreadsheet.first_case = NULL;

  if (opts->cell_range)
    {
      if (! convert_cell_ref (opts->cell_range,
                               &r->spreadsheet.start_col, &r->spreadsheet.start_row,
                               &r->spreadsheet.stop_col, &r->spreadsheet.stop_row))
        {
          msg (SE, _("Invalid cell range `%s'"),
               opts->cell_range);
          goto error;
        }
    }
  else
    {
      r->spreadsheet.start_col = 0;
      r->spreadsheet.start_row = 0;
      r->spreadsheet.stop_col = -1;
      r->spreadsheet.stop_row = -1;
    }

  r->target_sheet_name = xmlStrdup (BAD_CAST opts->sheet_name);
  r->target_sheet_index = opts->sheet_index;

  /* Advance to the start of the cells for the target sheet */
  while (!reading_target_sheet (r, &r->rsd)
         || r->rsd.state != STATE_ROW
         || r->rsd.row <= r->spreadsheet.start_row)
    if (!process_node (r, &r->rsd))
      {
        msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
             spreadsheet->file_name);
        goto error;
      }

  if (opts->read_names)
    {
      while (process_node (r, &r->rsd))
        {
          /* If the row is finished then stop for now */
          if (r->rsd.state == STATE_TABLE && r->rsd.row > r->spreadsheet.start_row)
            break;

          int idx = r->rsd.col - r->spreadsheet.start_col - 1;

          if (idx < 0)
            continue;

          if (r->spreadsheet.stop_col != -1 && idx > r->spreadsheet.stop_col - r->spreadsheet.start_col)
            continue;

          if (r->rsd.state == STATE_CELL)
            {
              char *value = CHAR_CAST (char *, xmlNodeGetContent (
                                         xmlTextReaderExpand (r->rsd.xtr)));
              if (idx >= n_var_specs)
                {
                  var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + 1));

                  /* xrealloc (unlike realloc) doesn't initialise its memory to 0 */
                  memset (var_spec + n_var_specs,
                          0,
                          (idx - n_var_specs + 1) * sizeof (*var_spec));
                  n_var_specs = idx + 1;
                }
              for (int i = 0; i < r->rsd.col_span; ++i)
                {
                  var_spec[idx - i].firstval.text = 0;
                  var_spec[idx - i].firstval.value = 0;
                  var_spec[idx - i].firstval.type = 0;
                  var_spec[idx - i].name = xstrdup (value);
                }

              xmlFree (value);
            }
        }
    }

  /* Read in the first row of data */
  while (process_node (r, &r->rsd))
    {
      int idx;
      if (! reading_target_sheet (r, &r->rsd))
        break;

      /* If the row is finished then stop for now */
      if (r->rsd.state == STATE_TABLE &&
          r->rsd.row > r->spreadsheet.start_row + (opts->read_names ? 1 : 0))
        break;

      idx = r->rsd.col - r->spreadsheet.start_col - 1;
      if (idx < 0)
        continue;

      if (r->spreadsheet.stop_col != -1 && idx > r->spreadsheet.stop_col - r->spreadsheet.start_col)
        continue;

      if (r->rsd.state == STATE_CELL &&
           XML_READER_TYPE_ELEMENT  == r->rsd.node_type)
        {
          type = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value-type"));
          val_string = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value"));
        }

      if (r->rsd.state == STATE_CELL)
        {
          if (idx >= n_var_specs)
            {
              var_spec = xrealloc (var_spec, sizeof (*var_spec) * (idx + r->rsd.col_span));
              memset (var_spec + n_var_specs,
                      0,
                      (idx + r->rsd.col_span - n_var_specs) * sizeof (*var_spec));

              var_spec [idx].name = NULL;
              n_var_specs = idx + 1;
            }

          for (int x = 0; x < r->rsd.col_span; ++x)
          {
            var_spec [idx - x].firstval.type = xmlStrdup (type);
            var_spec [idx - x].firstval.text
              = xmlNodeGetContent (xmlTextReaderExpand (r->rsd.xtr));
            var_spec [idx - x].firstval.value = xmlStrdup (val_string);
          }

          free (val_string);
          free (type);
        }
    }

  /* Create the dictionary and populate it */
  r->spreadsheet.dict = dict_create (
    CHAR_CAST (const char *, xmlTextReaderConstEncoding (r->rsd.xtr)));

  for (i = 0; i < n_var_specs ; ++i)
    {
      int width = xmv_to_width (&var_spec[i].firstval, opts->asw);
      struct variable *var = dict_create_var_with_unique_name (
        r->spreadsheet.dict, var_spec[i].name, width);

      struct fmt_spec fmt;
      if (0 == xmlStrcmp (var_spec[i].firstval.type, _xml("date")))
        {
          fmt.type = FMT_DATE;
          fmt.d = 0;
          fmt.w = 20;
        }
      else
        fmt = fmt_default_for_width (width);
      var_set_both_formats (var, fmt);
    }

  if (n_var_specs ==  0)
    {
      msg (MW, _("Selected sheet or range of spreadsheet `%s' is empty."),
           spreadsheet->file_name);
      goto error;
    }

  /* Create the first case, and cache it */
  r->spreadsheet.proto = caseproto_ref (dict_get_proto (r->spreadsheet.dict));
  r->spreadsheet.first_case = case_create (r->spreadsheet.proto);
  case_set_missing (r->spreadsheet.first_case);

  for (i = 0 ; i < n_var_specs; ++i)
    {
      const struct variable *var = dict_get_var (r->spreadsheet.dict, i);

      convert_xml_to_value (r->spreadsheet.first_case, var,  &var_spec[i].firstval,
                            r->rsd.col - n_var_specs + i,
                            r->rsd.row - 1);
    }

  /* Read in the first row of data */
  while (process_node (r, &r->rsd))
    if (r->rsd.state == STATE_ROW)
      break;

  for (i = 0 ; i < n_var_specs ; ++i)
    {
      free (var_spec[i].firstval.type);
      free (var_spec[i].firstval.value);
      free (var_spec[i].firstval.text);
      free (var_spec[i].name);
    }

  free (var_spec);


  return casereader_create_sequential
    (NULL,
     r->spreadsheet.proto,
     n_cases,
     &ods_file_casereader_class, r);

 error:

  for (i = 0 ; i < n_var_specs ; ++i)
    {
      free (var_spec[i].firstval.type);
      free (var_spec[i].firstval.value);
      free (var_spec[i].firstval.text);
      free (var_spec[i].name);
    }

  free (var_spec);

  ods_file_casereader_destroy (NULL, r);

  return NULL;
}


/* Reads and returns one case from READER's file.  Returns a null
   pointer on failure. */
static struct ccase *
ods_file_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct ccase *c = NULL;
  struct ods_reader *r = r_;

  xmlChar *val_string = NULL;
  xmlChar *type = NULL;

  if (!r->spreadsheet.used_first_case)
    {
      r->spreadsheet.used_first_case = true;
      return r->spreadsheet.first_case;
    }


  /* Advance to the start of a row. (If there is one) */
  while (r->rsd.state != STATE_ROW && process_node (r, &r->rsd))
    continue;

  if (! reading_target_sheet (r, &r->rsd)
       ||  r->rsd.state < STATE_TABLE
       ||  (r->spreadsheet.stop_row != -1 && r->rsd.row > r->spreadsheet.stop_row + 1)
)
    {
      return NULL;
    }

  c = case_create (r->spreadsheet.proto);
  case_set_missing (c);

  while (process_node (r, &r->rsd))
    {
      if (r->spreadsheet.stop_row != -1 && r->rsd.row > r->spreadsheet.stop_row + 1)
        break;

      if (r->rsd.state == STATE_CELL &&
           r->rsd.node_type == XML_READER_TYPE_ELEMENT)
        {
          type = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value-type"));
          val_string = xmlTextReaderGetAttribute (r->rsd.xtr, _xml ("office:value"));
        }

      if (r->rsd.state == STATE_CELL)
        {
          int col;
          struct xml_value *xmv = XZALLOC (struct xml_value);
          xmv->text = xmlNodeGetContent (xmlTextReaderExpand (r->rsd.xtr));
          xmv->value = val_string;
          val_string = NULL;
          xmv->type = type;
          type = NULL;

          for (col = 0; col < r->rsd.col_span; ++col)
            {
              const struct variable *var;
              const int idx = r->rsd.col - col - r->spreadsheet.start_col - 1;
              if (idx < 0)
                continue;
              if (r->spreadsheet.stop_col != -1 && idx > r->spreadsheet.stop_col - r->spreadsheet.start_col)
                break;
              if (idx >= dict_get_n_vars (r->spreadsheet.dict))
                break;

              var = dict_get_var (r->spreadsheet.dict, idx);
              convert_xml_to_value (c, var, xmv, idx + r->spreadsheet.start_col, r->rsd.row - 1);
            }

          xmlFree (xmv->text);
          xmlFree (xmv->value);
          xmlFree (xmv->type);
          free (xmv);
        }
      if (r->rsd.state <= STATE_TABLE)
        break;
    }

  xmlFree (type);
  xmlFree (val_string);

  return c;
}

static bool
init_reader (struct ods_reader *r, bool report_errors,
             struct state_data *state)
{
  struct spreadsheet *s = SPREADSHEET_CAST (r);

  if (state)
    {
      struct zip_member *content;
      char *error = zip_member_open (r->zreader, "content.xml", &content);
      if (content == NULL)
        {
          free (error);
          return false;
        }

      xmlTextReaderPtr xtr = xmlReaderForIO (xml_reader_for_zip_member, NULL, content, NULL, NULL,
                                             report_errors
                                             ? 0
                                             : (XML_PARSE_NOERROR | XML_PARSE_NOWARNING));

      if (xtr == NULL)
        return false;

     *state = (struct state_data) { .xtr = xtr,
                                    .zm = content,
                                    .state = STATE_INIT };
     if (report_errors)
       xmlTextReaderSetErrorHandler (xtr, ods_error_handler, r);
  }

  strcpy (s->type, "ODS");
  s->destroy = ods_destroy;
  s->make_reader = ods_make_reader;
  s->get_sheet_name = ods_get_sheet_name;
  s->get_sheet_range = ods_get_sheet_range;
  s->get_sheet_n_sheets = ods_get_sheet_n_sheets;
  s->get_sheet_n_rows = ods_get_sheet_n_rows;
  s->get_sheet_n_columns = ods_get_sheet_n_columns;
  s->get_sheet_cell = ods_get_sheet_cell;

  return true;
}

struct spreadsheet *
ods_probe (const char *filename, bool report_errors)
{
  struct zip_reader *zr;
  char *error = zip_reader_create (filename, &zr);
  if (error)
    {
      if (report_errors)
        msg (ME, _("Cannot open %s as a OpenDocument file: %s"),
             filename, error);
      free (error);
      return NULL;
    }
  if (!zip_reader_contains_member (zr, "meta.xml")
      || !zip_reader_contains_member (zr, "content.xml"))
    {
      if (report_errors)
        msg (ME, _("%s is not an OpenDocument file."), filename);
      zip_reader_unref (zr);
      return NULL;
    }

  struct ods_reader *r = xmalloc (sizeof *r);
  *r = (struct ods_reader) {
    .zreader = zr,
    .cache = HMAP_INITIALIZER (r->cache),
    .n_sheets = -1,
    .spreadsheet = {
      .ref_cnt = 1,
      .file_name = xstrdup (filename),
    },
  };

  if (!init_reader (r, report_errors, NULL))
    goto error;

  return &r->spreadsheet;

 error:
  ods_destroy (&r->spreadsheet);
  return NULL;
}
