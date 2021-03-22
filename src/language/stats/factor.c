/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2014, 2015,
   2016, 2017 Free Software Foundation, Inc.

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

#include <gsl/gsl_vector.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_cdf.h>

#include "data/any-reader.h"
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
#include "language/data-io/file-handle.h"
#include "language/data-io/matrix-reader.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/correlation.h"
#include "math/covariance.h"
#include "math/moments.h"
#include "output/charts/scree.h"
#include "output/pivot-table.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum method
  {
    METHOD_CORR,
    METHOD_COV
  };

enum missing_type
  {
    MISS_LISTWISE,
    MISS_PAIRWISE,
    MISS_MEANSUB,
  };

enum extraction_method
  {
    EXTRACTION_PC,
    EXTRACTION_PAF,
  };

enum plot_opts
  {
    PLOT_SCREE = 0x0001,
    PLOT_ROTATION = 0x0002
  };

enum print_opts
  {
    PRINT_UNIVARIATE  = 0x0001,
    PRINT_DETERMINANT = 0x0002,
    PRINT_INV         = 0x0004,
    PRINT_AIC         = 0x0008,
    PRINT_SIG         = 0x0010,
    PRINT_COVARIANCE  = 0x0020,
    PRINT_CORRELATION = 0x0040,
    PRINT_ROTATION    = 0x0080,
    PRINT_EXTRACTION  = 0x0100,
    PRINT_INITIAL     = 0x0200,
    PRINT_KMO         = 0x0400,
    PRINT_REPR        = 0x0800,
    PRINT_FSCORE      = 0x1000
  };

enum rotation_type
  {
    ROT_VARIMAX = 0,
    ROT_EQUAMAX,
    ROT_QUARTIMAX,
    ROT_PROMAX,
    ROT_NONE
  };

typedef void (*rotation_coefficients) (double *x, double *y,
				    double a, double b, double c, double d,
				    const gsl_matrix *loadings);


static void
varimax_coefficients (double *x, double *y,
		      double a, double b, double c, double d,
		      const gsl_matrix *loadings)
{
  *x = d - 2 * a * b / loadings->size1;
  *y = c - (a * a - b * b) / loadings->size1;
}

static void
equamax_coefficients (double *x, double *y,
		      double a, double b, double c, double d,
		      const gsl_matrix *loadings)
{
  *x = d - loadings->size2 * a * b / loadings->size1;
  *y = c - loadings->size2 * (a * a - b * b) / (2 * loadings->size1);
}

static void
quartimax_coefficients (double *x, double *y,
		      double a UNUSED, double b UNUSED, double c, double d,
		      const gsl_matrix *loadings UNUSED)
{
  *x = d ;
  *y = c ;
}

static const rotation_coefficients rotation_coeff[] = {
  varimax_coefficients,
  equamax_coefficients,
  quartimax_coefficients,
  varimax_coefficients  /* PROMAX is identical to VARIMAX */
};


/* return diag (C'C) ^ {-0.5} */
static gsl_matrix *
diag_rcp_sqrt (const gsl_matrix *C)
{
  int j;
  gsl_matrix *d =  gsl_matrix_calloc (C->size1, C->size2);
  gsl_matrix *r =  gsl_matrix_calloc (C->size1, C->size2);

  assert (C->size1 == C->size2);

  gsl_linalg_matmult_mod (C,  GSL_LINALG_MOD_TRANSPOSE,
			  C,  GSL_LINALG_MOD_NONE,
			  d);

  for (j = 0 ; j < d->size2; ++j)
    {
      double e = gsl_matrix_get (d, j, j);
      e = 1.0 / sqrt (e);
      gsl_matrix_set (r, j, j, e);
    }

  gsl_matrix_free (d);

  return r;
}



/* return diag ((C'C)^-1) ^ {-0.5} */
static gsl_matrix *
diag_rcp_inv_sqrt (const gsl_matrix *CCinv)
{
  int j;
  gsl_matrix *r =  gsl_matrix_calloc (CCinv->size1, CCinv->size2);

  assert (CCinv->size1 == CCinv->size2);

  for (j = 0 ; j < CCinv->size2; ++j)
    {
      double e = gsl_matrix_get (CCinv, j, j);
      e = 1.0 / sqrt (e);
      gsl_matrix_set (r, j, j, e);
    }

  return r;
}





struct cmd_factor
{
  size_t n_vars;
  const struct variable **vars;

  const struct variable *wv;

  enum method method;
  enum missing_type missing_type;
  enum mv_class exclude;
  enum print_opts print;
  enum extraction_method extraction;
  enum plot_opts plot;
  enum rotation_type rotation;
  int rotation_iterations;
  int promax_power;

  /* Extraction Criteria */
  int n_factors;
  double min_eigen;
  double econverge;
  int extraction_iterations;

  double rconverge;

  /* Format */
  double blank;
  bool sort;
};


struct idata
{
  /* Intermediate values used in calculation */
  struct matrix_material mm;

  gsl_matrix *analysis_matrix; /* A pointer to either mm.corr or mm.cov */

  gsl_vector *eval ;  /* The eigenvalues */
  gsl_matrix *evec ;  /* The eigenvectors */

  int n_extractions;

  gsl_vector *msr ;  /* Multiple Squared Regressions */

  double detR;  /* The determinant of the correlation matrix */

  gsl_matrix *ai_cov; /* The anti-image covariance matrix */
  gsl_matrix *ai_cor; /* The anti-image correlation matrix */
  struct covariance *cvm;
};

static struct idata *
idata_alloc (size_t n_vars)
{
  struct idata *id = xzalloc (sizeof (*id));

  id->n_extractions = 0;
  id->msr = gsl_vector_alloc (n_vars);

  id->eval = gsl_vector_alloc (n_vars);
  id->evec = gsl_matrix_alloc (n_vars, n_vars);

  return id;
}

static void
idata_free (struct idata *id)
{
  gsl_vector_free (id->msr);
  gsl_vector_free (id->eval);
  gsl_matrix_free (id->evec);
  gsl_matrix_free (id->ai_cov);
  gsl_matrix_free (id->ai_cor);

  free (id);
}

/* Return the sum of squares of all the elements in row J excluding column J */
static double
ssq_row_od_n (const gsl_matrix *m, int j)
{
  int i;
  double ss = 0;
  assert (m->size1 == m->size2);

  assert (j < m->size1);

  for (i = 0; i < m->size1; ++i)
    {
      if (i == j) continue;
      ss += pow2 (gsl_matrix_get (m, i, j));
    }

  return ss;
}

/* Return the sum of squares of all the elements excluding row N */
static double
ssq_od_n (const gsl_matrix *m, int n)
{
  int i, j;
  double ss = 0;
  assert (m->size1 == m->size2);

  assert (n < m->size1);

  for (i = 0; i < m->size1; ++i)
    {
      for (j = 0; j < m->size2; ++j)
	{
	  if (i == j) continue;
	  ss += pow2 (gsl_matrix_get (m, i, j));
	}
    }

  return ss;
}


static gsl_matrix *
anti_image_corr (const gsl_matrix *m, const struct idata *idata)
{
  int i, j;
  gsl_matrix *a;
  assert (m->size1 == m->size2);

  a = gsl_matrix_alloc (m->size1, m->size2);

  for (i = 0; i < m->size1; ++i)
    {
      for (j = 0; j < m->size2; ++j)
        {
          double *p = gsl_matrix_ptr (a, i, j);
          *p = gsl_matrix_get (m, i, j);
          *p /= sqrt (gsl_matrix_get (m, i, i) *
                      gsl_matrix_get (m, j, j));
        }
    }

  for (i = 0; i < m->size1; ++i)
    {
      double r = ssq_row_od_n (idata->mm.corr, i);
      double u = ssq_row_od_n (a, i);
      gsl_matrix_set (a, i, i, r / (r + u));
    }

  return a;
}

