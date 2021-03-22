/* pspp - a program for statistical analysis.
   Copyright (C) 2012 Free Software Foundation, Inc.

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


/*
   References:
   1. "Coding Logistic Regression with Newton-Raphson", James McCaffrey
   http://msdn.microsoft.com/en-us/magazine/jj618304.aspx

   2. "SPSS Statistical Algorithms" Chapter LOGISTIC REGRESSION Algorithms


   The Newton Raphson method finds successive approximations to $\bf b$ where
   approximation ${\bf b}_t$ is (hopefully) better than the previous ${\bf b}_{t-1}$.

   $ {\bf b}_t = {\bf b}_{t -1} + ({\bf X}^T{\bf W}_{t-1}{\bf X})^{-1}{\bf X}^T({\bf y} - {\bf \pi}_{t-1})$
   where:

   $\bf X$ is the $n \times p$ design matrix, $n$ being the number of cases,
   $p$ the number of parameters, \par
   $\bf W$ is the diagonal matrix whose diagonal elements are
   $\hat{\pi}_0(1 - \hat{\pi}_0), \, \hat{\pi}_1(1 - \hat{\pi}_2)\dots \hat{\pi}_{n-1}(1 - \hat{\pi}_{n-1})$
   \par

*/

#include <config.h>

#include <gsl/gsl_blas.h>

#include <gsl/gsl_linalg.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/categoricals.h"
#include "math/interaction.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)




#define   PRINT_EACH_STEP  0x01
#define   PRINT_SUMMARY    0x02
#define   PRINT_CORR       0x04
#define   PRINT_ITER       0x08
#define   PRINT_GOODFIT    0x10
#define   PRINT_CI         0x20


#define PRINT_DEFAULT (PRINT_SUMMARY | PRINT_EACH_STEP)

/*
  The constant parameters of the procedure.
  That is, those which are set by the user.
*/
struct lr_spec
{
  /* The dependent variable */
  const struct variable *dep_var;

  /* The predictor variables (excluding categorical ones) */
  const struct variable **predictor_vars;
  size_t n_predictor_vars;

  /* The categorical predictors */
  struct interaction **cat_predictors;
  size_t n_cat_predictors;


  /* The union of the categorical and non-categorical variables */
  const struct variable **indep_vars;
  size_t n_indep_vars;


  /* Which classes of missing vars are to be excluded */
  enum mv_class exclude;

  /* The weight variable */
  const struct variable *wv;

  /* The dictionary of the dataset */
  const struct dictionary *dict;

  /* True iff the constant (intercept) is to be included in the model */
  bool constant;

  /* Ths maximum number of iterations */
  int max_iter;

  /* Other iteration limiting conditions */
  double bcon;
  double min_epsilon;
  double lcon;

  /* The confidence interval (in percent) */
  int confidence;

  /* What results should be presented */
  unsigned int print;

  /* Inverse logit of the cut point */
  double ilogit_cut_point;
};


/* The results and intermediate result of the procedure.
   These are mutated as the procedure runs. Used for
   temporary variables etc.
*/
struct lr_result
{
  /* Used to indicate if a pass should flag a warning when
     invalid (ie negative or missing) weight values are encountered */
  bool warn_bad_weight;

  /* The two values of the dependent variable. */
  union value y0;
  union value y1;


  /* The sum of caseweights */
  double cc;

  /* The number of missing and nonmissing cases */
  casenumber n_missing;
  casenumber n_nonmissing;


  gsl_matrix *hessian;

  /* The categoricals and their payload. Null if  the analysis has no
   categorical predictors */
  struct categoricals *cats;
  struct payload cp;


  /* The estimates of the predictor coefficients */
  gsl_vector *beta_hat;

  /* The predicted classifications:
     True Negative, True Positive, False Negative, False Positive */
  double tn, tp, fn, fp;
};


/*
  Convert INPUT into a dichotomous scalar, according to how the dependent variable's
  values are mapped.
  For simple cases, this is a 1:1 mapping
  The return value is always either 0 or 1
*/
static double
map_dependent_var (const struct lr_spec *cmd, const struct lr_result *res, const union value *input)
{
  const int width = var_get_width (cmd->dep_var);
  if (value_equal (input, &res->y0, width))
    return 0;

  if (value_equal (input, &res->y1, width))
    return 1;

  /* This should never happen.  If it does,  then y0 and/or y1 have probably not been set */
  NOT_REACHED ();

  return SYSMIS;
}

static void output_classification_table (const struct lr_spec *cmd, const struct lr_result *res);

static void output_categories (const struct lr_spec *cmd, const struct lr_result *res);

static void output_depvarmap (const struct lr_spec *cmd, const struct lr_result *);

static void output_variables (const struct lr_spec *cmd,
			      const struct lr_result *);

