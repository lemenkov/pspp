/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2012, 2013, 2014
   2021,  Free Software Foundation, Inc.

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

#include <float.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"
#include "gl/c-xvasprintf.h"
#include "gl/mbiter.h"
#include "gl/size_max.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Explains how to recode one value. */
struct arc_item
  {
    struct hmap_node hmap_node; /* Element in "struct arc_spec" hash table. */
    union value from;           /* Original value. */
    int width;                  /* Width of the original value */
    bool missing;               /* Is 'from' missing in its source varible? */
    char *value_label;          /* Value label in source variable, if any. */

    double to;                  /* Recoded value. */
  };

/* Explains how to recode an AUTORECODE variable. */
struct arc_spec
  {
    int width;                  /* Variable width. */
    int src_idx;                /* Case index of source variable. */
    char *src_name;             /* Name of source variable. */
    struct fmt_spec format;     /* Print format in source variable. */
    struct variable *dst;       /* Target variable. */
    struct missing_values mv;   /* Missing values of source variable. */
    char *label;                /* Variable label of source variable. */
    struct rec_items *items;
  };

/* Descending or ascending sort order. */
enum arc_direction
  {
    ASCENDING,
    DESCENDING
  };

struct rec_items
{
  struct hmap ht;         /* Hash table of "struct arc_item"s. */
};



/* AUTORECODE data. */
struct autorecode_pgm
{
  struct arc_spec *specs;
  size_t n_specs;

  bool blank_valid;
};

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;

static int compare_arc_items (const void *, const void *, const void *aux);
static void arc_free (struct autorecode_pgm *);
static struct arc_item *find_arc_item (
  const struct rec_items *, const union value *, int width,
  size_t hash);

/* Returns WIDTH with any trailing spaces in VALUE trimmed off (except that a
   minimum width of 1 is always returned because otherwise the width would
   indicate a numeric type). */