static gsl_matrix *
anti_image_cov (const gsl_matrix *m)
{
  int i, j;
  gsl_matrix *a;
  assert (m->size1 == m->size2);

  a = gsl_matrix_alloc (m->size1, m->size2);

  for (i = 0; i < m->size1; ++i)
    {
      for (j = 0; j < m->size2; ++j)
	{
	  double *p = gsl_matrix_ptr (a, i, j);
	  *p = gsl_matrix_get (m, i, j);
	  *p /= gsl_matrix_get (m, i, i);
	  *p /= gsl_matrix_get (m, j, j);
	}
    }

  return a;
}

#if 0
static void
dump_matrix (const gsl_matrix *m)
{
  size_t i, j;

  for (i = 0 ; i < m->size1; ++i)
    {
      for (j = 0 ; j < m->size2; ++j)
	printf ("%02f ", gsl_matrix_get (m, i, j));
      printf ("\n");
    }
}

static void
dump_matrix_permute (const gsl_matrix *m, const gsl_permutation *p)
{
  size_t i, j;

  for (i = 0 ; i < m->size1; ++i)
    {
      for (j = 0 ; j < m->size2; ++j)
	printf ("%02f ", gsl_matrix_get (m, gsl_permutation_get (p, i), j));
      printf ("\n");
    }
}


static void
dump_vector (const gsl_vector *v)
{
  size_t i;
  for (i = 0 ; i < v->size; ++i)
    {
      printf ("%02f\n", gsl_vector_get (v, i));
    }
  printf ("\n");
}
#endif


static int
n_extracted_factors (const struct cmd_factor *factor, struct idata *idata)
{
  int i;

  /* If there is a cached value, then return that. */
  if (idata->n_extractions != 0)
    return idata->n_extractions;

  /* Otherwise, if the number of factors has been explicitly requested,
     use that. */
  if (factor->n_factors > 0)
    {
      idata->n_extractions = factor->n_factors;
      goto finish;
    }

  /* Use the MIN_EIGEN setting. */
  for (i = 0 ; i < idata->eval->size; ++i)
    {
      double evali = fabs (gsl_vector_get (idata->eval, i));

      idata->n_extractions = i;

      if (evali < factor->min_eigen)
	goto finish;
    }

 finish:
  return idata->n_extractions;
}


/* Returns a newly allocated matrix identical to M.
   It it the callers responsibility to free the returned value.
*/
static gsl_matrix *
matrix_dup (const gsl_matrix *m)
{
  gsl_matrix *n =  gsl_matrix_alloc (m->size1, m->size2);

  gsl_matrix_memcpy (n, m);

  return n;
}


struct smr_workspace
{
  /* Copy of the subject */
  gsl_matrix *m;

  gsl_matrix *inverse;

  gsl_permutation *perm;

  gsl_matrix *result1;
  gsl_matrix *result2;
};


static struct smr_workspace *ws_create (const gsl_matrix *input)
{
  struct smr_workspace *ws = xmalloc (sizeof (*ws));

  ws->m = gsl_matrix_alloc (input->size1, input->size2);
  ws->inverse = gsl_matrix_calloc (input->size1 - 1, input->size2 - 1);
  ws->perm = gsl_permutation_alloc (input->size1 - 1);
  ws->result1 = gsl_matrix_calloc (input->size1 - 1, 1);
  ws->result2 = gsl_matrix_calloc (1, 1);

  return ws;
}

static void
ws_destroy (struct smr_workspace *ws)
{
  gsl_matrix_free (ws->result2);
  gsl_matrix_free (ws->result1);
  gsl_permutation_free (ws->perm);
  gsl_matrix_free (ws->inverse);
  gsl_matrix_free (ws->m);

  free (ws);
}


/*
   Return the square of the regression coefficient for VAR regressed against all other variables.
 */
static double
squared_multiple_correlation (const gsl_matrix *corr, int var, struct smr_workspace *ws)
{
  /* For an explanation of what this is doing, see
     http://www.visualstatistics.net/Visual%20Statistics%20Multimedia/multiple_regression_analysis.htm
  */

  int signum = 0;
  gsl_matrix_view rxx;

  gsl_matrix_memcpy (ws->m, corr);

  gsl_matrix_swap_rows (ws->m, 0, var);
  gsl_matrix_swap_columns (ws->m, 0, var);

  rxx = gsl_matrix_submatrix (ws->m, 1, 1, ws->m->size1 - 1, ws->m->size1 - 1);

  gsl_linalg_LU_decomp (&rxx.matrix, ws->perm, &signum);

  gsl_linalg_LU_invert (&rxx.matrix, ws->perm, ws->inverse);

  {
    gsl_matrix_const_view rxy = gsl_matrix_const_submatrix (ws->m, 1, 0, ws->m->size1 - 1, 1);
    gsl_matrix_const_view ryx = gsl_matrix_const_submatrix (ws->m, 0, 1, 1, ws->m->size1 - 1);

    gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans,
		    1.0, ws->inverse, &rxy.matrix, 0.0, ws->result1);

    gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans,
		    1.0, &ryx.matrix, ws->result1, 0.0, ws->result2);
  }

  return gsl_matrix_get (ws->result2, 0, 0);
}



static double the_communality (const gsl_matrix *evec, const gsl_vector *eval, int n, int n_factors);


struct factor_matrix_workspace
{
  size_t n_factors;
  gsl_eigen_symmv_workspace *eigen_ws;

  gsl_vector *eval ;
  gsl_matrix *evec ;

  gsl_matrix *gamma ;

  gsl_matrix *r;
};

static struct factor_matrix_workspace *
factor_matrix_workspace_alloc (size_t n, size_t nf)
{
  struct factor_matrix_workspace *ws = xmalloc (sizeof (*ws));

  ws->n_factors = nf;
  ws->gamma = gsl_matrix_calloc (nf, nf);
  ws->eigen_ws = gsl_eigen_symmv_alloc (n);
  ws->eval = gsl_vector_alloc (n);
  ws->evec = gsl_matrix_alloc (n, n);
  ws->r  = gsl_matrix_alloc (n, n);

  return ws;
}

static void
factor_matrix_workspace_free (struct factor_matrix_workspace *ws)
{
  gsl_eigen_symmv_free (ws->eigen_ws);
  gsl_vector_free (ws->eval);
  gsl_matrix_free (ws->evec);
  gsl_matrix_free (ws->gamma);
  gsl_matrix_free (ws->r);
  free (ws);
}

/*
  Shift P left by OFFSET places, and overwrite TARGET
  with the shifted result.
  Positions in TARGET less than OFFSET are unchanged.
*/
static void
perm_shift_apply (gsl_permutation *target, const gsl_permutation *p,
		  size_t offset)
{
  size_t i;
  assert (target->size == p->size);
  assert (offset <= target->size);

  for (i = 0; i < target->size - offset; ++i)
    {
      target->data[i] = p->data [i + offset];
    }
}


/*
   Indirectly sort the rows of matrix INPUT, storing the sort order in PERM.
   The sort criteria are as follows:

   Rows are sorted on the first column, until the absolute value of an
   element in a subsequent column  is greater than that of the first
   column.  Thereafter, rows will be sorted on the second column,
   until the absolute value of an element in a subsequent column
   exceeds that of the second column ...
*/
static void
sort_matrix_indirect (const gsl_matrix *input, gsl_permutation *perm)
{
  const size_t n = perm->size;
  const size_t m = input->size2;
  int i, j;
  gsl_matrix *mat ;
  int column_n = 0;
  int row_n = 0;
  gsl_permutation *p;

  assert (perm->size == input->size1);

  p = gsl_permutation_alloc (n);

  /* Copy INPUT into MAT, discarding the sign */
  mat = gsl_matrix_alloc (n, m);
  for (i = 0 ; i < mat->size1; ++i)
    {
      for (j = 0 ; j < mat->size2; ++j)
	{
	  double x = gsl_matrix_get (input, i, j);
	  gsl_matrix_set (mat, i, j, fabs (x));
	}
    }

  while (column_n < m && row_n < n)
    {
      gsl_vector_const_view columni = gsl_matrix_const_column (mat, column_n);
      gsl_sort_vector_index (p, &columni.vector);

      for (i = 0 ; i < n; ++i)
	{
	  gsl_vector_view row = gsl_matrix_row (mat, p->data[n - 1 - i]);
	  size_t maxindex = gsl_vector_max_index (&row.vector);

	  if (maxindex > column_n)
	    break;

	  /* All subsequent elements of this row, are of no interest.
	     So set them all to a highly negative value */
	  for (j = column_n + 1; j < row.vector.size ; ++j)
	    gsl_vector_set (&row.vector, j, -DBL_MAX);
	}

      perm_shift_apply (perm, p, row_n);
      row_n += i;

      column_n++;
    }

  gsl_permutation_free (p);
  gsl_matrix_free (mat);

  assert (0 == gsl_permutation_valid (perm));

  /* We want the biggest value to be first */
  gsl_permutation_reverse (perm);
}


