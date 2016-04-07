/* PSPP - a program for statistical analysis.
   Copyright (C) 2016 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/attributes.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum key
  {
    K_NAME,
    K_TYPE,
    K_FORMAT,
    K_VAR_LABEL,
    K_VALUE_LABELS,
    K_MISSING_VALUES,
    K_MEASURE,
    K_ROLE,
    K_COLUMNS,
    K_ALIGNMENT,
    K_ATTRIBUTE,
  };

struct criterion
  {
    enum key key;
    char *attr_name;
    bool descending;
  };

static int
compare_ints (int a, int b)
{
  return a < b ? -1 : a > b;
}

static int
compare_formats (const struct fmt_spec *a, const struct fmt_spec *b)
{
  int retval = compare_ints (fmt_to_io (a->type), fmt_to_io (b->type));
  if (!retval)
    retval = compare_ints (a->w, b->w);
  if (!retval)
    retval = compare_ints (a->d, b->d);
  return retval;
}

static int
compare_var_labels (const struct variable *a, const struct variable *b)
{
  const char *a_label = var_get_label (a);
  const char *b_label = var_get_label (b);
  return utf8_strcasecmp (a_label ? a_label : "",
                          b_label ? b_label : "");
}

static int
map_measure (enum measure m)
{
  return (m == MEASURE_NOMINAL ? 0
          : m == MEASURE_ORDINAL ? 1
          : 2);
}

static int
map_role (enum var_role r)
{
  return (r == ROLE_INPUT ? 0
          : r == ROLE_TARGET ? 1
          : r == ROLE_BOTH ? 2
          : r == ROLE_NONE ? 3
          : r == ROLE_PARTITION ? 4
          : 5);
}

static const char *
get_attribute (const struct variable *v, const char *name)
{
  const struct attrset *set = var_get_attributes (v);
  const struct attribute *attr = attrset_lookup (set, name);
  const char *value = attr ? attribute_get_value (attr, 0) : NULL;
  return value ? value : "";
}

static int
map_alignment (enum alignment a)
{
  return (a == ALIGN_LEFT ? 0
          : a == ALIGN_RIGHT ? 1
          : 2);
}

static int
compare_vars (const void *a_, const void *b_, const void *c_)
{
  const struct variable *const *ap = a_;
  const struct variable *const *bp = b_;
  const struct variable *a = *ap;
  const struct variable *b = *bp;
  const struct criterion *c = c_;

  int retval;
  switch (c->key)
    {
    case K_NAME:
      retval = utf8_strverscasecmp (var_get_name (a), var_get_name (b));
      break;

    case K_TYPE:
      retval = compare_ints (var_get_width (a), var_get_width (b));
      break;

    case K_FORMAT:
      retval = compare_formats (var_get_print_format (a),
                                var_get_print_format (b));
      break;

    case K_VAR_LABEL:
      retval = compare_var_labels (a, b);
      break;

    case K_VALUE_LABELS:
      retval = compare_ints (var_has_value_labels (a),
                             var_has_value_labels (b));
      break;

    case K_MISSING_VALUES:
      retval = compare_ints (var_has_missing_values (a),
                             var_has_missing_values (b));
      break;

    case K_MEASURE:
      retval = compare_ints (map_measure (var_get_measure (a)),
                             map_measure (var_get_measure (b)));
      break;

    case K_ROLE:
      retval = compare_ints (map_role (var_get_role (a)),
                             map_role (var_get_role (b)));
      break;

    case K_COLUMNS:
      retval = compare_ints (var_get_display_width (a),
                             var_get_display_width (b));
      break;

    case K_ALIGNMENT:
      retval = compare_ints (map_alignment (var_get_alignment (a)),
                             map_alignment (var_get_alignment (b)));
      break;

    case K_ATTRIBUTE:
      retval = utf8_strcasecmp (get_attribute (a, c->attr_name),
                                get_attribute (b, c->attr_name));
      break;

    default:
      NOT_REACHED ();
    }

  /* Make this a stable sort. */
  if (!retval)
    retval = a < b ? -1 : a > b;

  if (c->descending)
    retval = -retval;

  return retval;
}

/* Performs SORT VARIABLES command. */
int
cmd_sort_variables (struct lexer *lexer, struct dataset *ds)
{
  enum cmd_result result = CMD_FAILURE;

  lex_match (lexer, T_BY);

  /* Parse sort key. */
  struct criterion c = { .attr_name = NULL };
  if (lex_match_id (lexer, "NAME"))
    c.key = K_NAME;
  else if (lex_match_id (lexer, "TYPE"))
    c.key = K_TYPE;
  else if (lex_match_id (lexer, "FORMAT"))
    c.key = K_FORMAT;
  else if (lex_match_id (lexer, "LABEL"))
    c.key = K_VAR_LABEL;
  else if (lex_match_id (lexer, "VALUES"))
    c.key = K_VALUE_LABELS;
  else if (lex_match_id (lexer, "MISSING"))
    c.key = K_MISSING_VALUES;
  else if (lex_match_id (lexer, "MEASURE"))
    c.key = K_MEASURE;
  else if (lex_match_id (lexer, "ROLE"))
    c.key = K_ROLE;
  else if (lex_match_id (lexer, "COLUMNS"))
    c.key = K_COLUMNS;
  else if (lex_match_id (lexer, "ALIGNMENT"))
    c.key = K_ALIGNMENT;
  else if (lex_match_id (lexer, "ATTRIBUTE"))
    {
      if (!lex_force_id (lexer))
        goto exit;
      c.key = K_ATTRIBUTE;
      c.attr_name = xstrdup (lex_tokcstr (lexer));
      lex_get (lexer);
    }

  /* Parse sort direction. */
  if (lex_match (lexer, T_LPAREN))
    {
      if (lex_match_id (lexer, "A") || lex_match_id (lexer, "UP"))
        c.descending = false;
      else if (lex_match_id (lexer, "D") || lex_match_id (lexer, "DOWN"))
        c.descending = true;
      else
        {
          lex_error (lexer, NULL);
          goto exit;
        }
      if (!lex_force_match (lexer, T_RPAREN))
        goto exit;
    }
  else
    c.descending = false;

  /* Sort variables. */
  struct dictionary *d = dataset_dict (ds);
  struct variable **vars;
  size_t n_vars;
  dict_get_vars_mutable (d, &vars, &n_vars, 0);
  sort (vars, n_vars, sizeof *vars, compare_vars, &c);
  dict_reorder_vars (d, CONST_CAST (struct variable *const *, vars), n_vars);
  free (vars);

  result = CMD_SUCCESS;

exit:
  free (c.attr_name);
  return result;
}
