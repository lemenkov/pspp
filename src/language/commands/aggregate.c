/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2008, 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "language/commands/aggregate.h"

#include <stdlib.h>

#include "data/any-writer.h"
#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/subcase.h"
#include "data/sys-file-writer.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/file-handle.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/commands/sort-criteria.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "math/moments.h"
#include "math/percentiles.h"
#include "math/sort.h"
#include "math/statistic.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Argument for AGGREGATE function.

   Only one of the members is used, so this could be a union, but it's simpler
   to just have both. */
struct agr_argument
  {
    double f;                           /* Numeric. */
    struct substring s;                 /* String. */
  };

/* Specifies how to make an aggregate variable. */
struct agr_var
  {
    /* Collected during parsing. */
    const struct variable *src;        /* Source variable. */
    struct variable *dest;        /* Target variable. */
    enum agr_function function; /* Function. */
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    struct agr_argument arg[2];        /* Arguments. */

    /* Accumulated during AGGREGATE execution. */
    double dbl;
    double W;                   /* Total non-missing weight. */
    int int1;
    char *string;
    bool saw_missing;
    struct moments1 *moments;

    struct dictionary *dict;
    struct variable *subject;
    struct variable *weight;
    struct casewriter *writer;
  };

/* Attributes of aggregation functions. */
const struct agr_func agr_func_tab[] =
  {
#define AGRF(ENUM, NAME, DESCRIPTION, SRC_VARS, N_ARGS, ALPHA_TYPE, W, D) \
    [ENUM] = { NAME, DESCRIPTION, SRC_VARS, N_ARGS, ALPHA_TYPE, \
               { .type = (W) > 0 ? FMT_F : -1, .w = W, .d = D } },
AGGREGATE_FUNCTIONS
#undef AGRF
    {NULL, NULL, AGR_SV_NO, 0, -1, {-1, -1, -1}},
  };

/* Missing value types. */
enum missing_treatment
  {
    ITEMWISE,                /* Missing values item by item. */
    COLUMNWISE                /* Missing values column by column. */
  };

/* An entire AGGREGATE procedure. */
struct agr_proc
  {
    /* Break variables. */
    struct subcase sort;                /* Sort criteria (break variables). */
    const struct variable **break_vars;       /* Break variables. */
    size_t break_n_vars;                /* Number of break variables. */

    enum missing_treatment missing;     /* How to treat missing values. */
    struct agr_var *agr_vars;           /* Aggregate variables. */
    size_t n_agr_vars;
    struct dictionary *dict;            /* Aggregate dictionary. */
    const struct dictionary *src_dict;  /* Dict of the source */
    int n_cases;                        /* Counts aggregated cases. */

    bool add_variables;                 /* True iff the aggregated variables should
                                           be appended to the existing dictionary */
  };

static void initialize_aggregate_info (struct agr_proc *);

static void accumulate_aggregate_info (struct agr_proc *,
                                       const struct ccase *);
/* Prototypes. */
static bool parse_aggregate_functions (struct lexer *, const struct dictionary *,
                                       struct agr_proc *);
static void agr_destroy (struct agr_proc *);
static void dump_aggregate_info (const struct agr_proc *agr,
                                 struct casewriter *output,
                                 const struct ccase *break_case);

/* Parsing. */