static void
drot_go (double phi, double *l0, double *l1)
{
  double r0 = cos (phi) * *l0 + sin (phi) * *l1;
  double r1 = - sin (phi) * *l0 + cos (phi) * *l1;

  *l0 = r0;
  *l1 = r1;
}


static gsl_matrix *
clone_matrix (const gsl_matrix *m)
{
  int j, k;
  gsl_matrix *c = gsl_matrix_calloc (m->size1, m->size2);

  for (j = 0 ; j < c->size1; ++j)
    {
      for (k = 0 ; k < c->size2; ++k)
	{
	  const double *v = gsl_matrix_const_ptr (m, j, k);
	  gsl_matrix_set (c, j, k, *v);
	}
    }

  return c;
}


static double
initial_sv (const gsl_matrix *fm)
{
  int j, k;

  double sv = 0.0;
  for (j = 0 ; j < fm->size2; ++j)
    {
      double l4s = 0;
      double l2s = 0;

      for (k = j + 1 ; k < fm->size2; ++k)
	{
	  double lambda = gsl_matrix_get (fm, k, j);
	  double lambda_sq = lambda * lambda;
	  double lambda_4 = lambda_sq * lambda_sq;

	  l4s += lambda_4;
	  l2s += lambda_sq;
	}
      sv += (fm->size1 * l4s - (l2s * l2s)) / (fm->size1 * fm->size1);
    }
  return sv;
}

static void
rotate (const struct cmd_factor *cf, const gsl_matrix *unrot,
	const gsl_vector *communalities,
	gsl_matrix *result,
	gsl_vector *rotated_loadings,
	gsl_matrix *pattern_matrix,
	gsl_matrix *factor_correlation_matrix
	)
{
  int j, k;
  int i;
  double prev_sv;

  /* First get a normalised version of UNROT */
  gsl_matrix *normalised = gsl_matrix_calloc (unrot->size1, unrot->size2);
  gsl_matrix *h_sqrt = gsl_matrix_calloc (communalities->size, communalities->size);
  gsl_matrix *h_sqrt_inv ;

  /* H is the diagonal matrix containing the absolute values of the communalities */
  for (i = 0 ; i < communalities->size ; ++i)
    {
      double *ptr = gsl_matrix_ptr (h_sqrt, i, i);
      *ptr = fabs (gsl_vector_get (communalities, i));
    }

  /* Take the square root of the communalities */
  gsl_linalg_cholesky_decomp (h_sqrt);


  /* Save a copy of h_sqrt and invert it */
  h_sqrt_inv = clone_matrix (h_sqrt);
  gsl_linalg_cholesky_decomp (h_sqrt_inv);
  gsl_linalg_cholesky_invert (h_sqrt_inv);

  /* normalised vertion is H^{1/2} x UNROT */
  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0, h_sqrt_inv, unrot, 0.0, normalised);

  gsl_matrix_free (h_sqrt_inv);


  /* Now perform the rotation iterations */

  prev_sv = initial_sv (normalised);
  for (i = 0 ; i < cf->rotation_iterations ; ++i)
    {
      double sv = 0.0;
      for (j = 0 ; j < normalised->size2; ++j)
	{
	  /* These variables relate to the convergence criterium */
	  double l4s = 0;
	  double l2s = 0;

	  for (k = j + 1 ; k < normalised->size2; ++k)
	    {
	      int p;
	      double a = 0.0;
	      double b = 0.0;
	      double c = 0.0;
	      double d = 0.0;
	      double x, y;
	      double phi;

	      for (p = 0; p < normalised->size1; ++p)
		{
		  double jv = gsl_matrix_get (normalised, p, j);
		  double kv = gsl_matrix_get (normalised, p, k);

		  double u = jv * jv - kv * kv;
		  double v = 2 * jv * kv;
		  a += u;
		  b += v;
		  c +=  u * u - v * v;
		  d += 2 * u * v;
		}

	      rotation_coeff [cf->rotation] (&x, &y, a, b, c, d, normalised);

	      phi = atan2 (x,  y) / 4.0 ;

	      /* Don't bother rotating if the angle is small */
	      if (fabs (sin (phi)) <= pow (10.0, -15.0))
		  continue;

	      for (p = 0; p < normalised->size1; ++p)
		{
		  double *lambda0 = gsl_matrix_ptr (normalised, p, j);
		  double *lambda1 = gsl_matrix_ptr (normalised, p, k);
		  drot_go (phi, lambda0, lambda1);
		}

	      /* Calculate the convergence criterium */
	      {
		double lambda = gsl_matrix_get (normalised, k, j);
		double lambda_sq = lambda * lambda;
		double lambda_4 = lambda_sq * lambda_sq;

		l4s += lambda_4;
		l2s += lambda_sq;
	      }
	    }
	  sv += (normalised->size1 * l4s - (l2s * l2s)) / (normalised->size1 * normalised->size1);
	}

      if (fabs (sv - prev_sv) <= cf->rconverge)
	break;

      prev_sv = sv;
    }

  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0,
		  h_sqrt, normalised,  0.0,   result);

  gsl_matrix_free (h_sqrt);
  gsl_matrix_free (normalised);

  if (cf->rotation == ROT_PROMAX)
    {
      /* general purpose m by m matrix, where m is the number of factors */
      gsl_matrix *mm1 =  gsl_matrix_calloc (unrot->size2, unrot->size2);
      gsl_matrix *mm2 =  gsl_matrix_calloc (unrot->size2, unrot->size2);

      /* general purpose m by p matrix, where p is the number of variables */
      gsl_matrix *mp1 =  gsl_matrix_calloc (unrot->size2, unrot->size1);

      gsl_matrix *pm1 =  gsl_matrix_calloc (unrot->size1, unrot->size2);

      gsl_permutation *perm = gsl_permutation_alloc (unrot->size2);

      int signum;

      int i, j;

      /* The following variables follow the notation by SPSS Statistical Algorithms
	 page 342 */
      gsl_matrix *L =  gsl_matrix_calloc (unrot->size2, unrot->size2);
      gsl_matrix *P = clone_matrix (result);
      gsl_matrix *D ;
      gsl_matrix *Q ;


      /* Vector of length p containing (indexed by i)
	 \Sum^m_j {\lambda^2_{ij}} */
      gsl_vector *rssq = gsl_vector_calloc (unrot->size1);

      for (i = 0; i < P->size1; ++i)
	{
	  double sum = 0;
	  for (j = 0; j < P->size2; ++j)
	    {
	      sum += gsl_matrix_get (result, i, j)
		* gsl_matrix_get (result, i, j);

	    }

	  gsl_vector_set (rssq, i, sqrt (sum));
	}

      for (i = 0; i < P->size1; ++i)
	{
	  for (j = 0; j < P->size2; ++j)
	    {
	      double l = gsl_matrix_get (result, i, j);
	      double r = gsl_vector_get (rssq, i);
	      gsl_matrix_set (P, i, j, pow (fabs (l / r), cf->promax_power + 1) * r / l);
	    }
	}

      gsl_vector_free (rssq);

      gsl_linalg_matmult_mod (result,
			      GSL_LINALG_MOD_TRANSPOSE,
			      result,
			      GSL_LINALG_MOD_NONE,
			      mm1);

      gsl_linalg_LU_decomp (mm1, perm, &signum);
      gsl_linalg_LU_invert (mm1, perm, mm2);

      gsl_linalg_matmult_mod (mm2,   GSL_LINALG_MOD_NONE,
			      result,  GSL_LINALG_MOD_TRANSPOSE,
			      mp1);

      gsl_linalg_matmult_mod (mp1, GSL_LINALG_MOD_NONE,
			      P,   GSL_LINALG_MOD_NONE,
			      L);

      D = diag_rcp_sqrt (L);
      Q = gsl_matrix_calloc (unrot->size2, unrot->size2);

      gsl_linalg_matmult_mod (L, GSL_LINALG_MOD_NONE,
			      D, GSL_LINALG_MOD_NONE,
			      Q);

      gsl_matrix *QQinv = gsl_matrix_calloc (unrot->size2, unrot->size2);

      gsl_linalg_matmult_mod (Q, GSL_LINALG_MOD_TRANSPOSE,
			      Q,  GSL_LINALG_MOD_NONE,
			      QQinv);

      gsl_linalg_cholesky_decomp (QQinv);
      gsl_linalg_cholesky_invert (QQinv);


      gsl_matrix *C = diag_rcp_inv_sqrt (QQinv);
      gsl_matrix *Cinv =  clone_matrix (C);

      gsl_linalg_cholesky_decomp (Cinv);
      gsl_linalg_cholesky_invert (Cinv);


      gsl_linalg_matmult_mod (result, GSL_LINALG_MOD_NONE,
			      Q,      GSL_LINALG_MOD_NONE,
			      pm1);

      gsl_linalg_matmult_mod (pm1,      GSL_LINALG_MOD_NONE,
			      Cinv,         GSL_LINALG_MOD_NONE,
			      pattern_matrix);


      gsl_linalg_matmult_mod (C,      GSL_LINALG_MOD_NONE,
			      QQinv,  GSL_LINALG_MOD_NONE,
			      mm1);

      gsl_linalg_matmult_mod (mm1,      GSL_LINALG_MOD_NONE,
			      C,  GSL_LINALG_MOD_TRANSPOSE,
			      factor_correlation_matrix);

      gsl_linalg_matmult_mod (pattern_matrix,      GSL_LINALG_MOD_NONE,
			      factor_correlation_matrix,  GSL_LINALG_MOD_NONE,
			      pm1);

      gsl_matrix_memcpy (result, pm1);


      gsl_matrix_free (QQinv);
      gsl_matrix_free (C);
      gsl_matrix_free (Cinv);

      gsl_matrix_free (D);
      gsl_matrix_free (Q);
      gsl_matrix_free (L);
      gsl_matrix_free (P);

      gsl_permutation_free (perm);

      gsl_matrix_free (mm1);
      gsl_matrix_free (mm2);
      gsl_matrix_free (mp1);
      gsl_matrix_free (pm1);
    }


  /* reflect negative sums and populate the rotated loadings vector*/
  for (i = 0 ; i < result->size2; ++i)
    {
      double ssq = 0.0;
      double sum = 0.0;
      for (j = 0 ; j < result->size1; ++j)
	{
	  double s = gsl_matrix_get (result, j, i);
	  ssq += s * s;
	  sum += s;
	}

      gsl_vector_set (rotated_loadings, i, ssq);

      if (sum < 0)
	for (j = 0 ; j < result->size1; ++j)
	  {
	    double *lambda = gsl_matrix_ptr (result, j, i);
	    *lambda = - *lambda;
	  }
    }
}