static int
value_trim_spaces (const union value *value, int width)
{
  while (width > 1 && value->s[width - 1] == ' ')
    width--;
  return width;
}

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  const struct variable **src_vars = NULL;
  size_t n_srcs = 0;

  char **dst_names = NULL;
  size_t n_dsts = 0;

  enum arc_direction direction = ASCENDING;
  bool print = false;

  /* Create procedure. */
  struct autorecode_pgm *arc = xzalloc (sizeof *arc);
  arc->blank_valid = true;

  /* Parse variable lists. */
  lex_match_id (lexer, "VARIABLES");
  lex_match (lexer, T_EQUALS);
  if (!parse_variables_const (lexer, dict, &src_vars, &n_srcs,
                              PV_NO_DUPLICATE | PV_NO_SCRATCH))
    goto error;
  lex_match (lexer, T_SLASH);
  if (!lex_force_match_id (lexer, "INTO"))
    goto error;
  lex_match (lexer, T_EQUALS);
  if (!parse_DATA_LIST_vars (lexer, dict, &dst_names, &n_dsts,
                             PV_NO_DUPLICATE))
    goto error;
  if (n_dsts != n_srcs)
    {
      msg (SE, _("Source variable count (%zu) does not match "
                 "target variable count (%zu)."),
           n_srcs, n_dsts);

      goto error;
    }
  for (size_t i = 0; i < n_dsts; i++)
    {
      const char *name = dst_names[i];

      if (dict_lookup_var (dict, name) != NULL)
        {
          msg (SE, _("Target variable %s duplicates existing variable %s."),
               name, name);
          goto error;
        }
    }

  /* Parse options. */
  bool group = false;
  while (lex_match (lexer, T_SLASH))
    {
      if (lex_match_id (lexer, "DESCENDING"))
        direction = DESCENDING;
      else if (lex_match_id (lexer, "PRINT"))
        print = true;
      else if (lex_match_id (lexer, "GROUP"))
        group = true;
      else if (lex_match_id (lexer, "BLANK"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "VALID"))
            {
              arc->blank_valid = true;
            }
          else if (lex_match_id (lexer, "MISSING"))
            {
              arc->blank_valid = false;
            }
          else
            {
              lex_error_expecting (lexer, "VALID", "MISSING");
              goto error;
            }
        }
      else
        {
          lex_error_expecting (lexer, "DESCENDING", "PRINT", "GROUP", "BLANK");
          goto error;
        }
    }

  if (lex_token (lexer) != T_ENDCMD)
    {
      lex_error (lexer, _("expecting end of command"));
      goto error;
    }

  /* If GROUP is specified, verify that the variables are all string or all
     numeric.  */
  if (group)
    {
      enum val_type type = var_get_type (src_vars[0]);
      for (size_t i = 1; i < n_dsts; i++)
        {
          if (var_get_type (src_vars[i]) != type)
            {
              size_t string_idx = type == VAL_STRING ? 0 : i;
              size_t numeric_idx = type == VAL_STRING ? i : 0;
              lex_error (lexer, _("With GROUP, variables may not mix string "
                                  "variables (such as %s) and numeric "
                                  "variables (such as %s)."),
                         var_get_name (src_vars[string_idx]),
                         var_get_name (src_vars[numeric_idx]));
              goto error;
            }
        }
    }

  /* Allocate all the specs and the rec_items that they point to.

     If GROUP is specified, there is only a single global rec_items, with the
     maximum width 'width', and all of the specs point to it; otherwise each
     spec has its own rec_items. */
  arc->specs = xmalloc (n_dsts * sizeof *arc->specs);
  arc->n_specs = n_dsts;
  for (size_t i = 0; i < n_dsts; i++)
    {
      struct arc_spec *spec = &arc->specs[i];

      spec->width = var_get_width (src_vars[i]);
      spec->src_idx = var_get_case_index (src_vars[i]);
      spec->src_name = xstrdup (var_get_name (src_vars[i]));
      spec->format = *var_get_print_format (src_vars[i]);

      const char *label = var_get_label (src_vars[i]);
      spec->label = xstrdup_if_nonnull (label);

      if (group && i > 0)
        spec->items = arc->specs[0].items;
      else
        {
          spec->items = xzalloc (sizeof (*spec->items));
          hmap_init (&spec->items->ht);
        }
    }

  /* Initialize specs[*]->mv to the user-missing values for each
     source variable. */
  if (group)
    {
      /* Use the first source variable that has any user-missing values. */
      size_t mv_idx = 0;
      for (size_t i = 0; i < n_dsts; i++)
        if (var_has_missing_values (src_vars[i]))
          {
            mv_idx = i;
            break;
          }

      for (size_t i = 0; i < n_dsts; i++)
        mv_copy (&arc->specs[i].mv, var_get_missing_values (src_vars[mv_idx]));
    }
  else
    {
      /* Each variable uses its own user-missing values. */
      for (size_t i = 0; i < n_dsts; i++)
        mv_copy (&arc->specs[i].mv, var_get_missing_values (src_vars[i]));
    }

  /* Execute procedure. */
  struct casereader *input = proc_open (ds);
  struct ccase *c;
  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    for (size_t i = 0; i < arc->n_specs; i++)
      {
        struct arc_spec *spec = &arc->specs[i];
        const union value *value = case_data_idx (c, spec->src_idx);
        if (spec->width == 0 && value->f == SYSMIS)
          {
            /* AUTORECODE never changes the system-missing value.
               (Leaving it out of the translation table has this
               effect automatically because values not found in the
               translation table get translated to system-missing.) */
            continue;
          }

        int width = value_trim_spaces (value, spec->width);
        if (width == 1 && value->s[0] == ' ' && !arc->blank_valid)
          continue;

        size_t hash = value_hash (value, width, 0);
        if (find_arc_item (spec->items, value, width, hash))
          continue;

        struct string value_label = DS_EMPTY_INITIALIZER;
        var_append_value_name__ (src_vars[i], value,
                                 SETTINGS_VALUE_SHOW_LABEL, &value_label);

        struct arc_item *item = xmalloc (sizeof *item);
        item->width = width;
        value_clone (&item->from, value, width);
        item->missing = mv_is_value_missing_varwidth (&spec->mv, value, spec->width,
                                                      MV_ANY);
        item->value_label = ds_steal_cstr (&value_label);
        hmap_insert (&spec->items->ht, &item->hmap_node, hash);

        ds_destroy (&value_label);
      }
  bool ok = casereader_destroy (input);
  ok = proc_commit (ds) && ok;

  /* Re-fetch dictionary because it might have changed (if TEMPORARY was in
     use). */
  dict = dataset_dict (ds);

  /* Create transformation. */
  for (size_t i = 0; i < arc->n_specs; i++)
    {
      struct arc_spec *spec = &arc->specs[i];

      /* Create destination variable. */
      spec->dst = dict_create_var_assert (dict, dst_names[i], 0);
      var_set_label (spec->dst, spec->label);

      /* Set print format. */
      size_t n_items = hmap_count (&spec->items->ht);
      char *longest_value = xasprintf ("%zu", n_items);
      struct fmt_spec format = { .type = FMT_F, .w = strlen (longest_value) };
      var_set_both_formats (spec->dst, &format);
      free (longest_value);

      /* Create array of pointers to items. */
      struct arc_item **items = xmalloc (n_items * sizeof *items);
      struct arc_item *item;
      size_t j = 0;
      HMAP_FOR_EACH (item, struct arc_item, hmap_node, &spec->items->ht)
        items[j++] = item;
      assert (j == n_items);

      /* Sort array by value. */
      sort (items, n_items, sizeof *items, compare_arc_items, &direction);

      /* Assign recoded values in sorted order. */
      for (j = 0; j < n_items; j++)
        items[j]->to = j + 1;

      if (print && (!group || i == 0))
        {
          struct pivot_value *title
            = (group
               ? pivot_value_new_text (N_("Recoding grouped variables."))
               : spec->label && spec->label[0]
               ? pivot_value_new_text_format (N_("Recoding %s into %s (%s)."),
                                              spec->src_name,
                                              var_get_name (spec->dst),
                                              spec->label)
               : pivot_value_new_text_format (N_("Recoding %s into %s."),
                                              spec->src_name,
                                              var_get_name (spec->dst)));
          struct pivot_table *table = pivot_table_create__ (title, "Recoding");

          pivot_dimension_create (
            table, PIVOT_AXIS_COLUMN, N_("Attributes"),
            N_("New Value"), N_("Value Label"));

          struct pivot_dimension *old_values = pivot_dimension_create (
            table, PIVOT_AXIS_ROW, N_("Old Value"));
          old_values->root->show_label = true;

          for (size_t k = 0; k < n_items; k++)
            {
              const struct arc_item *item = items[k];
              int old_value_idx = pivot_category_create_leaf (
                old_values->root, pivot_value_new_value (
                  &item->from, item->width,
                  (item->width
                   ? &(struct fmt_spec) { FMT_F, item->width, 0 }
                   : &spec->format),
                  dict_get_encoding (dict)));
              pivot_table_put2 (table, 0, old_value_idx,
                                pivot_value_new_integer (item->to));

              const char *value_label = item->value_label;
              if (value_label && value_label[0])
                pivot_table_put2 (table, 1, old_value_idx,
                                  pivot_value_new_user_text (value_label, -1));
            }

          pivot_table_submit (table);
        }

      /* Assign user-missing values.

         User-missing values in the source variable(s) must be marked
         as user-missing values in the destination variable.  There
         might be an arbitrary number of missing values, since the
         source variable might have a range.  Our sort function always
         puts missing values together at the top of the range, so that
         means that we can use a missing value range to cover all of
         the user-missing values in any case (but we avoid it unless
         necessary because user-missing value ranges are an obscure
         feature). */
      size_t n_missing = n_items;
      for (size_t k = 0; k < n_items; k++)
        if (!items[n_items - k - 1]->missing)
          {
            n_missing = k;
            break;
          }
      if (n_missing > 0)
        {
          size_t lo = n_items - (n_missing - 1);
          size_t hi = n_items;

          struct missing_values mv;
          mv_init (&mv, 0);
          if (n_missing > 3)
            mv_add_range (&mv, lo, hi);
          else
            for (size_t k = 0; k < n_missing; k++)
              mv_add_num (&mv, lo + k);
          var_set_missing_values (spec->dst, &mv);
          mv_destroy (&mv);
        }

      /* Add value labels to the destination variable. */
      for (j = 0; j < n_items; j++)
        {
          const char *value_label = items[j]->value_label;
          if (value_label && value_label[0])
            {
              union value to_val = { .f = items[j]->to };
              var_add_value_label (spec->dst, &to_val, value_label);
            }
        }

      /* Free array. */
      free (items);
    }
  add_transformation (ds, autorecode_trns_proc, autorecode_trns_free, arc);

  for (size_t i = 0; i < n_dsts; i++)
    free (dst_names[i]);
  free (dst_names);
  free (src_vars);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