/* Parses and executes the AGGREGATE procedure. */
int
cmd_aggregate (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct agr_proc agr = {
    .missing = ITEMWISE,
    .src_dict = dict,
  };
  struct file_handle *out_file = NULL;
  struct casereader *input = NULL;
  struct casewriter *output = NULL;

  bool copy_documents = false;
  bool presorted = false;
  int addvariables_ofs = 0;

  /* OUTFILE subcommand must be first. */
  if (lex_match_phrase (lexer, "/OUTFILE") || lex_match_id (lexer, "OUTFILE"))
    {
      lex_match (lexer, T_EQUALS);
      if (!lex_match (lexer, T_ASTERISK))
        {
          out_file = fh_parse (lexer, FH_REF_FILE, dataset_session (ds));
          if (out_file == NULL)
            goto error;
        }

      if (!out_file && lex_match_id (lexer, "MODE"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "ADDVARIABLES"))
            {
              addvariables_ofs = lex_ofs (lexer) - 1;
              agr.add_variables = true;
              presorted = true;
            }
          else if (lex_match_id (lexer, "REPLACE"))
            agr.add_variables = false;
          else
            {
              lex_error_expecting (lexer, "ADDVARIABLES", "REPLACE");
              goto error;
            }
        }
    }
  else
    {
      agr.add_variables = true;
      presorted = true;
    }

  if (lex_match_phrase (lexer, "/MISSING"))
    {
      lex_match (lexer, T_EQUALS);
      if (!lex_match_id (lexer, "COLUMNWISE"))
        {
          lex_error_expecting (lexer, "COLUMNWISE");
          goto error;
        }
      agr.missing = COLUMNWISE;
    }

  int presorted_ofs = 0;
  for (;;)
    if (lex_match_phrase (lexer, "/DOCUMENT"))
      copy_documents = true;
    else if (lex_match_phrase (lexer, "/PRESORTED"))
      {
        presorted = true;
        presorted_ofs = lex_ofs (lexer) - 1;
      }
    else
      break;

  if (agr.add_variables)
    agr.dict = dict_clone (dict);
  else
    agr.dict = dict_create (dict_get_encoding (dict));

  dict_set_label (agr.dict, dict_get_label (dict));
  dict_set_documents (agr.dict, dict_get_documents (dict));

  if (lex_match_phrase (lexer, "/BREAK"))
    {
      lex_match (lexer, T_EQUALS);
      bool saw_direction;
      int break_start = lex_ofs (lexer);
      if (!parse_sort_criteria (lexer, dict, &agr.sort, &agr.break_vars,
                                &saw_direction))
        goto error;
      int break_end = lex_ofs (lexer) - 1;
      agr.break_n_vars = subcase_get_n_fields (&agr.sort);

      if  (! agr.add_variables)
        for (size_t i = 0; i < agr.break_n_vars; i++)
          dict_clone_var_assert (agr.dict, agr.break_vars[i]);

      if (presorted && saw_direction)
        {
          lex_ofs_msg (lexer, SW, break_start, break_end,
                       _("When the input data is presorted, specifying "
                         "sorting directions with (A) or (D) has no effect.  "
                         "Output data will be sorted the same way as the "
                         "input data."));
          if (presorted_ofs)
            lex_ofs_msg (lexer, SN, presorted_ofs, presorted_ofs,
                         _("The PRESORTED subcommand states that the "
                           "input data is presorted."));
          else if (addvariables_ofs)
            lex_ofs_msg (lexer, SN, addvariables_ofs, addvariables_ofs,
                         _("ADDVARIABLES implies that the input data "
                           "is presorted."));
          else
            msg (SN, _("The input data must be presorted because the "
                       "OUTFILE subcommand is not specified."));
        }
    }

  /* Read in the aggregate functions. */
  if (!parse_aggregate_functions (lexer, dict, &agr))
    goto error;

  /* Delete documents. */
  if (!copy_documents)
    dict_clear_documents (agr.dict);

  /* Cancel SPLIT FILE. */
  dict_clear_split_vars (agr.dict);

  /* Initialize. */
  agr.n_cases = 0;

  if (out_file == NULL)
    {
      /* The active dataset will be replaced by the aggregated data,
         so TEMPORARY is moot. */
      proc_make_temporary_transformations_permanent (ds);
      proc_discard_output (ds);
      output = autopaging_writer_create (dict_get_proto (agr.dict));
    }
  else
    {
      output = any_writer_open (out_file, agr.dict);
      if (output == NULL)
        goto error;
    }

  input = proc_open (ds);
  if (!subcase_is_empty (&agr.sort) && !presorted)
    {
      input = sort_execute (input, &agr.sort);
      subcase_clear (&agr.sort);
    }

  struct casegrouper *grouper;
  struct casereader *group;
  for (grouper = casegrouper_create_vars (input, agr.break_vars,
                                          agr.break_n_vars);
       casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      struct casereader *placeholder = NULL;
      struct ccase *c = casereader_peek (group, 0);

      if (c == NULL)
        {
          casereader_destroy (group);
          continue;
        }

      initialize_aggregate_info (&agr);

      if (agr.add_variables)
        placeholder = casereader_clone (group);

      {
        struct ccase *cg;
        for (; (cg = casereader_read (group)) != NULL; case_unref (cg))
          accumulate_aggregate_info (&agr, cg);
      }


      if  (agr.add_variables)
        {
          struct ccase *cg;
          for (; (cg = casereader_read (placeholder)) != NULL; case_unref (cg))
            dump_aggregate_info (&agr, output, cg);

          casereader_destroy (placeholder);
        }
      else
        {
          dump_aggregate_info (&agr, output, c);
        }
      case_unref (c);
    }
  if (!casegrouper_destroy (grouper))
    goto error;

  bool ok = proc_commit (ds);
  input = NULL;
  if (!ok)
    goto error;

  if (out_file == NULL)
    {
      struct casereader *next_input = casewriter_make_reader (output);
      if (next_input == NULL)
        goto error;

      dataset_set_dict (ds, agr.dict);
      dataset_set_source (ds, next_input);
      agr.dict = NULL;
    }
  else
    {
      ok = casewriter_destroy (output);
      output = NULL;
      if (!ok)
        goto error;
    }

  agr_destroy (&agr);
  fh_unref (out_file);
  return CMD_SUCCESS;