/*
  Get an approximation for the factor matrix into FACTORS, and the communalities into COMMUNALITIES.
  R is the matrix to be analysed.
  WS is a pointer to a structure which must have been initialised with factor_matrix_workspace_init.
 */
static void
iterate_factor_matrix (const gsl_matrix *r, gsl_vector *communalities, gsl_matrix *factors,
		       struct factor_matrix_workspace *ws)
{
  size_t i;
  gsl_matrix_view mv ;

  assert (r->size1 == r->size2);
  assert (r->size1 == communalities->size);

  assert (factors->size1 == r->size1);
  assert (factors->size2 == ws->n_factors);

  gsl_matrix_memcpy (ws->r, r);

  /* Apply Communalities to diagonal of correlation matrix */
  for (i = 0 ; i < communalities->size ; ++i)
    {
      double *x = gsl_matrix_ptr (ws->r, i, i);
      *x = gsl_vector_get (communalities, i);
    }

  gsl_eigen_symmv (ws->r, ws->eval, ws->evec, ws->eigen_ws);

  mv = gsl_matrix_submatrix (ws->evec, 0, 0, ws->evec->size1, ws->n_factors);

  /* Gamma is the diagonal matrix containing the absolute values of the eigenvalues */
  for (i = 0 ; i < ws->n_factors ; ++i)
    {
      double *ptr = gsl_matrix_ptr (ws->gamma, i, i);
      *ptr = fabs (gsl_vector_get (ws->eval, i));
    }

  /* Take the square root of gamma */
  gsl_linalg_cholesky_decomp (ws->gamma);

  gsl_blas_dgemm (CblasNoTrans,  CblasNoTrans, 1.0, &mv.matrix, ws->gamma, 0.0, factors);

  for (i = 0 ; i < r->size1 ; ++i)
    {
      double h = the_communality (ws->evec, ws->eval, i, ws->n_factors);
      gsl_vector_set (communalities, i, h);
    }
}



static bool run_factor (struct dataset *ds, const struct cmd_factor *factor);

static void do_factor_by_matrix (const struct cmd_factor *factor, struct idata *idata);



int
cmd_factor (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = NULL;
  int n_iterations = 25;
  struct cmd_factor factor;
  factor.n_vars = 0;
  factor.vars = NULL;
  factor.method = METHOD_CORR;
  factor.missing_type = MISS_LISTWISE;
  factor.exclude = MV_ANY;
  factor.print = PRINT_INITIAL | PRINT_EXTRACTION | PRINT_ROTATION;
  factor.extraction = EXTRACTION_PC;
  factor.n_factors = 0;
  factor.min_eigen = SYSMIS;
  factor.extraction_iterations = 25;
  factor.rotation_iterations = 25;
  factor.econverge = 0.001;

  factor.blank = 0;
  factor.sort = false;
  factor.plot = 0;
  factor.rotation = ROT_VARIMAX;
  factor.wv = NULL;

  factor.rconverge = 0.0001;

  lex_match (lexer, T_SLASH);

  struct matrix_reader *mr = NULL;
  struct casereader *matrix_reader = NULL;

  if (lex_match_id (lexer, "VARIABLES"))
    {
      lex_match (lexer, T_EQUALS);
      dict = dataset_dict (ds);
      factor.wv = dict_get_weight (dict);

      if (!parse_variables_const (lexer, dict, &factor.vars, &factor.n_vars,
				  PV_NO_DUPLICATE | PV_NUMERIC))
	goto error;
    }
  else if (lex_match_id (lexer, "MATRIX"))
    {
      lex_match (lexer, T_EQUALS);
      if (! lex_force_match_id (lexer, "IN"))
	goto error;
      if (!lex_force_match (lexer, T_LPAREN))
	{
	  goto error;
	}
      if (lex_match_id (lexer, "CORR"))
	{
	}
      else if (lex_match_id (lexer, "COV"))
	{
	}
      else
	{
	  lex_error (lexer, _("Matrix input for %s must be either COV or CORR"), "FACTOR");
	  goto error;
	}
      if (! lex_force_match (lexer, T_EQUALS))
	goto error;
      if (lex_match (lexer, T_ASTERISK))
	{
	  dict = dataset_dict (ds);
	  matrix_reader = casereader_clone (dataset_source (ds));
	}
      else
	{
	  struct file_handle *fh = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (fh == NULL)
	    goto error;

	  matrix_reader
	    = any_reader_open_and_decode (fh, NULL, &dict, NULL);

	  if (! (matrix_reader && dict))
	    {
	      goto error;
	    }
	}

      if (! lex_force_match (lexer, T_RPAREN))
	goto error;

      mr = create_matrix_reader_from_case_reader (dict, matrix_reader,
						  &factor.vars, &factor.n_vars);
    }
  else
    {
      goto error;
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "ANALYSIS"))
        {
          struct const_var_set *vs;
          const struct variable **vars;
          size_t n_vars;
          bool ok;

          lex_match (lexer, T_EQUALS);

          vs = const_var_set_create_from_array (factor.vars, factor.n_vars);
          ok = parse_const_var_set_vars (lexer, vs, &vars, &n_vars,
                                         PV_NO_DUPLICATE | PV_NUMERIC);
          const_var_set_destroy (vs);

          if (!ok)
            goto error;

          free (factor.vars);
          factor.vars = vars;
          factor.n_vars = n_vars;
        }
      else if (lex_match_id (lexer, "PLOT"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "EIGEN"))
		{
		  factor.plot |= PLOT_SCREE;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "ROTATION"))
		{
		}