static void output_model_summary (const struct lr_result *,
				  double initial_likelihood, double likelihood);

static void case_processing_summary (const struct lr_result *);


/* Return the value of case C corresponding to the INDEX'th entry in the
   model */
static double
predictor_value (const struct ccase *c,
                    const struct variable **x, size_t n_x,
                    const struct categoricals *cats,
                    size_t index)
{
  /* Values of the scalar predictor variables */
  if (index < n_x)
    return case_data (c, x[index])->f;

  /* Coded values of categorical predictor variables (or interactions) */
  if (cats && index - n_x  < categoricals_df_total (cats))
    {
      double x = categoricals_get_dummy_code_for_case (cats, index - n_x, c);
      return x;
    }

  /* The constant term */
  return 1.0;
}


/*
  Return the probability beta_hat (that is the estimator logit(y))
  corresponding to the coefficient estimator for case C
*/
static double
pi_hat (const struct lr_spec *cmd,
	const struct lr_result *res,
	const struct variable **x, size_t n_x,
	const struct ccase *c)
{
  int v0;
  double pi = 0;
  size_t n_coeffs = res->beta_hat->size;

  if (cmd->constant)
    {
      pi += gsl_vector_get (res->beta_hat, res->beta_hat->size - 1);
      n_coeffs--;
    }

  for (v0 = 0; v0 < n_coeffs; ++v0)
    {
      pi += gsl_vector_get (res->beta_hat, v0) *
	predictor_value (c, x, n_x, res->cats, v0);
    }

  pi = 1.0 / (1.0 + exp(-pi));

  return pi;
}


/*
  Calculates the Hessian matrix X' V  X,
  where: X is the n by N_X matrix comprising the n cases in INPUT
  V is a diagonal matrix { (pi_hat_0)(1 - pi_hat_0), (pi_hat_1)(1 - pi_hat_1), ... (pi_hat_{N-1})(1 - pi_hat_{N-1})}
  (the partial derivative of the predicted values)

  If ALL predicted values derivatives are close to zero or one, then CONVERGED
  will be set to true.
*/
static void
hessian (const struct lr_spec *cmd,
	 struct lr_result *res,
	 struct casereader *input,
	 const struct variable **x, size_t n_x,
	 bool *converged)
{
  struct casereader *reader;
  struct ccase *c;

  double max_w = -DBL_MAX;

  gsl_matrix_set_zero (res->hessian);

  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int v0, v1;
      double pi = pi_hat (cmd, res, x, n_x, c);

      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);
      double w = pi * (1 - pi);
      if (w > max_w)
	max_w = w;
      w *= weight;

      for (v0 = 0; v0 < res->beta_hat->size; ++v0)
	{
	  double in0 = predictor_value (c, x, n_x, res->cats, v0);
	  for (v1 = 0; v1 < res->beta_hat->size; ++v1)
	    {
	      double in1 = predictor_value (c, x, n_x, res->cats, v1);
	      double *o = gsl_matrix_ptr (res->hessian, v0, v1);
	      *o += in0 * w * in1;
	    }
	}
    }
  casereader_destroy (reader);

  if (max_w < cmd->min_epsilon)
    {
      *converged = true;
      msg (MN, _("All predicted values are either 1 or 0"));
    }
}


/* Calculates the value  X' (y - pi)
   where X is the design model,
   y is the vector of observed independent variables
   pi is the vector of estimates for y

   Side effects:
     the likelihood is stored in LIKELIHOOD;
     the predicted values are placed in the respective tn, fn, tp fp values in RES
*/
static gsl_vector *
xt_times_y_pi (const struct lr_spec *cmd,
	       struct lr_result *res,
	       struct casereader *input,
	       const struct variable **x, size_t n_x,
	       const struct variable *y_var,
	       double *llikelihood)
{
  struct casereader *reader;
  struct ccase *c;
  gsl_vector *output = gsl_vector_calloc (res->beta_hat->size);

  *llikelihood = 0.0;
  res->tn = res->tp = res->fn = res->fp = 0;
  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      double pred_y = 0;
      int v0;
      double pi = pi_hat (cmd, res, x, n_x, c);
      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);


      double y = map_dependent_var (cmd, res, case_data (c, y_var));

      *llikelihood += (weight * y) * log (pi) + log (1 - pi) * weight * (1 - y);

      for (v0 = 0; v0 < res->beta_hat->size; ++v0)
	{
	  double in0 = predictor_value (c, x, n_x, res->cats, v0);
	  double *o = gsl_vector_ptr (output, v0);
      	  *o += in0 * (y - pi) * weight;
	  pred_y += gsl_vector_get (res->beta_hat, v0) * in0;
	}

      /* Count the number of cases which would be correctly/incorrectly classified by this
	 estimated model */
      if (pred_y <= cmd->ilogit_cut_point)
	{
	  if (y == 0)
	    res->tn += weight;
	  else
	    res->fn += weight;
	}
      else
	{
	  if (y == 0)
	    res->fp += weight;
	  else
	    res->tp += weight;
	}
    }

  casereader_destroy (reader);

  return output;
}