error:
  if (input != NULL)
    proc_commit (ds);
  casewriter_destroy (output);
  agr_destroy (&agr);
  fh_unref (out_file);
  return CMD_CASCADING_FAILURE;
}

static bool
parse_agr_func_name (struct lexer *lexer, int *func_index,
                     enum mv_class *exclude)
{
  if (lex_token (lexer) != T_ID)
    {
      lex_error (lexer, _("Syntax error expecting aggregation function."));
      return false;
    }

  struct substring name = lex_tokss (lexer);
  *exclude = ss_chomp_byte (&name, '.') ? MV_SYSTEM : MV_ANY;

  for (const struct agr_func *f = agr_func_tab; f->name; f++)
    if (ss_equals_case (ss_cstr (f->name), name))
      {
        *func_index = f - agr_func_tab;
        lex_get (lexer);
        return true;
      }
  lex_error (lexer, _("Unknown aggregation function %s."), lex_tokcstr (lexer));
  return false;
}

/* Parse all the aggregate functions. */
static bool
parse_aggregate_functions (struct lexer *lexer, const struct dictionary *dict,
                           struct agr_proc *agr)
{
  if (!lex_force_match (lexer, T_SLASH))
    return false;

  size_t starting_n_vars = dict_get_n_vars (dict);
  size_t allocated_agr_vars = 0;