error:
  for (size_t i = 0; i < n_dsts; i++)
    free (dst_names[i]);
  free (dst_names);
  free (src_vars);
  arc_free (arc);
  return CMD_CASCADING_FAILURE;
}

static void
arc_free (struct autorecode_pgm *arc)
{
  if (arc != NULL)
    {
      for (size_t i = 0; i < arc->n_specs; i++)
        {
          struct arc_spec *spec = &arc->specs[i];
          struct arc_item *item, *next;

          HMAP_FOR_EACH_SAFE (item, next, struct arc_item, hmap_node,
                              &spec->items->ht)
            {
              value_destroy (&item->from, item->width);
              free (item->value_label);
              hmap_delete (&spec->items->ht, &item->hmap_node);
              free (item);
            }
          free (spec->label);
          free (spec->src_name);
          mv_destroy (&spec->mv);
        }

      size_t n_rec_items =
        (arc->n_specs >= 2 && arc->specs[0].items == arc->specs[1].items
         ? 1
         : arc->n_specs);

      for (size_t i = 0; i < n_rec_items; i++)
        {
          struct arc_spec *spec = &arc->specs[i];
          hmap_destroy (&spec->items->ht);
          free (spec->items);
        }

      free (arc->specs);
      free (arc);
    }
}

static struct arc_item *
find_arc_item (const struct rec_items *items,
               const union value *value, int width,
               size_t hash)
{
  struct arc_item *item;

  HMAP_FOR_EACH_WITH_HASH (item, struct arc_item, hmap_node, hash, &items->ht)
    if (item->width == width && value_equal (value, &item->from, width))
      return item;
  return NULL;
}