#endif
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "METHOD"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "COVARIANCE"))
		{
		  factor.method = METHOD_COV;
		}
	      else if (lex_match_id (lexer, "CORRELATION"))
		{
		  factor.method = METHOD_CORR;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "ROTATION"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      /* VARIMAX and DEFAULT are defaults */
	      if (lex_match_id (lexer, "VARIMAX") || lex_match_id (lexer, "DEFAULT"))
		{
		  factor.rotation = ROT_VARIMAX;
		}
	      else if (lex_match_id (lexer, "EQUAMAX"))
		{
		  factor.rotation = ROT_EQUAMAX;
		}
	      else if (lex_match_id (lexer, "QUARTIMAX"))
		{
		  factor.rotation = ROT_QUARTIMAX;
		}
	      else if (lex_match_id (lexer, "PROMAX"))
		{
		  factor.promax_power = 5;
		  if (lex_match (lexer, T_LPAREN)
                      && lex_force_int (lexer))
		    {
		      factor.promax_power = lex_integer (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		  factor.rotation = ROT_PROMAX;
		}
	      else if (lex_match_id (lexer, "NOROTATE"))
		{
		  factor.rotation = ROT_NONE;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
          factor.rotation_iterations = n_iterations;
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "FACTORS"))
		{
		  if (lex_force_match (lexer, T_LPAREN)
                       && lex_force_int (lexer))
		    {
		      factor.n_factors = lex_integer (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "MINEIGEN"))
		{
		  if (lex_force_match (lexer, T_LPAREN)
                       && lex_force_num (lexer))
		    {
		      factor.min_eigen = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "ECONVERGE"))
		{
		  if (lex_force_match (lexer, T_LPAREN)
                       && lex_force_num (lexer))
		    {
		      factor.econverge = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "RCONVERGE"))
                {
                  if (lex_force_match (lexer, T_LPAREN)
                      && lex_force_num (lexer))
                    {
                      factor.rconverge = lex_number (lexer);
                      lex_get (lexer);
                      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
                    }
		}
	      else if (lex_match_id (lexer, "ITERATE"))
		{
		  if (lex_force_match (lexer, T_LPAREN)
                      && lex_force_int_range (lexer, "ITERATE", 0, INT_MAX))
		    {
		      n_iterations = lex_integer (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.n_factors = 0;
		  factor.min_eigen = 1;
		  n_iterations = 25;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "EXTRACTION"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "PAF"))
		{
		  factor.extraction = EXTRACTION_PAF;
		}
	      else if (lex_match_id (lexer, "PC"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else if (lex_match_id (lexer, "PA1"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.extraction = EXTRACTION_PC;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
          factor.extraction_iterations = n_iterations;
	}
      else if (lex_match_id (lexer, "FORMAT"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "SORT"))
		{
		  factor.sort = true;
		}
	      else if (lex_match_id (lexer, "BLANK"))
		{
		  if (lex_force_match (lexer, T_LPAREN)
                       && lex_force_num (lexer))
		    {
		      factor.blank = lex_number (lexer);
		      lex_get (lexer);
		      if (! lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.blank = 0;
		  factor.sort = false;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  factor.print = 0;
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "UNIVARIATE"))
		{
		  factor.print |= PRINT_UNIVARIATE;
		}
	      else if (lex_match_id (lexer, "DET"))
		{
		  factor.print |= PRINT_DETERMINANT;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "INV"))
		{
		}
#endif
	      else if (lex_match_id (lexer, "AIC"))
		{
		  factor.print |= PRINT_AIC;
		}
	      else if (lex_match_id (lexer, "SIG"))
		{
		  factor.print |= PRINT_SIG;
		}
	      else if (lex_match_id (lexer, "CORRELATION"))
		{
		  factor.print |= PRINT_CORRELATION;
		}
	      else if (lex_match_id (lexer, "COVARIANCE"))
		{
		  factor.print |= PRINT_COVARIANCE;
		}
	      else if (lex_match_id (lexer, "ROTATION"))
		{
		  factor.print |= PRINT_ROTATION;
		}
	      else if (lex_match_id (lexer, "EXTRACTION"))
		{
		  factor.print |= PRINT_EXTRACTION;
		}
	      else if (lex_match_id (lexer, "INITIAL"))
		{
		  factor.print |= PRINT_INITIAL;
		}
	      else if (lex_match_id (lexer, "KMO"))
		{
		  factor.print |= PRINT_KMO;
		}
#if FACTOR_FULLY_IMPLEMENTED
	      else if (lex_match_id (lexer, "REPR"))
		{
		}
	      else if (lex_match_id (lexer, "FSCORE"))
		{
		}
#endif
              else if (lex_match (lexer, T_ALL))
		{
		  factor.print = 0xFFFF;
		}
	      else if (lex_match_id (lexer, "DEFAULT"))
		{
		  factor.print |= PRINT_INITIAL ;
		  factor.print |= PRINT_EXTRACTION ;
		  factor.print |= PRINT_ROTATION ;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  factor.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  factor.exclude = MV_ANY;
		}
	      else if (lex_match_id (lexer, "LISTWISE"))
		{
		  factor.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "PAIRWISE"))
		{
		  factor.missing_type = MISS_PAIRWISE;
		}
	      else if (lex_match_id (lexer, "MEANSUB"))
		{
		  factor.missing_type = MISS_MEANSUB;
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

  if (factor.rotation == ROT_NONE)
    factor.print &= ~PRINT_ROTATION;

  if (factor.n_vars < 2)
    msg (MW, _("Factor analysis on a single variable is not useful."));

  if (factor.n_vars < 1)
    {
      msg (ME, _("Factor analysis without variables is not possible."));
      goto error;
    }

  if (matrix_reader)
    {
      struct idata *id = idata_alloc (factor.n_vars);

      while (next_matrix_from_reader (&id->mm, mr,
				      factor.vars, factor.n_vars))
	{
	  do_factor_by_matrix (&factor, id);

          gsl_matrix_free (id->ai_cov);
          id->ai_cov = NULL;
          gsl_matrix_free (id->ai_cor);
          id->ai_cor = NULL;
	  gsl_matrix_free (id->mm.corr);
	  id->mm.corr = NULL;
	  gsl_matrix_free (id->mm.cov);
	  id->mm.cov = NULL;
	}

      idata_free (id);
    }
  else
    if (! run_factor (ds, &factor))
      goto error;


  destroy_matrix_reader (mr);
  free (factor.vars);
  return CMD_SUCCESS;

 error:
  destroy_matrix_reader (mr);
  free (factor.vars);
  return CMD_FAILURE;
}

static void do_factor (const struct cmd_factor *factor, struct casereader *group);


static bool
run_factor (struct dataset *ds, const struct cmd_factor *factor)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);

  while (casegrouper_get_next_group (grouper, &group))
    {
      if (factor->missing_type == MISS_LISTWISE)
	group  = casereader_create_filter_missing (group, factor->vars, factor->n_vars,
						   factor->exclude,
						   NULL,  NULL);
      do_factor (factor, group);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}


/* Return the communality of variable N, calculated to N_FACTORS */
static double
the_communality (const gsl_matrix *evec, const gsl_vector *eval, int n, int n_factors)
{
  size_t i;

  double comm = 0;

  assert (n >= 0);
  assert (n < eval->size);
  assert (n < evec->size1);
  assert (n_factors <= eval->size);

  for (i = 0 ; i < n_factors; ++i)
    {
      double evali = fabs (gsl_vector_get (eval, i));

      double eveci = gsl_matrix_get (evec, n, i);

      comm += pow2 (eveci) * evali;
    }

  return comm;
}

/* Return the communality of variable N, calculated to N_FACTORS */
static double
communality (const struct idata *idata, int n, int n_factors)
{
  return the_communality (idata->evec, idata->eval, n, n_factors);
}


static void
show_scree (const struct cmd_factor *f, const struct idata *idata)
{
  struct scree *s;
  const char *label ;

  if (!(f->plot & PLOT_SCREE))
    return;


  label = f->extraction == EXTRACTION_PC ? _("Component Number") : _("Factor Number");

  s = scree_create (idata->eval, label);

  scree_submit (s);
}

static void
show_communalities (const struct cmd_factor * factor,
		    const gsl_vector *initial, const gsl_vector *extracted)
{
  if (!(factor->print & (PRINT_INITIAL | PRINT_EXTRACTION)))
    return;

  struct pivot_table *table = pivot_table_create (N_("Communalities"));

  struct pivot_dimension *communalities = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Communalities"));
  if (factor->print & PRINT_INITIAL)
    pivot_category_create_leaves (communalities->root, N_("Initial"));
  if (factor->print & PRINT_EXTRACTION)
    pivot_category_create_leaves (communalities->root, N_("Extraction"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0 ; i < factor->n_vars; ++i)
    {
      int row = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (factor->vars[i]));

      int col = 0;
      if (factor->print & PRINT_INITIAL)
        pivot_table_put2 (table, col++, row, pivot_value_new_number (
                            gsl_vector_get (initial, i)));
      if (factor->print & PRINT_EXTRACTION)
        pivot_table_put2 (table, col++, row, pivot_value_new_number (
                            gsl_vector_get (extracted, i)));
    }

  pivot_table_submit (table);
}

static struct pivot_dimension *
create_numeric_dimension (struct pivot_table *table,
                          enum pivot_axis_type axis_type, const char *name,
                          size_t n, bool show_label)
{
  struct pivot_dimension *d = pivot_dimension_create (table, axis_type, name);
  d->root->show_label = show_label;
  for (int i = 0 ; i < n; ++i)
    pivot_category_create_leaf (d->root, pivot_value_new_integer (i + 1));
  return d;
}

static void
show_factor_matrix (const struct cmd_factor *factor, const struct idata *idata, const char *title, const gsl_matrix *fm)
{
  struct pivot_table *table = pivot_table_create (title);

  const int n_factors = idata->n_extractions;
  create_numeric_dimension (
    table, PIVOT_AXIS_COLUMN,
    factor->extraction == EXTRACTION_PC ? N_("Component") : N_("Factor"),
    n_factors, true);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  /* Initialise to the identity permutation */
  gsl_permutation *perm = gsl_permutation_calloc (factor->n_vars);

  if (factor->sort)
    sort_matrix_indirect (fm, perm);

  for (size_t i = 0 ; i < factor->n_vars; ++i)
    {
      const int matrix_row = perm->data[i];

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (factor->vars[matrix_row]));

      for (size_t j = 0 ; j < n_factors; ++j)
	{
	  double x = gsl_matrix_get (fm, matrix_row, j);
	  if (fabs (x) < factor->blank)
	    continue;

          pivot_table_put2 (table, j, var_idx, pivot_value_new_number (x));
	}
    }

  gsl_permutation_free (perm);

  pivot_table_submit (table);
}

static void
put_variance (struct pivot_table *table, int row, int phase_idx,
              double lambda, double percent, double cum)
{
  double entries[] = { lambda, percent, cum };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    pivot_table_put3 (table, i, phase_idx, row,
                      pivot_value_new_number (entries[i]));
}

static void
show_explained_variance (const struct cmd_factor * factor,
			 const struct idata *idata,
			 const gsl_vector *initial_eigenvalues,
			 const gsl_vector *extracted_eigenvalues,
			 const gsl_vector *rotated_loadings)
{
  if (!(factor->print & (PRINT_INITIAL | PRINT_EXTRACTION | PRINT_ROTATION)))
    return;

  struct pivot_table *table = pivot_table_create (
    N_("Total Variance Explained"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Total"), PIVOT_RC_OTHER,
                          /* xgettext:no-c-format */
                          N_("% of Variance"), PIVOT_RC_PERCENT,
			  /* xgettext:no-c-format */
                          N_("Cumulative %"), PIVOT_RC_PERCENT);

  struct pivot_dimension *phase = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Phase"));
  if (factor->print & PRINT_INITIAL)
    pivot_category_create_leaves (phase->root, N_("Initial Eigenvalues"));

  if (factor->print & PRINT_EXTRACTION)
    pivot_category_create_leaves (phase->root,
                                  N_("Extraction Sums of Squared Loadings"));

  if (factor->print & PRINT_ROTATION)
    pivot_category_create_leaves (phase->root,
                                  N_("Rotation Sums of Squared Loadings"));

  struct pivot_dimension *components = pivot_dimension_create (
    table, PIVOT_AXIS_ROW,
    factor->extraction == EXTRACTION_PC ? N_("Component") : N_("Factor"));

  double i_total = 0.0;
  for (size_t i = 0 ; i < initial_eigenvalues->size; ++i)
    i_total += gsl_vector_get (initial_eigenvalues, i);

  double e_total = (factor->extraction == EXTRACTION_PAF
                    ? factor->n_vars
                    : i_total);

  double i_cum = 0.0;
  double e_cum = 0.0;
  double r_cum = 0.0;
  for (size_t i = 0 ; i < factor->n_vars; ++i)
    {
      const double i_lambda = gsl_vector_get (initial_eigenvalues, i);
      double i_percent = 100.0 * i_lambda / i_total ;
      i_cum += i_percent;

      const double e_lambda = gsl_vector_get (extracted_eigenvalues, i);
      double e_percent = 100.0 * e_lambda / e_total ;
      e_cum += e_percent;

      int row = pivot_category_create_leaf (
        components->root, pivot_value_new_integer (i + 1));

      int phase_idx = 0;

      /* Initial Eigenvalues */
      if (factor->print & PRINT_INITIAL)
        put_variance (table, row, phase_idx++, i_lambda, i_percent, i_cum);

      if (i < idata->n_extractions)
        {
          if (factor->print & PRINT_EXTRACTION)
            put_variance (table, row, phase_idx++, e_lambda, e_percent, e_cum);

          if (rotated_loadings != NULL && factor->print & PRINT_ROTATION)
            {
              double r_lambda = gsl_vector_get (rotated_loadings, i);
              double r_percent = 100.0 * r_lambda / e_total ;
              if (factor->rotation == ROT_PROMAX)
                r_lambda = r_percent = SYSMIS;

              r_cum += r_percent;
              put_variance (table, row, phase_idx++, r_lambda, r_percent,
                            r_cum);
            }
        }
    }

  pivot_table_submit (table);
}

static void
show_factor_correlation (const struct cmd_factor * factor, const gsl_matrix *fcm)
{
  struct pivot_table *table = pivot_table_create (
    N_("Factor Correlation Matrix"));

  create_numeric_dimension (
    table, PIVOT_AXIS_ROW,
    factor->extraction == EXTRACTION_PC ? N_("Component") : N_("Factor"),
    fcm->size2, true);

  create_numeric_dimension (table, PIVOT_AXIS_COLUMN, N_("Factor 2"),
                            fcm->size1, false);

  for (size_t i = 0 ; i < fcm->size1; ++i)
    for (size_t j = 0 ; j < fcm->size2; ++j)
      pivot_table_put2 (table, j, i,
                        pivot_value_new_number (gsl_matrix_get (fcm, i, j)));

  pivot_table_submit (table);
}

static void
add_var_dims (struct pivot_table *table, const struct cmd_factor *factor)
{
  for (int i = 0; i < 2; i++)
    {
      struct pivot_dimension *d = pivot_dimension_create (
        table, i ? PIVOT_AXIS_ROW : PIVOT_AXIS_COLUMN,
        N_("Variables"));

      for (size_t j = 0; j < factor->n_vars; j++)
        pivot_category_create_leaf (
          d->root, pivot_value_new_variable (factor->vars[j]));
    }
}

static void
show_aic (const struct cmd_factor *factor, const struct idata *idata)
{
  if ((factor->print & PRINT_AIC) == 0)
    return;

  struct pivot_table *table = pivot_table_create (N_("Anti-Image Matrices"));

  add_var_dims (table, factor);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("Anti-image Covariance"),
                          N_("Anti-image Correlation"));

  for (size_t i = 0; i < factor->n_vars; ++i)
    for (size_t j = 0; j < factor->n_vars; ++j)
      {
        double cov = gsl_matrix_get (idata->ai_cov, i, j);
        pivot_table_put3 (table, i, j, 0, pivot_value_new_number (cov));

        double corr = gsl_matrix_get (idata->ai_cor, i, j);
        pivot_table_put3 (table, i, j, 1, pivot_value_new_number (corr));
      }

  pivot_table_submit (table);
}

static void
show_correlation_matrix (const struct cmd_factor *factor, const struct idata *idata)
{
  if (!(factor->print & (PRINT_CORRELATION | PRINT_SIG | PRINT_DETERMINANT)))
    return;

  struct pivot_table *table = pivot_table_create (N_("Correlation Matrix"));

  if (factor->print & (PRINT_CORRELATION | PRINT_SIG))
    {
      add_var_dims (table, factor);

      struct pivot_dimension *statistics = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Statistics"));
      if (factor->print & PRINT_CORRELATION)
        pivot_category_create_leaves (statistics->root, N_("Correlation"),
                                      PIVOT_RC_CORRELATION);
      if (factor->print & PRINT_SIG)
        pivot_category_create_leaves (statistics->root, N_("Sig. (1-tailed)"),
                                      PIVOT_RC_SIGNIFICANCE);

      int stat_idx = 0;
      if (factor->print & PRINT_CORRELATION)
        {
          for (int i = 0; i < factor->n_vars; ++i)
            for (int j = 0; j < factor->n_vars; ++j)
              {
                double corr = gsl_matrix_get (idata->mm.corr, i, j);
                pivot_table_put3 (table, j, i, stat_idx,
                                  pivot_value_new_number (corr));
              }
          stat_idx++;
        }

      if (factor->print & PRINT_SIG)
        {
          for (int i = 0; i < factor->n_vars; ++i)
            for (int j = 0; j < factor->n_vars; ++j)
              if (i != j)
                {
                  double rho = gsl_matrix_get (idata->mm.corr, i, j);
                  double w = gsl_matrix_get (idata->mm.n, i, j);
                  double sig = significance_of_correlation (rho, w);
                  pivot_table_put3 (table, j, i, stat_idx,
                                    pivot_value_new_number (sig));
                }
          stat_idx++;
        }
    }

  if (factor->print & PRINT_DETERMINANT)
    table->caption = pivot_value_new_user_text_nocopy (
      xasprintf ("%s: %.2f", _("Determinant"), idata->detR));

  pivot_table_submit (table);
}

static void
show_covariance_matrix (const struct cmd_factor *factor, const struct idata *idata)
{
  if (!(factor->print & PRINT_COVARIANCE))
    return;

  struct pivot_table *table = pivot_table_create (N_("Covariance Matrix"));
  add_var_dims (table, factor);

  for (int i = 0; i < factor->n_vars; ++i)
    for (int j = 0; j < factor->n_vars; ++j)
      {
        double cov = gsl_matrix_get (idata->mm.cov, i, j);
        pivot_table_put2 (table, j, i, pivot_value_new_number (cov));
      }

  pivot_table_submit (table);
}


static void
do_factor (const struct cmd_factor *factor, struct casereader *r)
{
  struct ccase *c;
  struct idata *idata = idata_alloc (factor->n_vars);

  idata->cvm = covariance_1pass_create (factor->n_vars, factor->vars,
					factor->wv, factor->exclude, true);

  for (; (c = casereader_read (r)); case_unref (c))
    {
      covariance_accumulate (idata->cvm, c);
    }

  idata->mm.cov = covariance_calculate (idata->cvm);

  if (idata->mm.cov == NULL)
    {
      msg (MW, _("The dataset contains no complete observations. No analysis will be performed."));
      covariance_destroy (idata->cvm);
      goto finish;
    }

  idata->mm.var_matrix = covariance_moments (idata->cvm, MOMENT_VARIANCE);
  idata->mm.mean_matrix = covariance_moments (idata->cvm, MOMENT_MEAN);
  idata->mm.n = covariance_moments (idata->cvm, MOMENT_NONE);

  do_factor_by_matrix (factor, idata);

 finish:
  gsl_matrix_free (idata->mm.corr);
  gsl_matrix_free (idata->mm.cov);

  idata_free (idata);
  casereader_destroy (r);
}

static void
do_factor_by_matrix (const struct cmd_factor *factor, struct idata *idata)
{
  if (!idata->mm.cov && !idata->mm.corr)
    {
      msg (ME, _("The dataset has no complete covariance or correlation matrix."));
      return;
    }

  if (idata->mm.cov && !idata->mm.corr)
    idata->mm.corr = correlation_from_covariance (idata->mm.cov, idata->mm.var_matrix);
  if (idata->mm.corr && !idata->mm.cov)
    idata->mm.cov = covariance_from_correlation (idata->mm.corr, idata->mm.var_matrix);
  if (factor->method == METHOD_CORR)
    idata->analysis_matrix = idata->mm.corr;
  else
    idata->analysis_matrix = idata->mm.cov;

  gsl_matrix *r_inv;
  r_inv  = clone_matrix (idata->mm.corr);
  gsl_linalg_cholesky_decomp (r_inv);
  gsl_linalg_cholesky_invert (r_inv);

  idata->ai_cov = anti_image_cov (r_inv);
  idata->ai_cor = anti_image_corr (r_inv, idata);

  int i;
  double sum_ssq_r = 0;
  double sum_ssq_a = 0;
  for (i = 0; i < r_inv->size1; ++i)
    {
      sum_ssq_r += ssq_od_n (idata->mm.corr, i);
      sum_ssq_a += ssq_od_n (idata->ai_cor, i);
    }

  gsl_matrix_free (r_inv);

  if (factor->print & PRINT_DETERMINANT
      || factor->print & PRINT_KMO)
    {
      int sign = 0;

      const int size = idata->mm.corr->size1;
      gsl_permutation *p = gsl_permutation_calloc (size);
      gsl_matrix *tmp = gsl_matrix_calloc (size, size);
      gsl_matrix_memcpy (tmp, idata->mm.corr);

      gsl_linalg_LU_decomp (tmp, p, &sign);
      idata->detR = gsl_linalg_LU_det (tmp, sign);
      gsl_permutation_free (p);
      gsl_matrix_free (tmp);
    }

  if (factor->print & PRINT_UNIVARIATE)
    {
      struct pivot_table *table = pivot_table_create (
        N_("Descriptive Statistics"));
      pivot_table_set_weight_var (table, factor->wv);

      pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                              N_("Mean"), PIVOT_RC_OTHER,
                              N_("Std. Deviation"), PIVOT_RC_OTHER,
                              N_("Analysis N"), PIVOT_RC_COUNT);

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Variables"));

      for (i = 0 ; i < factor->n_vars; ++i)
	{
	  const struct variable *v = factor->vars[i];

          int row = pivot_category_create_leaf (
            variables->root, pivot_value_new_variable (v));

          double entries[] = {
            gsl_matrix_get (idata->mm.mean_matrix, i, i),
            sqrt (gsl_matrix_get (idata->mm.var_matrix, i, i)),
            gsl_matrix_get (idata->mm.n, i, i),
          };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            pivot_table_put2 (table, j, row,
                              pivot_value_new_number (entries[j]));
	}

      pivot_table_submit (table);
    }

  if (factor->print & PRINT_KMO)
    {
      struct pivot_table *table = pivot_table_create (
        N_("KMO and Bartlett's Test"));

      struct pivot_dimension *statistics = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Statistics"),
        N_("Kaiser-Meyer-Olkin Measure of Sampling Adequacy"), PIVOT_RC_OTHER);
      pivot_category_create_group (
        statistics->root, N_("Bartlett's Test of Sphericity"),
        N_("Approx. Chi-Square"), PIVOT_RC_OTHER,
        N_("df"), PIVOT_RC_INTEGER,
        N_("Sig."), PIVOT_RC_SIGNIFICANCE);

      /* The literature doesn't say what to do for the value of W when
	 missing values are involved.  The best thing I can think of
	 is to take the mean average. */
      double w = 0;
      for (i = 0; i < idata->mm.n->size1; ++i)
	w += gsl_matrix_get (idata->mm.n, i, i);
      w /= idata->mm.n->size1;

      double xsq = ((w - 1 - (2 * factor->n_vars + 5) / 6.0)
                    * -log (idata->detR));
      double df = factor->n_vars * (factor->n_vars - 1) / 2;
      double entries[] = {
        sum_ssq_r / (sum_ssq_r + sum_ssq_a),
        xsq,
        df,
        gsl_cdf_chisq_Q (xsq, df)
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put1 (table, i, pivot_value_new_number (entries[i]));

      pivot_table_submit (table);
    }

  show_correlation_matrix (factor, idata);
  show_covariance_matrix (factor, idata);
  if (idata->cvm)
    covariance_destroy (idata->cvm);

  {
    gsl_matrix *am = matrix_dup (idata->analysis_matrix);
    gsl_eigen_symmv_workspace *workspace = gsl_eigen_symmv_alloc (factor->n_vars);

    gsl_eigen_symmv (am, idata->eval, idata->evec, workspace);

    gsl_eigen_symmv_free (workspace);
    gsl_matrix_free (am);
  }

  gsl_eigen_symmv_sort (idata->eval, idata->evec, GSL_EIGEN_SORT_ABS_DESC);

  idata->n_extractions = n_extracted_factors (factor, idata);

  if (idata->n_extractions == 0)
    {
      msg (MW, _("The %s criteria result in zero factors extracted. Therefore no analysis will be performed."), "FACTOR");
      return;
    }

  if (idata->n_extractions > factor->n_vars)
    {
      msg (MW,
	   _("The %s criteria result in more factors than variables, which is not meaningful. No analysis will be performed."),
	   "FACTOR");
      return;
    }

  {
    gsl_matrix *rotated_factors = NULL;
    gsl_matrix *pattern_matrix = NULL;
    gsl_matrix *fcm = NULL;
    gsl_vector *rotated_loadings = NULL;

    const gsl_vector *extracted_eigenvalues = NULL;
    gsl_vector *initial_communalities = gsl_vector_alloc (factor->n_vars);
    gsl_vector *extracted_communalities = gsl_vector_alloc (factor->n_vars);
    size_t i;
    struct factor_matrix_workspace *fmw = factor_matrix_workspace_alloc (idata->msr->size, idata->n_extractions);
    gsl_matrix *factor_matrix = gsl_matrix_calloc (factor->n_vars, fmw->n_factors);

    if (factor->extraction == EXTRACTION_PAF)
      {
	gsl_vector *diff = gsl_vector_alloc (idata->msr->size);
	struct smr_workspace *ws = ws_create (idata->analysis_matrix);

	for (i = 0 ; i < factor->n_vars ; ++i)
	  {
	    double r2 = squared_multiple_correlation (idata->analysis_matrix, i, ws);

	    gsl_vector_set (idata->msr, i, r2);
	  }
	ws_destroy (ws);

	gsl_vector_memcpy (initial_communalities, idata->msr);

	for (i = 0; i < factor->extraction_iterations; ++i)
	  {
	    double min, max;
	    gsl_vector_memcpy (diff, idata->msr);

	    iterate_factor_matrix (idata->analysis_matrix, idata->msr, factor_matrix, fmw);

	    gsl_vector_sub (diff, idata->msr);

	    gsl_vector_minmax (diff, &min, &max);

	    if (fabs (min) < factor->econverge && fabs (max) < factor->econverge)
	      break;
	  }
	gsl_vector_free (diff);



	gsl_vector_memcpy (extracted_communalities, idata->msr);
	extracted_eigenvalues = fmw->eval;
      }
    else if (factor->extraction == EXTRACTION_PC)
      {
	for (i = 0; i < factor->n_vars; ++i)
	  gsl_vector_set (initial_communalities, i, communality (idata, i, factor->n_vars));

	gsl_vector_memcpy (extracted_communalities, initial_communalities);

	iterate_factor_matrix (idata->analysis_matrix, extracted_communalities, factor_matrix, fmw);


	extracted_eigenvalues = idata->eval;
      }


    show_aic (factor, idata);
    show_communalities (factor, initial_communalities, extracted_communalities);

    if (factor->rotation != ROT_NONE)
      {
	rotated_factors = gsl_matrix_calloc (factor_matrix->size1, factor_matrix->size2);
	rotated_loadings = gsl_vector_calloc (factor_matrix->size2);
	if (factor->rotation == ROT_PROMAX)
	  {
	    pattern_matrix = gsl_matrix_calloc (factor_matrix->size1, factor_matrix->size2);
	    fcm = gsl_matrix_calloc (factor_matrix->size2, factor_matrix->size2);
	  }


	rotate (factor, factor_matrix, extracted_communalities, rotated_factors, rotated_loadings, pattern_matrix, fcm);
      }

    show_explained_variance (factor, idata, idata->eval, extracted_eigenvalues, rotated_loadings);

    factor_matrix_workspace_free (fmw);

    show_scree (factor, idata);

    show_factor_matrix (factor, idata,
			(factor->extraction == EXTRACTION_PC
                         ? N_("Component Matrix") : N_("Factor Matrix")),
			factor_matrix);

    if (factor->rotation == ROT_PROMAX)
      {
	show_factor_matrix (factor, idata, N_("Pattern Matrix"),
                            pattern_matrix);
	gsl_matrix_free (pattern_matrix);
      }

    if (factor->rotation != ROT_NONE)
      {
	show_factor_matrix (factor, idata,
			    (factor->rotation == ROT_PROMAX
                             ? N_("Structure Matrix")
                             : factor->extraction == EXTRACTION_PC
                             ? N_("Rotated Component Matrix")
			     : N_("Rotated Factor Matrix")),
			    rotated_factors);

	gsl_matrix_free (rotated_factors);
      }

    if (factor->rotation == ROT_PROMAX)
      {
	show_factor_correlation (factor, fcm);
	gsl_matrix_free (fcm);
      }

    gsl_matrix_free (factor_matrix);
    gsl_vector_free (rotated_loadings);
    gsl_vector_free (initial_communalities);
    gsl_vector_free (extracted_communalities);
  }
}