  /* Parse everything. */
  for (;;)
    {
      char **dest = NULL;
      char **dest_label = NULL;
      size_t n_vars = 0;

      struct agr_argument arg[2] = { { .f = 0 }, { .f = 0 } };

      const struct variable **src = NULL;

      /* Parse the list of target variables. */
      int dst_start_ofs = lex_ofs (lexer);
      while (!lex_match (lexer, T_EQUALS))
        {
          size_t n_vars_prev = n_vars;

          if (!parse_DATA_LIST_vars (lexer, dict, &dest, &n_vars,
                                     (PV_APPEND | PV_SINGLE | PV_NO_SCRATCH
                                      | PV_NO_DUPLICATE)))
            goto error;

          /* Assign empty labels. */
          dest_label = xnrealloc (dest_label, n_vars, sizeof *dest_label);
          for (size_t j = n_vars_prev; j < n_vars; j++)
            dest_label[j] = NULL;

          if (lex_is_string (lexer))
            {
              dest_label[n_vars - 1] = xstrdup (lex_tokcstr (lexer));
              lex_get (lexer);
            }
        }
      int dst_end_ofs = lex_ofs (lexer) - 2;

      /* Get the name of the aggregation function. */
      int func_index;
      enum mv_class exclude;
      if (!parse_agr_func_name (lexer, &func_index, &exclude))
        goto error;
      const struct agr_func *function = &agr_func_tab[func_index];

      /* Check for leading lparen. */
      if (!lex_match (lexer, T_LPAREN))
        {
          if (function->src_vars == AGR_SV_YES)
            {
              bool ok UNUSED = lex_force_match (lexer, T_LPAREN);
              goto error;
            }
        }
      else
        {
          /* Parse list of source variables. */
          int pv_opts = PV_NO_SCRATCH;
          if (func_index == AGRF_SUM || func_index == AGRF_MEAN
              || func_index == AGRF_MEDIAN || func_index == AGRF_SD)
            pv_opts |= PV_NUMERIC;
          else if (function->n_args)
            pv_opts |= PV_SAME_TYPE;

          int src_start_ofs = lex_ofs (lexer);
          size_t n_src;
          if (!parse_variables_const (lexer, dict, &src, &n_src, pv_opts))
            goto error;
          int src_end_ofs = lex_ofs (lexer) - 1;

          /* Parse function arguments, for those functions that
             require arguments. */
          int args_start_ofs = 0;
          if (function->n_args != 0)
            for (size_t i = 0; i < function->n_args; i++)
              {
                lex_match (lexer, T_COMMA);

                enum val_type type;
                if (lex_is_string (lexer))
                  type = VAL_STRING;
                else if (lex_is_number (lexer))
                  type = VAL_NUMERIC;
                else
                  {
                    lex_error (lexer, _("Missing argument %zu to %s."),
                               i + 1, function->name);
                    goto error;
                  }

                if (type != var_get_type (src[0]))
                  {
                    msg (SE, _("Arguments to %s must be of same type as "
                               "source variables."),
                         function->name);
                    if (type == VAL_NUMERIC)
                      {
                        lex_next_msg (lexer, SN, 0, 0,
                                      _("The argument is numeric."));
                        lex_ofs_msg (lexer, SN, src_start_ofs, src_end_ofs,
                                     _("The variables have string type."));
                      }
                    else
                      {
                        lex_next_msg (lexer, SN, 0, 0,
                                      _("The argument is a string."));
                        lex_ofs_msg (lexer, SN, src_start_ofs, src_end_ofs,
                                     _("The variables are numeric."));
                      }
                    goto error;
                  }

                if (i == 0)
                  args_start_ofs = lex_ofs (lexer);
                if (type == VAL_NUMERIC)
                  arg[i].f = lex_tokval (lexer);
                else
                  arg[i].s = recode_substring_pool (dict_get_encoding (agr->dict),
                                                    "UTF-8", lex_tokss (lexer),
                                                    NULL);
                lex_get (lexer);
              }
          int args_end_ofs = lex_ofs (lexer) - 1;

          /* Trailing rparen. */
          if (!lex_force_match (lexer, T_RPAREN))
            goto error;

          /* Now check that the number of source variables match
             the number of target variables.  If we check earlier
             than this, the user can get very misleading error
             message, i.e. `AGGREGATE x=SUM(y t).' will get this
             error message when a proper message would be more
             like `unknown variable t'. */
          if (n_src != n_vars)
            {
              msg (SE, _("Number of source variables (%zu) does not match "
                         "number of target variables (%zu)."),
                   n_src, n_vars);
              lex_ofs_msg (lexer, SN, src_start_ofs, src_end_ofs,
                           _("These are the source variables."));
              lex_ofs_msg (lexer, SN, dst_start_ofs, dst_end_ofs,
                           _("These are the target variables."));
              goto error;
            }

          if ((func_index == AGRF_PIN || func_index == AGRF_POUT
              || func_index == AGRF_FIN || func_index == AGRF_FOUT)
              && (var_is_numeric (src[0])
                  ? arg[0].f > arg[1].f
                  : buf_compare_rpad (arg[0].s.string, arg[0].s.length,
                                      arg[1].s.string, arg[1].s.length) > 0))
            {
              struct agr_argument tmp = arg[0];
              arg[0] = arg[1];
              arg[1] = tmp;

              lex_ofs_msg (lexer, SW, args_start_ofs, args_end_ofs,
                           _("The value arguments passed to the %s function "
                             "are out of order.  They will be treated as if "
                             "they had been specified in the correct order."),
                           function->name);
            }
        }

      /* Finally add these to the aggregation variables. */
      for (size_t i = 0; i < n_vars; i++)
        {
          const struct variable *existing_var = dict_lookup_var (agr->dict,
                                                                 dest[i]);
          if (existing_var)
            {
              if (var_get_dict_index (existing_var) >= starting_n_vars)
                lex_ofs_error (lexer, dst_start_ofs, dst_end_ofs,
                               _("Duplicate target variable name %s."),
                               dest[i]);
              else if (agr->add_variables)
                lex_ofs_error (lexer, dst_start_ofs, dst_end_ofs,
                               _("Variable name %s duplicates the name of a "
                                 "variable in the active file dictionary."),
                               dest[i]);
              else
                lex_ofs_error (lexer, dst_start_ofs, dst_end_ofs,
                               _("Variable name %s duplicates the name of a "
                                 "break variable."), dest[i]);
              goto error;
            }

          /* Add variable. */
          if (agr->n_agr_vars >= allocated_agr_vars)
            agr->agr_vars = x2nrealloc (agr->agr_vars, &allocated_agr_vars,
                                        sizeof *agr->agr_vars);
          struct agr_var *v = &agr->agr_vars[agr->n_agr_vars++];
          *v = (struct agr_var) {
            .exclude = exclude,
            .moments = NULL,
            .function = func_index,
            .src = src ? src[i] : NULL,
          };

          /* Create the target variable in the aggregate dictionary. */
          if (v->src && var_is_alpha (v->src))
            v->string = xmalloc (var_get_width (v->src));

          if (v->src && function->alpha_type == VAL_STRING)
            v->dest = dict_clone_var_as_assert (agr->dict, v->src, dest[i]);
          else
            {
              v->dest = dict_create_var_assert (agr->dict, dest[i], 0);

              struct fmt_spec f;
              if ((func_index == AGRF_N || func_index == AGRF_NMISS)
                  && dict_get_weight (dict) != NULL)
                f = fmt_for_output (FMT_F, 8, 2);
              else
                f = function->format;
              var_set_both_formats (v->dest, f);
            }
          if (dest_label[i])
            var_set_label (v->dest, dest_label[i]);

          if (v->src != NULL)
            for (size_t j = 0; j < function->n_args; j++)
              v->arg[j] = (struct agr_argument) {
                .f = arg[j].f,
                .s = arg[j].s.string ? ss_clone (arg[j].s) : ss_empty (),
              };
        }

      ss_dealloc (&arg[0].s);
      ss_dealloc (&arg[1].s);

      free (src);
      for (size_t i = 0; i < n_vars; i++)
        {
          free (dest[i]);
          free (dest_label[i]);
        }
      free (dest);
      free (dest_label);

      if (!lex_match (lexer, T_SLASH))
        {
          if (lex_token (lexer) == T_ENDCMD)
            return true;

          lex_error (lexer, "Syntax error expecting end of command.");
          return false;
        }
      continue;

    error:
      for (size_t i = 0; i < n_vars; i++)
        {
          free (dest[i]);
          free (dest_label[i]);
        }
      free (dest);
      free (dest_label);
      ss_dealloc (&arg[0].s);
      ss_dealloc (&arg[1].s);
      free (src);

      return false;
    }
}

