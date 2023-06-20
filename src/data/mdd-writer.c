/* PSPP - a program for statistical analysis.
   Copyright (C) 2018 Free Software Foundation, Inc.

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

#include "data/mdd-writer.h"

#include <errno.h>
#include <libxml/xmlwriter.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/format.h"
#include "data/make-file.h"
#include "data/missing-values.h"
#include "data/mrset.h"
#include "data/short-names.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "libpspp/version.h"

#include "gl/c-ctype.h"
#include "gl/ftoastr.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

#define _xml(X) CHAR_CAST (const xmlChar *, X)

enum val_numeric_type
  {
    VAL_INTEGER_TYPE = 1,
    VAL_STRING_TYPE = 2,
    VAL_CATEGORICAL_TYPE = 3,
    VAL_DATETIME_TYPE = 5,
    VAL_DECIMAL_TYPE = 6
  };

/* Get the numeric type of the variable. */
static enum val_numeric_type
var_get_numeric_type_ (const struct variable *var)
{
  const struct fmt_spec print = var_get_print_format (var);
  if (var_get_type (var) == VAL_STRING)
    return VAL_STRING_TYPE;

  if (var_has_value_labels (var))
    return VAL_CATEGORICAL_TYPE;

  if (print.d > 0)
    return VAL_DECIMAL_TYPE;

  if (print.type == FMT_DATETIME)
    return VAL_DATETIME_TYPE;

  if (print.type == FMT_F)
    return VAL_INTEGER_TYPE;

  return VAL_CATEGORICAL_TYPE;
}

/* Metadata file writer. */
struct mdd_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion for file. */
    FILE *file;                        /* File stream. */
    struct replace_file *rf;    /* Ticket for replacing output file. */

    xmlTextWriter *writer;
  };

/* Either a variable or a multi-response set. */
struct var_or_mrset
  {
    /* If true, the union contains a multi-response set.  Otherwise, it
       contains a variable. */
    bool is_mrset;
    union
      {
        const struct mrset *mrset;
        const struct variable *variable;
      };
  };

struct all_dict_variables
  {
    struct var_or_mrset *vars;
    size_t count;
  };

struct all_dict_variables
all_variables (struct dictionary *dict);

/* Extract all variables in a dictionary, both normal and multi.

   Excludes variables which are subvariables of an MRSET. */
struct all_dict_variables
all_variables (struct dictionary *dict)
{
  size_t n_vars = dict_get_n_vars (dict);

  /* Start with a set of all variable names. */
  struct string_set var_names = STRING_SET_INITIALIZER (var_names);
  for (size_t i = 0; i < n_vars; ++i)
    {
      const struct variable *var = dict_get_var (dict, i);
      string_set_insert (&var_names, var_get_name (var));
    }

  /* For each MRSET M, remove all subvariable names of M from S. */
  size_t n_sets = dict_get_n_mrsets (dict);
  for (size_t set_idx = 0; set_idx < n_sets; set_idx++)
    {
      const struct mrset *mrset = dict_get_mrset (dict, set_idx);
      for (size_t i = 0; i < mrset->n_vars; ++i)
        {
          const struct variable *var = mrset->vars[i];
          string_set_delete (&var_names, var_get_name (var));
        }
    }

  /* Add the number of remaining variables to the number of MRSets. */
  size_t var_count = n_sets + string_set_count (&var_names);

  /* Allocate an array of var_or_mrset pointers (initially null). */
  struct var_or_mrset *var_or_mrset_array
    = XCALLOC (var_count, struct var_or_mrset);

  /* Fill the array. */
  struct string_set added_mrsets = STRING_SET_INITIALIZER (added_mrsets);

  size_t var_idx = 0;
  for (size_t i = 0; i < n_vars; ++i)
    {
      const struct variable *var = dict_get_var (dict, i);
      bool found_in_mrset = false;
      for (size_t set_idx = 0; set_idx < n_sets; set_idx++)
        {
          const struct mrset *mrset = dict_get_mrset (dict, set_idx);
          for (size_t i = 0; i < mrset->n_vars; ++i)
            {
              const struct variable *subvar = mrset->vars[i];
              if (!strcmp (var_get_name (var), var_get_name (subvar)))
                {
                  /* Then this variable is a member of this MRSet. */
                  found_in_mrset = true;

                  /* Check if this MRSet has already been added and add
                     otherwise. */
                  if (!string_set_contains (&added_mrsets, mrset->name))
                    {
                      string_set_insert (&added_mrsets, mrset->name);

                      assert (var_idx < var_count);
                      struct var_or_mrset *v_o_m
                        = &var_or_mrset_array[var_idx++];
                      v_o_m->is_mrset = true;
                      v_o_m->mrset = mrset;
                    }
                }
            }
        }

      /* If the variable wasn't found to be a member of any MRSets, record it as
         a normal variable. */
      if (!found_in_mrset)
        {
          /* The variable is not part of a multi-response set. */
          assert (var_idx < var_count);
          struct var_or_mrset *v_o_m = &var_or_mrset_array[var_idx++];
          v_o_m->is_mrset = false;
          v_o_m->variable = var;
        }
    }

  /* Ensure that we filled up the array. */
  assert (var_idx == var_count);

  /* Cleanup. */
  string_set_destroy (&added_mrsets);
  string_set_destroy (&var_names);

  struct all_dict_variables result;
  result.vars = var_or_mrset_array;
  result.count = var_count;
  return result;
}