/* "payload" functions for the categoricals.
   The only function is to accumulate the frequency of each
   category.
 */

static void *
frq_create  (const void *aux1 UNUSED, void *aux2 UNUSED)
{
  return xzalloc (sizeof (double));
}

static void
frq_update  (const void *aux1 UNUSED, void *aux2 UNUSED,
	     void *ud, const struct ccase *c UNUSED , double weight)
{
  double *freq = ud;
  *freq += weight;
}

static void
frq_destroy (const void *aux1 UNUSED, void *aux2 UNUSED, void *user_data)
{
  free (user_data);
}



/*
   Makes an initial pass though the data, doing the following:

   * Checks that the dependent variable is  dichotomous,
   * Creates and initialises the categoricals,
   * Accumulates summary results,
   * Calculates necessary initial values.
   * Creates an initial value for \hat\beta the vector of beta_hats of \beta

   Returns true if successful
*/
static bool
initial_pass (const struct lr_spec *cmd, struct lr_result *res, struct casereader *input)
{
  const int width = var_get_width (cmd->dep_var);

  struct ccase *c;
  struct casereader *reader;

  double sum;
  double sumA = 0.0;
  double sumB = 0.0;

  bool v0set = false;
  bool v1set = false;

  size_t n_coefficients = cmd->n_predictor_vars;
  if (cmd->constant)
    n_coefficients++;

  /* Create categoricals if appropriate */
  if (cmd->n_cat_predictors > 0)
    {
      res->cp.create = frq_create;
      res->cp.update = frq_update;
      res->cp.calculate = NULL;
      res->cp.destroy = frq_destroy;

      res->cats = categoricals_create (cmd->cat_predictors, cmd->n_cat_predictors,
				       cmd->wv, MV_ANY);

      categoricals_set_payload (res->cats, &res->cp, cmd, res);
    }

  res->cc = 0;
  for (reader = casereader_clone (input);
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int v;
      bool missing = false;
      double weight = dict_get_case_weight (cmd->dict, c, &res->warn_bad_weight);
      const union value *depval = case_data (c, cmd->dep_var);

      if (var_is_value_missing (cmd->dep_var, depval, cmd->exclude))
	{
	  missing = true;
	}
      else
      for (v = 0; v < cmd->n_indep_vars; ++v)
	{
	  const union value *val = case_data (c, cmd->indep_vars[v]);
	  if (var_is_value_missing (cmd->indep_vars[v], val, cmd->exclude))
	    {
	      missing = true;
	      break;
	    }
	}

      /* Accumulate the missing and non-missing counts */
      if (missing)
	{
	  res->n_missing++;
	  continue;
	}
      res->n_nonmissing++;

      /* Find the values of the dependent variable */
      if (!v0set)
	{
	  value_clone (&res->y0, depval, width);
	  v0set = true;
	}
      else if (!v1set)
	{
	  if (!value_equal (&res->y0, depval, width))
	    {
	      value_clone (&res->y1, depval, width);
	      v1set = true;
	    }
	}
      else
	{
	  if (! value_equal (&res->y0, depval, width)
	      &&
	      ! value_equal (&res->y1, depval, width)
	)
	    {
	      msg (ME, _("Dependent variable's values are not dichotomous."));
              case_unref (c);
	      goto error;
	    }
	}

      if (v0set && value_equal (&res->y0, depval, width))
	  sumA += weight;

      if (v1set && value_equal (&res->y1, depval, width))
	  sumB += weight;


      res->cc += weight;

      categoricals_update (res->cats, c);
    }
  casereader_destroy (reader);

  categoricals_done (res->cats);

  sum = sumB;

  /* Ensure that Y0 is less than Y1.  Otherwise the mapping gets
     inverted, which is confusing to users */
  if (var_is_numeric (cmd->dep_var) && value_compare_3way (&res->y0, &res->y1, width) > 0)
    {
      union value tmp;
      value_clone (&tmp, &res->y0, width);
      value_copy (&res->y0, &res->y1, width);
      value_copy (&res->y1, &tmp, width);
      value_destroy (&tmp, width);
      sum = sumA;
    }

  n_coefficients += categoricals_df_total (res->cats);
  res->beta_hat = gsl_vector_calloc (n_coefficients);

  if (cmd->constant)
    {
      double mean = sum / res->cc;
      gsl_vector_set (res->beta_hat, res->beta_hat->size - 1, log (mean / (1 - mean)));
    }

  return true;

 error:
  casereader_destroy (reader);
  return false;
}