/* Destroys AGR. */
static void
agr_destroy (struct agr_proc *agr)
{
  subcase_uninit (&agr->sort);
  free (agr->break_vars);
  for (size_t i = 0; i < agr->n_agr_vars; i++)
    {
      struct agr_var *av = &agr->agr_vars[i];

      ss_dealloc (&av->arg[0].s);
      ss_dealloc (&av->arg[1].s);
      free (av->string);

      if (av->function == AGRF_SD)
        moments1_destroy (av->moments);

      dict_unref (av->dict);
    }
  free (agr->agr_vars);
  if (agr->dict != NULL)
    dict_unref (agr->dict);
}

/* Execution. */

/* Accumulates aggregation data from the case INPUT. */
static void
accumulate_aggregate_info (struct agr_proc *agr, const struct ccase *input)
{
  bool bad_warn = true;
  double weight = dict_get_case_weight (agr->src_dict, input, &bad_warn);
  for (size_t i = 0; i < agr->n_agr_vars; i++)
    {
      struct agr_var *av = &agr->agr_vars[i];
      if (av->src)
        {
          bool is_string = var_is_alpha (av->src);
          const union value *v = case_data (input, av->src);
          int src_width = var_get_width (av->src);
          const struct substring vs = (src_width > 0
                                       ? value_ss (v, src_width)
                                       : ss_empty ());

          if (var_is_value_missing (av->src, v) & av->exclude)
            {
              switch (av->function)
                {
                case AGRF_NMISS:
                  av->dbl += weight;
                  break;

                case AGRF_NUMISS:
                  av->int1++;
                  break;

                case AGRF_SUM:
                case AGRF_MEAN:
                case AGRF_MEDIAN:
                case AGRF_SD:
                case AGRF_MAX:
                case AGRF_MIN:
                case AGRF_PGT:
                case AGRF_PLT:
                case AGRF_PIN:
                case AGRF_POUT:
                case AGRF_FGT:
                case AGRF_FLT:
                case AGRF_FIN:
                case AGRF_FOUT:
                case AGRF_CGT:
                case AGRF_CLT:
                case AGRF_CIN:
                case AGRF_COUT:
                case AGRF_N:
                case AGRF_NU:
                case AGRF_FIRST:
                case AGRF_LAST:
                  break;
                }
              av->saw_missing = true;
              continue;
            }

          /* This is horrible.  There are too many possibilities. */
          av->W += weight;
          switch (av->function)
            {
            case AGRF_SUM:
              av->dbl += v->f * weight;
              av->int1 = 1;
              break;

            case AGRF_MEAN:
              av->dbl += v->f * weight;
              break;

            case AGRF_MEDIAN:
              {
                struct ccase *cout = case_create (casewriter_get_proto (av->writer));
                *case_num_rw (cout, av->subject) = case_num (input, av->src);
                *case_num_rw (cout, av->weight) = weight;
                casewriter_write (av->writer, cout);
              }
              break;

            case AGRF_SD:
              moments1_add (av->moments, v->f, weight);
              break;

            case AGRF_MAX:
              if (!is_string)
                av->dbl = MAX (av->dbl, v->f);
              else if (memcmp (av->string, v->s, src_width) < 0)
                memcpy (av->string, v->s, src_width);
              av->int1 = 1;
              break;

            case AGRF_MIN:
              if (!is_string)
                av->dbl = MIN (av->dbl, v->f);
              else if (memcmp (av->string, v->s, src_width) > 0)
                memcpy (av->string, v->s, src_width);
              av->dbl = MIN (av->dbl, v->f);
              av->int1 = 1;
              break;

            case AGRF_FGT:
            case AGRF_PGT:
            case AGRF_CGT:
              if (is_string
                  ? ss_compare_rpad (av->arg[0].s, vs) < 0
                  : v->f > av->arg[0].f)
                av->dbl += weight;
              break;

            case AGRF_FLT:
            case AGRF_PLT:
            case AGRF_CLT:
              if (is_string
                  ? ss_compare_rpad (av->arg[0].s, vs) > 0
                  : v->f < av->arg[0].f)
                av->dbl += weight;
              break;

            case AGRF_FIN:
            case AGRF_PIN:
            case AGRF_CIN:
              if (is_string
                  ? (ss_compare_rpad (av->arg[0].s, vs) <= 0
                     && ss_compare_rpad (av->arg[1].s, vs) >= 0)
                  : av->arg[0].f <= v->f && v->f <= av->arg[1].f)
                av->dbl += weight;
              break;

            case AGRF_FOUT:
            case AGRF_POUT:
            case AGRF_COUT:
              if (is_string
                  ? (ss_compare_rpad (av->arg[0].s, vs) > 0
                     || ss_compare_rpad (av->arg[1].s, vs) < 0)
                  : av->arg[0].f > v->f || v->f > av->arg[1].f)
                av->dbl += weight;
              break;

            case AGRF_N:
              av->dbl += weight;
              break;

            case AGRF_NU:
              av->int1++;
              break;

            case AGRF_FIRST:
              if (av->int1 == 0)
                {
                  if (is_string)
                    memcpy (av->string, v->s, src_width);
                  else
                    av->dbl = v->f;
                  av->int1 = 1;
                }
              break;

            case AGRF_LAST:
              if (is_string)
                memcpy (av->string, v->s, src_width);
              else
                av->dbl = v->f;
              av->int1 = 1;
              break;

            case AGRF_NMISS:
            case AGRF_NUMISS:
              /* Our value is not missing or it would have been
                 caught earlier.  Nothing to do. */
              break;
            }
        }
      else
        {
          av->W += weight;
          switch (av->function)
            {
            case AGRF_N:
              break;

            case AGRF_NU:
              av->int1++;
              break;

            case AGRF_SUM:
            case AGRF_MEAN:
            case AGRF_MEDIAN:
            case AGRF_SD:
            case AGRF_MAX:
            case AGRF_MIN:
            case AGRF_PGT:
            case AGRF_PLT:
            case AGRF_PIN:
            case AGRF_POUT:
            case AGRF_FGT:
            case AGRF_FLT:
            case AGRF_FIN:
            case AGRF_FOUT:
            case AGRF_CGT:
            case AGRF_CLT:
            case AGRF_CIN:
            case AGRF_COUT:
            case AGRF_NMISS:
            case AGRF_NUMISS:
            case AGRF_FIRST:
            case AGRF_LAST:
              NOT_REACHED ();
            }
        }
    }
}