/* Returns true if an I/O error has occurred on WRITER, false otherwise. */
static bool
mdd_write_error (const struct mdd_writer *writer)
{
  return ferror (writer->file);
}

static bool
mdd_close (struct mdd_writer *w)
{
  if (!w)
    return true;

  if (w->writer)
    xmlFreeTextWriter (w->writer);

  bool ok = true;
  if (w->file)
    {
      fflush (w->file);

      ok = !mdd_write_error (w);
      if (fclose (w->file) == EOF)
        ok = false;

      if (!ok)
        msg (ME, _("An I/O error occurred writing metadata file `%s'."),
             fh_get_file_name (w->fh));

      if (ok ? !replace_file_commit (w->rf) : !replace_file_abort (w->rf))
        ok = false;
    }

  fh_unlock (w->lock);
  fh_unref (w->fh);

  free (w);

  return ok;
}

static void
write_empty_element (xmlTextWriter *writer, const char *name)
{
  xmlTextWriterStartElement (writer, _xml (name));
  xmlTextWriterEndElement (writer);
}

static void
write_attr (xmlTextWriter *writer, const char *key, const char *value)
{
  xmlTextWriterWriteAttribute (writer, _xml (key), _xml (value));
}

static void
write_global_name_space (xmlTextWriter *writer)
{
  write_attr (writer, "global-name-space", "-1");
}

static void
write_xml_lang (xmlTextWriter *writer)
{
  /* XXX should write real language */
  xmlTextWriterWriteAttributeNS (writer, _xml ("xml"), _xml ("lang"), NULL,
                                 _xml ("en-US"));
}

static void
write_value_label_value (xmlTextWriter *writer, const struct val_lab *vl,
                         int width)
{
  /* XXX below would better use syntax_gen_value(). */
  const union value *value = val_lab_get_value (vl);
  if (width)
    {
      char *s = xmemdup0 (value->s, width);
      xmlTextWriterWriteAttribute (writer, _xml ("value"), _xml (s));
      free (s);
    }
  else
    {
      char s[DBL_BUFSIZE_BOUND];

      c_dtoastr (s, sizeof s, 0, 0, value->f);
      xmlTextWriterWriteAttribute (writer, _xml ("value"), _xml (s));
    }
}

static void
write_context (xmlTextWriter *writer, const char *name,
               const char *alternative)
{
  xmlTextWriterStartElement (writer, _xml ("context"));
  write_attr (writer, "name", name);
  if (alternative)
    {
      xmlTextWriterStartElement (writer, _xml ("alternatives"));
      xmlTextWriterStartElement (writer, _xml ("alternative"));
      write_attr (writer, "name", alternative);
      xmlTextWriterEndElement (writer);
      write_empty_element (writer, "deleted");
      xmlTextWriterEndElement (writer);
    }
  xmlTextWriterEndElement (writer);
}