/* Start of the logistic regression routine proper */
static bool
run_lr (const struct lr_spec *cmd, struct casereader *input,
	const struct dataset *ds UNUSED)
{
  int i;

  bool converged = false;

  /* Set the log likelihoods to a sentinel value */
  double log_likelihood = SYSMIS;
  double prev_log_likelihood = SYSMIS;
  double initial_log_likelihood = SYSMIS;

  struct lr_result work;
  work.n_missing = 0;
  work.n_nonmissing = 0;
  work.warn_bad_weight = true;
  work.cats = NULL;
  work.beta_hat = NULL;
  work.hessian = NULL;

  /* Get the initial estimates of \beta and their standard errors.
     And perform other auxiliary initialisation.  */
  if (! initial_pass (cmd, &work, input))
    goto error;

  for (i = 0; i < cmd->n_cat_predictors; ++i)
    {
      if (1 >= categoricals_n_count (work.cats, i))
	{
	  struct string str;
	  ds_init_empty (&str);

	  interaction_to_string (cmd->cat_predictors[i], &str);

	  msg (ME, _("Category %s does not have at least two distinct values. Logistic regression will not be run."),
	       ds_cstr(&str));
	  ds_destroy (&str);
	  goto error;
      	}
    }

  output_depvarmap (cmd, &work);

  case_processing_summary (&work);


  input = casereader_create_filter_missing (input,
					    cmd->indep_vars,
					    cmd->n_indep_vars,
					    cmd->exclude,
					    NULL,
					    NULL);

  input = casereader_create_filter_missing (input,
					    &cmd->dep_var,
					    1,
					    cmd->exclude,
					    NULL,
					    NULL);

  work.hessian = gsl_matrix_calloc (work.beta_hat->size, work.beta_hat->size);

  /* Start the Newton Raphson iteration process... */
  for(i = 0 ; i < cmd->max_iter ; ++i)
    {
      double min, max;
      gsl_vector *v ;


      hessian (cmd, &work, input,
	       cmd->predictor_vars, cmd->n_predictor_vars,
	       &converged);

      gsl_linalg_cholesky_decomp (work.hessian);
      gsl_linalg_cholesky_invert (work.hessian);

      v = xt_times_y_pi (cmd, &work, input,
			 cmd->predictor_vars, cmd->n_predictor_vars,
			 cmd->dep_var,
			 &log_likelihood);

      {
	/* delta = M.v */
	gsl_vector *delta = gsl_vector_alloc (v->size);
	gsl_blas_dgemv (CblasNoTrans, 1.0, work.hessian, v, 0, delta);
	gsl_vector_free (v);


	gsl_vector_add (work.beta_hat, delta);

	gsl_vector_minmax (delta, &min, &max);

	if (fabs (min) < cmd->bcon && fabs (max) < cmd->bcon)
	  {
	    msg (MN, _("Estimation terminated at iteration number %d because parameter estimates changed by less than %g"),
		 i + 1, cmd->bcon);
	    converged = true;
	  }

	gsl_vector_free (delta);
      }

      if (i > 0)
	{
	  if (-log_likelihood > -(1.0 - cmd->lcon) * prev_log_likelihood)
	    {
	      msg (MN, _("Estimation terminated at iteration number %d because Log Likelihood decreased by less than %g%%"), i + 1, 100 * cmd->lcon);
	      converged = true;
	    }
	}
      if (i == 0)
	initial_log_likelihood = log_likelihood;
      prev_log_likelihood = log_likelihood;

      if (converged)
	break;
    }



  if (! converged)
    msg (MW, _("Estimation terminated at iteration number %d because maximum iterations has been reached"), i);


  output_model_summary (&work, initial_log_likelihood, log_likelihood);

  if (work.cats)
    output_categories (cmd, &work);

  output_classification_table (cmd, &work);
  output_variables (cmd, &work);

  casereader_destroy (input);
  gsl_matrix_free (work.hessian);
  gsl_vector_free (work.beta_hat);
  categoricals_destroy (work.cats);

  return true;

 error:
  casereader_destroy (input);
  gsl_matrix_free (work.hessian);
  gsl_vector_free (work.beta_hat);
  categoricals_destroy (work.cats);

  return false;
}

struct variable_node
{
  struct hmap_node node;      /* Node in hash map. */
  const struct variable *var; /* The variable */
};

static struct variable_node *
lookup_variable (const struct hmap *map, const struct variable *var, unsigned int hash)
{
  struct variable_node *vn = NULL;
  HMAP_FOR_EACH_WITH_HASH (vn, struct variable_node, node, hash, map)
    {
      if (vn->var == var)
	break;
    }

  return vn;
}