/* Writes an aggregated record to OUTPUT. */
static void
dump_aggregate_info (const struct agr_proc *agr, struct casewriter *output, const struct ccase *break_case)
{
  struct ccase *c = case_create (dict_get_proto (agr->dict));

  if (agr->add_variables)
    {
      case_copy (c, 0, break_case, 0, dict_get_n_vars (agr->src_dict));
    }
  else
    {
      int value_idx = 0;

      for (size_t i = 0; i < agr->break_n_vars; i++)
        {
          const struct variable *v = agr->break_vars[i];
          value_copy (case_data_rw_idx (c, value_idx),
                      case_data (break_case, v),
                      var_get_width (v));
          value_idx++;
        }
    }

  for (size_t i = 0; i < agr->n_agr_vars; i++)
    {
      struct agr_var *av = &agr->agr_vars[i];
      union value *v = case_data_rw (c, av->dest);
      int width = var_get_width (av->dest);

      if (agr->missing == COLUMNWISE && av->saw_missing
          && av->function != AGRF_N
          && av->function != AGRF_NU
          && av->function != AGRF_NMISS
          && av->function != AGRF_NUMISS)
        {
          value_set_missing (v, width);
          casewriter_destroy (av->writer);
          continue;
        }

      switch (av->function)
        {
        case AGRF_SUM:
          v->f = av->int1 ? av->dbl : SYSMIS;
          break;

        case AGRF_MEAN:
          v->f = av->W != 0.0 ? av->dbl / av->W : SYSMIS;
          break;

        case AGRF_MEDIAN:
          {
            if (av->writer)
              {
                struct percentile *median = percentile_create (0.5, av->W);
                struct order_stats *os = &median->parent;
                struct casereader *sorted_reader = casewriter_make_reader (av->writer);
                av->writer = NULL;

                order_stats_accumulate (&os, 1,
                                        sorted_reader,
                                        av->weight,
                                        av->subject,
                                        av->exclude);
                av->dbl = percentile_calculate (median, PC_HAVERAGE);
                statistic_destroy (&median->parent.parent);
              }
            v->f = av->dbl;
          }
          break;

        case AGRF_SD:
          {
            double variance;

            moments1_calculate (av->moments, NULL, NULL, &variance,
                                NULL, NULL);
            v->f = variance != SYSMIS ? sqrt (variance) : SYSMIS;
          }
          break;

        case AGRF_MAX:
        case AGRF_MIN:
        case AGRF_FIRST:
        case AGRF_LAST:
          if (!width)
            v->f = av->int1 ? av->dbl : SYSMIS;
          else
            {
              if (av->int1)
                memcpy (v->s, av->string, width);
              else
                value_set_missing (v, width);
            }
          break;

        case AGRF_FGT:
        case AGRF_FLT:
        case AGRF_FIN:
        case AGRF_FOUT:
          v->f = av->W ? av->dbl / av->W : SYSMIS;
          break;

        case AGRF_PGT:
        case AGRF_PLT:
        case AGRF_PIN:
        case AGRF_POUT:
          v->f = av->W ? av->dbl / av->W * 100.0 : SYSMIS;
          break;

        case AGRF_CGT:
        case AGRF_CLT:
        case AGRF_CIN:
        case AGRF_COUT:
          v->f = av->dbl;
          break;

        case AGRF_N:
          v->f = av->W;
          break;

        case AGRF_NU:
        case AGRF_NUMISS:
          v->f = av->int1;
          break;

        case AGRF_NMISS:
          v->f = av->dbl;
          break;
        }
    }

  casewriter_write (output, c);
}