static char *
name_to_id (const char *name)
{
  char *id = xmalloc (strlen (name) + 2);
  char *d = id;
  for (const char *s = name; *s; s++)
    {
      if (c_isalpha (*s))
        *d++ = c_tolower (*s);
      else if (c_isdigit (*s))
        {
          if (d == id)
            *d++ = '_';
          *d++ = *s;
        }
      else
        {
          if (d == id || d[-1] != '_')
            *d++ = '_';
        }
    }
  if (d > id && d[-1] == '_')
    d--;
  *d = '\0';

  return id;
}

static void
write_variable_section (xmlTextWriter *writer, const struct variable *var, int id)
{
  xmlTextWriterStartElement (writer, _xml ("variable"));
  write_attr (writer, "name", var_get_name (var));

  bool is_string = var_get_type (var) == VAL_STRING;

  int type = var_get_numeric_type_ (var);
  xmlTextWriterWriteFormatAttribute (writer, _xml ("type"), "%d", type);

  int max = is_string ? var_get_width (var) : 1;
  xmlTextWriterWriteFormatAttribute (writer, _xml ("max"), "%d", max);

  write_attr (writer, "maxtype", "3");

  const char *label = var_get_label (var);
  if (label)
    {
      xmlTextWriterStartElement (writer, _xml ("labels"));
      write_attr (writer, "context", "LABEL");

      xmlTextWriterStartElement (writer, _xml ("text"));
      write_attr (writer, "context", "ANALYSIS");
      write_xml_lang (writer);
      xmlTextWriterWriteString (writer, _xml (label));
      xmlTextWriterEndElement (writer);

      xmlTextWriterEndElement (writer);
    }

  const struct val_labs *val_labs = var_get_value_labels (var);
  size_t n_vls = val_labs_count (val_labs);
  if (n_vls)
    {
      const struct val_lab **vls = val_labs_sorted (val_labs);

      /* <categories/> */
      xmlTextWriterStartElement (writer, _xml ("categories"));
      write_global_name_space (writer);
      int width = var_get_width (var);
      for (size_t j = 0; j < n_vls; j++)
        {
          const struct val_lab *vl = vls[j];
          const union value *value = val_lab_get_value (vl);

          /* <category> */
          xmlTextWriterStartElement (writer, _xml ("category"));
          xmlTextWriterWriteFormatAttribute (writer, _xml ("id"), "_%d", id);

          char *name = name_to_id (val_lab_get_label (vl));
          write_attr (writer, "name", name);
          free (name);

          /* If the value here is missing, annotate it.

             XXX only checking "user" here because not sure of correct other
             cases. */
          enum mv_class miss = var_is_value_missing (var, value);
          if (miss)
            write_attr (writer, "missing", miss == MV_USER ? "user" : "system");

          /* <properties/> */
          xmlTextWriterStartElement (writer, _xml ("properties"));
          xmlTextWriterStartElement (writer, _xml ("property"));
          write_attr (writer, "name", "Value");
          write_value_label_value (writer, vl, width);
          write_attr (writer, "type", "5");
          write_attr (writer, "context", "Analysis");
          xmlTextWriterEndElement (writer); /* </property> */
          xmlTextWriterEndElement (writer); /* </properties> */

          /* <labels/> */
          xmlTextWriterStartElement (writer, _xml ("labels"));
          write_attr (writer, "context", "LABEL");
          xmlTextWriterStartElement (writer, _xml ("text"));
          write_attr (writer, "context", "ANALYSIS");
          write_xml_lang (writer);
          xmlTextWriterWriteString (writer,
                                    _xml (val_lab_get_label (vl)));
          xmlTextWriterEndElement (writer); /* </text> */
          xmlTextWriterEndElement (writer); /* </labels> */


          /* </category> */
          xmlTextWriterEndElement (writer);
        }
      write_empty_element (writer, "deleted");
      xmlTextWriterEndElement (writer); /* </categories> */

      free (vls);
    }
  /* </variable> */
  xmlTextWriterEndElement (writer);
}