/* Parse the LOGISTIC REGRESSION command syntax */
int
cmd_logistic (struct lexer *lexer, struct dataset *ds)
{
  int i;
  /* Temporary location for the predictor variables.
     These may or may not include the categorical predictors */
  const struct variable **pred_vars;
  size_t n_pred_vars;
  double cp = 0.5;

  int v, x;
  struct lr_spec lr;
  lr.dict = dataset_dict (ds);
  lr.n_predictor_vars = 0;
  lr.predictor_vars = NULL;
  lr.exclude = MV_ANY;
  lr.wv = dict_get_weight (lr.dict);
  lr.max_iter = 20;
  lr.lcon = 0.0000;
  lr.bcon = 0.001;
  lr.min_epsilon = 0.00000001;
  lr.constant = true;
  lr.confidence = 95;
  lr.print = PRINT_DEFAULT;
  lr.cat_predictors = NULL;
  lr.n_cat_predictors = 0;
  lr.indep_vars = NULL;


  if (lex_match_id (lexer, "VARIABLES"))
    lex_match (lexer, T_EQUALS);

  if (! (lr.dep_var = parse_variable_const (lexer, lr.dict)))
    goto error;

  if (! lex_force_match (lexer, T_WITH))
    goto error;

  if (!parse_variables_const (lexer, lr.dict,
			      &pred_vars, &n_pred_vars,
			      PV_NO_DUPLICATE))
    goto error;


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  lr.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  lr.exclude = MV_ANY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "ORIGIN"))
	{
	  lr.constant = false;
	}
      else if (lex_match_id (lexer, "NOORIGIN"))
	{
	  lr.constant = true;
	}
      else if (lex_match_id (lexer, "NOCONST"))
	{
	  lr.constant = false;
	}
      else if (lex_match_id (lexer, "EXTERNAL"))
	{
	  /* This is for compatibility.  It does nothing */
	}
      else if (lex_match_id (lexer, "CATEGORICAL"))
	{
	  lex_match (lexer, T_EQUALS);
	  do
	    {
	      lr.cat_predictors = xrealloc (lr.cat_predictors,
				  sizeof (*lr.cat_predictors) * ++lr.n_cat_predictors);
	      lr.cat_predictors[lr.n_cat_predictors - 1] = 0;
	    }
	  while (parse_design_interaction (lexer, lr.dict,
					   lr.cat_predictors + lr.n_cat_predictors - 1));
	  lr.n_cat_predictors--;
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "DEFAULT"))
		{
		  lr.print |= PRINT_DEFAULT;
		}
	      else if (lex_match_id (lexer, "SUMMARY"))
		{
		  lr.print |= PRINT_SUMMARY;
		}
#if 0
	      else if (lex_match_id (lexer, "CORR"))
		{
		  lr.print |= PRINT_CORR;
		}
	      else if (lex_match_id (lexer, "ITER"))
		{
		  lr.print |= PRINT_ITER;
		}
	      else if (lex_match_id (lexer, "GOODFIT"))
		{
		  lr.print |= PRINT_GOODFIT;
		}
#endif
	      else if (lex_match_id (lexer, "CI"))
		{
		  lr.print |= PRINT_CI;
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.confidence = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "ALL"))
		{
		  lr.print = ~0x0000;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "BCON"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.bcon = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "ITERATE"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_int_range (lexer, "ITERATE", 0, INT_MAX))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.max_iter = lex_integer (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "LCON"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.lcon = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "EPS"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      lr.min_epsilon = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else if (lex_match_id (lexer, "CUT"))
		{
		  if (lex_force_match (lexer, T_LPAREN))
		    {
		      if (! lex_force_num (lexer))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		      cp = lex_number (lexer);

		      if (cp < 0 || cp > 1.0)
			{
			  msg (ME, _("Cut point value must be in the range [0,1]"));
			  goto error;
			}
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			{
			  lex_error (lexer, NULL);
			  goto error;
			}
		    }
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  lr.ilogit_cut_point = - log (1/cp - 1);


  /* Copy the predictor variables from the temporary location into the
     final one, dropping any categorical variables which appear there.
     FIXME: This is O(NxM).
  */
  {
  struct variable_node *vn, *next;
  struct hmap allvars;
  hmap_init (&allvars);
  for (v = x = 0; v < n_pred_vars; ++v)
    {
      bool drop = false;
      const struct variable *var = pred_vars[v];
      int cv = 0;

      unsigned int hash = hash_pointer (var, 0);
      struct variable_node *vn = lookup_variable (&allvars, var, hash);
      if (vn == NULL)
	{
	  vn = xmalloc (sizeof *vn);
	  vn->var = var;
	  hmap_insert (&allvars, &vn->node,  hash);
	}

      for (cv = 0; cv < lr.n_cat_predictors ; ++cv)
	{
	  int iv;
	  const struct interaction *iact = lr.cat_predictors[cv];
	  for (iv = 0 ; iv < iact->n_vars ; ++iv)
	    {
	      const struct variable *ivar = iact->vars[iv];
	      unsigned int hash = hash_pointer (ivar, 0);
	      struct variable_node *vn = lookup_variable (&allvars, ivar, hash);
	      if (vn == NULL)
		{
		  vn = xmalloc (sizeof *vn);
		  vn->var = ivar;

		  hmap_insert (&allvars, &vn->node,  hash);
		}

	      if (var == ivar)
		{
		  drop = true;
		}
	    }
	}

      if (drop)
	continue;

      lr.predictor_vars = xrealloc (lr.predictor_vars, sizeof *lr.predictor_vars * (x + 1));
      lr.predictor_vars[x++] = var;
      lr.n_predictor_vars++;
    }
  free (pred_vars);

  lr.n_indep_vars = hmap_count (&allvars);
  lr.indep_vars = xmalloc (lr.n_indep_vars * sizeof *lr.indep_vars);

  /* Interate over each variable and push it into the array */
  x = 0;
  HMAP_FOR_EACH_SAFE (vn, next, struct variable_node, node, &allvars)
    {
      lr.indep_vars[x++] = vn->var;
      free (vn);
    }
  hmap_destroy (&allvars);
  }


  /* logistical regression for each split group */
  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), lr.dict);
    while (casegrouper_get_next_group (grouper, &group))
      ok = run_lr (&lr, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  for (i = 0 ; i < lr.n_cat_predictors; ++i)
    {
      interaction_destroy (lr.cat_predictors[i]);
    }
  free (lr.predictor_vars);
  free (lr.cat_predictors);
  free (lr.indep_vars);

  return CMD_SUCCESS;

 error:

  for (i = 0 ; i < lr.n_cat_predictors; ++i)
    {
      interaction_destroy (lr.cat_predictors[i]);
    }
  free (lr.predictor_vars);
  free (lr.cat_predictors);
  free (lr.indep_vars);

  return CMD_FAILURE;
}




