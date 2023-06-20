/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/commands/roc.h"

#include <gsl/gsl_cdf.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/charts/roc-chart.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct cmd_roc
{
  size_t n_vars;
  const struct variable **vars;
  const struct dictionary *dict;

  const struct variable *state_var;
  union value state_value;
  size_t state_var_width;

  /* Plot the roc curve */
  bool curve;
  /* Plot the reference line */
  bool reference;

  double ci;

  bool print_coords;
  bool print_se;
  bool bi_neg_exp; /* True iff the bi-negative exponential critieria
                      should be used */
  enum mv_class exclude;

  bool invert; /* True iff a smaller test result variable indicates
                  a positive result */

  double pos;
  double neg;
  double pos_weighted;
  double neg_weighted;
};

static int run_roc (struct dataset *, struct cmd_roc *);
static void do_roc (struct cmd_roc *, struct casereader *, struct dictionary *);


int
cmd_roc (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct cmd_roc roc = {
    .exclude = MV_ANY,
    .curve = true,
    .ci = 95,
    .dict = dict,
    .state_var_width = -1,
  };

  lex_match (lexer, T_SLASH);
  if (!parse_variables_const (lexer, dict, &roc.vars, &roc.n_vars,
                              PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (!lex_force_match (lexer, T_BY))
    goto error;

  roc.state_var = parse_variable (lexer, dict);
  if (!roc.state_var)
    goto error;

  if (!lex_force_match (lexer, T_LPAREN))
    goto error;

  roc.state_var_width = var_get_width (roc.state_var);
  value_init (&roc.state_value, roc.state_var_width);
  if (!parse_value (lexer, &roc.state_value, roc.state_var)
      || !lex_force_match (lexer, T_RPAREN))
    goto error;

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "INCLUDE"))
                roc.exclude = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
                roc.exclude = MV_ANY;
              else
                {
                  lex_error_expecting (lexer, "INCLUDE", "EXCLUDE");
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "PLOT"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "CURVE"))
            {
              roc.curve = true;
              if (lex_match (lexer, T_LPAREN))
                {
                  roc.reference = true;
                  if (!lex_force_match_id (lexer, "REFERENCE")
                      || !lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
            }
          else if (lex_match_id (lexer, "NONE"))
            roc.curve = false;
          else
            {
              lex_error_expecting (lexer, "CURVE", "NONE");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "PRINT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "SE"))
                roc.print_se = true;
              else if (lex_match_id (lexer, "COORDINATES"))
                roc.print_coords = true;
              else
                {
                  lex_error_expecting (lexer, "SE", "COORDINATES");
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "CRITERIA"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "CUTOFF"))
                {
                  if (!lex_force_match (lexer, T_LPAREN))
                    goto error;
                  if (lex_match_id (lexer, "INCLUDE"))
                    roc.exclude = MV_SYSTEM;
                  else if (lex_match_id (lexer, "EXCLUDE"))
                    roc.exclude = MV_USER | MV_SYSTEM;
                  else
                    {
                      lex_error_expecting (lexer, "INCLUDE", "EXCLUDE");
                      goto error;
                    }
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
              else if (lex_match_id (lexer, "TESTPOS"))
                {
                  if (!lex_force_match (lexer, T_LPAREN))
                    goto error;
                  if (lex_match_id (lexer, "LARGE"))
                    roc.invert = false;
                  else if (lex_match_id (lexer, "SMALL"))
                    roc.invert = true;
                  else
                    {
                      lex_error_expecting (lexer, "LARGE", "SMALL");
                      goto error;
                    }
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
              else if (lex_match_id (lexer, "CI"))
                {
                  if (!lex_force_match (lexer, T_LPAREN))
                    goto error;
                  if (!lex_force_num (lexer))
                    goto error;
                  roc.ci = lex_number (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
              else if (lex_match_id (lexer, "DISTRIBUTION"))
                {
                  if (!lex_force_match (lexer, T_LPAREN))
                    goto error;
                  if (lex_match_id (lexer, "FREE"))
                    roc.bi_neg_exp = false;
                  else if (lex_match_id (lexer, "NEGEXPO"))
                    roc.bi_neg_exp = true;
                  else
                    {
                      lex_error_expecting (lexer, "FREE", "NEGEXPO");
                      goto error;
                    }
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
              else
                {
                  lex_error_expecting (lexer, "CUTOFF", "TESTPOS", "CI",
                                       "DISTRIBUTION");
                  goto error;
                }
            }
        }
      else
        {
          lex_error_expecting (lexer, "MISSING", "PLOT", "PRINT", "CRITERIA");
          goto error;
        }
    }

  if (!run_roc (ds, &roc))
    goto error;

  if (roc.state_var)
    value_destroy (&roc.state_value, roc.state_var_width);
  free (roc.vars);
  return CMD_SUCCESS;

 error:
  if (roc.state_var)
    value_destroy (&roc.state_value, roc.state_var_width);
  free (roc.vars);
  return CMD_FAILURE;
}

static int
run_roc (struct dataset *ds, struct cmd_roc *roc)
{
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    do_roc (roc, group, dataset_dict (ds));

  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;
  return ok;
}

#if 0
static void
dump_casereader (struct casereader *reader)
{
  struct ccase *c;
  struct casereader *r = casereader_clone (reader);

  for (; (c = casereader_read (r)); case_unref (c))
    {
      for (size_t i = 0; i < case_get_n_values (c); ++i)
        printf ("%g ", case_num_idx (c, i));
      printf ("\n");
    }

  casereader_destroy (r);
}
#endif


/*
   Return true iff the state variable indicates that C has positive actual state.

   As a side effect, this function also accumulates the roc->{pos,neg} and
   roc->{pos,neg}_weighted counts.
 */
static bool
match_positives (const struct ccase *c, void *aux)
{
  struct cmd_roc *roc = aux;
  const struct variable *wv = dict_get_weight (roc->dict);
  const double weight = wv ? case_num (c, wv) : 1.0;

  const bool positive =
  (0 == value_compare_3way (case_data (c, roc->state_var), &roc->state_value,
    var_get_width (roc->state_var)));

  if (positive)
    {
      roc->pos++;
      roc->pos_weighted += weight;
    }
  else
    {
      roc->neg++;
      roc->neg_weighted += weight;
    }

  return positive;
}


#define VALUE  0
#define N_EQ   1
#define N_PRED 2

/* Some intermediate state for calculating the cutpoints and the
   standard error values */
struct roc_state
{
  double auc;  /* Area under the curve */

  double n1;  /* total weight of positives */
  double n2;  /* total weight of negatives */

  /* intermediates for standard error */
  double q1hat;
  double q2hat;

  /* intermediates for cutpoints */
  struct casewriter *cutpoint_wtr;
  struct casereader *cutpoint_rdr;
  double prev_result;
  double min;
  double max;
};

/*
   Return a new casereader based upon CUTPOINT_RDR.
   The number of "positive" cases are placed into
   the position TRUE_INDEX, and the number of "negative" cases
   into FALSE_INDEX.
   POS_COND and RESULT determine the semantics of what is
   "positive".
   WEIGHT is the value of a single count.
 */
static struct casereader *
accumulate_counts (struct casereader *input,
                   double result, double weight,
                   bool (*pos_cond) (double, double),
                   int true_index, int false_index)
{
  const struct caseproto *proto = casereader_get_proto (input);
  struct casewriter *w =
    autopaging_writer_create (proto);
  struct ccase *cpc;
  double prev_cp = SYSMIS;

  for (; (cpc = casereader_read (input)); case_unref (cpc))
    {
      struct ccase *new_case;
      const double cp = case_num_idx (cpc, ROC_CUTPOINT);

      assert (cp != SYSMIS);

      /* We don't want duplicates here */
      if (cp == prev_cp)
        continue;

      new_case = case_clone (cpc);

      int index = pos_cond (result, cp) ? true_index : false_index;
      *case_num_rw_idx (new_case, index) += weight;

      prev_cp = cp;

      casewriter_write (w, new_case);
    }
  casereader_destroy (input);

  return casewriter_make_reader (w);
}



static void output_roc (struct roc_state *rs, const struct cmd_roc *roc);

/*
  This function does 3 things:

  1. Counts the number of cases which are equal to every other case in READER,
  and those cases for which the relationship between it and every other case
  satifies PRED (normally either > or <).  VAR is variable defining a case's value
  for this purpose.

  2. Counts the number of true and false cases in reader, and populates
  CUTPOINT_RDR accordingly.  TRUE_INDEX and FALSE_INDEX are the indices
  which receive these values.  POS_COND is the condition defining true
  and false.

  3. CC is filled with the cumulative weight of all cases of READER.
*/
static struct casereader *
process_group (const struct variable *var, struct casereader *reader,
               bool (*pred) (double, double),
               const struct dictionary *dict,
               double *cc,
               struct casereader **cutpoint_rdr,
               bool (*pos_cond) (double, double),
               int true_index,
               int false_index)
{
  const struct variable *w = dict_get_weight (dict);

  struct casereader *r1 =
    casereader_create_distinct (sort_execute_1var (reader, var), var, w);

  const int weight_idx  = w ? var_get_dict_index (w) :
    caseproto_get_n_widths (casereader_get_proto (r1)) - 1;

  struct ccase *c1;

  struct casereader *rclone = casereader_clone (r1);
  struct casewriter *wtr;
  struct caseproto *proto = caseproto_create ();

  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);

  wtr = autopaging_writer_create (proto);

  *cc = 0;

  for (; (c1 = casereader_read (r1)); case_unref (c1))
    {
      struct ccase *new_case = case_create (proto);
      struct ccase *c2;
      struct casereader *r2 = casereader_clone (rclone);

      const double weight1 = case_num_idx (c1, weight_idx);
      const double d1 = case_num (c1, var);
      double n_eq = 0.0;
      double n_pred = 0.0;

      *cutpoint_rdr = accumulate_counts (*cutpoint_rdr, d1, weight1,
                                         pos_cond,
                                         true_index, false_index);

      *cc += weight1;

      for (; (c2 = casereader_read (r2)); case_unref (c2))
        {
          const double d2 = case_num (c2, var);
          const double weight2 = case_num_idx (c2, weight_idx);

          if (d1 == d2)
            {
              n_eq += weight2;
              continue;
            }
          else  if (pred (d2, d1))
            {
              n_pred += weight2;
            }
        }

      *case_num_rw_idx (new_case, VALUE) = d1;
      *case_num_rw_idx (new_case, N_EQ) = n_eq;
      *case_num_rw_idx (new_case, N_PRED) = n_pred;

      casewriter_write (wtr, new_case);

      casereader_destroy (r2);
    }

  casereader_destroy (r1);
  casereader_destroy (rclone);

  caseproto_unref (proto);

  return casewriter_make_reader (wtr);
}

/* Some more indeces into case data */
#define N_POS_EQ 1  /* number of positive cases with values equal to n */
#define N_POS_GT 2  /* number of positive cases with values greater than n */
#define N_NEG_EQ 3  /* number of negative cases with values equal to n */
#define N_NEG_LT 4  /* number of negative cases with values less than n */

static bool
gt (double d1, double d2)
{
  return d1 > d2;
}


static bool
ge (double d1, double d2)
{
  return d1 > d2;
}

static bool
lt (double d1, double d2)
{
  return d1 < d2;
}


/*
  Return a casereader with width 3,
  populated with cases based upon READER.
  The cases will have the values:
  (N, number of cases equal to N, number of cases greater than N)
  As a side effect, update RS->n1 with the number of positive cases.
*/
static struct casereader *
process_positive_group (const struct variable *var, struct casereader *reader,
                        const struct dictionary *dict,
                        struct roc_state *rs)
{
  return process_group (var, reader, gt, dict, &rs->n1,
                        &rs->cutpoint_rdr,
                        ge,
                        ROC_TP, ROC_FN);
}

/*
  Return a casereader with width 3,
  populated with cases based upon READER.
  The cases will have the values:
  (N, number of cases equal to N, number of cases less than N)
  As a side effect, update RS->n2 with the number of negative cases.
*/
static struct casereader *
process_negative_group (const struct variable *var, struct casereader *reader,
                        const struct dictionary *dict,
                        struct roc_state *rs)
{
  return process_group (var, reader, lt, dict, &rs->n2,
                        &rs->cutpoint_rdr,
                        lt,
                        ROC_TN, ROC_FP);
}




static void
append_cutpoint (struct casewriter *writer, double cutpoint)
{
  struct ccase *cc = case_create (casewriter_get_proto (writer));

  *case_num_rw_idx (cc, ROC_CUTPOINT) = cutpoint;
  *case_num_rw_idx (cc, ROC_TP) = 0;
  *case_num_rw_idx (cc, ROC_FN) = 0;
  *case_num_rw_idx (cc, ROC_TN) = 0;
  *case_num_rw_idx (cc, ROC_FP) = 0;

  casewriter_write (writer, cc);
}

/*
   Create and initialise the rs[x].cutpoint_rdr casereaders.  That is, the
   readers will be created with width 5, ready to take the values (cutpoint,
   ROC_TP, ROC_FN, ROC_TN, ROC_FP), and the reader will be populated with its
   final number of cases.  However on exit from this function, only
   ROC_CUTPOINT entries will be set to their final value.  The other entries
   will be initialised to zero.
*/
static struct roc_state *
prepare_cutpoints (struct cmd_roc *roc, struct casereader *input)
{
  struct casereader *r = casereader_clone (input);
  struct ccase *c;

  struct subcase ordering;
  subcase_init (&ordering, ROC_CUTPOINT, 0, SC_ASCEND);

  struct caseproto *proto = caseproto_create ();
  proto = caseproto_add_width (proto, 0); /* cutpoint */
  proto = caseproto_add_width (proto, 0); /* ROC_TP */
  proto = caseproto_add_width (proto, 0); /* ROC_FN */
  proto = caseproto_add_width (proto, 0); /* ROC_TN */
  proto = caseproto_add_width (proto, 0); /* ROC_FP */

  struct roc_state *rs = xnmalloc (roc->n_vars, sizeof *rs);
  for (size_t i = 0; i < roc->n_vars; ++i)
    rs[i] = (struct roc_state) {
      .cutpoint_wtr = sort_create_writer (&ordering, proto),
      .prev_result = SYSMIS,
      .max = -DBL_MAX,
      .min = DBL_MAX,
    };

  caseproto_unref (proto);
  subcase_uninit (&ordering);

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    for (size_t i = 0; i < roc->n_vars; ++i)
      {
        const union value *v = case_data (c, roc->vars[i]);
        const double result = v->f;

        if (mv_is_value_missing (var_get_missing_values (roc->vars[i]), v)
            & roc->exclude)
          continue;

        minimize (&rs[i].min, result);
        maximize (&rs[i].max, result);

        if (rs[i].prev_result != SYSMIS && rs[i].prev_result != result)
          {
            const double mean = (result + rs[i].prev_result) / 2.0;
            append_cutpoint (rs[i].cutpoint_wtr, mean);
          }

        rs[i].prev_result = result;
      }
  casereader_destroy (r);

  /* Append the min and max cutpoints */
  for (size_t i = 0; i < roc->n_vars; ++i)
    {
      append_cutpoint (rs[i].cutpoint_wtr, rs[i].min - 1);
      append_cutpoint (rs[i].cutpoint_wtr, rs[i].max + 1);

      rs[i].cutpoint_rdr = casewriter_make_reader (rs[i].cutpoint_wtr);
    }

  return rs;
}

static void
do_roc (struct cmd_roc *roc, struct casereader *reader, struct dictionary *dict)
{
  struct casereader *input = casereader_create_filter_missing (
    reader, roc->vars, roc->n_vars, roc->exclude, NULL, NULL);
  input = casereader_create_filter_missing (
    input, &roc->state_var, 1, roc->exclude, NULL, NULL);

  struct roc_state *rs = prepare_cutpoints (roc, input);

  /* Separate the positive actual state cases from the negative ones */
  struct casewriter *neg_wtr
    = autopaging_writer_create (casereader_get_proto (input));
  struct casereader *positives = casereader_create_filter_func (
    input, match_positives, NULL, roc, neg_wtr);

  struct caseproto *n_proto = caseproto_create ();
  for (size_t i = 0; i < 5; i++)
    n_proto = caseproto_add_width (n_proto, 0);

  struct subcase up_ordering;
  struct subcase down_ordering;
  subcase_init (&up_ordering, VALUE, 0, SC_ASCEND);
  subcase_init (&down_ordering, VALUE, 0, SC_DESCEND);

  struct casereader *negatives = NULL;
  for (size_t i = 0; i < roc->n_vars; ++i)
    {
      const struct variable *var = roc->vars[i];

      struct casereader *pos = casereader_clone (positives);

      struct casereader *n_pos_reader =
        process_positive_group (var, pos, dict, &rs[i]);

      if (!negatives)
        negatives = casewriter_make_reader (neg_wtr);

      struct casereader *neg = casereader_clone (negatives);
      struct casereader *n_neg_reader
        = process_negative_group (var, neg, dict, &rs[i]);

      /* Merge the n_pos and n_neg casereaders */
      struct casewriter *w = sort_create_writer (&up_ordering, n_proto);
      struct ccase *cpos;
      for (; (cpos = casereader_read (n_pos_reader)); case_unref (cpos))
        {
          struct ccase *pos_case = case_create (n_proto);
          const double jpos = case_num_idx (cpos, VALUE);

          struct ccase *cneg;
          while ((cneg = casereader_read (n_neg_reader)))
            {
              struct ccase *nc = case_create (n_proto);

              const double jneg = case_num_idx (cneg, VALUE);

              *case_num_rw_idx (nc, VALUE) = jneg;
              *case_num_rw_idx (nc, N_POS_EQ) = 0;

              *case_num_rw_idx (nc, N_POS_GT) = SYSMIS;

              *case_data_rw_idx (nc, N_NEG_EQ) = *case_data_idx (cneg, N_EQ);
              *case_data_rw_idx (nc, N_NEG_LT) = *case_data_idx (cneg, N_PRED);

              casewriter_write (w, nc);

              case_unref (cneg);
              if (jneg > jpos)
                break;
            }

          *case_num_rw_idx (pos_case, VALUE) = jpos;
          *case_data_rw_idx (pos_case, N_POS_EQ) = *case_data_idx (cpos, N_EQ);
          *case_data_rw_idx (pos_case, N_POS_GT) = *case_data_idx (cpos, N_PRED);
          *case_num_rw_idx (pos_case, N_NEG_EQ) = 0;
          *case_num_rw_idx (pos_case, N_NEG_LT) = SYSMIS;

          casewriter_write (w, pos_case);
        }

      casereader_destroy (n_pos_reader);
      casereader_destroy (n_neg_reader);

      struct casereader *r = casewriter_make_reader (w);

      /* Propagate the N_POS_GT values from the positive cases
         to the negative ones */
      double prev_pos_gt = rs[i].n1;
      w = sort_create_writer (&down_ordering, n_proto);

      struct ccase *c;
      for (; (c = casereader_read (r)); case_unref (c))
        {
          double n_pos_gt = case_num_idx (c, N_POS_GT);
          struct ccase *nc = case_clone (c);

          if (n_pos_gt == SYSMIS)
            {
              n_pos_gt = prev_pos_gt;
              *case_num_rw_idx (nc, N_POS_GT) = n_pos_gt;
            }

          casewriter_write (w, nc);
          prev_pos_gt = n_pos_gt;
        }
      casereader_destroy (r);
      r = casewriter_make_reader (w);

      /* Propagate the N_NEG_LT values from the negative cases
         to the positive ones */
      double prev_neg_lt = rs[i].n2;
      w = sort_create_writer (&up_ordering, n_proto);

      for (; (c = casereader_read (r)); case_unref (c))
        {
          double n_neg_lt = case_num_idx (c, N_NEG_LT);
          struct ccase *nc = case_clone (c);

          if (n_neg_lt == SYSMIS)
            {
              n_neg_lt = prev_neg_lt;
              *case_num_rw_idx (nc, N_NEG_LT) = n_neg_lt;
            }

          casewriter_write (w, nc);
          prev_neg_lt = n_neg_lt;
        }

      casereader_destroy (r);
      r = casewriter_make_reader (w);

      struct ccase *prev_case = NULL;
      for (; (c = casereader_read (r)); case_unref (c))
        {
          struct ccase *next_case = casereader_peek (r, 0);

          const double j = case_num_idx (c, VALUE);
          double n_pos_eq = case_num_idx (c, N_POS_EQ);
          double n_pos_gt = case_num_idx (c, N_POS_GT);
          double n_neg_eq = case_num_idx (c, N_NEG_EQ);
          double n_neg_lt = case_num_idx (c, N_NEG_LT);

          if (prev_case && j == case_num_idx (prev_case, VALUE))
            {
              if (0 ==  case_num_idx (c, N_POS_EQ))
                {
                  n_pos_eq = case_num_idx (prev_case, N_POS_EQ);
                  n_pos_gt = case_num_idx (prev_case, N_POS_GT);
                }

              if (0 ==  case_num_idx (c, N_NEG_EQ))
                {
                  n_neg_eq = case_num_idx (prev_case, N_NEG_EQ);
                  n_neg_lt = case_num_idx (prev_case, N_NEG_LT);
                }
            }

          if (NULL == next_case || j != case_num_idx (next_case, VALUE))
            {
              rs[i].auc += n_pos_gt * n_neg_eq + (n_pos_eq * n_neg_eq) / 2.0;

              rs[i].q1hat +=
                n_neg_eq * (pow2 (n_pos_gt) + n_pos_gt * n_pos_eq + pow2 (n_pos_eq) / 3.0);
              rs[i].q2hat +=
                n_pos_eq * (pow2 (n_neg_lt) + n_neg_lt * n_neg_eq + pow2 (n_neg_eq) / 3.0);

            }

          case_unref (next_case);
          case_unref (prev_case);
          prev_case = case_clone (c);
        }
      casereader_destroy (r);
      case_unref (prev_case);

      rs[i].auc /= rs[i].n1 * rs[i].n2;
      if (roc->invert)
        rs[i].auc = 1 - rs[i].auc;

      if (roc->bi_neg_exp)
        {
          rs[i].q1hat = rs[i].auc / (2 - rs[i].auc);
          rs[i].q2hat = 2 * pow2 (rs[i].auc) / (1 + rs[i].auc);
        }
      else
        {
          rs[i].q1hat /= rs[i].n2 * pow2 (rs[i].n1);
          rs[i].q2hat /= rs[i].n1 * pow2 (rs[i].n2);
        }
    }

  casereader_destroy (positives);
  casereader_destroy (negatives);

  caseproto_unref (n_proto);
  subcase_uninit (&up_ordering);
  subcase_uninit (&down_ordering);

  output_roc (rs, roc);

  for (size_t i = 0; i < roc->n_vars; ++i)
    casereader_destroy (rs[i].cutpoint_rdr);

  free (rs);
}

static void
show_auc (struct roc_state *rs, const struct cmd_roc *roc)
{
  struct pivot_table *table = pivot_table_create (N_("Area Under the Curve"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("Area"), PIVOT_RC_OTHER);
  if (roc->print_se)
    {
      pivot_category_create_leaves (
        statistics->root,
        N_("Std. Error"), PIVOT_RC_OTHER,
        N_("Asymptotic Sig."), PIVOT_RC_SIGNIFICANCE);
      struct pivot_category *interval = pivot_category_create_group__ (
        statistics->root,
        pivot_value_new_text_format (N_("Asymp. %g%% Confidence Interval"),
                                     roc->ci));
      pivot_category_create_leaves (interval,
                                    N_("Lower Bound"), PIVOT_RC_OTHER,
                                    N_("Upper Bound"), PIVOT_RC_OTHER);
    }

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable under test"));
  variables->root->show_label = true;

  for (size_t i = 0; i < roc->n_vars; ++i)
    {
      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (roc->vars[i]));

      pivot_table_put2 (table, 0, var_idx, pivot_value_new_number (rs[i].auc));

      if (roc->print_se)
        {
          double se = (rs[i].auc * (1 - rs[i].auc)
                       + (rs[i].n1 - 1) * (rs[i].q1hat - pow2 (rs[i].auc))
                       + (rs[i].n2 - 1) * (rs[i].q2hat - pow2 (rs[i].auc)));
          se /= rs[i].n1 * rs[i].n2;
          se = sqrt (se);

          double ci = 1 - roc->ci / 100.0;
          double yy = gsl_cdf_gaussian_Qinv (ci, se);

          double sd_0_5 = sqrt ((rs[i].n1 + rs[i].n2 + 1) /
                                (12 * rs[i].n1 * rs[i].n2));
          double sig = 2.0 * gsl_cdf_ugaussian_Q (fabs ((rs[i].auc - 0.5)
                                                        / sd_0_5));
          double entries[] = { se, sig, rs[i].auc - yy, rs[i].auc + yy };
          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            pivot_table_put2 (table, i + 1, var_idx,
                              pivot_value_new_number (entries[i]));
        }
    }

  pivot_table_submit (table);
}

static void
show_summary (const struct cmd_roc *roc)
{
  struct pivot_table *table = pivot_table_create (N_("Case Summary"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Valid N (listwise)"),
    N_("Unweighted"), PIVOT_RC_INTEGER,
    N_("Weighted"), PIVOT_RC_OTHER);
  statistics->root->show_label = true;

  struct pivot_dimension *cases = pivot_dimension_create__ (
    table, PIVOT_AXIS_ROW, pivot_value_new_variable (roc->state_var));
  cases->root->show_label = true;
  pivot_category_create_leaves (cases->root, N_("Positive"), N_("Negative"));

  struct entry
    {
      int stat_idx;
      int case_idx;
      double x;
    }
  entries[] = {
    { 0, 0, roc->pos },
    { 0, 1, roc->neg },
    { 1, 0, roc->pos_weighted },
    { 1, 1, roc->neg_weighted },
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    {
      const struct entry *e = &entries[i];
      pivot_table_put2 (table, e->stat_idx, e->case_idx,
                        pivot_value_new_number (e->x));
    }
  pivot_table_submit (table);
}

static void
show_coords (struct roc_state *rs, const struct cmd_roc *roc)
{
  struct pivot_table *table = pivot_table_create (
    N_("Coordinates of the Curve"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Positive if greater than or equal to"),
                          N_("Sensitivity"), N_("1 - Specificity"));

  struct pivot_dimension *coordinates = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Coordinates"));
  coordinates->hide_all_labels = true;

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Test variable"));
  variables->root->show_label = true;


  int n_coords = 0;
  for (size_t i = 0; i < roc->n_vars; ++i)
    {
      struct casereader *r = casereader_clone (rs[i].cutpoint_rdr);

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (roc->vars[i]));

      struct ccase *cc;
      int coord_idx = 0;
      for (; (cc = casereader_read (r)) != NULL; case_unref (cc))
        {
          const double se = case_num_idx (cc, ROC_TP) /
            (case_num_idx (cc, ROC_TP) + case_num_idx (cc, ROC_FN));

          const double sp = case_num_idx (cc, ROC_TN) /
            (case_num_idx (cc, ROC_TN) + case_num_idx (cc, ROC_FP));

          if (coord_idx >= n_coords)
            {
              assert (coord_idx == n_coords);
              pivot_category_create_leaf (
                coordinates->root, pivot_value_new_integer (++n_coords));
            }

          pivot_table_put3 (
            table, 0, coord_idx, var_idx,
            pivot_value_new_var_value (roc->vars[i],
                                       case_data_idx (cc, ROC_CUTPOINT)));

          pivot_table_put3 (table, 1, coord_idx, var_idx,
                            pivot_value_new_number (se));
          pivot_table_put3 (table, 2, coord_idx, var_idx,
                            pivot_value_new_number (1 - sp));
          coord_idx++;
        }

      casereader_destroy (r);
    }

  pivot_table_submit (table);
}

static void
output_roc (struct roc_state *rs, const struct cmd_roc *roc)
{
  show_summary (roc);

  if (roc->curve)
    {
      struct roc_chart *rc = roc_chart_create (roc->reference);
      for (size_t i = 0; i < roc->n_vars; i++)
        roc_chart_add_var (rc, var_get_name (roc->vars[i]),
                           rs[i].cutpoint_rdr);
      roc_chart_submit (rc);
    }

  show_auc (rs, roc);

  if (roc->print_coords)
    show_coords (rs, roc);
}