static int
compare_arc_items (const void *a_, const void *b_, const void *direction_)
{
  const struct arc_item *const *ap = a_;
  const struct arc_item *const *bp = b_;
  const struct arc_item *a = *ap;
  const struct arc_item *b = *bp;

  /* User-missing values always sort to the highest target values
     (regardless of sort direction). */
  if (a->missing != b->missing)
    return a->missing < b->missing ? -1 : 1;

  /* Otherwise, compare the data. */
  int aw = a->width;
  int bw = b->width;
  int cmp;
  if (aw == bw)
    cmp = value_compare_3way (&a->from, &b->from, aw);
  else
    {
      assert (aw && bw);
      cmp = buf_compare_rpad (CHAR_CAST_BUG (const char *, a->from.s), aw,
                              CHAR_CAST_BUG (const char *, b->from.s), bw);
    }

  /* Then apply sort direction. */
  const enum arc_direction *directionp = direction_;
  enum arc_direction direction = *directionp;
  return direction == ASCENDING ? cmp : -cmp;
}

static int
autorecode_trns_proc (void *arc_, struct ccase **c,
                      casenumber case_idx UNUSED)
{
  struct autorecode_pgm *arc = arc_;

  *c = case_unshare (*c);
  for (size_t i = 0; i < arc->n_specs; i++)
    {
      const struct arc_spec *spec = &arc->specs[i];
      const union value *value = case_data_idx (*c, spec->src_idx);
      int width = value_trim_spaces (value, spec->width);
      size_t hash = value_hash (value, width, 0);
      const struct arc_item *item = find_arc_item (spec->items, value, width,
                                                   hash);
      case_data_rw (*c, spec->dst)->f = item ? item->to : SYSMIS;
    }

  return TRNS_CONTINUE;
}

static bool
autorecode_trns_free (void *arc_)
{
  struct autorecode_pgm *arc = arc_;

  arc_free (arc);
  return true;
}
