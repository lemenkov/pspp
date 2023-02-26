/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/mrset.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/stringi-map.h"
#include "libpspp/stringi-set.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

static bool parse_group (struct lexer *, struct dictionary *, enum mrset_type);
static bool parse_delete (struct lexer *, struct dictionary *);
static bool parse_display (struct lexer *, struct dictionary *);

int
cmd_mrsets (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  while (lex_match (lexer, T_SLASH))
    {
      bool ok;

      if (lex_match_id (lexer, "MDGROUP"))
        ok = parse_group (lexer, dict, MRSET_MD);
      else if (lex_match_id (lexer, "MCGROUP"))
        ok = parse_group (lexer, dict, MRSET_MC);
      else if (lex_match_id (lexer, "DELETE"))
        ok = parse_delete (lexer, dict);
      else if (lex_match_id (lexer, "DISPLAY"))
        ok = parse_display (lexer, dict);
      else
        {
          ok = false;
          lex_error_expecting (lexer, "MDGROUP", "MCGROUP",
                               "DELETE", "DISPLAY");
        }

      if (!ok)
        return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

static bool
parse_group (struct lexer *lexer, struct dictionary *dict,
             enum mrset_type type)
{
  const char *subcommand_name = type == MRSET_MD ? "MDGROUP" : "MCGROUP";

  struct mrset *mrset = XZALLOC (struct mrset);
  mrset->type = type;
  mrset->cat_source = MRSET_VARLABELS;

  bool labelsource_varlabel = false;
  bool has_value = false;

  int vars_start = 0;
  int vars_end = 0;
  int value_ofs = 0;
  int labelsource_start = 0;
  int labelsource_end = 0;
  int label_start = 0;
  int label_end = 0;
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "NAME"))
        {
          if (!lex_force_match (lexer, T_EQUALS) || !lex_force_id (lexer))
            goto error;
          char *error = mrset_is_valid_name__ (lex_tokcstr (lexer),
                                               dict_get_encoding (dict));
          if (error)
            {
              lex_error (lexer, "%s", error);
              free (error);
              goto error;
            }

          free (mrset->name);
          mrset->name = xstrdup (lex_tokcstr (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "VARIABLES"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          free (mrset->vars);
          vars_start = lex_ofs (lexer);
          if (!parse_variables (lexer, dict, &mrset->vars, &mrset->n_vars,
                                PV_SAME_TYPE | PV_NO_SCRATCH))
            goto error;
          vars_end = lex_ofs (lexer) - 1;

          if (mrset->n_vars < 2)
            {
              lex_ofs_error (lexer, vars_start, vars_end,
                             _("At least two variables are required."));
              goto error;
            }
        }
      else if (lex_match_id (lexer, "LABEL"))
        {
          label_start = lex_ofs (lexer) - 1;
          if (!lex_force_match (lexer, T_EQUALS) || !lex_force_string (lexer))
            goto error;
          label_end = lex_ofs (lexer);

          free (mrset->label);
          mrset->label = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "LABELSOURCE"))
        {
          if (!lex_force_match_phrase (lexer, "=VARLABEL"))
            goto error;

          labelsource_varlabel = true;
          labelsource_start = lex_ofs (lexer) - 3;
          labelsource_end = lex_ofs (lexer) - 1;
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "VALUE"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          has_value = true;
          value_ofs = lex_ofs (lexer);
          if (lex_is_number (lexer))
            {
              if (!lex_is_integer (lexer))
                {
                  lex_error (lexer, _("Numeric VALUE must be an integer."));
                  goto error;
                }
              value_destroy (&mrset->counted, mrset->width);
              mrset->counted.f = lex_integer (lexer);
              mrset->width = 0;
            }
          else if (lex_is_string (lexer))
            {
              size_t width;
              char *s;

              s = recode_string (dict_get_encoding (dict), "UTF-8",
                                 lex_tokcstr (lexer), -1);
              width = strlen (s);

              /* Trim off trailing spaces, but don't trim the string until
                 it's empty because a width of 0 is a numeric type. */
              while (width > 1 && s[width - 1] == ' ')
                width--;

              value_destroy (&mrset->counted, mrset->width);
              value_init (&mrset->counted, width);
              memcpy (mrset->counted.s, s, width);
              mrset->width = width;

              free (s);
            }
          else
            {
              lex_error (lexer, _("Syntax error expecting integer or string."));
              goto error;
            }
          lex_get (lexer);
        }
      else if (type == MRSET_MD && lex_match_id (lexer, "CATEGORYLABELS"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;

          if (lex_match_id (lexer, "VARLABELS"))
            mrset->cat_source = MRSET_VARLABELS;
          else if (lex_match_id (lexer, "COUNTEDVALUES"))
            mrset->cat_source = MRSET_COUNTEDVALUES;
          else
            {
              lex_error_expecting (lexer, "VARLABELS", "COUNTEDVALUES");
              goto error;
            }
        }
      else
        {
          if (type == MRSET_MD)
            lex_error_expecting (lexer, "NAME", "VARIABLES", "LABEL",
                                 "LABELSOURCE", "VALUE", "CATEGORYLABELS");
          else
            lex_error_expecting (lexer, "NAME", "VARIABLES", "LABEL");
          goto error;
        }
    }

  if (mrset->name == NULL)
    {
      lex_spec_missing (lexer, subcommand_name, "NAME");
      goto error;
    }
  else if (mrset->n_vars == 0)
    {
      lex_spec_missing (lexer, subcommand_name, "VARIABLES");
      goto error;
    }

  if (type == MRSET_MD)
    {
      /* Check that VALUE is specified and is valid for the VARIABLES. */
      if (!has_value)
        {
          lex_spec_missing (lexer, subcommand_name, "VALUE");
          goto error;
        }

      if (var_is_alpha (mrset->vars[0]) != (mrset->width > 0))
        {
          msg (SE, _("VARIABLES and VALUE must have the same type."));
          if (var_is_alpha (mrset->vars[0]))
            lex_ofs_msg (lexer, SN, vars_start, vars_end,
                         _("These are string variables."));
          else
            lex_ofs_msg (lexer, SN, vars_start, vars_end,
                         _("These are numeric variables."));
          if (mrset->width > 0)
            lex_ofs_msg (lexer, SN, value_ofs, value_ofs,
                         _("This is a string value."));
          else
            lex_ofs_msg (lexer, SN, value_ofs, value_ofs,
                         _("This is a numeric value."));
          goto error;
        }
      if (var_is_alpha (mrset->vars[0]))
        {
          const struct variable *shortest_var = NULL;
          int min_width = INT_MAX;

          for (size_t i = 0; i < mrset->n_vars; i++)
            {
              int width = var_get_width (mrset->vars[i]);
              if (width < min_width)
                {
                  shortest_var = mrset->vars[i];
                  min_width = width;
                }
            }
          if (mrset->width > min_width)
            {
              msg (SE, _("The VALUE string must be no longer than the "
                         "narrowest variable in the group."));
              lex_ofs_msg (lexer, SN, value_ofs, value_ofs,
                           _("The VALUE string is %d bytes long."),
                           mrset->width);
              lex_ofs_msg (lexer, SN, vars_start, vars_end,
                           _("Variable %s has a width of %d bytes."),
                           var_get_name (shortest_var), min_width);
              goto error;
            }
        }

      /* Implement LABELSOURCE=VARLABEL. */
      if (labelsource_varlabel)
        {
          if (mrset->cat_source != MRSET_COUNTEDVALUES)
            lex_ofs_msg (lexer, SW, labelsource_start, labelsource_end,
                         _("MDGROUP subcommand for group %s specifies "
                           "LABELSOURCE=VARLABEL but not "
                           "CATEGORYLABELS=COUNTEDVALUES.  "
                           "Ignoring LABELSOURCE."),
                 mrset->name);
          else if (mrset->label)
            {
              msg (SW, _("MDGROUP subcommand for group %s specifies both "
                         "LABEL and LABELSOURCE, but only one of these "
                         "subcommands may be used at a time.  "
                         "Ignoring LABELSOURCE."),
                   mrset->name);
              lex_ofs_msg (lexer, SN, label_start, label_end,
                           _("Here is the %s setting."), "LABEL");
              lex_ofs_msg (lexer, SN, labelsource_start, labelsource_end,
                           _("Here is the %s setting."), "LABELSOURCE");
            }
          else
            {
              mrset->label_from_var_label = true;
              for (size_t i = 0; mrset->label == NULL && i < mrset->n_vars; i++)
                {
                  const char *label = var_get_label (mrset->vars[i]);
                  if (label != NULL)
                    {
                      mrset->label = xstrdup (label);
                      break;
                    }
                }
            }
        }

      /* Warn if categories cannot be distinguished in output. */
      if (mrset->cat_source == MRSET_VARLABELS)
        {
          struct stringi_map seen;
          size_t i;

          stringi_map_init (&seen);
          for (i = 0; i < mrset->n_vars; i++)
            {
              const struct variable *var = mrset->vars[i];
              const char *name = var_get_name (var);
              const char *label = var_get_label (var);
              if (label != NULL)
                {
                  const char *other_name = stringi_map_find (&seen, label);

                  if (other_name == NULL)
                    stringi_map_insert (&seen, label, name);
                  else
                    lex_ofs_msg (lexer, SW, vars_start, vars_end,
                                 _("Variables %s and %s specified as part of "
                                   "multiple dichotomy group %s have the same "
                                   "variable label.  Categories represented by "
                                   "these variables will not be distinguishable "
                                   "in output."),
                                 other_name, name, mrset->name);
                }
            }
          stringi_map_destroy (&seen);
        }
      else
        {
          struct stringi_map seen = STRINGI_MAP_INITIALIZER (seen);
          for (size_t i = 0; i < mrset->n_vars; i++)
            {
              const struct variable *var = mrset->vars[i];
              const char *name = var_get_name (var);

              union value value;
              value_clone (&value, &mrset->counted, mrset->width);
              value_resize (&value, mrset->width, var_get_width (var));

              const struct val_labs *val_labs = var_get_value_labels (var);
              const char *label = val_labs_find (val_labs, &value);
              if (label == NULL)
                lex_ofs_msg (lexer, SW, vars_start, vars_end,
                             _("Variable %s specified as part of multiple "
                               "dichotomy group %s (which has "
                               "CATEGORYLABELS=COUNTEDVALUES) has no value "
                               "label for its counted value.  This category "
                               "will not be distinguishable in output."),
                     name, mrset->name);
              else
                {
                  const char *other_name = stringi_map_find (&seen, label);

                  if (other_name == NULL)
                    stringi_map_insert (&seen, label, name);
                  else
                    lex_ofs_msg (lexer, SW, vars_start, vars_end,
                                 _("Variables %s and %s specified as part of "
                                   "multiple dichotomy group %s (which has "
                                   "CATEGORYLABELS=COUNTEDVALUES) have the same "
                                   "value label for the group's counted "
                                   "value.  These categories will not be "
                                   "distinguishable in output."),
                                 other_name, name, mrset->name);
                }

              value_destroy (&value, var_get_width (var));
            }
          stringi_map_destroy (&seen);
        }
    }
  else                          /* MCGROUP. */
    {
      /* Warn if categories cannot be distinguished in output. */
      struct category
        {
          struct hmap_node hmap_node;
          union value value;
          int width;
          const char *label;
          const char *var_name;
          bool warned;
        };

      struct hmap categories = HMAP_INITIALIZER (categories);
      for (size_t i = 0; i < mrset->n_vars; i++)
        {
          const struct variable *var = mrset->vars[i];
          const char *name = var_get_name (var);
          int width = var_get_width (var);
          const struct val_labs *val_labs = var_get_value_labels (var);

          const struct val_lab *vl;
          for (vl = val_labs_first (val_labs); vl != NULL;
               vl = val_labs_next (val_labs, vl))
            {
              const union value *value = val_lab_get_value (vl);
              const char *label = val_lab_get_label (vl);
              unsigned int hash = value_hash (value, width, 0);

              struct category *c;
              HMAP_FOR_EACH_WITH_HASH (c, struct category, hmap_node,
                                       hash, &categories)
                {
                  if (width == c->width
                      && value_equal (value, &c->value, width))
                    {
                      if (!c->warned && utf8_strcasecmp (c->label, label))
                        {
                          char *s = data_out (value, var_get_encoding (var),
                                              var_get_print_format (var),
                                              settings_get_fmt_settings ());
                          c->warned = true;
                          lex_ofs_msg (lexer, SW, vars_start, vars_end,
                                       _("Variables specified on MCGROUP should "
                                         "have the same categories, but %s and "
                                         "%s (and possibly others) in multiple "
                                         "category group %s have different "
                                         "value labels for value %s."),
                                       c->var_name, name, mrset->name, s);
                          free (s);
                        }
                      goto found;
                    }
                }

              c = xmalloc (sizeof *c);
              *c = (struct category) {
                .width = width,
                .label = label,
                .var_name = name,
                .warned = false,
              };
              value_clone (&c->value, value, width);
              hmap_insert (&categories, &c->hmap_node, hash);

            found: ;
            }
        }

      struct category *c, *next;
      HMAP_FOR_EACH_SAFE (c, next, struct category, hmap_node, &categories)
        {
          value_destroy (&c->value, c->width);
          hmap_delete (&categories, &c->hmap_node);
          free (c);
        }
      hmap_destroy (&categories);
    }

  dict_add_mrset (dict, mrset);
  return true;

error:
  mrset_destroy (mrset);
  return false;
}

static bool
parse_mrset_names (struct lexer *lexer, struct dictionary *dict,
                   struct stringi_set *mrset_names)
{
  if (!lex_force_match_phrase (lexer, "NAME="))
    return false;

  stringi_set_init (mrset_names);
  if (lex_match (lexer, T_LBRACK))
    {
      while (!lex_match (lexer, T_RBRACK))
        {
          if (!lex_force_id (lexer))
            return false;
          if (dict_lookup_mrset (dict, lex_tokcstr (lexer)) == NULL)
            {
              lex_error (lexer, _("No multiple response set named %s."),
                         lex_tokcstr (lexer));
              stringi_set_destroy (mrset_names);
              return false;
            }
          stringi_set_insert (mrset_names, lex_tokcstr (lexer));
          lex_get (lexer);
        }
    }
  else if (lex_match (lexer, T_ALL))
    {
      size_t n_sets = dict_get_n_mrsets (dict);
      size_t i;

      for (i = 0; i < n_sets; i++)
        stringi_set_insert (mrset_names, dict_get_mrset (dict, i)->name);
    }
  else
    {
      lex_error_expecting (lexer, "`['", "ALL");
      return false;
    }

  return true;
}

static bool
parse_delete (struct lexer *lexer, struct dictionary *dict)
{
  struct stringi_set mrset_names;
  const char *name;
  if (!parse_mrset_names (lexer, dict, &mrset_names))
    return false;

  const struct stringi_set_node *node;
  STRINGI_SET_FOR_EACH (name, node, &mrset_names)
    dict_delete_mrset (dict, name);
  stringi_set_destroy (&mrset_names);

  return true;
}

static bool
parse_display (struct lexer *lexer, struct dictionary *dict)
{
  struct stringi_set mrset_names_set;
  if (!parse_mrset_names (lexer, dict, &mrset_names_set))
    return false;

  size_t n = stringi_set_count (&mrset_names_set);
  if (n == 0)
    {
      if (dict_get_n_mrsets (dict) == 0)
        lex_next_msg (lexer, SN, -1, -1,
                      _("The active dataset dictionary does not contain any "
                        "multiple response sets."));
      stringi_set_destroy (&mrset_names_set);
      return true;
    }

  struct pivot_table *table = pivot_table_create (
    N_("Multiple Response Sets"));

  pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Attributes"),
    N_("Label"), N_("Encoding"), N_("Counted Value"), N_("Member Variables"));

  struct pivot_dimension *mrsets = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Name"));
  mrsets->root->show_label = true;

  char **mrset_names = stringi_set_get_sorted_array (&mrset_names_set);
  for (size_t i = 0; i < n; i++)
    {
      const struct mrset *mrset = dict_lookup_mrset (dict, mrset_names[i]);

      int row = pivot_category_create_leaf (
        mrsets->root, pivot_value_new_user_text (mrset->name, -1));

      if (mrset->label != NULL)
        pivot_table_put2 (table, 0, row,
                          pivot_value_new_user_text (mrset->label, -1));

      pivot_table_put2 (table, 1, row,
                        pivot_value_new_text (mrset->type == MRSET_MD
                                              ? _("Dichotomies")
                                              : _("Categories")));

      if (mrset->type == MRSET_MD)
        pivot_table_put2 (table, 2, row,
                          pivot_value_new_value (
                            &mrset->counted, mrset->width,
                            F_8_0, dict_get_encoding (dict)));

      /* Variable names. */
      struct string var_names = DS_EMPTY_INITIALIZER;
      for (size_t j = 0; j < mrset->n_vars; j++)
        ds_put_format (&var_names, "%s\n", var_get_name (mrset->vars[j]));
      ds_chomp_byte (&var_names, '\n');
      pivot_table_put2 (table, 3, row,
                        pivot_value_new_user_text_nocopy (
                          ds_steal_cstr (&var_names)));
    }
  free (mrset_names);
  stringi_set_destroy (&mrset_names_set);

  pivot_table_submit (table);

  return true;
}