/* Show the Dependent Variable Encoding box.
   This indicates how the dependent variable
   is mapped to the internal zero/one values.
*/
static void
output_depvarmap (const struct lr_spec *cmd, const struct lr_result *res)
{
  struct pivot_table *table = pivot_table_create (
    N_("Dependent Variable Encoding"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Mapping"),
                          N_("Internal Value"));

  struct pivot_dimension *original = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Original Value"));
  original->root->show_label = true;

  for (int i = 0; i < 2; i++)
    {
      const union value *v = i ? &res->y1 : &res->y0;
      int orig_idx = pivot_category_create_leaf (
        original->root, pivot_value_new_var_value (cmd->dep_var, v));
      pivot_table_put2 (table, 0, orig_idx, pivot_value_new_number (
                          map_dependent_var (cmd, res, v)));
    }

  pivot_table_submit (table);
}


/* Show the Variables in the Equation box */
static void
output_variables (const struct lr_spec *cmd,
		  const struct lr_result *res)
{
  struct pivot_table *table = pivot_table_create (
    N_("Variables in the Equation"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("B"), PIVOT_RC_OTHER,
    N_("S.E."), PIVOT_RC_OTHER,
    N_("Wald"), PIVOT_RC_OTHER,
    N_("df"), PIVOT_RC_INTEGER,
    N_("Sig."), PIVOT_RC_SIGNIFICANCE,
    N_("Exp(B)"), PIVOT_RC_OTHER);
  if (cmd->print & PRINT_CI)
    {
      struct pivot_category *group = pivot_category_create_group__ (
        statistics->root,
        pivot_value_new_text_format (N_("%d%% CI for Exp(B)"),
                                     cmd->confidence));
      pivot_category_create_leaves (group, N_("Lower"), N_("Upper"));
    }

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));
  struct pivot_category *step1 = pivot_category_create_group (
    variables->root, N_("Step 1"));

  int ivar = 0;
  int idx_correction = 0;
  int i = 0;

  int nr = cmd->n_predictor_vars;
  if (cmd->constant)
    nr++;
  if (res->cats)
    nr += categoricals_df_total (res->cats) + cmd->n_cat_predictors;

  for (int row = 0; row < nr; row++)
    {
      const int idx = row - idx_correction;

      int var_idx;
      if (idx < cmd->n_predictor_vars)
        var_idx = pivot_category_create_leaf (
          step1, pivot_value_new_variable (cmd->predictor_vars[idx]));
      else if (i < cmd->n_cat_predictors)
	{
	  const struct interaction *cat_predictors = cmd->cat_predictors[i];
	  struct string str = DS_EMPTY_INITIALIZER;
	  interaction_to_string (cat_predictors, &str);
	  if (ivar != 0)
            ds_put_format (&str, "(%d)", ivar);
          var_idx = pivot_category_create_leaf (
            step1, pivot_value_new_user_text_nocopy (ds_steal_cstr (&str)));

	  int df = categoricals_df (res->cats, i);
	  bool summary = ivar == 0;
          if (summary)
	    {
	      /* Calculate the Wald statistic,
		 which is \beta' C^-1 \beta .
		 where \beta is the vector of the coefficient estimates comprising this
		 categorial variable. and C is the corresponding submatrix of the
		 hessian matrix.
	      */
	      gsl_matrix_const_view mv =
		gsl_matrix_const_submatrix (res->hessian, idx, idx, df, df);
	      gsl_matrix *subhessian = gsl_matrix_alloc (mv.matrix.size1, mv.matrix.size2);
	      gsl_vector_const_view vv = gsl_vector_const_subvector (res->beta_hat, idx, df);
	      gsl_vector *temp = gsl_vector_alloc (df);

	      gsl_matrix_memcpy (subhessian, &mv.matrix);
	      gsl_linalg_cholesky_decomp (subhessian);
	      gsl_linalg_cholesky_invert (subhessian);

	      gsl_blas_dgemv (CblasTrans, 1.0, subhessian, &vv.vector, 0, temp);
              double wald;
	      gsl_blas_ddot (temp, &vv.vector, &wald);

              double entries[] = { wald, df, gsl_cdf_chisq_Q (wald, df) };
              for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
                pivot_table_put2 (table, j + 2, var_idx,
                                  pivot_value_new_number (entries[j]));

	      idx_correction++;
	      gsl_matrix_free (subhessian);
	      gsl_vector_free (temp);
      	    }

	  if (ivar++ == df)
	    {
	      ++i; /* next interaction */
	      ivar = 0;
	    }

	  if (summary)
	    continue;
	}
      else
        var_idx = pivot_category_create_leaves (step1, N_("Constant"));

      double b = gsl_vector_get (res->beta_hat, idx);
      double sigma2 = gsl_matrix_get (res->hessian, idx, idx);
      double wald = pow2 (b) / sigma2;
      double df = 1;
      double wc = (gsl_cdf_ugaussian_Pinv (0.5 + cmd->confidence / 200.0)
                   * sqrt (sigma2));
      bool show_ci = cmd->print & PRINT_CI && row < nr - cmd->constant;

      double entries[] = {
        b,
        sqrt (sigma2),
        wald,
        df,
        gsl_cdf_chisq_Q (wald, df),
        exp (b),
        show_ci ? exp (b - wc) : SYSMIS,
        show_ci ? exp (b + wc) : SYSMIS,
      };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        if (entries[j] != SYSMIS)
          pivot_table_put2 (table, j, var_idx,
                            pivot_value_new_number (entries[j]));
    }

  pivot_table_submit (table);
}