bool
mdd_write (struct file_handle *fh, struct dictionary *dict,
           const char *sav_name)
{
  struct mdd_writer *w = XZALLOC (struct mdd_writer);
  size_t n_vars = dict_get_n_vars (dict);

  /* Open file handle as an exclusive writer. */
  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  w->lock = fh_lock (fh, FH_REF_FILE, N_("metadata file"), FH_ACC_WRITE, true);
  if (w->lock == NULL)
    goto error;

  /* Create the file on disk. */
  w->rf = replace_file_start (fh, "wb", 0444, &w->file);
  if (w->rf == NULL)
    {
      msg (ME, _("Error opening `%s' for writing as a metadata file: %s."),
           fh_get_file_name (fh), strerror (errno));
      goto error;
    }

  w->writer = xmlNewTextWriter (xmlOutputBufferCreateFile (w->file, NULL));
  if (!w->writer)
    {
      msg (ME, _("Internal error creating xmlTextWriter.  "
                 "Please report this to %s."), PACKAGE_BUGREPORT);
      goto error;
    }

  xmlTextWriterStartDocument (w->writer, NULL, "UTF-8", NULL);

  /* The MDD file contents seem to roughly correspond to the object model
     documentatation at:
     https://support.unicomsi.com/manuals/intelligence/75/DDL/MDM/docjet/metadata/chm/contentsf.html.  */

  /* <?xml-stylesheet type="text/xsl" href="mdd.xslt"?> */
  xmlTextWriterStartPI (w->writer, _xml ("xml-stylesheet"));
  xmlTextWriterWriteString (w->writer,
                            _xml ("type=\"text/xsl\" href=\"mdd.xslt\""));
  xmlTextWriterEndPI (w->writer);

  xmlTextWriterStartElement (w->writer, _xml ("xml"));

  /* <mdm:metadata xmlns:mdm="http://www.spss.com/mr/dm/metadatamodel/Arc
     3/2000-02-04" mdm_createversion="7.0.0.0.331"
     mdm_lastversion="7.0.0.0.331" id="c4c181c1-0d7c-42e3-abcd-f08296d1dfdc"
     data_version="9" data_sub_version="1" systemvariable="0"
     dbfiltervalidation="-1"> */
  xmlTextWriterStartElementNS (
    w->writer, _xml ("mdm"), _xml ("metadata"),
    _xml ("http://www.spss.com/mr/dm/metadatamodel/Arc%203/2000-02-04"));
  static const struct pair
    {
      const char *key, *value;
    }
  pairs[] =
    {
      { "mdm_createversion", "7.0.0.0.331" },
      { "mdm_lastversion", "7.0.0.0.331" },
      { "id", "c4c181c1-0d7c-42e3-abcd-f08296d1dfdc" },
      { "data_version", "9" },
      { "data_sub_version", "1" },
      { "systemvariable", "0" },
      { "dbfiltervalidation", "-1" },
    };
  const int n_pairs = sizeof pairs / sizeof *pairs;
  for (const struct pair *p = pairs; p < &pairs[n_pairs]; p++)
    xmlTextWriterWriteAttribute (w->writer, _xml (p->key), _xml (p->value));

  /* <atoms/> */
  xmlTextWriterStartElement (w->writer, _xml ("atoms"));
  /* XXX Real files contain a list of languages and a few other random strings
     here in <atom name="..."/> elements.  It's really not clear what they're
     good for. */
  xmlTextWriterEndElement (w->writer);

  /* <datasources> */
  xmlTextWriterStartElement (w->writer, _xml ("datasources"));
  xmlTextWriterWriteAttribute (w->writer, _xml ("default"), _xml ("mrSavDsc"));

  /* <connection/> */
  xmlTextWriterStartElement (w->writer, _xml ("connection"));
  write_attr (w->writer, "name", "mrSavDsc");
  write_attr (w->writer, "dblocation", sav_name);
  write_attr (w->writer, "cdscname", "mrSavDsc");
  write_attr (w->writer, "project", "126");

  struct all_dict_variables allvars = all_variables (dict);
  struct var_or_mrset *var_or_mrset_array = allvars.vars;
  size_t var_count = allvars.count;

  short_names_assign (dict);
  for (size_t i = 0; i < var_count; i++)
    {
      const struct var_or_mrset var_or_mrset = var_or_mrset_array[i];
      xmlTextWriterStartElement (w->writer, _xml ("var"));
      if (var_or_mrset.is_mrset)
        {
          const struct mrset *mrset = var_or_mrset.mrset;
          write_attr (w->writer, "fullname", mrset->name + 1);
          write_attr (w->writer, "aliasname", mrset->name);

          for (size_t subvar_idx = 0; subvar_idx < mrset->n_vars; subvar_idx++)
            {
              xmlTextWriterStartElement(w->writer, _xml ("subalias"));
              xmlTextWriterWriteFormatAttribute (w->writer, _xml ("index"),
                                                 "%zu", subvar_idx);
              write_attr (w->writer, "name",
                          var_get_name (mrset->vars[subvar_idx]));

              xmlTextWriterEndElement (w->writer); /* subalias */
            }
        }
      else
        {
          const struct variable *var = var_or_mrset.variable;

          char *short_name = xstrdup (var_get_short_name (var, 0));
          for (char *p = short_name; *p; p++)
            *p = c_tolower (*p);
          write_attr (w->writer, "fullname", short_name);
          free (short_name);

          write_attr (w->writer, "aliasname", var_get_name (var));

          const struct val_labs *val_labs = var_get_value_labels (var);
          size_t n_vls = val_labs_count (val_labs);
          if (n_vls)
            {
              const struct val_lab **vls = val_labs_sorted (val_labs);

              xmlTextWriterStartElement (w->writer, _xml ("nativevalues"));
              int width = var_get_width (var);
              for (size_t j = 0; j < n_vls; j++)
                {
                  const struct val_lab *vl = vls[j];
                  xmlTextWriterStartElement (w->writer, _xml ("nativevalue"));

                  char *fullname = name_to_id (val_lab_get_label (vl));
                  write_attr (w->writer, "fullname", fullname);
                  free (fullname);

                  write_value_label_value (w->writer, vl, width);
                  xmlTextWriterEndElement (w->writer);
                }
              xmlTextWriterEndElement (w->writer);

              free (vls);
            }

        }
      xmlTextWriterEndElement (w->writer); /* var */
    }

  xmlTextWriterEndElement (w->writer); /* connection */
  xmlTextWriterEndElement (w->writer); /* datasources */

  /* If the dictionary has a label, record it here */
  const char *file_label = dict_get_label (dict);
  if (file_label != NULL)
    {
      xmlTextWriterStartElement (w->writer, _xml ("labels"));
      write_attr (w->writer, "context", "LABEL");
      xmlTextWriterStartElement (w->writer, _xml ("text"));

      write_attr (w->writer, "context", "ANALYSIS");
      write_xml_lang (w->writer);
      xmlTextWriterWriteString (w->writer, _xml (file_label));

      /* </text> */
      xmlTextWriterEndElement (w->writer);

      /* </labels> */
      xmlTextWriterEndElement (w->writer);
    }

  /* We reserve ids 1...N_VARS for variables and then start other ids after
     that. */
  int id = dict_get_n_vars (dict) + 1;

  /* <definition/> */
  xmlTextWriterStartElement (w->writer, _xml ("definition"));
  for (size_t i = 0; i < var_count; i++)
    {
      xmlTextWriterWriteFormatAttribute (w->writer, _xml ("id"), "%zu", i + 1);
      const struct var_or_mrset var_or_mrset = var_or_mrset_array[i];

      if (var_or_mrset.is_mrset)
        {
          const struct mrset *mrset = var_or_mrset.mrset;
          xmlTextWriterStartElement (w->writer, _xml ("variable"));
          write_attr (w->writer, "name", mrset->name);

          /* Use the type of the first subvariable as the type of the MRSET? */
          write_attr (w->writer, "type", "3");

          xmlTextWriterStartElement (w->writer, _xml ("properties"));
          xmlTextWriterStartElement (w->writer, _xml ("property"));
          write_attr (w->writer, "name", "QvLabel");
          write_attr (w->writer, "value", mrset->name);
          write_attr (w->writer, "type", "8");
          write_attr (w->writer, "context", "Analysis");
          /* </property> */
          xmlTextWriterEndElement (w->writer);
          /* </properties> */
          xmlTextWriterEndElement (w->writer);


          xmlTextWriterStartElement (w->writer, _xml ("labels"));
          write_attr (w->writer, "context", "LABEL");
          xmlTextWriterStartElement (w->writer, _xml ("text"));

          write_attr (w->writer, "context", "ANALYSIS");
          write_xml_lang (w->writer);
          xmlTextWriterWriteString (w->writer, _xml (mrset->label));

          /* </text> */
          xmlTextWriterEndElement (w->writer);

          /* </labels> */
          xmlTextWriterEndElement (w->writer);

          xmlTextWriterStartElement (w->writer, _xml ("categories"));
          write_attr (w->writer, "global-name-space", "-1");
          write_empty_element (w->writer, "deleted");

          /* Individual categories */
          int value = 2;
          for (size_t var_idx = 0; var_idx < mrset->n_vars; ++var_idx)
            {
              const struct variable *subvar = mrset->vars[var_idx];
              value += 2;
              xmlTextWriterStartElement (w->writer, _xml ("category"));
              write_attr (w->writer, "context", "LABEL");
              char *name_without_spaces = strdup (var_get_name (subvar));
              for (size_t i = 0; name_without_spaces[i]; ++i)
                if (name_without_spaces[i] == ' ') name_without_spaces[i] = '_';
              write_attr (w->writer, "name", name_without_spaces);
              free (name_without_spaces);

              xmlTextWriterStartElement (w->writer, _xml ("properties"));
              xmlTextWriterStartElement (w->writer, _xml ("property"));
              write_attr (w->writer, "name", "QvBasicNum");
              xmlTextWriterWriteFormatAttribute
                (w->writer, _xml ("value"), "%d", value);
              write_attr (w->writer, "type", "3");
              write_attr (w->writer, "context", "Analysis");
              /* </property> */
              xmlTextWriterEndElement (w->writer);
              /* </properties> */
              xmlTextWriterEndElement (w->writer);

              xmlTextWriterStartElement (w->writer, _xml ("labels"));
              write_attr (w->writer, "context", "LABEL");

              xmlTextWriterStartElement (w->writer, _xml ("text"));
              write_attr (w->writer, "context", "ANALYSIS");
              write_xml_lang (w->writer);
              xmlTextWriterWriteString (w->writer, _xml (var_get_label (subvar)));
              /* </text> */
              xmlTextWriterEndElement (w->writer);


              /* </labels> */
              xmlTextWriterEndElement (w->writer);
              /* </category> */
              xmlTextWriterEndElement (w->writer);
            }

          /* </categories> */
          xmlTextWriterEndElement (w->writer);
          /* </variable> */
          xmlTextWriterEndElement (w->writer);
        }
      else
        {
          const struct variable *var = var_or_mrset.variable;
          write_variable_section(w->writer, var, id++);
        }
    }
  xmlTextWriterEndElement (w->writer); /* </definition> */

  write_empty_element (w->writer, "system");
  write_empty_element (w->writer, "systemrouting");
  write_empty_element (w->writer, "mappings");

  /* <design/> */
  xmlTextWriterStartElement (w->writer, _xml ("design"));
  xmlTextWriterStartElement (w->writer, _xml ("fields"));
  write_attr (w->writer, "name", "@fields");
  write_global_name_space (w->writer);
  for (size_t i = 0; i < n_vars; i++)
    {
      const struct variable *var = dict_get_var (dict, i);
      xmlTextWriterStartElement (w->writer, _xml ("variable"));
      xmlTextWriterWriteFormatAttribute (w->writer, _xml ("id"),
                                         "_%zu", i + 1);
      write_attr (w->writer, "name", var_get_name (var));
      xmlTextWriterWriteFormatAttribute (w->writer, _xml ("ref"),
                                         "%zu", i + 1);
      xmlTextWriterEndElement (w->writer);
    }
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterStartElement (w->writer, _xml ("types"));
  write_attr (w->writer, "name", "@types");
  write_global_name_space (w->writer);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterStartElement (w->writer, _xml ("pages"));
  write_attr (w->writer, "name", "@pages");
  write_global_name_space (w->writer);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterStartElement (w->writer, _xml ("routings"));
  xmlTextWriterStartElement (w->writer, _xml ("scripts"));
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterEndElement (w->writer);

  /* <languages/> */
  /* XXX should use the real language */
  xmlTextWriterStartElement (w->writer, _xml ("languages"));
  write_attr (w->writer, "base", "EN-US");
  xmlTextWriterStartElement (w->writer, _xml ("language"));
  write_attr (w->writer, "name", "EN-US");
  write_attr (w->writer, "id", "0409");
  xmlTextWriterEndElement (w->writer);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);

  /* <contexts/> */
  xmlTextWriterStartElement (w->writer, _xml ("contexts"));
  write_attr (w->writer, "base", "Analysis");
  write_context (w->writer, "ANALYSIS", "QUESTION");
  write_context (w->writer, "QUESTION", "ANALYSIS");
  write_context (w->writer, "WEBAPP", NULL);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);

  /* <labeltypes/> */
  xmlTextWriterStartElement (w->writer, _xml ("labeltypes"));
  write_attr (w->writer, "base", "label");
  write_context (w->writer, "LABEL", NULL);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);

  /* <routingcontexts/> */
  write_empty_element (w->writer, "routingcontexts");

  /* <scripttypes/> */
  xmlTextWriterStartElement (w->writer, _xml ("scripttypes"));
  write_attr (w->writer, "base", "mrScriptBasic");
  write_context (w->writer, "MRSCRIPTBASIC", NULL);
  write_empty_element (w->writer, "deleted");
  xmlTextWriterEndElement (w->writer);

  /* <versionlist/> */
  write_empty_element (w->writer, "versionlist");

  /* <categorymap/> */
  xmlTextWriterStartElement (w->writer, _xml ("categorymap"));
  struct string_set categories = STRING_SET_INITIALIZER (categories);
  for (size_t i = 0; i < n_vars; i++)
    {
      const struct variable *var = dict_get_var (dict, i);
      const struct val_labs *val_labs = var_get_value_labels (var);
      size_t n_vls = val_labs_count (val_labs);
      if (n_vls)
        {
          const struct val_lab **vls = val_labs_sorted (val_labs);

          for (size_t j = 0; j < n_vls; j++)
            {
              const struct val_lab *vl = vls[j];

              char *label = name_to_id (val_lab_get_label (vl));
              if (string_set_insert_nocopy (&categories, label))
                {
                  xmlTextWriterStartElement (w->writer, _xml ("categoryid"));
                  write_attr (w->writer, "name", label);
                  xmlTextWriterWriteFormatAttribute (
                    w->writer, _xml ("value"),
                    "%zu", string_set_count (&categories));
                  xmlTextWriterEndElement (w->writer);
                }
            }

          free (vls);
        }
    }
  string_set_destroy (&categories);
  xmlTextWriterEndElement (w->writer);

  /* <savelogs/> */
  xmlTextWriterStartElement (w->writer, _xml ("savelogs"));
  xmlTextWriterStartElement (w->writer, _xml ("savelog"));
  write_attr (w->writer, "fileversion", "7.0.0.0.331");
  write_attr (w->writer, "versionset", "");
  write_attr (w->writer, "username", "Administrator");
  time_t t;
  if (time (&t) == (time_t) -1)
    write_attr (w->writer, "date", "01/01/1970 00:00:00 AM");
  else
    {
      struct tm *tm = localtime (&t);
      int hour = tm->tm_hour % 12;
      xmlTextWriterWriteFormatAttribute (w->writer, _xml ("date"),
                                         "%02d/%02d/%04d %02d:%02d:%02d %s",
                                         tm->tm_mon + 1, tm->tm_mday,
                                         tm->tm_year + 1900,
                                         hour ? hour : 12, tm->tm_min,
                                         tm->tm_sec,
                                         tm->tm_hour < 12 ? "AM" : "PM");
    }
  xmlTextWriterStartElement (w->writer, _xml ("user"));
  write_attr (w->writer, "name", "pspp");
  write_attr (w->writer, "fileversion", version);
  write_attr (w->writer, "comment", "Written by GNU PSPP");
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterEndElement (w->writer);
  xmlTextWriterEndElement (w->writer);

  /* </xml> */
  xmlTextWriterEndElement (w->writer);

  xmlTextWriterEndDocument (w->writer);

  free(var_or_mrset_array);

error:
  mdd_close (w);
  return NULL;
}