/* Resets the state for all the aggregate functions. */
static void
initialize_aggregate_info (struct agr_proc *agr)
{
  for (size_t i = 0; i < agr->n_agr_vars; i++)
    {
      struct agr_var *av = &agr->agr_vars[i];
      av->saw_missing = false;
      av->dbl = av->W = 0.0;
      av->int1 = 0;

      int width = av->src ? var_get_width (av->src) : 0;
      switch (av->function)
        {
        case AGRF_MIN:
          if (!width)
            av->dbl = DBL_MAX;
          else
            memset (av->string, 255, width);
          break;

        case AGRF_MAX:
          if (!width)
            av->dbl = -DBL_MAX;
          else
            memset (av->string, 0, width);
          break;

        case AGRF_MEDIAN:
          {
            struct caseproto *proto = caseproto_create ();
            proto = caseproto_add_width (proto, 0);
            proto = caseproto_add_width (proto, 0);

            if (!av->dict)
              av->dict = dict_create ("UTF-8");
            if (! av->subject)
              av->subject = dict_create_var (av->dict, "subject", 0);
            if (! av->weight)
              av->weight = dict_create_var (av->dict, "weight", 0);

            struct subcase ordering;
            subcase_init_var (&ordering, av->subject, SC_ASCEND);
            av->writer = sort_create_writer (&ordering, proto);
            subcase_uninit (&ordering);
            caseproto_unref (proto);
          }
          break;

        case AGRF_SD:
          if (av->moments == NULL)
            av->moments = moments1_create (MOMENT_VARIANCE);
          else
            moments1_clear (av->moments);
          break;

        case AGRF_SUM:
        case AGRF_MEAN:
        case AGRF_PGT:
        case AGRF_PLT:
        case AGRF_PIN:
        case AGRF_POUT:
        case AGRF_FGT:
        case AGRF_FLT:
        case AGRF_FIN:
        case AGRF_FOUT:
        case AGRF_CGT:
        case AGRF_CLT:
        case AGRF_CIN:
        case AGRF_COUT:
        case AGRF_N:
        case AGRF_NU:
        case AGRF_NMISS:
        case AGRF_NUMISS:
        case AGRF_FIRST:
        case AGRF_LAST:
          break;
        }
    }
}