/* Show the model summary box */
static void
output_model_summary (const struct lr_result *res,
		      double initial_log_likelihood, double log_likelihood)
{
  struct pivot_table *table = pivot_table_create (N_("Model Summary"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("-2 Log likelihood"), PIVOT_RC_OTHER,
                          N_("Cox & Snell R Square"), PIVOT_RC_OTHER,
                          N_("Nagelkerke R Square"), PIVOT_RC_OTHER);

  struct pivot_dimension *step = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Step"));
  step->root->show_label = true;
  pivot_category_create_leaf (step->root, pivot_value_new_integer (1));

  double cox = (1.0 - exp ((initial_log_likelihood - log_likelihood)
                           * (2 / res->cc)));
  double entries[] = {
    -2 * log_likelihood,
    cox,
    cox / (1.0 - exp(initial_log_likelihood * (2 / res->cc)))
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    pivot_table_put2 (table, i, 0, pivot_value_new_number (entries[i]));

  pivot_table_submit (table);
}

/* Show the case processing summary box */
static void
case_processing_summary (const struct lr_result *res)
{
  struct pivot_table *table = pivot_table_create (
    N_("Case Processing Summary"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Percent"), PIVOT_RC_PERCENT);

  struct pivot_dimension *cases = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Unweighted Cases"),
    N_("Included in Analysis"), N_("Missing Cases"), N_("Total"));
  cases->root->show_label = true;

  double total = res->n_nonmissing + res->n_missing;
  struct entry
    {
      int stat_idx;
      int case_idx;
      double x;
    }
  entries[] = {
    { 0, 0, res->n_nonmissing },
    { 0, 1, res->n_missing },
    { 0, 2, total },
    { 1, 0, 100.0 * res->n_nonmissing / total },
    { 1, 1, 100.0 * res->n_missing / total },
    { 1, 2, 100.0 },
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    pivot_table_put2 (table, entries[i].stat_idx, entries[i].case_idx,
                      pivot_value_new_number (entries[i].x));

  pivot_table_submit (table);
}

static void
output_categories (const struct lr_spec *cmd, const struct lr_result *res)
{
  struct pivot_table *table = pivot_table_create (
    N_("Categorical Variables' Codings"));
  pivot_table_set_weight_var (table, dict_get_weight (cmd->dict));

  int max_df = 0;
  int total_cats = 0;
  for (int i = 0; i < cmd->n_cat_predictors; ++i)
    {
      size_t n = categoricals_n_count (res->cats, i);
      size_t df = categoricals_df (res->cats, i);
      if (max_df < df)
	max_df = df;
      total_cats += n;
    }

  struct pivot_dimension *codings = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Codings"),
    N_("Frequency"), PIVOT_RC_COUNT);
  struct pivot_category *coding_group = pivot_category_create_group (
    codings->root, N_("Parameter coding"));
  for (int i = 0; i < max_df; ++i)
    pivot_category_create_leaf_rc (
      coding_group,
      pivot_value_new_user_text_nocopy (xasprintf ("(%d)", i + 1)),
      PIVOT_RC_INTEGER);

  struct pivot_dimension *categories = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Categories"));

  int cumulative_df = 0;
  for (int v = 0; v < cmd->n_cat_predictors; ++v)
    {
      int cat;
      const struct interaction *cat_predictors = cmd->cat_predictors[v];
      int df = categoricals_df (res->cats, v);

      struct string str = DS_EMPTY_INITIALIZER;
      interaction_to_string (cat_predictors, &str);
      struct pivot_category *var_group = pivot_category_create_group__ (
        categories->root,
        pivot_value_new_user_text_nocopy (ds_steal_cstr (&str)));

      for (cat = 0; cat < categoricals_n_count (res->cats, v) ; ++cat)
	{
	  const struct ccase *c = categoricals_get_case_by_category_real (
            res->cats, v, cat);
          struct string label = DS_EMPTY_INITIALIZER;
	  for (int x = 0; x < cat_predictors->n_vars; ++x)
	    {
              if (!ds_is_empty (&label))
                ds_put_byte (&label, ' ');

	      const union value *val = case_data (c, cat_predictors->vars[x]);
	      var_append_value_name (cat_predictors->vars[x], val, &label);
	    }
          int cat_idx = pivot_category_create_leaf (
            var_group,
            pivot_value_new_user_text_nocopy (ds_steal_cstr (&label)));

	  double *freq = categoricals_get_user_data_by_category_real (
            res->cats, v, cat);
          pivot_table_put2 (table, 0, cat_idx, pivot_value_new_number (*freq));

	  for (int x = 0; x < df; ++x)
            pivot_table_put2 (table, x + 1, cat_idx,
                              pivot_value_new_number (cat == x));
	}
      cumulative_df += df;
    }

  pivot_table_submit (table);
}

static void
create_classification_dimension (const struct lr_spec *cmd,
                                 const struct lr_result *res,
                                 struct pivot_table *table,
                                 enum pivot_axis_type axis_type,
                                 const char *label, const char *total)
{
  struct pivot_dimension *d = pivot_dimension_create (
    table, axis_type, label);
  d->root->show_label = true;
  struct pivot_category *pred_group = pivot_category_create_group__ (
    d->root, pivot_value_new_variable (cmd->dep_var));
  for (int i = 0; i < 2; i++)
    {
      const union value *y = i ? &res->y1 : &res->y0;
      pivot_category_create_leaf_rc (
        pred_group, pivot_value_new_var_value (cmd->dep_var, y),
        PIVOT_RC_COUNT);
    }
  pivot_category_create_leaves (d->root, total, PIVOT_RC_PERCENT);
}

static void
output_classification_table (const struct lr_spec *cmd, const struct lr_result *res)
{
  struct pivot_table *table = pivot_table_create (N_("Classification Table"));
  pivot_table_set_weight_var (table, cmd->wv);

  create_classification_dimension (cmd, res, table, PIVOT_AXIS_COLUMN,
                                   N_("Predicted"), N_("Percentage Correct"));
  create_classification_dimension (cmd, res, table, PIVOT_AXIS_ROW,
                                   N_("Observed"), N_("Overall Percentage"));

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Step"), N_("Step 1"));

  struct entry
    {
      int pred_idx;
      int obs_idx;
      double x;
    }
  entries[] = {
    { 0, 0, res->tn },
    { 0, 1, res->fn },
    { 1, 0, res->fp },
    { 1, 1, res->tp },
    { 2, 0, 100 * res->tn / (res->tn + res->fp) },
    { 2, 1, 100 * res->tp / (res->tp + res->fn) },
    { 2, 2,
      100 * (res->tp + res->tn) / (res->tp  + res->tn + res->fp + res->fn)},
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    {
      const struct entry *e = &entries[i];
      pivot_table_put3 (table, e->pred_idx, e->obs_idx, 0,
                        pivot_value_new_number (e->x));
    }

  pivot_table_submit (table);
}
