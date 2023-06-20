/* PSPP - a program for statistical analysis.
   Copyright (C) 2021 Free Software Foundation, Inc.

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

#include <gsl/gsl_blas.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_eigen.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_vector.h>
#include <limits.h>
#include <math.h>
#include <uniwidth.h>

#include "data/any-reader.h"
#include "data/any-writer.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "language/command.h"
#include "language/commands/data-reader.h"
#include "language/commands/data-writer.h"
#include "language/commands/file-handle.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/float-range.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/intern.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/stringi-set.h"
#include "libpspp/u8-line.h"
#include "math/distributions.h"
#include "math/random.h"
#include "output/driver.h"
#include "output/output-item.h"
#include "output/pivot-table.h"

#include "gl/c-ctype.h"
#include "gl/c-strcase.h"
#include "gl/ftoastr.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct matrix_state;

/* A variable in the matrix language. */
struct matrix_var
  {
    struct hmap_node hmap_node; /* In matrix_state's 'vars' hmap. */
    char *name;                 /* UTF-8. */
    gsl_matrix *value;          /* NULL, if the variable is uninitialized. */
  };

/* All the MSAVE commands within a matrix program share common configuration,
   provided by the first MSAVE command within the program.  This structure
   encapsulates this configuration. */
struct msave_common
  {
    /* Common configuration for all MSAVEs. */
    struct msg_location *location; /* Range of lines for first MSAVE. */
    struct file_handle *outfile;   /* Output file for all the MSAVEs. */
    struct msg_location *outfile_location;
    struct string_array variables; /* VARIABLES subcommand. */
    struct msg_location *variables_location;
    struct string_array fnames;    /* FNAMES subcommand. */
    struct msg_location *fnames_location;
    struct string_array snames;    /* SNAMES subcommand. */
    struct msg_location *snames_location;

    /* Collects and owns factors and splits.  The individual msave_command
       structs point to these but do not own them.  (This is because factors
       and splits can be carried over from one MSAVE to the next, so it's
       easiest to just take the most recent.) */
    struct matrix_expr **factors;
    size_t n_factors, allocated_factors;
    struct matrix_expr **splits;
    size_t n_splits, allocated_splits;

    /* Execution state. */
    struct dictionary *dict;
    struct casewriter *writer;
  };

/* A file used by one or more READ commands. */
struct read_file
  {
    /* Parse state. */
    struct file_handle *file;

    /* Execution state. */
    struct dfm_reader *reader;
    char *encoding;
  };

static struct read_file *read_file_create (struct matrix_state *,
                                           struct file_handle *);
static struct dfm_reader *read_file_open (struct read_file *);

/* A file used by one or more WRITE comamnds. */
struct write_file
  {
    /* Parse state. */
    struct file_handle *file;

    /* Execution state. */
    struct dfm_writer *writer;
    char *encoding;
    struct u8_line *held;     /* Output held by a previous WRITE with /HOLD. */
  };

static struct write_file *write_file_create (struct matrix_state *,
                                             struct file_handle *);
static struct dfm_writer *write_file_open (struct write_file *);
static void write_file_destroy (struct write_file *);

/* A file used by one or more SAVE commands. */
struct save_file
  {
    /* Parse state. */
    struct file_handle *file;
    struct dataset *dataset;
    struct string_array variables;
    struct matrix_expr *names;
    struct stringi_set strings;

    /* Execution state. */
    bool error;
    struct casewriter *writer;
    struct dictionary *dict;
    struct msg_location *location;
  };

/* State of an entire matrix program. */
struct matrix_state
  {
    /* State passed into MATRIX from outside. */
    struct dataset *dataset;
    struct session *session;
    struct lexer *lexer;

    /* Matrix program's own state. */
    struct hmap vars;           /* Dictionary of matrix variables. */
    bool in_loop;               /* True if parsing within a LOOP. */

    /* MSAVE. */
    struct msave_common *msave_common;

    /* READ. */
    struct file_handle *prev_read_file;
    struct read_file **read_files;
    size_t n_read_files;

    /* WRITE. */
    struct file_handle *prev_write_file;
    struct write_file **write_files;
    size_t n_write_files;

    /* SAVE. */
    struct file_handle *prev_save_file;
    struct save_file **save_files;
    size_t n_save_files;
  };

/* Finds and returns the variable with the given NAME (case-insensitive) within
   S, if there is one, or a null pointer if there is not. */
static struct matrix_var *
matrix_var_lookup (struct matrix_state *s, struct substring name)
{
  struct matrix_var *var;

  HMAP_FOR_EACH_WITH_HASH (var, struct matrix_var, hmap_node,
                           utf8_hash_case_substring (name, 0), &s->vars)
    if (!utf8_sscasecmp (ss_cstr (var->name), name))
      return var;

  return NULL;
}

/* Creates and returns a new variable named NAME within S.  There must not
   already be a variable with the same (case-insensitive) name.  The variable
   is created uninitialized. */
static struct matrix_var *
matrix_var_create (struct matrix_state *s, struct substring name)
{
  struct matrix_var *var = xmalloc (sizeof *var);
  *var = (struct matrix_var) { .name = ss_xstrdup (name) };
  hmap_insert (&s->vars, &var->hmap_node, utf8_hash_case_substring (name, 0));
  return var;
}

/* Replaces VAR's value by VALUE.  Takes ownership of VALUE. */
static void
matrix_var_set (struct matrix_var *var, gsl_matrix *value)
{
  gsl_matrix_free (var->value);
  var->value = value;
}

/* Matrix function catalog. */

/* The third argument to F() is a "prototype".  For most prototypes, the first
   letter (before the _) represents the return type and each other letter
   (after the _) is an argument type.  The types are:

     - "m": A matrix of unrestricted dimensions.

     - "d": A scalar.

     - "v": A row or column vector.

     - "e": Primarily for the first argument, this is a matrix with
       unrestricted dimensions treated elementwise.  Each element in the matrix
       is passed to the implementation function separately.

     - "n": This gets passed the "const struct matrix_expr *" that represents
       the expression.  This allows the evaluation function to grab the source
       location of arguments so that it can report accurate error locations.
       This type doesn't correspond to an argument passed in by the user.

   The fourth argument is an optional constraints string.  For this purpose the
   first argument is named "a", the second "b", and so on.  The following kinds
   of constraints are supported.  For matrix arguments, the constraints are
   applied to each value in the matrix separately:

     - "a(0,1)" or "a[0,1]": 0 < a < 1 or 0 <= a <= 1, respectively.  Any
       integer may substitute for 0 and 1.  Half-open constraints (] and [) are
       also supported.

     - "ai": Restrict a to integer values.

     - "a>0", "a<0", "a>=0", "a<=0", "a!=0".

     - "a<b", "a>b", "a<=b", "a>=b", "b!=0".
*/
#define MATRIX_FUNCTIONS                                                \
    F(ABS,      "ABS",      m_e, NULL)                                  \
    F(ALL,      "ALL",      d_m, NULL)                                  \
    F(ANY,      "ANY",      d_m, NULL)                                  \
    F(ARSIN,    "ARSIN",    m_e, "a[-1,1]")                             \
    F(ARTAN,    "ARTAN",    m_e, NULL)                                  \
    F(BLOCK,    "BLOCK",    m_any, NULL)                                \
    F(CHOL,     "CHOL",     m_mn, NULL)                                 \
    F(CMIN,     "CMIN",     m_m, NULL)                                  \
    F(CMAX,     "CMAX",     m_m, NULL)                                  \
    F(COS,      "COS",      m_e, NULL)                                  \
    F(CSSQ,     "CSSQ",     m_m, NULL)                                  \
    F(CSUM,     "CSUM",     m_m, NULL)                                  \
    F(DESIGN,   "DESIGN",   m_mn, NULL)                                 \
    F(DET,      "DET",      d_m, NULL)                                  \
    F(DIAG,     "DIAG",     m_m, NULL)                                  \
    F(EVAL,     "EVAL",     m_mn, NULL)                                 \
    F(EXP,      "EXP",      m_e, NULL)                                  \
    F(GINV,     "GINV",     m_m, NULL)                                  \
    F(GRADE,    "GRADE",    m_m, NULL)                                  \
    F(GSCH,     "GSCH",     m_mn, NULL)                                 \
    F(IDENT,    "IDENT",    IDENT, NULL)                                \
    F(INV,      "INV",      m_m, NULL)                                  \
    F(KRONEKER, "KRONEKER", m_mm, NULL)                                 \
    F(LG10,     "LG10",     m_e, "a>0")                                 \
    F(LN,       "LN",       m_e, "a>0")                                 \
    F(MAGIC,    "MAGIC",    m_d, "ai>=3")                               \
    F(MAKE,     "MAKE",     m_ddd, "ai>=0 bi>=0")                       \
    F(MDIAG,    "MDIAG",    m_v, NULL)                                  \
    F(MMAX,     "MMAX",     d_m, NULL)                                  \
    F(MMIN,     "MMIN",     d_m, NULL)                                  \
    F(MOD,      "MOD",      m_md, "b!=0")                               \
    F(MSSQ,     "MSSQ",     d_m, NULL)                                  \
    F(MSUM,     "MSUM",     d_m, NULL)                                  \
    F(NCOL,     "NCOL",     d_m, NULL)                                  \
    F(NROW,     "NROW",     d_m, NULL)                                  \
    F(RANK,     "RANK",     d_m, NULL)                                  \
    F(RESHAPE,  "RESHAPE",  m_mddn, NULL)                                \
    F(RMAX,     "RMAX",     m_m, NULL)                                  \
    F(RMIN,     "RMIN",     m_m, NULL)                                  \
    F(RND,      "RND",      m_e, NULL)                                  \
    F(RNKORDER, "RNKORDER", m_m, NULL)                                  \
    F(RSSQ,     "RSSQ",     m_m, NULL)                                  \
    F(RSUM,     "RSUM",     m_m, NULL)                                  \
    F(SIN,      "SIN",      m_e, NULL)                                  \
    F(SOLVE,    "SOLVE",    m_mmn, NULL)                                \
    F(SQRT,     "SQRT",     m_e, "a>=0")                                \
    F(SSCP,     "SSCP",     m_m, NULL)                                  \
    F(SVAL,     "SVAL",     m_m, NULL)                                  \
    F(SWEEP,    "SWEEP",    m_mdn, NULL)                                \
    F(T,        "T",        m_m, NULL)                                  \
    F(TRACE,    "TRACE",    d_m, NULL)                                  \
    F(TRANSPOS, "TRANSPOS", m_m, NULL)                                  \
    F(TRUNC,    "TRUNC",    m_e, NULL)                                  \
    F(UNIFORM,  "UNIFORM",  m_ddn, "ai>=0 bi>=0")                       \
    F(PDF_BETA, "PDF.BETA", m_edd, "a[0,1] b>0 c>0")                    \
    F(CDF_BETA, "CDF.BETA", m_edd, "a[0,1] b>0 c>0")                    \
    F(IDF_BETA, "IDF.BETA", m_edd, "a[0,1] b>0 c>0")                    \
    F(RV_BETA,  "RV.BETA",  d_dd, "a>0 b>0")                            \
    F(NCDF_BETA, "NCDF.BETA", m_eddd, "a>=0 b>0 c>0 d>0")               \
    F(NPDF_BETA, "NCDF.BETA", m_eddd, "a>=0 b>0 c>0 d>0")               \
    F(CDF_BVNOR, "CDF.BVNOR", m_eed, "c[-1,1]")                         \
    F(PDF_BVNOR, "PDF.BVNOR", m_eed, "c[-1,1]")                         \
    F(CDF_CAUCHY, "CDF.CAUCHY", m_edd, "c>0")                           \
    F(IDF_CAUCHY, "IDF.CAUCHY", m_edd, "a(0,1) c>0")                    \
    F(PDF_CAUCHY, "PDF.CAUCHY", m_edd, "c>0")                           \
    F(RV_CAUCHY, "RV.CAUCHY", d_dd, "b>0")                              \
    F(CDF_CHISQ, "CDF.CHISQ", m_ed, "a>=0 b>0")                         \
    F(CHICDF, "CHICDF", m_ed, "a>=0 b>0")                               \
    F(IDF_CHISQ, "IDF.CHISQ", m_ed, "a[0,1) b>0")                       \
    F(PDF_CHISQ, "PDF.CHISQ", m_ed, "a>=0 b>0")                         \
    F(RV_CHISQ, "RV.CHISQ", d_d, "a>0")                                 \
    F(SIG_CHISQ, "SIG.CHISQ", m_ed, "a>=0 b>0")                         \
    F(CDF_EXP, "CDF.EXP", m_ed, "a>=0 b>=0")                            \
    F(IDF_EXP, "IDF.EXP", m_ed, "a[0,1) b>0")                           \
    F(PDF_EXP, "PDF.EXP", m_ed, "a>=0 b>0")                             \
    F(RV_EXP, "RV.EXP", d_d, "a>0")                                     \
    F(PDF_XPOWER, "PDF.XPOWER", m_edd, "b>0 c>=0")                      \
    F(RV_XPOWER, "RV.XPOWER", d_dd, "a>0 c>=0")                         \
    F(CDF_F, "CDF.F", m_edd, "a>=0 b>0 c>0")                            \
    F(FCDF, "FCDF", m_edd, "a>=0 b>0 c>0")                              \
    F(IDF_F, "IDF.F", m_edd, "a[0,1) b>0 c>0")                          \
    F(PDF_F, "PDF.F", m_edd, "a>=0 b>0 c>0")                            \
    F(RV_F, "RV.F", d_dd, "a>0 b>0")                                    \
    F(SIG_F, "SIG.F", m_edd, "a>=0 b>0 c>0")                            \
    F(CDF_GAMMA, "CDF.GAMMA", m_edd, "a>=0 b>0 c>0")                    \
    F(IDF_GAMMA, "IDF.GAMMA", m_edd, "a[0,1] b>0 c>0")                  \
    F(PDF_GAMMA, "PDF.GAMMA", m_edd, "a>=0 b>0 c>0")                    \
    F(RV_GAMMA, "RV.GAMMA", d_dd, "a>0 b>0")                            \
    F(PDF_LANDAU, "PDF.LANDAU", m_e, NULL)                              \
    F(RV_LANDAU, "RV.LANDAU", d_none, NULL)                             \
    F(CDF_LAPLACE, "CDF.LAPLACE", m_edd, "c>0")                         \
    F(IDF_LAPLACE, "IDF.LAPLACE", m_edd, "a(0,1) c>0")                  \
    F(PDF_LAPLACE, "PDF.LAPLACE", m_edd, "c>0")                         \
    F(RV_LAPLACE, "RV.LAPLACE", d_dd, "b>0")                            \
    F(RV_LEVY, "RV.LEVY", d_dd, "b(0,2]")                               \
    F(RV_LVSKEW, "RV.LVSKEW", d_ddd, "b(0,2] c[-1,1]")                  \
    F(CDF_LOGISTIC, "CDF.LOGISTIC", m_edd, "c>0")                       \
    F(IDF_LOGISTIC, "IDF.LOGISTIC", m_edd, "a(0,1) c>0")                \
    F(PDF_LOGISTIC, "PDF.LOGISTIC", m_edd, "c>0")                       \
    F(RV_LOGISTIC, "RV.LOGISTIC", d_dd, "b>0")                          \
    F(CDF_LNORMAL, "CDF.LNORMAL", m_edd, "a>=0 b>0 c>0")                \
    F(IDF_LNORMAL, "IDF.LNORMAL", m_edd, "a[0,1) b>0 c>0")              \
    F(PDF_LNORMAL, "PDF.LNORMAL", m_edd, "a>=0 b>0 c>0")                \
    F(RV_LNORMAL, "RV.LNORMAL", d_dd, "a>0 b>0")                        \
    F(CDF_NORMAL, "CDF.NORMAL", m_edd, "c>0")                           \
    F(IDF_NORMAL, "IDF.NORMAL", m_edd, "a(0,1) c>0")                    \
    F(PDF_NORMAL, "PDF.NORMAL", m_edd, "c>0")                           \
    F(RV_NORMAL, "RV.NORMAL", d_dd, "b>0")                              \
    F(CDFNORM, "CDFNORM", m_e, NULL)                                    \
    F(PROBIT, "PROBIT", m_e, "a(0,1)")                                  \
    F(NORMAL, "NORMAL", m_e, "a>0")                                     \
    F(PDF_NTAIL, "PDF.NTAIL", m_edd, "b>0 c>0")                         \
    F(RV_NTAIL, "RV.NTAIL", d_dd, "a>0 b>0")                            \
    F(CDF_PARETO, "CDF.PARETO", m_edd, "a>=b b>0 c>0")                  \
    F(IDF_PARETO, "IDF.PARETO", m_edd, "a[0,1) b>0 c>0")                \
    F(PDF_PARETO, "PDF.PARETO", m_edd, "a>=b b>0 c>0")                  \
    F(RV_PARETO, "RV.PARETO", d_dd, "a>0 b>0")                          \
    F(CDF_RAYLEIGH, "CDF.RAYLEIGH", m_ed, "b>0")                        \
    F(IDF_RAYLEIGH, "IDF.RAYLEIGH", m_ed, "a[0,1] b>0")                 \
    F(PDF_RAYLEIGH, "PDF.RAYLEIGH", m_ed, "b>0")                        \
    F(RV_RAYLEIGH, "RV.RAYLEIGH", d_d, "a>0")                           \
    F(PDF_RTAIL, "PDF.RTAIL", m_edd, NULL)                              \
    F(RV_RTAIL, "RV.RTAIL", d_dd, NULL)                                 \
    F(CDF_T, "CDF.T", m_ed, "b>0")                                      \
    F(TCDF, "TCDF", m_ed, "b>0")                                        \
    F(IDF_T, "IDF.T", m_ed, "a(0,1) b>0")                               \
    F(PDF_T, "PDF.T", m_ed, "b>0")                                      \
    F(RV_T, "RV.T", d_d, "a>0")                                         \
    F(CDF_T1G, "CDF.T1G", m_edd, NULL)                                  \
    F(IDF_T1G, "IDF.T1G", m_edd, "a(0,1)")                              \
    F(PDF_T1G, "PDF.T1G", m_edd, NULL)                                  \
    F(RV_T1G, "RV.T1G", d_dd, NULL)                                     \
    F(CDF_T2G, "CDF.T2G", m_edd, NULL)                                  \
    F(IDF_T2G, "IDF.T2G", m_edd, "a(0,1)")                              \
    F(PDF_T2G, "PDF.T2G", m_edd, NULL)                                  \
    F(RV_T2G, "RV.T2G", d_dd, NULL)                                     \
    F(CDF_UNIFORM, "CDF.UNIFORM", m_edd, "a<=c b<=c")                   \
    F(IDF_UNIFORM, "IDF.UNIFORM", m_edd, "a[0,1] b<=c")                 \
    F(PDF_UNIFORM, "PDF.UNIFORM", m_edd, "a<=c b<=c")                   \
    F(RV_UNIFORM, "RV.UNIFORM", d_dd, "a<=b")                           \
    F(CDF_WEIBULL, "CDF.WEIBULL", m_edd, "a>=0 b>0 c>0")                \
    F(IDF_WEIBULL, "IDF.WEIBULL", m_edd, "a[0,1) b>0 c>0")              \
    F(PDF_WEIBULL, "PDF.WEIBULL", m_edd, "a>=0 b>0 c>0")                \
    F(RV_WEIBULL, "RV.WEIBULL", d_dd, "a>0 b>0")                        \
    F(CDF_BERNOULLI, "CDF.BERNOULLI", m_ed, "ai[0,1] b[0,1]")           \
    F(PDF_BERNOULLI, "PDF.BERNOULLI", m_ed, "ai[0,1] b[0,1]")           \
    F(RV_BERNOULLI, "RV.BERNOULLI", d_d, "a[0,1]")                      \
    F(CDF_BINOM, "CDF.BINOM", m_edd, "bi>0 c[0,1]")                     \
    F(PDF_BINOM, "PDF.BINOM", m_edd, "ai>=0<=b bi>0 c[0,1]")            \
    F(RV_BINOM, "RV.BINOM", d_dd, "ai>0 b[0,1]")                        \
    F(CDF_GEOM, "CDF.GEOM", m_ed, "ai>=1 b[0,1]")                       \
    F(PDF_GEOM, "PDF.GEOM", m_ed, "ai>=1 b[0,1]")                       \
    F(RV_GEOM, "RV.GEOM", d_d, "a[0,1]")                                \
    F(CDF_HYPER, "CDF.HYPER", m_eddd, "ai>=0<=d bi>0 ci>0<=b di>0<=b")  \
    F(PDF_HYPER, "PDF.HYPER", m_eddd, "ai>=0<=d bi>0 ci>0<=b di>0<=b")  \
    F(RV_HYPER, "RV.HYPER", d_ddd, "ai>0 bi>0<=a ci>0<=a")              \
    F(PDF_LOG, "PDF.LOG", m_ed, "a>=1 b(0,1]")                          \
    F(RV_LOG, "RV.LOG", d_d, "a(0,1]")                                  \
    F(CDF_NEGBIN, "CDF.NEGBIN", m_edd, "a>=1 bi c(0,1]")                \
    F(PDF_NEGBIN, "PDF.NEGBIN", m_edd, "a>=1 bi c(0,1]")                \
    F(RV_NEGBIN, "RV.NEGBIN", d_dd, "ai b(0,1]")                        \
    F(CDF_POISSON, "CDF.POISSON", m_ed, "ai>=0 b>0")                    \
    F(PDF_POISSON, "PDF.POISSON", m_ed, "ai>=0 b>0")                    \
    F(RV_POISSON, "RV.POISSON", d_d, "a>0")

/* Properties of a matrix function.

   These come straight from the macro invocations above. */
struct matrix_function_properties
  {
    const char *name;
    const char *constraints;
  };

/* Minimum and maximum argument counts for each matrix function prototype. */
enum { IDENT_MIN_ARGS = 1,  IDENT_MAX_ARGS = 2 };
enum { d_d_MIN_ARGS = 1,    d_d_MAX_ARGS = 1 };
enum { d_dd_MIN_ARGS = 2,   d_dd_MAX_ARGS = 2 };
enum { d_ddd_MIN_ARGS = 3,  d_ddd_MAX_ARGS = 3 };
enum { d_m_MIN_ARGS = 1,    d_m_MAX_ARGS = 1 };
enum { d_none_MIN_ARGS = 0, d_none_MAX_ARGS = 0 };
enum { m_any_MIN_ARGS = 1,  m_any_MAX_ARGS = INT_MAX };
enum { m_d_MIN_ARGS = 1,    m_d_MAX_ARGS = 1 };
enum { m_ddd_MIN_ARGS = 3,  m_ddd_MAX_ARGS = 3 };
enum { m_ddn_MIN_ARGS = 2,  m_ddn_MAX_ARGS = 2 };
enum { m_e_MIN_ARGS = 1,    m_e_MAX_ARGS = 1 };
enum { m_ed_MIN_ARGS = 2,   m_ed_MAX_ARGS = 2 };
enum { m_edd_MIN_ARGS = 3,  m_edd_MAX_ARGS = 3 };
enum { m_eddd_MIN_ARGS = 4, m_eddd_MAX_ARGS = 4 };
enum { m_eed_MIN_ARGS = 3,  m_eed_MAX_ARGS = 3 };
enum { m_m_MIN_ARGS = 1,    m_m_MAX_ARGS = 1 };
enum { m_md_MIN_ARGS = 2,   m_md_MAX_ARGS = 2 };
enum { m_mddn_MIN_ARGS = 3, m_mddn_MAX_ARGS = 3 };
enum { m_mdn_MIN_ARGS = 2,  m_mdn_MAX_ARGS = 2 };
enum { m_mm_MIN_ARGS = 2,   m_mm_MAX_ARGS = 2 };
enum { m_mmn_MIN_ARGS = 2,  m_mmn_MAX_ARGS = 2 };
enum { m_mn_MIN_ARGS = 1,   m_mn_MAX_ARGS = 1 };
enum { m_v_MIN_ARGS = 1,    m_v_MAX_ARGS = 1 };

/* C function prototype for each matrix function prototype. */
typedef double matrix_proto_d_none (void);
typedef double matrix_proto_d_d (double);
typedef double matrix_proto_d_dd (double, double);
typedef double matrix_proto_d_dd (double, double);
typedef double matrix_proto_d_ddd (double, double, double);
typedef gsl_matrix *matrix_proto_m_d (double);
typedef gsl_matrix *matrix_proto_m_ddd (double, double, double);
typedef gsl_matrix *matrix_proto_m_ddn (double, double,
                                        const struct matrix_expr *);
typedef gsl_matrix *matrix_proto_m_m (gsl_matrix *);
typedef gsl_matrix *matrix_proto_m_mn (gsl_matrix *,
                                       const struct matrix_expr *);
typedef double matrix_proto_m_e (double);
typedef gsl_matrix *matrix_proto_m_md (gsl_matrix *, double);
typedef gsl_matrix *matrix_proto_m_mdn (gsl_matrix *, double,
                                        const struct matrix_expr *);
typedef double matrix_proto_m_ed (double, double);
typedef gsl_matrix *matrix_proto_m_mddn (gsl_matrix *, double, double,
                                          const struct matrix_expr *);
typedef double matrix_proto_m_edd (double, double, double);
typedef double matrix_proto_m_eddd (double, double, double, double);
typedef double matrix_proto_m_eed (double, double, double);
typedef gsl_matrix *matrix_proto_m_mm (gsl_matrix *, gsl_matrix *);
typedef gsl_matrix *matrix_proto_m_mmn (gsl_matrix *, gsl_matrix *,
                                        const struct matrix_expr *);
typedef gsl_matrix *matrix_proto_m_v (gsl_vector *);
typedef double matrix_proto_d_m (gsl_matrix *);
typedef gsl_matrix *matrix_proto_m_any (gsl_matrix *[], size_t n);
typedef gsl_matrix *matrix_proto_IDENT (double, double);

#define F(ENUM, STRING, PROTO, CONSTRAINTS) \
    static matrix_proto_##PROTO matrix_eval_##ENUM;
MATRIX_FUNCTIONS
#undef F

/* Matrix expression data structure and parsing. */

/* A node in a matrix expression. */
struct matrix_expr
  {
    enum matrix_op
      {
        /* Functions. */
#define F(ENUM, STRING, PROTO, CONSTRAINTS) MOP_F_##ENUM,
        MATRIX_FUNCTIONS
#undef F

        /* Elementwise and scalar arithmetic. */
        MOP_NEGATE,             /* unary - */
        MOP_ADD_ELEMS,          /* + */
        MOP_SUB_ELEMS,          /* - */
        MOP_MUL_ELEMS,          /* &* */
        MOP_DIV_ELEMS,          /* / and &/ */
        MOP_EXP_ELEMS,          /* &** */
        MOP_SEQ,                /* a:b */
        MOP_SEQ_BY,             /* a:b:c */

        /* Matrix arithmetic. */
        MOP_MUL_MAT,            /* * */
        MOP_EXP_MAT,            /* ** */

        /* Relational. */
        MOP_GT,                 /* > */
        MOP_GE,                 /* >= */
        MOP_LT,                 /* < */
        MOP_LE,                 /* <= */
        MOP_EQ,                 /* = */
        MOP_NE,                 /* <> */

        /* Logical. */
        MOP_NOT,                /* NOT */
        MOP_AND,                /* AND */
        MOP_OR,                 /* OR */
        MOP_XOR,                /* XOR */

        /* {}. */
        MOP_PASTE_HORZ,         /* a, b, c, ... */
        MOP_PASTE_VERT,         /* a; b; c; ... */
        MOP_EMPTY,              /* {} */

        /* Sub-matrices. */
        MOP_VEC_INDEX,          /* x(y) */
        MOP_VEC_ALL,            /* x(:) */
        MOP_MAT_INDEX,          /* x(y,z) */
        MOP_ROW_INDEX,          /* x(y,:) */
        MOP_COL_INDEX,          /* x(:,z) */

        /* Literals. */
        MOP_NUMBER,
        MOP_VARIABLE,

        /* Oddball stuff. */
        MOP_EOF,                /* EOF('file') */
      }
    op;

    union
      {
        /* Nonterminal expression nodes. */
        struct
          {
            struct matrix_expr **subs;
            size_t n_subs;
          };

        /* Terminal expression nodes. */
        double number;               /* MOP_NUMBER. */
        struct matrix_var *variable; /* MOP_VARIABLE. */
        struct read_file *eof;       /* MOP_EOF. */
      };

    /* The syntax location corresponding to this expression node, for use in
       error messages.  This is always nonnull for terminal expression nodes.
       For most others, it is null because it can be computed lazily if and
       when it is needed.

       Use matrix_expr_location() instead of using this member directly, so
       that it gets computed lazily if needed. */
    struct msg_location *location;
  };

static void
matrix_expr_location__ (const struct matrix_expr *e,
                        const struct msg_location **minp,
                        const struct msg_location **maxp)
{
  struct msg_location *loc = e->location;
  if (loc)
    {
      const struct msg_location *min = *minp;
      if (loc->start.line
          && (!min
              || loc->start.line < min->start.line
              || (loc->start.line == min->start.line
                  && loc->start.column < min->start.column)))
        *minp = loc;

      const struct msg_location *max = *maxp;
      if (loc->end.line
          && (!max
              || loc->end.line > max->end.line
              || (loc->end.line == max->end.line
                  && loc->end.column > max->end.column)))
        *maxp = loc;

      return;
    }

  assert (e->op != MOP_NUMBER && e->op != MOP_VARIABLE && e->op != MOP_EOF);
  for (size_t i = 0; i < e->n_subs; i++)
    matrix_expr_location__ (e->subs[i], minp, maxp);
}

/* Returns the source code location corresponding to expression E, computing it
   lazily if needed. */
static const struct msg_location *
matrix_expr_location (const struct matrix_expr *e_)
{
  struct matrix_expr *e = CONST_CAST (struct matrix_expr *, e_);
  if (!e)
    return NULL;

  if (!e->location)
    {
      const struct msg_location *min = NULL;
      const struct msg_location *max = NULL;
      matrix_expr_location__ (e, &min, &max);
      if (min && max)
        {
          e->location = msg_location_dup (min);
          e->location->end = max->end;
        }
    }
  return e->location;
}

/* Sets e->location to the tokens in S's lexer from offset START_OFS to the
   token before the current one.  Has no effect if E already has a location or
   if E is null. */
static void
matrix_expr_add_location (struct matrix_state *s, int start_ofs,
                          struct matrix_expr *e)
{
  if (e && !e->location)
    e->location = lex_ofs_location (s->lexer, start_ofs,
                                    lex_ofs (s->lexer) - 1);
}

/* Frees E and all the data and sub-expressions that it references. */
static void
matrix_expr_destroy (struct matrix_expr *e)
{
  if (!e)
    return;

  switch (e->op)
    {
#define F(ENUM, STRING, PROTO, CONSTRAINTS) case MOP_F_##ENUM:
MATRIX_FUNCTIONS
#undef F
    case MOP_NEGATE:
    case MOP_ADD_ELEMS:
    case MOP_SUB_ELEMS:
    case MOP_MUL_ELEMS:
    case MOP_DIV_ELEMS:
    case MOP_EXP_ELEMS:
    case MOP_SEQ:
    case MOP_SEQ_BY:
    case MOP_MUL_MAT:
    case MOP_EXP_MAT:
    case MOP_GT:
    case MOP_GE:
    case MOP_LT:
    case MOP_LE:
    case MOP_EQ:
    case MOP_NE:
    case MOP_NOT:
    case MOP_AND:
    case MOP_OR:
    case MOP_XOR:
    case MOP_EMPTY:
    case MOP_PASTE_HORZ:
    case MOP_PASTE_VERT:
    case MOP_VEC_INDEX:
    case MOP_VEC_ALL:
    case MOP_MAT_INDEX:
    case MOP_ROW_INDEX:
    case MOP_COL_INDEX:
      for (size_t i = 0; i < e->n_subs; i++)
        matrix_expr_destroy (e->subs[i]);
      free (e->subs);
      break;

    case MOP_NUMBER:
    case MOP_VARIABLE:
    case MOP_EOF:
      break;
    }
  msg_location_destroy (e->location);
  free (e);
}

/* Creates and returns a new matrix_expr with type OP, which must be a
   nonterminal type.  Initializes the new matrix_expr with the N_SUBS
   expressions in SUBS as subexpressions. */
static struct matrix_expr *
matrix_expr_create_subs (enum matrix_op op, struct matrix_expr **subs,
                         size_t n_subs)
{
  struct matrix_expr *e = xmalloc (sizeof *e);
  *e = (struct matrix_expr) {
    .op = op,
    .subs = xmemdup (subs, n_subs * sizeof *subs),
    .n_subs = n_subs
  };
  return e;
}

static struct matrix_expr *
matrix_expr_create_0 (enum matrix_op op)
{
  struct matrix_expr *sub;
  return matrix_expr_create_subs (op, &sub, 0);
}

static struct matrix_expr *
matrix_expr_create_1 (enum matrix_op op, struct matrix_expr *sub)
{
  return matrix_expr_create_subs (op, &sub, 1);
}

static struct matrix_expr *
matrix_expr_create_2 (enum matrix_op op,
                      struct matrix_expr *sub0, struct matrix_expr *sub1)
{
  struct matrix_expr *subs[] = { sub0, sub1 };
  return matrix_expr_create_subs (op, subs, sizeof subs / sizeof *subs);
}

static struct matrix_expr *
matrix_expr_create_3 (enum matrix_op op, struct matrix_expr *sub0,
                      struct matrix_expr *sub1, struct matrix_expr *sub2)
{
  struct matrix_expr *subs[] = { sub0, sub1, sub2 };
  return matrix_expr_create_subs (op, subs, sizeof subs / sizeof *subs);
}

/* Creates and returns a new MOP_NUMBER expression node to contain NUMBER. */
static struct matrix_expr *
matrix_expr_create_number (double number)
{
  struct matrix_expr *e = xmalloc (sizeof *e);
  *e = (struct matrix_expr) {
    .op = MOP_NUMBER,
    .number = number,
  };
  return e;
}

static struct matrix_expr *matrix_expr_parse (struct matrix_state *);

/* A binary operator for matrix_parse_binary_operator(). */
struct matrix_operator_syntax
  {
    /* Exactly one of these specifies the operator syntax. */
    enum token_type token;      /* A token, e.g. T_ASTERISK. */
    const char *id;             /* An identifier, e.g. "XOR". */
    const char *phrase;         /* A token phrase, e.g. "&**". */

    /* The matrix operator corresponding to the syntax. */
    enum matrix_op op;
  };

static bool
matrix_operator_syntax_match (struct lexer *lexer,
                              const struct matrix_operator_syntax *syntax,
                              size_t n_syntax, enum matrix_op *op)
{
  const struct matrix_operator_syntax *end = &syntax[n_syntax];
  for (const struct matrix_operator_syntax *syn = syntax; syn < end; syn++)
    if (syn->id ? lex_match_id (lexer, syn->id)
        : syn->phrase ? lex_match_phrase (lexer, syn->phrase)
        : lex_match (lexer, syn->token))
      {
        *op = syn->op;
        return true;
      }
  return false;
}

/* Parses a binary operator level in the recursive descent parser, returning a
   matrix expression if successful or a null pointer otherwise.  PARSE_NEXT
   must be the function to parse the next level of precedence.  The N_SYNTAX
   elements of SYNTAX must specify the syntax and matrix_expr node type to
   parse at this level.  */
static struct matrix_expr *
matrix_parse_binary_operator (
  struct matrix_state *s,
  struct matrix_expr *(*parse_next) (struct matrix_state *),
  const struct matrix_operator_syntax *syntax, size_t n_syntax)
{
  struct matrix_expr *lhs = parse_next (s);
  if (!lhs)
    return NULL;

  for (;;)
    {
      enum matrix_op op;
      if (!matrix_operator_syntax_match (s->lexer, syntax, n_syntax, &op))
        return lhs;

      struct matrix_expr *rhs = parse_next (s);
      if (!rhs)
        {
          matrix_expr_destroy (lhs);
          return NULL;
        }
      lhs = matrix_expr_create_2 (op, lhs, rhs);
    }
}

/* Parses a comma-separated list of expressions within {}, transforming them
   into MOP_PASTE_HORZ operators.  Returns the new expression or NULL on
   error. */
static struct matrix_expr *
matrix_parse_curly_comma (struct matrix_state *s)
{
  static const struct matrix_operator_syntax op = {
    .token = T_COMMA, .op = MOP_PASTE_HORZ
  };
  return matrix_parse_binary_operator (s, matrix_expr_parse, &op, 1);
}

/* Parses a semicolon-separated list of expressions within {}, transforming
   them into MOP_PASTE_VERT operators.  Returns the new expression or NULL on
   error. */
static struct matrix_expr *
matrix_parse_curly_semi (struct matrix_state *s)
{
  if (lex_token (s->lexer) == T_RCURLY)
    {
      /* {} is a special case for a 0×0 matrix. */
      return matrix_expr_create_0 (MOP_EMPTY);
    }

  static const struct matrix_operator_syntax op = {
    .token = T_SEMICOLON, .op = MOP_PASTE_VERT
  };
  return matrix_parse_binary_operator (s, matrix_parse_curly_comma, &op, 1);
}

struct matrix_function
  {
    const char *name;
    enum matrix_op op;
    size_t min_args, max_args;
  };

static struct matrix_expr *matrix_expr_parse (struct matrix_state *);

static bool
word_matches (const char **test, const char **name)
{
  size_t test_len = strcspn (*test, ".");
  size_t name_len = strcspn (*name, ".");
  if (test_len == name_len)
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }
  else if (test_len < 3 || test_len > name_len)
    return false;
  else
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }

  *test += test_len;
  *name += name_len;
  if (**test != **name)
    return false;

  if (**test == '.')
    {
      (*test)++;
      (*name)++;
    }
  return true;
}

/* Returns 0 if TOKEN and FUNC do not match,
   1 if TOKEN is an acceptable abbreviation for FUNC,
   2 if TOKEN equals FUNC. */
static int
compare_function_names (const char *token_, const char *func_)
{
  const char *token = token_;
  const char *func = func_;
  while (*token || *func)
    if (!word_matches (&token, &func))
      return 0;
  return !c_strcasecmp (token_, func_) ? 2 : 1;
}

static const struct matrix_function *
matrix_parse_function_name (const char *token)
{
  static const struct matrix_function functions[] =
    {
#define F(ENUM, STRING, PROTO, CONSTRAINTS)                             \
      { STRING, MOP_F_##ENUM, PROTO##_MIN_ARGS, PROTO##_MAX_ARGS },
      MATRIX_FUNCTIONS
#undef F
    };
  enum { N_FUNCTIONS = sizeof functions / sizeof *functions };

  for (size_t i = 0; i < N_FUNCTIONS; i++)
    {
      if (compare_function_names (token, functions[i].name) > 0)
        return &functions[i];
    }
  return NULL;
}

static bool
matrix_parse_function (struct matrix_state *s, const char *token,
                       struct matrix_expr **exprp)
{
  *exprp = NULL;
  if (lex_next_token (s->lexer, 1) != T_LPAREN)
    return false;

  int start_ofs = lex_ofs (s->lexer);
  if (lex_match_id (s->lexer, "EOF"))
    {
      lex_get (s->lexer);
      struct file_handle *fh = fh_parse (s->lexer, FH_REF_FILE, s->session);
      if (!fh)
        return true;

      if (!lex_force_match (s->lexer, T_RPAREN))
        {
          fh_unref (fh);
          return true;
        }

      struct read_file *rf = read_file_create (s, fh);

      struct matrix_expr *e = xmalloc (sizeof *e);
      *e = (struct matrix_expr) { .op = MOP_EOF, .eof = rf };
      matrix_expr_add_location (s, start_ofs, e);
      *exprp = e;
      return true;
    }

  const struct matrix_function *f = matrix_parse_function_name (token);
  if (!f)
    return false;

  struct matrix_expr *e = xmalloc (sizeof *e);
  *e = (struct matrix_expr) { .op = f->op };

  lex_get_n (s->lexer, 2);
  if (lex_token (s->lexer) != T_RPAREN)
    {
      size_t allocated_subs = 0;
      do
        {
          struct matrix_expr *sub = matrix_expr_parse (s);
          if (!sub)
            goto error;

          if (e->n_subs >= allocated_subs)
            e->subs = x2nrealloc (e->subs, &allocated_subs, sizeof *e->subs);
          e->subs[e->n_subs++] = sub;
        }
      while (lex_match (s->lexer, T_COMMA));
    }
  if (!lex_force_match (s->lexer, T_RPAREN))
    goto error;

  if (e->n_subs < f->min_args || e->n_subs > f->max_args)
    {
      if (f->min_args == f->max_args)
        msg_at (SE, e->location,
                ngettext ("Matrix function %s requires %zu argument.",
                          "Matrix function %s requires %zu arguments.",
                          f->min_args),
             f->name, f->min_args);
      else if (f->min_args == 1 && f->max_args == 2)
        msg_at (SE, e->location,
                ngettext ("Matrix function %s requires 1 or 2 arguments, "
                          "but %zu was provided.",
                          "Matrix function %s requires 1 or 2 arguments, "
                          "but %zu were provided.",
                          e->n_subs),
             f->name, e->n_subs);
      else if (f->min_args == 1 && f->max_args == INT_MAX)
        msg_at (SE, e->location,
                _("Matrix function %s requires at least one argument."),
                f->name);
      else
        NOT_REACHED ();

      goto error;
    }

  matrix_expr_add_location (s, start_ofs, e);

  *exprp = e;
  return true;

error:
  matrix_expr_destroy (e);
  return true;
}

static struct matrix_expr *
matrix_parse_primary__ (struct matrix_state *s)
{
  if (lex_is_number (s->lexer))
    {
      double number = lex_number (s->lexer);
      lex_get (s->lexer);

      return matrix_expr_create_number (number);
    }
  else if (lex_is_string (s->lexer))
    {
      char string[sizeof (double)];
      buf_copy_str_rpad (string, sizeof string, lex_tokcstr (s->lexer), ' ');
      lex_get (s->lexer);

      double number;
      memcpy (&number, string, sizeof number);

      return matrix_expr_create_number (number);
    }
  else if (lex_match (s->lexer, T_LPAREN))
    {
      struct matrix_expr *e = matrix_expr_parse (s);
      if (!e || !lex_force_match (s->lexer, T_RPAREN))
        {
          matrix_expr_destroy (e);
          return NULL;
        }
      return e;
    }
  else if (lex_match (s->lexer, T_LCURLY))
    {
      struct matrix_expr *e = matrix_parse_curly_semi (s);
      if (!e || !lex_force_match (s->lexer, T_RCURLY))
        {
          matrix_expr_destroy (e);
          return NULL;
        }
      return e;
    }
  else if (lex_token (s->lexer) == T_ID)
    {
      struct matrix_expr *retval;
      if (matrix_parse_function (s, lex_tokcstr (s->lexer), &retval))
        return retval;

      struct matrix_var *var = matrix_var_lookup (s, lex_tokss (s->lexer));
      if (!var)
        {
          lex_error (s->lexer, _("Unknown variable %s."),
                     lex_tokcstr (s->lexer));
          return NULL;
        }
      lex_get (s->lexer);

      struct matrix_expr *e = xmalloc (sizeof *e);
      *e = (struct matrix_expr) { .op = MOP_VARIABLE, .variable = var };
      return e;
    }
  else if (lex_token (s->lexer) == T_ALL)
    {
      struct matrix_expr *retval;
      if (matrix_parse_function (s, "ALL", &retval))
        return retval;
    }

  lex_error (s->lexer, _("Syntax error expecting matrix expression."));
  return NULL;
}

static struct matrix_expr *
matrix_parse_primary (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);
  struct matrix_expr *e = matrix_parse_primary__ (s);
  matrix_expr_add_location (s, start_ofs, e);
  return e;
}

static struct matrix_expr *matrix_parse_postfix (struct matrix_state *);

static bool
matrix_parse_index_expr (struct matrix_state *s,
                         struct matrix_expr **indexp,
                         struct msg_location **locationp)
{
  if (lex_match (s->lexer, T_COLON))
    {
      if (locationp)
        *locationp = lex_get_location (s->lexer, -1, -1);
      *indexp = NULL;
      return true;
    }
  else
    {
      *indexp = matrix_expr_parse (s);
      if (locationp && *indexp)
        *locationp = msg_location_dup (matrix_expr_location (*indexp));
      return *indexp != NULL;
    }
}

static struct matrix_expr *
matrix_parse_postfix (struct matrix_state *s)
{
  struct matrix_expr *lhs = matrix_parse_primary (s);
  if (!lhs || !lex_match (s->lexer, T_LPAREN))
    return lhs;

  struct matrix_expr *i0;
  if (!matrix_parse_index_expr (s, &i0, NULL))
    {
      matrix_expr_destroy (lhs);
      return NULL;
    }
  if (lex_match (s->lexer, T_RPAREN))
    return (i0
            ? matrix_expr_create_2 (MOP_VEC_INDEX, lhs, i0)
            : matrix_expr_create_1 (MOP_VEC_ALL, lhs));
  else if (lex_match (s->lexer, T_COMMA))
    {
      struct matrix_expr *i1;
      if (!matrix_parse_index_expr (s, &i1, NULL)
          || !lex_force_match (s->lexer, T_RPAREN))
        {
          matrix_expr_destroy (lhs);
          matrix_expr_destroy (i0);
          matrix_expr_destroy (i1);
          return NULL;
        }
      return (i0 && i1 ? matrix_expr_create_3 (MOP_MAT_INDEX, lhs, i0, i1)
              : i0 ? matrix_expr_create_2 (MOP_ROW_INDEX, lhs, i0)
              : i1 ? matrix_expr_create_2 (MOP_COL_INDEX, lhs, i1)
              : lhs);
    }
  else
    {
      lex_error_expecting (s->lexer, "`)'", "`,'");
      return NULL;
    }
}

static struct matrix_expr *
matrix_parse_unary (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);

  struct matrix_expr *e;
  if (lex_match (s->lexer, T_DASH))
    {
      struct matrix_expr *sub = matrix_parse_unary (s);
      if (!sub)
        return NULL;
      e = matrix_expr_create_1 (MOP_NEGATE, sub);
    }
  else if (lex_match (s->lexer, T_PLUS))
    {
      e = matrix_parse_unary (s);
      if (!e)
        return NULL;
    }
  else
    return matrix_parse_postfix (s);

  matrix_expr_add_location (s, start_ofs, e);
  e->location->start = lex_ofs_start_point (s->lexer, start_ofs);
  return e;
}

static struct matrix_expr *
matrix_parse_seq (struct matrix_state *s)
{
  struct matrix_expr *start = matrix_parse_unary (s);
  if (!start || !lex_match (s->lexer, T_COLON))
    return start;

  struct matrix_expr *end = matrix_parse_unary (s);
  if (!end)
    {
      matrix_expr_destroy (start);
      return NULL;
    }

  if (lex_match (s->lexer, T_COLON))
    {
      struct matrix_expr *increment = matrix_parse_unary (s);
      if (!increment)
        {
          matrix_expr_destroy (start);
          matrix_expr_destroy (end);
          return NULL;
        }
      return matrix_expr_create_3 (MOP_SEQ_BY, start, end, increment);
    }
  else
    return matrix_expr_create_2 (MOP_SEQ, start, end);
}

static struct matrix_expr *
matrix_parse_exp (struct matrix_state *s)
{
  static const struct matrix_operator_syntax syntax[] = {
    { .token = T_EXP, .op = MOP_EXP_MAT },
    { .phrase = "&**", .op = MOP_EXP_ELEMS },
  };
  size_t n_syntax = sizeof syntax / sizeof *syntax;

  return matrix_parse_binary_operator (s, matrix_parse_seq, syntax, n_syntax);
}

static struct matrix_expr *
matrix_parse_mul_div (struct matrix_state *s)
{
  static const struct matrix_operator_syntax syntax[] = {
    { .token = T_ASTERISK, .op = MOP_MUL_MAT },
    { .token = T_SLASH, .op = MOP_DIV_ELEMS },
    { .phrase = "&*", .op = MOP_MUL_ELEMS },
    { .phrase = "&/", .op = MOP_DIV_ELEMS },
  };
  size_t n_syntax = sizeof syntax / sizeof *syntax;

  return matrix_parse_binary_operator (s, matrix_parse_exp, syntax, n_syntax);
}

static struct matrix_expr *
matrix_parse_add_sub (struct matrix_state *s)
{
  struct matrix_expr *lhs = matrix_parse_mul_div (s);
  if (!lhs)
    return NULL;

  for (;;)
    {
      enum matrix_op op;
      if (lex_match (s->lexer, T_PLUS))
        op = MOP_ADD_ELEMS;
      else if (lex_match (s->lexer, T_DASH))
        op = MOP_SUB_ELEMS;
      else if (lex_token (s->lexer) == T_NEG_NUM)
        op = MOP_ADD_ELEMS;
      else
        return lhs;

      struct matrix_expr *rhs = matrix_parse_mul_div (s);
      if (!rhs)
        {
          matrix_expr_destroy (lhs);
          return NULL;
        }
      lhs = matrix_expr_create_2 (op, lhs, rhs);
    }
}

static struct matrix_expr *
matrix_parse_relational (struct matrix_state *s)
{
  static const struct matrix_operator_syntax syntax[] = {
    { .token = T_GT, .op = MOP_GT },
    { .token = T_GE, .op = MOP_GE },
    { .token = T_LT, .op = MOP_LT },
    { .token = T_LE, .op = MOP_LE },
    { .token = T_EQUALS, .op = MOP_EQ },
    { .token = T_EQ, .op = MOP_EQ },
    { .token = T_NE, .op = MOP_NE },
  };
  size_t n_syntax = sizeof syntax / sizeof *syntax;

  return matrix_parse_binary_operator (s, matrix_parse_add_sub,
                                       syntax, n_syntax);
}

static struct matrix_expr *
matrix_parse_not (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);
  if (lex_match (s->lexer, T_NOT))
    {
      struct matrix_expr *sub = matrix_parse_not (s);
      if (!sub)
        return NULL;

      struct matrix_expr *e = matrix_expr_create_1 (MOP_NOT, sub);
      matrix_expr_add_location (s, start_ofs, e);
      e->location->start = lex_ofs_start_point (s->lexer, start_ofs);
      return e;
    }
  else
    return matrix_parse_relational (s);
}

static struct matrix_expr *
matrix_parse_and (struct matrix_state *s)
{
  static const struct matrix_operator_syntax op = {
    .token = T_AND, .op = MOP_AND
  };

  return matrix_parse_binary_operator (s, matrix_parse_not, &op, 1);
}

static struct matrix_expr *
matrix_expr_parse__ (struct matrix_state *s)
{
  static const struct matrix_operator_syntax syntax[] = {
    { .token = T_OR, .op = MOP_OR },
    { .id = "XOR", .op = MOP_XOR },
  };
  size_t n_syntax = sizeof syntax / sizeof *syntax;

  return matrix_parse_binary_operator (s, matrix_parse_and, syntax, n_syntax);
}

static struct matrix_expr *
matrix_expr_parse (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);
  struct matrix_expr *e = matrix_expr_parse__ (s);
  matrix_expr_add_location (s, start_ofs, e);
  return e;
}

/* Matrix expression evaluation. */

/* Iterates over all the elements in matrix M, setting Y and X to the row and
   column indexes, respectively, and pointing D to the entry at each
   position. */
#define MATRIX_FOR_ALL_ELEMENTS(D, Y, X, M)                     \
  for (size_t Y = 0; Y < (M)->size1; Y++)                       \
    for (size_t X = 0; X < (M)->size2; X++)                     \
      for (double *D = gsl_matrix_ptr ((M), Y, X); D; D = NULL)

static bool
is_vector (const gsl_matrix *m)
{
  return m->size1 <= 1 || m->size2 <= 1;
}

static gsl_vector
to_vector (gsl_matrix *m)
{
  return (m->size1 == 1
          ? gsl_matrix_row (m, 0).vector
          : gsl_matrix_column (m, 0).vector);
}

static double
matrix_eval_ABS (double d)
{
  return fabs (d);
}

static double
matrix_eval_ALL (gsl_matrix *m)
{
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    if (*d == 0.0)
      return 0.0;
  return 1.0;
}

static double
matrix_eval_ANY (gsl_matrix *m)
{
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    if (*d != 0.0)
      return 1.0;
  return 0.0;
}

static double
matrix_eval_ARSIN (double d)
{
  return asin (d);
}

static double
matrix_eval_ARTAN (double d)
{
  return atan (d);
}

static gsl_matrix *
matrix_eval_BLOCK (gsl_matrix *m[], size_t n)
{
  size_t r = 0;
  size_t c = 0;
  for (size_t i = 0; i < n; i++)
    {
      r += m[i]->size1;
      c += m[i]->size2;
    }
  gsl_matrix *block = gsl_matrix_calloc (r, c);
  r = c = 0;
  for (size_t i = 0; i < n; i++)
    {
      for (size_t y = 0; y < m[i]->size1; y++)
        for (size_t x = 0; x < m[i]->size2; x++)
          gsl_matrix_set (block, r + y, c + x, gsl_matrix_get (m[i], y, x));
      r += m[i]->size1;
      c += m[i]->size2;
    }
  return block;
}

static gsl_matrix *
matrix_eval_CHOL (gsl_matrix *m, const struct matrix_expr *e)
{
  if (!gsl_linalg_cholesky_decomp1 (m))
    {
      for (size_t y = 0; y < m->size1; y++)
        for (size_t x = y + 1; x < m->size2; x++)
          gsl_matrix_set (m, y, x, gsl_matrix_get (m, x, y));

      for (size_t y = 0; y < m->size1; y++)
        for (size_t x = 0; x < y; x++)
          gsl_matrix_set (m, y, x, 0);
      return m;
    }
  else
    {
      msg_at (SE, e->subs[0]->location,
              _("Input to CHOL function is not positive-definite."));
      return NULL;
    }
}

static gsl_matrix *
matrix_eval_col_extremum (gsl_matrix *m, bool min)
{
  if (m->size1 <= 1)
    return m;
  else if (!m->size2)
    return gsl_matrix_alloc (1, 0);

  gsl_matrix *cext = gsl_matrix_alloc (1, m->size2);
  for (size_t x = 0; x < m->size2; x++)
    {
      double ext = gsl_matrix_get (m, 0, x);
      for (size_t y = 1; y < m->size1; y++)
        {
          double value = gsl_matrix_get (m, y, x);
          if (min ? value < ext : value > ext)
            ext = value;
        }
      gsl_matrix_set (cext, 0, x, ext);
    }
  return cext;
}

static gsl_matrix *
matrix_eval_CMAX (gsl_matrix *m)
{
  return matrix_eval_col_extremum (m, false);
}

static gsl_matrix *
matrix_eval_CMIN (gsl_matrix *m)
{
  return matrix_eval_col_extremum (m, true);
}

static double
matrix_eval_COS (double d)
{
  return cos (d);
}

static gsl_matrix *
matrix_eval_col_sum (gsl_matrix *m, bool square)
{
  if (m->size1 == 0)
    return m;
  else if (!m->size2)
    return gsl_matrix_alloc (1, 0);

  gsl_matrix *result = gsl_matrix_alloc (1, m->size2);
  for (size_t x = 0; x < m->size2; x++)
    {
      double sum = 0;
      for (size_t y = 0; y < m->size1; y++)
        {
          double d = gsl_matrix_get (m, y, x);
          sum += square ? pow2 (d) : d;
        }
      gsl_matrix_set (result, 0, x, sum);
    }
  return result;
}

static gsl_matrix *
matrix_eval_CSSQ (gsl_matrix *m)
{
  return matrix_eval_col_sum (m, true);
}

static gsl_matrix *
matrix_eval_CSUM (gsl_matrix *m)
{
  return matrix_eval_col_sum (m, false);
}

static int
compare_double_3way (const void *a_, const void *b_)
{
  const double *a = a_;
  const double *b = b_;
  return *a < *b ? -1 : *a > *b;
}

static gsl_matrix *
matrix_eval_DESIGN (gsl_matrix *m, const struct matrix_expr *e)
{
  double *tmp = xmalloc (m->size1 * m->size2 * sizeof *tmp);
  gsl_matrix m2 = gsl_matrix_view_array (tmp, m->size2, m->size1).matrix;
  gsl_matrix_transpose_memcpy (&m2, m);

  for (size_t y = 0; y < m2.size1; y++)
    qsort (tmp + y * m2.size2, m2.size2, sizeof *tmp, compare_double_3way);

  size_t *n = xcalloc (m2.size1, sizeof *n);
  size_t n_total = 0;
  for (size_t i = 0; i < m2.size1; i++)
    {
      double *row = tmp + m2.size2 * i;
      for (size_t j = 0; j < m2.size2; )
        {
          size_t k;
          for (k = j + 1; k < m2.size2; k++)
            if (row[j] != row[k])
              break;
          row[n[i]++] = row[j];
          j = k;
        }

      if (n[i] <= 1)
        msg_at (MW, e->subs[0]->location,
                _("Column %zu in DESIGN argument has constant value."), i + 1);
      else
        n_total += n[i];
    }

  gsl_matrix *result = gsl_matrix_alloc (m->size1, n_total);
  size_t x = 0;
  for (size_t i = 0; i < m->size2; i++)
    {
      if (n[i] <= 1)
        continue;

      const double *unique = tmp + m2.size2 * i;
      for (size_t j = 0; j < n[i]; j++, x++)
        {
          double value = unique[j];
          for (size_t y = 0; y < m->size1; y++)
            gsl_matrix_set (result, y, x, gsl_matrix_get (m, y, i) == value);
        }
    }

  free (n);
  free (tmp);

  return result;
}

static double
matrix_eval_DET (gsl_matrix *m)
{
  gsl_permutation *p = gsl_permutation_alloc (m->size1);
  int signum;
  gsl_linalg_LU_decomp (m, p, &signum);
  gsl_permutation_free (p);
  return gsl_linalg_LU_det (m, signum);
}

static gsl_matrix *
matrix_eval_DIAG (gsl_matrix *m)
{
  gsl_matrix *diag = gsl_matrix_alloc (MIN (m->size1, m->size2), 1);
  for (size_t i = 0; i < diag->size1; i++)
    gsl_matrix_set (diag, i, 0, gsl_matrix_get (m, i, i));
  return diag;
}

static bool
is_symmetric (const gsl_matrix *m)
{
  if (m->size1 != m->size2)
    return false;

  for (size_t y = 0; y < m->size1; y++)
    for (size_t x = 0; x < y; x++)
      if (gsl_matrix_get (m, y, x) != gsl_matrix_get (m, x, y))
        return false;

  return true;
}

static int
compare_double_desc (const void *a_, const void *b_)
{
  const double *a = a_;
  const double *b = b_;
  return *a > *b ? -1 : *a < *b;
}

static gsl_matrix *
matrix_eval_EVAL (gsl_matrix *m, const struct matrix_expr *e)
{
  if (!is_symmetric (m))
    {
      msg_at (SE, e->subs[0]->location,
              _("Argument of EVAL must be symmetric."));
      return NULL;
    }

  gsl_eigen_symm_workspace *w = gsl_eigen_symm_alloc (m->size1);
  gsl_matrix *eval = gsl_matrix_alloc (m->size1, 1);
  gsl_vector v_eval = to_vector (eval);
  gsl_eigen_symm (m, &v_eval, w);
  gsl_eigen_symm_free (w);

  assert (v_eval.stride == 1);
  qsort (v_eval.data, v_eval.size, sizeof *v_eval.data, compare_double_desc);

  return eval;
}

static double
matrix_eval_EXP (double d)
{
  return exp (d);
}

/* From https://gist.github.com/turingbirds/5e99656e08dbe1324c99, where it was
   marked as:

   Charl Linssen <charl@itfromb.it>
   Feb 2016
   PUBLIC DOMAIN */
static gsl_matrix *
matrix_eval_GINV (gsl_matrix *A)
{
  size_t n = A->size1;
  size_t m = A->size2;
  bool swap = m > n;
  gsl_matrix *tmp_mat = NULL;
  if (swap)
    {
      /* libgsl SVD can only handle the case m <= n, so transpose matrix. */
      tmp_mat = gsl_matrix_alloc (m, n);
      gsl_matrix_transpose_memcpy (tmp_mat, A);
      A = tmp_mat;
      size_t i = m;
      m = n;
      n = i;
    }

  /* Do SVD. */
  gsl_matrix *V = gsl_matrix_alloc (m, m);
  gsl_vector *u = gsl_vector_alloc (m);

  gsl_vector *tmp_vec = gsl_vector_alloc (m);
  gsl_linalg_SV_decomp (A, V, u, tmp_vec);
  gsl_vector_free (tmp_vec);

  /* Compute Σ⁻¹. */
  gsl_matrix *Sigma_pinv = gsl_matrix_alloc (m, n);
  gsl_matrix_set_zero (Sigma_pinv);
  double cutoff = 1e-15 * gsl_vector_max (u);

  for (size_t i = 0; i < m; ++i)
    {
      double x = gsl_vector_get (u, i);
      gsl_matrix_set (Sigma_pinv, i, i, x > cutoff ? 1.0 / x : 0);
    }

  /* libgsl SVD yields "thin" SVD.  Pad to full matrix by adding zeros. */
  gsl_matrix *U = gsl_matrix_calloc (n, n);
  for (size_t i = 0; i < n; i++)
    for (size_t j = 0; j < m; j++)
      gsl_matrix_set (U, i, j, gsl_matrix_get (A, i, j));

  /* Two dot products to obtain pseudoinverse. */
  gsl_matrix *tmp_mat2 = gsl_matrix_alloc (m, n);
  gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1., V, Sigma_pinv, 0., tmp_mat2);

  gsl_matrix *A_pinv;
  if (swap)
    {
      A_pinv = gsl_matrix_alloc (n, m);
      gsl_blas_dgemm (CblasNoTrans, CblasTrans, 1., U, tmp_mat2, 0., A_pinv);
    }
  else
    {
      A_pinv = gsl_matrix_alloc (m, n);
      gsl_blas_dgemm (CblasNoTrans, CblasTrans, 1., tmp_mat2, U, 0., A_pinv);
    }

  gsl_matrix_free (tmp_mat);
  gsl_matrix_free (tmp_mat2);
  gsl_matrix_free (U);
  gsl_matrix_free (Sigma_pinv);
  gsl_vector_free (u);
  gsl_matrix_free (V);

  return A_pinv;
}

struct grade
  {
    size_t y, x;
    double value;
  };

static int
grade_compare_3way (const void *a_, const void *b_)
{
  const struct grade *a = a_;
  const struct grade *b = b_;

  return (a->value < b->value ? -1
          : a->value > b->value ? 1
          : a->y < b->y ? -1
          : a->y > b->y ? 1
          : a->x < b->x ? -1
          : a->x > b->x);
}

static gsl_matrix *
matrix_eval_GRADE (gsl_matrix *m)
{
  size_t n = m->size1 * m->size2;
  struct grade *grades = xmalloc (n * sizeof *grades);

  size_t i = 0;
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    grades[i++] = (struct grade) { .y = y, .x = x, .value = *d };
  qsort (grades, n, sizeof *grades, grade_compare_3way);

  for (size_t i = 0; i < n; i++)
    gsl_matrix_set (m, grades[i].y, grades[i].x, i + 1);

  free (grades);

  return m;
}

static double
dot (gsl_vector *a, gsl_vector *b)
{
  double result = 0.0;
  for (size_t i = 0; i < a->size; i++)
    result += gsl_vector_get (a, i) * gsl_vector_get (b, i);
  return result;
}

static double
norm2 (gsl_vector *v)
{
  double result = 0.0;
  for (size_t i = 0; i < v->size; i++)
    result += pow2 (gsl_vector_get (v, i));
  return result;
}

static double
norm (gsl_vector *v)
{
  return sqrt (norm2 (v));
}

static gsl_matrix *
matrix_eval_GSCH (gsl_matrix *v, const struct matrix_expr *e)
{
  if (v->size2 < v->size1)
    {
      msg_at (SE, e->subs[0]->location,
              _("GSCH requires its argument to have at least as many columns "
                "as rows, but it has dimensions %zu×%zu."),
              v->size1, v->size2);
      return NULL;
    }
  if (!v->size1 || !v->size2)
    return v;

  gsl_matrix *u = gsl_matrix_calloc (v->size1, v->size2);
  size_t ux = 0;
  for (size_t vx = 0; vx < v->size2; vx++)
    {
      gsl_vector u_i = gsl_matrix_column (u, ux).vector;
      gsl_vector v_i = gsl_matrix_column (v, vx).vector;

      gsl_vector_memcpy (&u_i, &v_i);
      for (size_t j = 0; j < ux; j++)
        {
          gsl_vector u_j = gsl_matrix_column (u, j).vector;
          double scale = dot (&u_j, &u_i) / norm2 (&u_j);
          for (size_t k = 0; k < u_i.size; k++)
            gsl_vector_set (&u_i, k, (gsl_vector_get (&u_i, k)
                                      - scale * gsl_vector_get (&u_j, k)));
        }

      double len = norm (&u_i);
      if (len > 1e-15)
        {
          gsl_vector_scale (&u_i, 1.0 / len);
          if (++ux >= v->size1)
            break;
        }
    }

  if (ux < v->size1)
    {
      msg_at (SE, e->subs[0]->location,
              _("%zu×%zu argument to GSCH contains only "
                "%zu linearly independent columns."),
              v->size1, v->size2, ux);
      gsl_matrix_free (u);
      return NULL;
    }

  u->size2 = v->size1;
  return u;
}

static gsl_matrix *
matrix_eval_IDENT (double s1, double s2)
{
  gsl_matrix *m = gsl_matrix_alloc (s1, s2);
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    *d = x == y;
  return m;
}

/* Inverts X, storing the inverse into INVERSE.  As a side effect, replaces X
   by its LU decomposition. */
static void
invert_matrix (gsl_matrix *x, gsl_matrix *inverse)
{
  gsl_permutation *p = gsl_permutation_alloc (x->size1);
  int signum;
  gsl_linalg_LU_decomp (x, p, &signum);
  gsl_linalg_LU_invert (x, p, inverse);
  gsl_permutation_free (p);
}

static gsl_matrix *
matrix_eval_INV (gsl_matrix *src)
{
  gsl_matrix *dst = gsl_matrix_alloc (src->size1, src->size2);
  invert_matrix (src, dst);
  return dst;
}

static gsl_matrix *
matrix_eval_KRONEKER (gsl_matrix *a, gsl_matrix *b)
{
  gsl_matrix *k = gsl_matrix_alloc (a->size1 * b->size1,
                                    a->size2 * b->size2);
  size_t y = 0;
  for (size_t ar = 0; ar < a->size1; ar++)
    for (size_t br = 0; br < b->size1; br++, y++)
      {
        size_t x = 0;
        for (size_t ac = 0; ac < a->size2; ac++)
          for (size_t bc = 0; bc < b->size2; bc++, x++)
            {
              double av = gsl_matrix_get (a, ar, ac);
              double bv = gsl_matrix_get (b, br, bc);
              gsl_matrix_set (k, y, x, av * bv);
            }
      }
  return k;
}

static double
matrix_eval_LG10 (double d)
{
  return log10 (d);
}

static double
matrix_eval_LN (double d)
{
  return log (d);
}

static void
matrix_eval_MAGIC_odd (gsl_matrix *m, size_t n)
{
  /* Siamese method: https://en.wikipedia.org/wiki/Siamese_method. */
  size_t y = 0;
  size_t x = n / 2;
  for (size_t i = 1; i <= n * n; i++)
    {
      gsl_matrix_set (m, y, x, i);

      size_t y1 = !y ? n - 1 : y - 1;
      size_t x1 = x + 1 >= n ? 0 : x + 1;
      if (gsl_matrix_get (m, y1, x1) == 0)
        {
          y = y1;
          x = x1;
        }
      else
        y = y + 1 >= n ? 0 : y + 1;
    }
}

static void
magic_exchange (gsl_matrix *m, size_t y1, size_t x1, size_t y2, size_t x2)
{
  double a = gsl_matrix_get (m, y1, x1);
  double b = gsl_matrix_get (m, y2, x2);
  gsl_matrix_set (m, y1, x1, b);
  gsl_matrix_set (m, y2, x2, a);
}

static void
matrix_eval_MAGIC_doubly_even (gsl_matrix *m, size_t n)
{
  size_t x, y;

  /* A. Umar, "On the Construction of Even Order Magic Squares",
     https://arxiv.org/ftp/arxiv/papers/1202/1202.0948.pdf. */
  x = y = 0;
  for (size_t i = 1; i <= n * n / 2; i++)
    {
      gsl_matrix_set (m, y, x, i);
      if (++y >= n)
        {
          y = 0;
          x++;
        }
    }

  x = n - 1;
  y = 0;
  for (size_t i = n * n; i > n * n / 2; i--)
    {
      gsl_matrix_set (m, y, x, i);
      if (++y >= n)
        {
          y = 0;
          x--;
        }
    }

  for (size_t y = 0; y < n; y++)
    for (size_t x = 0; x < n / 2; x++)
      {
        unsigned int d = gsl_matrix_get (m, y, x);
        if (d % 2 != (y < n / 2))
          magic_exchange (m, y, x, y, n - x - 1);
      }

  size_t y1 = n / 2;
  size_t y2 = n - 1;
  size_t x1 = n / 2 - 1;
  size_t x2 = n / 2;
  magic_exchange (m, y1, x1, y2, x1);
  magic_exchange (m, y1, x2, y2, x2);
}

static void
matrix_eval_MAGIC_singly_even (gsl_matrix *m, size_t n)
{
  /* A. Umar, "On the Construction of Even Order Magic Squares",
     https://arxiv.org/ftp/arxiv/papers/1202/1202.0948.pdf. */
  size_t x, y;

  x = y = 0;
  for (size_t i = 1; ; i++)
    {
      gsl_matrix_set (m, y, x, i);
      if (++y == n / 2 - 1)
        y += 2;
      else if (y >= n)
        {
          y = 0;
          if (++x >= n / 2)
            break;
        }
    }

  x = n - 1;
  y = 0;
  for (size_t i = n * n; ; i--)
    {
      gsl_matrix_set (m, y, x, i);
      if (++y == n / 2 - 1)
        y += 2;
      else if (y >= n)
        {
          y = 0;
          if (--x < n / 2)
            break;
        }
    }
  for (size_t y = 0; y < n; y++)
    if (y != n / 2 - 1 && y != n / 2)
      for (size_t x = 0; x < n / 2; x++)
        {
          unsigned int d = gsl_matrix_get (m, y, x);
          if (d % 2 != (y < n / 2))
            magic_exchange (m, y, x, y, n - x - 1);
        }

  size_t a0 = (n * n - 2 * n) / 2 + 1;
  for (size_t i = 0; i < n / 2; i++)
    {
      size_t a = a0 + i;
      gsl_matrix_set (m, n / 2, i, a);
      gsl_matrix_set (m, n / 2 - 1, i, (n * n + 1) - a);
    }
  for (size_t i = 0; i < n / 2; i++)
    {
      size_t a = a0 + i + n / 2;
      gsl_matrix_set (m, n / 2 - 1, n - i - 1, a);
      gsl_matrix_set (m, n / 2, n - i - 1, (n * n + 1) - a);
    }
  for (size_t x = 1; x < n / 2; x += 2)
    magic_exchange (m, n / 2, x, n / 2 - 1, x);
  for (size_t x = n / 2 + 2; x <= n - 3; x += 2)
    magic_exchange (m, n / 2, x, n / 2 - 1, x);
  size_t x1 = n / 2 - 2;
  size_t x2 = n / 2 + 1;
  size_t y1 = n / 2 - 2;
  size_t y2 = n / 2 + 1;
  magic_exchange (m, y1, x1, y2, x1);
  magic_exchange (m, y1, x2, y2, x2);
}

static gsl_matrix *
matrix_eval_MAGIC (double n_)
{
  size_t n = n_;

  gsl_matrix *m = gsl_matrix_calloc (n, n);
  if (n % 2)
    matrix_eval_MAGIC_odd (m, n);
  else if (n % 4)
    matrix_eval_MAGIC_singly_even (m, n);
  else
    matrix_eval_MAGIC_doubly_even (m, n);
  return m;
}

static gsl_matrix *
matrix_eval_MAKE (double r, double c, double value)
{
  gsl_matrix *m = gsl_matrix_alloc (r, c);
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    *d = value;
  return m;
}

static gsl_matrix *
matrix_eval_MDIAG (gsl_vector *v)
{
  gsl_matrix *m = gsl_matrix_calloc (v->size, v->size);
  gsl_vector diagonal = gsl_matrix_diagonal (m).vector;
  gsl_vector_memcpy (&diagonal, v);
  return m;
}

static double
matrix_eval_MMAX (gsl_matrix *m)
{
  return gsl_matrix_max (m);
}

static double
matrix_eval_MMIN (gsl_matrix *m)
{
  return gsl_matrix_min (m);
}

static gsl_matrix *
matrix_eval_MOD (gsl_matrix *m, double divisor)
{
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    *d = fmod (*d, divisor);
  return m;
}

static double
matrix_eval_MSSQ (gsl_matrix *m)
{
  double mssq = 0.0;
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    mssq += *d * *d;
  return mssq;
}

static double
matrix_eval_MSUM (gsl_matrix *m)
{
  double msum = 0.0;
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    msum += *d;
  return msum;
}

static double
matrix_eval_NCOL (gsl_matrix *m)
{
  return m->size2;
}

static double
matrix_eval_NROW (gsl_matrix *m)
{
  return m->size1;
}

static double
matrix_eval_RANK (gsl_matrix *m)
{
  gsl_vector *tau = gsl_vector_alloc (MIN (m->size1, m->size2));
  gsl_linalg_QR_decomp (m, tau);
  gsl_vector_free (tau);

  return gsl_linalg_QRPT_rank (m, -1);
}

static gsl_matrix *
matrix_eval_RESHAPE (gsl_matrix *m, double r_, double c_,
                     const struct matrix_expr *e)
{
  bool r_ok = r_ >= 0 && r_ < SIZE_MAX;
  bool c_ok = c_ >= 0 && c_ < SIZE_MAX;
  if (!r_ok || !c_ok)
    {
      msg_at (SE,
              !r_ok ? e->subs[1]->location : e->subs[2]->location,
              _("Arguments 2 and 3 to RESHAPE must be integers."));
      return NULL;
    }
  size_t r = r_;
  size_t c = c_;
  if (size_overflow_p (xtimes (r, xmax (c, 1))) || c * r != m->size1 * m->size2)
    {
      struct msg_location *loc = msg_location_dup (e->subs[1]->location);
      loc->end = e->subs[2]->location->end;
      msg_at (SE, loc, _("Product of RESHAPE size arguments (%zu×%zu = %zu) "
                         "differs from product of matrix dimensions "
                         "(%zu×%zu = %zu)."),
              r, c, r * c,
              m->size1, m->size2, m->size1 * m->size2);
      msg_location_destroy (loc);
      return NULL;
    }

  gsl_matrix *dst = gsl_matrix_alloc (r, c);
  size_t y1 = 0;
  size_t x1 = 0;
  MATRIX_FOR_ALL_ELEMENTS (d, y2, x2, m)
    {
      gsl_matrix_set (dst, y1, x1, *d);
      if (++x1 >= c)
        {
          x1 = 0;
          y1++;
        }
    }
  return dst;
}

static gsl_matrix *
matrix_eval_row_extremum (gsl_matrix *m, bool min)
{
  if (m->size2 <= 1)
    return m;
  else if (!m->size1)
    return gsl_matrix_alloc (0, 1);

  gsl_matrix *rext = gsl_matrix_alloc (m->size1, 1);
  for (size_t y = 0; y < m->size1; y++)
    {
      double ext = gsl_matrix_get (m, y, 0);
      for (size_t x = 1; x < m->size2; x++)
        {
          double value = gsl_matrix_get (m, y, x);
          if (min ? value < ext : value > ext)
            ext = value;
        }
      gsl_matrix_set (rext, y, 0, ext);
    }
  return rext;
}

static gsl_matrix *
matrix_eval_RMAX (gsl_matrix *m)
{
  return matrix_eval_row_extremum (m, false);
}

static gsl_matrix *
matrix_eval_RMIN (gsl_matrix *m)
{
  return matrix_eval_row_extremum (m, true);
}

static double
matrix_eval_RND (double d)
{
  return rint (d);
}

struct rank
  {
    size_t y, x;
    double value;
  };

static int
rank_compare_3way (const void *a_, const void *b_)
{
  const struct rank *a = a_;
  const struct rank *b = b_;

  return a->value < b->value ? -1 : a->value > b->value;
}

static gsl_matrix *
matrix_eval_RNKORDER (gsl_matrix *m)
{
  size_t n = m->size1 * m->size2;
  struct rank *ranks = xmalloc (n * sizeof *ranks);
  size_t i = 0;
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    ranks[i++] = (struct rank) { .y = y, .x = x, .value = *d };
  qsort (ranks, n, sizeof *ranks, rank_compare_3way);

  for (size_t i = 0; i < n; )
    {
      size_t j;
      for (j = i + 1; j < n; j++)
        if (ranks[i].value != ranks[j].value)
          break;

      double rank = (i + j + 1.0) / 2.0;
      for (size_t k = i; k < j; k++)
        gsl_matrix_set (m, ranks[k].y, ranks[k].x, rank);

      i = j;
    }

  free (ranks);

  return m;
}

static gsl_matrix *
matrix_eval_row_sum (gsl_matrix *m, bool square)
{
  if (m->size1 == 0)
    return m;
  else if (!m->size1)
    return gsl_matrix_alloc (0, 1);

  gsl_matrix *result = gsl_matrix_alloc (m->size1, 1);
  for (size_t y = 0; y < m->size1; y++)
    {
      double sum = 0;
      for (size_t x = 0; x < m->size2; x++)
        {
          double d = gsl_matrix_get (m, y, x);
          sum += square ? pow2 (d) : d;
        }
      gsl_matrix_set (result, y, 0, sum);
    }
  return result;
}

static gsl_matrix *
matrix_eval_RSSQ (gsl_matrix *m)
{
  return matrix_eval_row_sum (m, true);
}

static gsl_matrix *
matrix_eval_RSUM (gsl_matrix *m)
{
  return matrix_eval_row_sum (m, false);
}

static double
matrix_eval_SIN (double d)
{
  return sin (d);
}

static gsl_matrix *
matrix_eval_SOLVE (gsl_matrix *m1, gsl_matrix *m2, const struct matrix_expr *e)
{
  if (m1->size1 != m2->size1)
    {
      struct msg_location *loc = msg_location_dup (e->subs[0]->location);
      loc->end = e->subs[1]->location->end;

      msg_at (SE, e->location,
              _("SOLVE arguments must have the same number of rows."));
      msg_at (SN, e->subs[0]->location,
              _("Argument 1 has dimensions %zu×%zu."), m1->size1, m1->size2);
      msg_at (SN, e->subs[1]->location,
              _("Argument 2 has dimensions %zu×%zu."), m2->size1, m2->size2);

      msg_location_destroy (loc);
      return NULL;
    }

  gsl_matrix *x = gsl_matrix_alloc (m2->size1, m2->size2);
  gsl_permutation *p = gsl_permutation_alloc (m1->size1);
  int signum;
  gsl_linalg_LU_decomp (m1, p, &signum);
  for (size_t i = 0; i < m2->size2; i++)
    {
      gsl_vector bi = gsl_matrix_column (m2, i).vector;
      gsl_vector xi = gsl_matrix_column (x, i).vector;
      gsl_linalg_LU_solve (m1, p, &bi, &xi);
    }
  gsl_permutation_free (p);
  return x;
}

static double
matrix_eval_SQRT (double d)
{
  return sqrt (d);
}

static gsl_matrix *
matrix_eval_SSCP (gsl_matrix *m)
{
  gsl_matrix *sscp = gsl_matrix_alloc (m->size2, m->size2);
  gsl_blas_dgemm (CblasTrans, CblasNoTrans, 1.0, m, m, 0.0, sscp);
  return sscp;
}

static gsl_matrix *
matrix_eval_SVAL (gsl_matrix *m)
{
  gsl_matrix *tmp_mat = NULL;
  if (m->size2 > m->size1)
    {
      tmp_mat = gsl_matrix_alloc (m->size2, m->size1);
      gsl_matrix_transpose_memcpy (tmp_mat, m);
      m = tmp_mat;
    }

  /* Do SVD. */
  gsl_matrix *V = gsl_matrix_alloc (m->size2, m->size2);
  gsl_vector *S = gsl_vector_alloc (m->size2);
  gsl_vector *work = gsl_vector_alloc (m->size2);
  gsl_linalg_SV_decomp (m, V, S, work);

  gsl_matrix *vals = gsl_matrix_alloc (m->size2, 1);
  for (size_t i = 0; i < m->size2; i++)
    gsl_matrix_set (vals, i, 0, gsl_vector_get (S, i));

  gsl_matrix_free (V);
  gsl_vector_free (S);
  gsl_vector_free (work);
  gsl_matrix_free (tmp_mat);

  return vals;
}

static gsl_matrix *
matrix_eval_SWEEP (gsl_matrix *m, double d, const struct matrix_expr *e)
{
  if (d < 1 || d > SIZE_MAX)
    {
      msg_at (SE, e->subs[1]->location,
              _("Scalar argument to SWEEP must be integer."));
      return NULL;
    }
  size_t k = d - 1;
  if (k >= MIN (m->size1, m->size2))
    {
      msg_at (SE, e->subs[1]->location,
              _("Scalar argument to SWEEP must be integer less than or "
                "equal to the smaller of the matrix argument's rows and "
                "columns."));
      return NULL;
    }

  double m_kk = gsl_matrix_get (m, k, k);
  if (fabs (m_kk) > 1e-19)
    {
      gsl_matrix *a = gsl_matrix_alloc (m->size1, m->size2);
      MATRIX_FOR_ALL_ELEMENTS (a_ij, i, j, a)
        {
          double m_ij = gsl_matrix_get (m, i, j);
          double m_ik = gsl_matrix_get (m, i, k);
          double m_kj = gsl_matrix_get (m, k, j);
          *a_ij = (i != k && j != k ? m_ij * m_kk - m_ik * m_kj
                   : i != k ? -m_ik
                   : j != k ? m_kj
                   : 1.0) / m_kk;
        }
      return a;
    }
  else
    {
      for (size_t i = 0; i < m->size1; i++)
        {
          gsl_matrix_set (m, i, k, 0);
          gsl_matrix_set (m, k, i, 0);
        }
      return m;
    }
}

static double
matrix_eval_TRACE (gsl_matrix *m)
{
  double sum = 0;
  size_t n = MIN (m->size1, m->size2);
  for (size_t i = 0; i < n; i++)
    sum += gsl_matrix_get (m, i, i);
  return sum;
}

static gsl_matrix *
matrix_eval_T (gsl_matrix *m)
{
  return matrix_eval_TRANSPOS (m);
}

static gsl_matrix *
matrix_eval_TRANSPOS (gsl_matrix *m)
{
  if (m->size1 == m->size2)
    {
      gsl_matrix_transpose (m);
      return m;
    }
  else
    {
      gsl_matrix *t = gsl_matrix_alloc (m->size2, m->size1);
      gsl_matrix_transpose_memcpy (t, m);
      return t;
    }
}

static double
matrix_eval_TRUNC (double d)
{
  return trunc (d);
}

static gsl_matrix *
matrix_eval_UNIFORM (double r_, double c_, const struct matrix_expr *e)
{
  size_t r = r_;
  size_t c = c_;
  if (size_overflow_p (xtimes (r, xmax (c, 1))))
    {
      struct msg_location *loc = msg_location_dup (e->subs[0]->location);
      loc->end = e->subs[1]->location->end;

      msg_at (SE, loc,
              _("Product of arguments to UNIFORM exceeds memory size."));

      msg_location_destroy (loc);
      return NULL;
    }

  gsl_matrix *m = gsl_matrix_alloc (r, c);
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, m)
    *d = gsl_ran_flat (get_rng (), 0, 1);
  return m;
}

static double
matrix_eval_PDF_BETA (double x, double a, double b)
{
  return gsl_ran_beta_pdf (x, a, b);
}

static double
matrix_eval_CDF_BETA (double x, double a, double b)
{
  return gsl_cdf_beta_P (x, a, b);
}

static double
matrix_eval_IDF_BETA (double P, double a, double b)
{
  return gsl_cdf_beta_Pinv (P, a, b);
}

static double
matrix_eval_RV_BETA (double a, double b)
{
  return gsl_ran_beta (get_rng (), a, b);
}

static double
matrix_eval_NCDF_BETA (double x, double a, double b, double lambda)
{
  return ncdf_beta (x, a, b, lambda);
}

static double
matrix_eval_NPDF_BETA (double x, double a, double b, double lambda)
{
  return npdf_beta (x, a, b, lambda);
}

static double
matrix_eval_CDF_BVNOR (double x0, double x1, double r)
{
  return cdf_bvnor (x0, x1, r);
}

static double
matrix_eval_PDF_BVNOR (double x0, double x1, double r)
{
  return gsl_ran_bivariate_gaussian_pdf (x0, x1, 1, 1, r);
}

static double
matrix_eval_CDF_CAUCHY (double x, double a, double b)
{
  return gsl_cdf_cauchy_P ((x - a) / b, 1);
}

static double
matrix_eval_IDF_CAUCHY (double P, double a, double b)
{
  return a + b * gsl_cdf_cauchy_Pinv (P, 1);
}

static double
matrix_eval_PDF_CAUCHY (double x, double a, double b)
{
  return gsl_ran_cauchy_pdf ((x - a) / b, 1) / b;
}

static double
matrix_eval_RV_CAUCHY (double a, double b)
{
  return a + b * gsl_ran_cauchy (get_rng (), 1);
}

static double
matrix_eval_CDF_CHISQ (double x, double df)
{
  return gsl_cdf_chisq_P (x, df);
}

static double
matrix_eval_CHICDF (double x, double df)
{
  return matrix_eval_CDF_CHISQ (x, df);
}

static double
matrix_eval_IDF_CHISQ (double P, double df)
{
  return gsl_cdf_chisq_Pinv (P, df);
}

static double
matrix_eval_PDF_CHISQ (double x, double df)
{
  return gsl_ran_chisq_pdf (x, df);
}

static double
matrix_eval_RV_CHISQ (double df)
{
  return gsl_ran_chisq (get_rng (), df);
}

static double
matrix_eval_SIG_CHISQ (double x, double df)
{
  return gsl_cdf_chisq_Q (x, df);
}

static double
matrix_eval_CDF_EXP (double x, double a)
{
  return gsl_cdf_exponential_P (x, 1. / a);
}

static double
matrix_eval_IDF_EXP (double P, double a)
{
  return gsl_cdf_exponential_Pinv (P, 1. / a);
}

static double
matrix_eval_PDF_EXP (double x, double a)
{
  return gsl_ran_exponential_pdf (x, 1. / a);
}

static double
matrix_eval_RV_EXP (double a)
{
  return gsl_ran_exponential (get_rng (), 1. / a);
}

static double
matrix_eval_PDF_XPOWER (double x, double a, double b)
{
  return gsl_ran_exppow_pdf (x, a, b);
}

static double
matrix_eval_RV_XPOWER (double a, double b)
{
  return gsl_ran_exppow (get_rng (), a, b);
}

static double
matrix_eval_CDF_F (double x, double df1, double df2)
{
  return gsl_cdf_fdist_P (x, df1, df2);
}

static double
matrix_eval_FCDF (double x, double df1, double df2)
{
  return matrix_eval_CDF_F (x, df1, df2);
}

static double
matrix_eval_IDF_F (double P, double df1, double df2)
{
  return idf_fdist (P, df1, df2);
}

static double
matrix_eval_RV_F (double df1, double df2)
{
  return gsl_ran_fdist (get_rng (), df1, df2);
}

static double
matrix_eval_PDF_F (double x, double df1, double df2)
{
  return gsl_ran_fdist_pdf (x, df1, df2);
}

static double
matrix_eval_SIG_F (double x, double df1, double df2)
{
  return gsl_cdf_fdist_Q (x, df1, df2);
}

static double
matrix_eval_CDF_GAMMA (double x, double a, double b)
{
  return gsl_cdf_gamma_P (x, a, 1. / b);
}

static double
matrix_eval_IDF_GAMMA (double P, double a, double b)
{
  return gsl_cdf_gamma_Pinv (P, a, 1. / b);
}

static double
matrix_eval_PDF_GAMMA (double x, double a, double b)
{
  return gsl_ran_gamma_pdf (x, a, 1. / b);
}

static double
matrix_eval_RV_GAMMA (double a, double b)
{
  return gsl_ran_gamma (get_rng (), a, 1. / b);
}

static double
matrix_eval_PDF_LANDAU (double x)
{
  return gsl_ran_landau_pdf (x);
}

static double
matrix_eval_RV_LANDAU (void)
{
  return gsl_ran_landau (get_rng ());
}

static double
matrix_eval_CDF_LAPLACE (double x, double a, double b)
{
  return gsl_cdf_laplace_P ((x - a) / b, 1);
}

static double
matrix_eval_IDF_LAPLACE (double P, double a, double b)
{
  return a + b * gsl_cdf_laplace_Pinv (P, 1);
}

static double
matrix_eval_PDF_LAPLACE (double x, double a, double b)
{
  return gsl_ran_laplace_pdf ((x - a) / b, 1);
}

static double
matrix_eval_RV_LAPLACE (double a, double b)
{
  return a + b * gsl_ran_laplace (get_rng (), 1);
}

static double
matrix_eval_RV_LEVY (double c, double alpha)
{
  return gsl_ran_levy (get_rng (), c, alpha);
}

static double
matrix_eval_RV_LVSKEW (double c, double alpha, double beta)
{
  return gsl_ran_levy_skew (get_rng (), c, alpha, beta);
}

static double
matrix_eval_CDF_LOGISTIC (double x, double a, double b)
{
  return gsl_cdf_logistic_P ((x - a) / b, 1);
}

static double
matrix_eval_IDF_LOGISTIC (double P, double a, double b)
{
  return a + b * gsl_cdf_logistic_Pinv (P, 1);
}

static double
matrix_eval_PDF_LOGISTIC (double x, double a, double b)
{
  return gsl_ran_logistic_pdf ((x - a) / b, 1) / b;
}

static double
matrix_eval_RV_LOGISTIC (double a, double b)
{
  return a + b * gsl_ran_logistic (get_rng (), 1);
}

static double
matrix_eval_CDF_LNORMAL (double x, double m, double s)
{
  return gsl_cdf_lognormal_P (x, log (m), s);
}

static double
matrix_eval_IDF_LNORMAL (double P, double m, double s)
{
  return gsl_cdf_lognormal_Pinv (P, log (m), s);;
}

static double
matrix_eval_PDF_LNORMAL (double x, double m, double s)
{
  return gsl_ran_lognormal_pdf (x, log (m), s);
}

static double
matrix_eval_RV_LNORMAL (double m, double s)
{
  return gsl_ran_lognormal (get_rng (), log (m), s);
}

static double
matrix_eval_CDF_NORMAL (double x, double u, double s)
{
  return gsl_cdf_gaussian_P (x - u, s);
}

static double
matrix_eval_IDF_NORMAL (double P, double u, double s)
{
  return u + gsl_cdf_gaussian_Pinv (P, s);
}

static double
matrix_eval_PDF_NORMAL (double x, double u, double s)
{
  return gsl_ran_gaussian_pdf ((x - u) / s, 1) / s;
}

static double
matrix_eval_RV_NORMAL (double u, double s)
{
  return u + gsl_ran_gaussian (get_rng (), s);
}

static double
matrix_eval_CDFNORM (double x)
{
  return gsl_cdf_ugaussian_P (x);
}

static double
matrix_eval_PROBIT (double P)
{
  return gsl_cdf_ugaussian_Pinv (P);
}

static double
matrix_eval_NORMAL (double s)
{
  return gsl_ran_gaussian (get_rng (), s);
}

static double
matrix_eval_PDF_NTAIL (double x, double a, double sigma)
{
  return gsl_ran_gaussian_tail_pdf (x, a, sigma);;
}

static double
matrix_eval_RV_NTAIL (double a, double sigma)
{
  return gsl_ran_gaussian_tail (get_rng (), a, sigma);
}

static double
matrix_eval_CDF_PARETO (double x, double a, double b)
{
  return gsl_cdf_pareto_P (x, b, a);
}

static double
matrix_eval_IDF_PARETO (double P, double a, double b)
{
  return gsl_cdf_pareto_Pinv (P, b, a);
}

static double
matrix_eval_PDF_PARETO (double x, double a, double b)
{
  return gsl_ran_pareto_pdf (x, b, a);
}

static double
matrix_eval_RV_PARETO (double a, double b)
{
  return gsl_ran_pareto (get_rng (), b, a);
}

static double
matrix_eval_CDF_RAYLEIGH (double x, double sigma)
{
  return gsl_cdf_rayleigh_P (x, sigma);
}

static double
matrix_eval_IDF_RAYLEIGH (double P, double sigma)
{
  return gsl_cdf_rayleigh_Pinv (P, sigma);
}

static double
matrix_eval_PDF_RAYLEIGH (double x, double sigma)
{
  return gsl_ran_rayleigh_pdf (x, sigma);
}

static double
matrix_eval_RV_RAYLEIGH (double sigma)
{
  return gsl_ran_rayleigh (get_rng (), sigma);
}

static double
matrix_eval_PDF_RTAIL (double x, double a, double sigma)
{
  return gsl_ran_rayleigh_tail_pdf (x, a, sigma);
}

static double
matrix_eval_RV_RTAIL (double a, double sigma)
{
  return gsl_ran_rayleigh_tail (get_rng (), a, sigma);
}

static double
matrix_eval_CDF_T (double x, double df)
{
  return gsl_cdf_tdist_P (x, df);
}

static double
matrix_eval_TCDF (double x, double df)
{
  return matrix_eval_CDF_T (x, df);
}

static double
matrix_eval_IDF_T (double P, double df)
{
  return gsl_cdf_tdist_Pinv (P, df);
}

static double
matrix_eval_PDF_T (double x, double df)
{
  return gsl_ran_tdist_pdf (x, df);
}

static double
matrix_eval_RV_T (double df)
{
  return gsl_ran_tdist (get_rng (), df);
}

static double
matrix_eval_CDF_T1G (double x, double a, double b)
{
  return gsl_cdf_gumbel1_P (x, a, b);
}

static double
matrix_eval_IDF_T1G (double P, double a, double b)
{
  return gsl_cdf_gumbel1_Pinv (P, a, b);
}

static double
matrix_eval_PDF_T1G (double x, double a, double b)
{
  return gsl_ran_gumbel1_pdf (x, a, b);
}

static double
matrix_eval_RV_T1G (double a, double b)
{
  return gsl_ran_gumbel1 (get_rng (), a, b);
}

static double
matrix_eval_CDF_T2G (double x, double a, double b)
{
  return gsl_cdf_gumbel1_P (x, a, b);
}

static double
matrix_eval_IDF_T2G (double P, double a, double b)
{
  return gsl_cdf_gumbel1_Pinv (P, a, b);
}

static double
matrix_eval_PDF_T2G (double x, double a, double b)
{
  return gsl_ran_gumbel1_pdf (x, a, b);
}

static double
matrix_eval_RV_T2G (double a, double b)
{
  return gsl_ran_gumbel1 (get_rng (), a, b);
}

static double
matrix_eval_CDF_UNIFORM (double x, double a, double b)
{
  return gsl_cdf_flat_P (x, a, b);
}

static double
matrix_eval_IDF_UNIFORM (double P, double a, double b)
{
  return gsl_cdf_flat_Pinv (P, a, b);
}

static double
matrix_eval_PDF_UNIFORM (double x, double a, double b)
{
  return gsl_ran_flat_pdf (x, a, b);
}

static double
matrix_eval_RV_UNIFORM (double a, double b)
{
  return gsl_ran_flat (get_rng (), a, b);
}

static double
matrix_eval_CDF_WEIBULL (double x, double a, double b)
{
  return gsl_cdf_weibull_P (x, a, b);
}

static double
matrix_eval_IDF_WEIBULL (double P, double a, double b)
{
  return gsl_cdf_weibull_Pinv (P, a, b);
}

static double
matrix_eval_PDF_WEIBULL (double x, double a, double b)
{
  return gsl_ran_weibull_pdf (x, a, b);
}

static double
matrix_eval_RV_WEIBULL (double a, double b)
{
  return gsl_ran_weibull (get_rng (), a, b);
}

static double
matrix_eval_CDF_BERNOULLI (double k, double p)
{
  return k ? 1 : 1 - p;
}

static double
matrix_eval_PDF_BERNOULLI (double k, double p)
{
  return gsl_ran_bernoulli_pdf (k, p);
}

static double
matrix_eval_RV_BERNOULLI (double p)
{
  return gsl_ran_bernoulli (get_rng (), p);
}

static double
matrix_eval_CDF_BINOM (double k, double n, double p)
{
  return gsl_cdf_binomial_P (k, p, n);
}

static double
matrix_eval_PDF_BINOM (double k, double n, double p)
{
  return gsl_ran_binomial_pdf (k, p, n);
}

static double
matrix_eval_RV_BINOM (double n, double p)
{
  return gsl_ran_binomial (get_rng (), p, n);
}

static double
matrix_eval_CDF_GEOM (double k, double p)
{
  return gsl_cdf_geometric_P (k, p);
}

static double
matrix_eval_PDF_GEOM (double k, double p)
{
  return gsl_ran_geometric_pdf (k, p);
}

static double
matrix_eval_RV_GEOM (double p)
{
  return gsl_ran_geometric (get_rng (), p);
}

static double
matrix_eval_CDF_HYPER (double k, double a, double b, double c)
{
  return gsl_cdf_hypergeometric_P (k, c, a - c, b);
}

static double
matrix_eval_PDF_HYPER (double k, double a, double b, double c)
{
  return gsl_ran_hypergeometric_pdf (k, c, a - c, b);
}

static double
matrix_eval_RV_HYPER (double a, double b, double c)
{
  return gsl_ran_hypergeometric (get_rng (), c, a - c, b);
}

static double
matrix_eval_PDF_LOG (double k, double p)
{
  return gsl_ran_logarithmic_pdf (k, p);
}

static double
matrix_eval_RV_LOG (double p)
{
  return gsl_ran_logarithmic (get_rng (), p);
}

static double
matrix_eval_CDF_NEGBIN (double k, double n, double p)
{
  return gsl_cdf_negative_binomial_P (k, p, n);
}

static double
matrix_eval_PDF_NEGBIN (double k, double n, double p)
{
  return gsl_ran_negative_binomial_pdf (k, p, n);
}

static double
matrix_eval_RV_NEGBIN (double n, double p)
{
  return gsl_ran_negative_binomial (get_rng (), p, n);
}

static double
matrix_eval_CDF_POISSON (double k, double mu)
{
  return gsl_cdf_poisson_P (k, mu);
}

static double
matrix_eval_PDF_POISSON (double k, double mu)
{
  return gsl_ran_poisson_pdf (k, mu);
}

static double
matrix_eval_RV_POISSON (double mu)
{
  return gsl_ran_poisson (get_rng (), mu);
}

static double
matrix_op_eval (enum matrix_op op, double a, double b)
{
  switch (op)
    {
    case MOP_ADD_ELEMS: return a + b;
    case MOP_SUB_ELEMS: return a - b;
    case MOP_MUL_ELEMS: return a * b;
    case MOP_DIV_ELEMS: return a / b;
    case MOP_EXP_ELEMS: return pow (a, b);
    case MOP_GT: return a > b;
    case MOP_GE: return a >= b;
    case MOP_LT: return a < b;
    case MOP_LE: return a <= b;
    case MOP_EQ: return a == b;
    case MOP_NE: return a != b;
    case MOP_AND: return (a > 0) && (b > 0);
    case MOP_OR: return (a > 0) || (b > 0);
    case MOP_XOR: return (a > 0) != (b > 0);

#define F(ENUM, STRING, PROTO, CONSTRAINTS) case MOP_F_##ENUM:
      MATRIX_FUNCTIONS
#undef F
    case MOP_NEGATE:
    case MOP_SEQ:
    case MOP_SEQ_BY:
    case MOP_MUL_MAT:
    case MOP_EXP_MAT:
    case MOP_NOT:
    case MOP_PASTE_HORZ:
    case MOP_PASTE_VERT:
    case MOP_EMPTY:
    case MOP_VEC_INDEX:
    case MOP_VEC_ALL:
    case MOP_MAT_INDEX:
    case MOP_ROW_INDEX:
    case MOP_COL_INDEX:
    case MOP_NUMBER:
    case MOP_VARIABLE:
    case MOP_EOF:
      NOT_REACHED ();
    }
  NOT_REACHED ();
}

static const char *
matrix_op_name (enum matrix_op op)
{
  switch (op)
    {
    case MOP_ADD_ELEMS: return "+";
    case MOP_SUB_ELEMS: return "-";
    case MOP_MUL_ELEMS: return "&*";
    case MOP_DIV_ELEMS: return "&/";
    case MOP_EXP_ELEMS: return "&**";
    case MOP_GT: return ">";
    case MOP_GE: return ">=";
    case MOP_LT: return "<";
    case MOP_LE: return "<=";
    case MOP_EQ: return "=";
    case MOP_NE: return "<>";
    case MOP_AND: return "AND";
    case MOP_OR: return "OR";
    case MOP_XOR: return "XOR";

#define F(ENUM, STRING, PROTO, CONSTRAINTS) case MOP_F_##ENUM:
      MATRIX_FUNCTIONS
#undef F
    case MOP_NEGATE:
    case MOP_SEQ:
    case MOP_SEQ_BY:
    case MOP_MUL_MAT:
    case MOP_EXP_MAT:
    case MOP_NOT:
    case MOP_PASTE_HORZ:
    case MOP_PASTE_VERT:
    case MOP_EMPTY:
    case MOP_VEC_INDEX:
    case MOP_VEC_ALL:
    case MOP_MAT_INDEX:
    case MOP_ROW_INDEX:
    case MOP_COL_INDEX:
    case MOP_NUMBER:
    case MOP_VARIABLE:
    case MOP_EOF:
      NOT_REACHED ();
    }
  NOT_REACHED ();
}

static bool
is_scalar (const gsl_matrix *m)
{
  return m->size1 == 1 && m->size2 == 1;
}

static double
to_scalar (const gsl_matrix *m)
{
  assert (is_scalar (m));
  return gsl_matrix_get (m, 0, 0);
}

static gsl_matrix *
matrix_expr_evaluate_elementwise (const struct matrix_expr *e,
                                  enum matrix_op op,
                                  gsl_matrix *a, gsl_matrix *b)
{
  if (is_scalar (b))
    {
      double be = to_scalar (b);
      for (size_t r = 0; r < a->size1; r++)
        for (size_t c = 0; c < a->size2; c++)
          {
            double *ae = gsl_matrix_ptr (a, r, c);
            *ae = matrix_op_eval (op, *ae, be);
          }
      return a;
    }
  else if (is_scalar (a))
    {
      double ae = to_scalar (a);
      for (size_t r = 0; r < b->size1; r++)
        for (size_t c = 0; c < b->size2; c++)
          {
            double *be = gsl_matrix_ptr (b, r, c);
            *be = matrix_op_eval (op, ae, *be);
          }
      return b;
    }
  else if (a->size1 == b->size1 && a->size2 == b->size2)
    {
      for (size_t r = 0; r < a->size1; r++)
        for (size_t c = 0; c < a->size2; c++)
          {
            double *ae = gsl_matrix_ptr (a, r, c);
            double be = gsl_matrix_get (b, r, c);
            *ae = matrix_op_eval (op, *ae, be);
          }
      return a;
    }
  else
    {
      msg_at (SE, matrix_expr_location (e),
              _("The operands of %s must have the same dimensions or one "
                "must be a scalar."),
           matrix_op_name (op));
      msg_at (SN, matrix_expr_location (e->subs[0]),
              _("The left-hand operand is a %zu×%zu matrix."),
              a->size1, a->size2);
      msg_at (SN, matrix_expr_location (e->subs[1]),
              _("The right-hand operand is a %zu×%zu matrix."),
              b->size1, b->size2);
      return NULL;
    }
}

static gsl_matrix *
matrix_expr_evaluate_mul_mat (const struct matrix_expr *e,
                              gsl_matrix *a, gsl_matrix *b)
{
  if (is_scalar (a) || is_scalar (b))
    return matrix_expr_evaluate_elementwise (e, MOP_MUL_ELEMS, a, b);

  if (a->size2 != b->size1)
    {
      msg_at (SE, e->location,
              _("Matrices not conformable for multiplication."));
      msg_at (SN, matrix_expr_location (e->subs[0]),
              _("The left-hand operand is a %zu×%zu matrix."),
              a->size1, a->size2);
      msg_at (SN, matrix_expr_location (e->subs[1]),
              _("The right-hand operand is a %zu×%zu matrix."),
              b->size1, b->size2);
      return NULL;
    }

  gsl_matrix *c = gsl_matrix_alloc (a->size1, b->size2);
  if (a->size1 && b->size2)
    gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1.0, a, b, 0.0, c);
  return c;
}

static void
swap_matrix (gsl_matrix **a, gsl_matrix **b)
{
  gsl_matrix *tmp = *a;
  *a = *b;
  *b = tmp;
}

static void
mul_matrix (gsl_matrix **z, const gsl_matrix *x, const gsl_matrix *y,
            gsl_matrix **tmp)
{
  gsl_blas_dgemm (CblasNoTrans, CblasNoTrans, 1.0, x, y, 0.0, *tmp);
  swap_matrix (z, tmp);
}

static void
square_matrix (gsl_matrix **x, gsl_matrix **tmp)
{
  mul_matrix (x, *x, *x, tmp);
}

static gsl_matrix *
matrix_expr_evaluate_exp_mat (const struct matrix_expr *e,
                              gsl_matrix *x_, gsl_matrix *b)
{
  gsl_matrix *x = x_;
  if (x->size1 != x->size2)
    {
      msg_at (SE, matrix_expr_location (e->subs[0]),
              _("Matrix exponentation with ** requires a square matrix on "
                "the left-hand size, not one with dimensions %zu×%zu."),
              x->size1, x->size2);
      return NULL;
    }
  if (!is_scalar (b))
    {
      msg_at (SE, matrix_expr_location (e->subs[1]),
              _("Matrix exponentiation with ** requires a scalar on the "
                "right-hand side, not a matrix with dimensions %zu×%zu."),
              b->size1, b->size2);
      return NULL;
    }
  double bf = to_scalar (b);
  if (bf != floor (bf) || bf < DBL_UNIT_LONG_MIN || bf > DBL_UNIT_LONG_MAX)
    {
      msg_at (SE, matrix_expr_location (e->subs[1]),
              _("Exponent %.1f in matrix exponentiation is non-integer "
                "or outside the valid range."), bf);
      return NULL;
    }
  long int bl = bf;

  gsl_matrix *y_ = gsl_matrix_alloc (x->size1, x->size2);
  gsl_matrix *y = y_;
  gsl_matrix_set_identity (y);
  if (bl == 0)
    return y;

  gsl_matrix *t_ = gsl_matrix_alloc (x->size1, x->size2);
  gsl_matrix *t = t_;
  for (unsigned long int n = labs (bl); n > 1; n /= 2)
    if (n & 1)
      {
        mul_matrix (&y, x, y, &t);
        square_matrix (&x, &t);
      }
    else
      square_matrix (&x, &t);

  mul_matrix (&y, x, y, &t);
  if (bf < 0)
    {
      invert_matrix (y, x);
      swap_matrix (&x, &y);
    }

  /* Garbage collection.

     There are three matrices: 'x_', 'y_', and 't_', and 'x', 'y', and 't' are
     a permutation of them.  We are returning one of them; that one must not be
     destroyed.  We must not destroy 'x_' because the caller owns it. */
  if (y != y_)
    gsl_matrix_free (y_);
  if (y != t_)
    gsl_matrix_free (t_);

  return y;
}

static void
note_operand_size (const gsl_matrix *m, const struct matrix_expr *e)
{
  msg_at (SN, matrix_expr_location (e),
          _("This operand is a %zu×%zu matrix."), m->size1, m->size2);
}

static bool
is_integer_range (const gsl_matrix *m)
{
  if (!is_scalar (m))
    return false;

  double d = to_scalar (m);
  return d >= DBL_UNIT_LONG_MIN && d <= DBL_UNIT_LONG_MAX;
}

static void
note_noninteger_range (const gsl_matrix *m, const struct matrix_expr *e)
{
  if (!is_scalar (m))
    note_operand_size (m, e);
  else
    {
      double d = to_scalar (m);
      if (d < DBL_UNIT_LONG_MIN || d > DBL_UNIT_LONG_MAX)
        msg_at (SN, matrix_expr_location (e),
                _("This operand with value %g is outside the supported integer "
                  "range from %ld to %ld."),
                d, DBL_UNIT_LONG_MIN, DBL_UNIT_LONG_MAX);
    }
}

static gsl_matrix *
matrix_expr_evaluate_seq (const struct matrix_expr *e,
                          gsl_matrix *start_, gsl_matrix *end_,
                          gsl_matrix *by_)
{
  if (!is_integer_range (start_)
      || !is_integer_range (end_)
      || (by_ && !is_integer_range (by_)))
    {
      msg_at (SE, matrix_expr_location (e),
              _("All operands of : must be scalars in the supported "
                "integer range."));

      note_noninteger_range (start_, e->subs[0]);
      note_noninteger_range (end_, e->subs[1]);
      if (by_)
        note_noninteger_range (by_, e->subs[2]);
      return NULL;
    }

  long int start = to_scalar (start_);
  long int end = to_scalar (end_);
  long int by = by_ ? to_scalar (by_) : 1;

  if (!by)
    {
      msg_at (SE, matrix_expr_location (e->subs[2]),
              _("The increment operand to : must be nonzero."));
      return NULL;
    }

  long int n = (end >= start && by > 0 ? (end - start + by) / by
                : end <= start && by < 0 ? (start - end - by) / -by
                : 0);
  gsl_matrix *m = gsl_matrix_alloc (1, n);
  for (long int i = 0; i < n; i++)
    gsl_matrix_set (m, 0, i, start + i * by);
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_not (gsl_matrix *a)
{
  MATRIX_FOR_ALL_ELEMENTS (d, y, x, a)
    *d = !(*d > 0);
  return a;
}

static gsl_matrix *
matrix_expr_evaluate_paste_horz (const struct matrix_expr *e,
                                 gsl_matrix *a, gsl_matrix *b)
{
  if (a->size1 != b->size1)
    {
      if (!a->size1 || !a->size2)
        return b;
      else if (!b->size1 || !b->size2)
        return a;

      msg_at (SE, matrix_expr_location (e),
              _("This expression tries to horizontally join matrices with "
                "differing numbers of rows."));
      note_operand_size (a, e->subs[0]);
      note_operand_size (b, e->subs[1]);
      return NULL;
    }

  gsl_matrix *c = gsl_matrix_alloc (a->size1, a->size2 + b->size2);
  for (size_t y = 0; y < a->size1; y++)
    {
      for (size_t x = 0; x < a->size2; x++)
        gsl_matrix_set (c, y, x, gsl_matrix_get (a, y, x));
      for (size_t x = 0; x < b->size2; x++)
        gsl_matrix_set (c, y, x + a->size2, gsl_matrix_get (b, y, x));
    }
  return c;
}

static gsl_matrix *
matrix_expr_evaluate_paste_vert (const struct matrix_expr *e,
                                 gsl_matrix *a, gsl_matrix *b)
{
  if (a->size2 != b->size2)
    {
      if (!a->size1 || !a->size2)
        return b;
      else if (!b->size1 || !b->size2)
        return a;

      msg_at (SE, matrix_expr_location (e),
              _("This expression tries to vertically join matrices with "
                "differing numbers of columns."));
      note_operand_size (a, e->subs[0]);
      note_operand_size (b, e->subs[1]);
      return NULL;
    }

  gsl_matrix *c = gsl_matrix_alloc (a->size1 + b->size1, a->size2);
  for (size_t x = 0; x < a->size2; x++)
    {
      for (size_t y = 0; y < a->size1; y++)
        gsl_matrix_set (c, y, x, gsl_matrix_get (a, y, x));
      for (size_t y = 0; y < b->size1; y++)
        gsl_matrix_set (c, y + a->size1, x, gsl_matrix_get (b, y, x));
    }
  return c;
}

static gsl_vector *
matrix_to_vector (gsl_matrix *m)
{
  assert (m->owner);
  gsl_vector v = to_vector (m);
  assert (v.block == m->block || !v.block);
  assert (!v.owner);
  v.owner = 1;
  m->owner = 0;
  gsl_matrix_free (m);
  return xmemdup (&v, sizeof v);
}

enum index_type {
  IV_ROW,
  IV_COLUMN,
  IV_VECTOR
};

struct index_vector
  {
    size_t *indexes;
    size_t n;
  };
#define INDEX_VECTOR_INIT (struct index_vector) { .n = 0 }

static void
index_vector_uninit (struct index_vector *iv)
{
  if (iv)
    free (iv->indexes);
}

static bool
matrix_normalize_index_vector (const gsl_matrix *m,
                               const struct matrix_expr *me, size_t size,
                               enum index_type index_type, size_t other_size,
                               struct index_vector *iv)
{
  if (m)
    {
      if (!is_vector (m))
        {
          switch (index_type)
            {
            case IV_VECTOR:
              msg_at (SE, matrix_expr_location (me),
                      _("Vector index must be scalar or vector, not a "
                        "%zu×%zu matrix."),
                      m->size1, m->size2);
              break;

            case IV_ROW:
              msg_at (SE, matrix_expr_location (me),
                      _("Matrix row index must be scalar or vector, not a "
                        "%zu×%zu matrix."),
                      m->size1, m->size2);
              break;

            case IV_COLUMN:
              msg_at (SE, matrix_expr_location (me),
                      _("Matrix column index must be scalar or vector, not a "
                        "%zu×%zu matrix."),
                      m->size1, m->size2);
              break;
            }
          return false;
        }

      gsl_vector v = to_vector (CONST_CAST (gsl_matrix *, m));
      *iv = (struct index_vector) {
        .indexes = xnmalloc (v.size, sizeof *iv->indexes),
        .n = v.size,
      };
      for (size_t i = 0; i < v.size; i++)
        {
          double index = gsl_vector_get (&v, i);
          if (index < 1 || index >= size + 1)
            {
              switch (index_type)
                {
                case IV_VECTOR:
                  msg_at (SE, matrix_expr_location (me),
                          _("Index %g is out of range for vector "
                            "with %zu elements."), index, size);
                  break;

                case IV_ROW:
                  msg_at (SE, matrix_expr_location (me),
                          _("%g is not a valid row index for "
                            "a %zu×%zu matrix."),
                          index, size, other_size);
                  break;

                case IV_COLUMN:
                  msg_at (SE, matrix_expr_location (me),
                          _("%g is not a valid column index for "
                            "a %zu×%zu matrix."),
                          index, other_size, size);
                  break;
                }

              index_vector_uninit (iv);
              return false;
            }
          iv->indexes[i] = index - 1;
        }
      return true;
    }
  else
    {
      *iv = (struct index_vector) {
        .indexes = xnmalloc (size, sizeof *iv->indexes),
        .n = size,
      };
      for (size_t i = 0; i < size; i++)
        iv->indexes[i] = i;
      return true;
    }
}

static gsl_matrix *
matrix_expr_evaluate_vec_all (const struct matrix_expr *e,
                              gsl_matrix *sm)
{
  if (!is_vector (sm))
    {
      msg_at (SE, matrix_expr_location (e->subs[0]),
              _("Vector index operator may not be applied to "
                "a %zu×%zu matrix."),
           sm->size1, sm->size2);
      return NULL;
    }

  return sm;
}

static gsl_matrix *
matrix_expr_evaluate_vec_index (const struct matrix_expr *e,
                                gsl_matrix *sm, gsl_matrix *im)
{
  if (!matrix_expr_evaluate_vec_all (e, sm))
    return NULL;

  gsl_vector sv = to_vector (sm);
  struct index_vector iv;
  if (!matrix_normalize_index_vector (im, e->subs[1],
                                      sv.size, IV_VECTOR, 0, &iv))
    return NULL;

  gsl_matrix *dm = gsl_matrix_alloc (sm->size1 == 1 ? 1 : iv.n,
                                     sm->size1 == 1 ? iv.n : 1);
  gsl_vector dv = to_vector (dm);
  for (size_t dx = 0; dx < iv.n; dx++)
    {
      size_t sx = iv.indexes[dx];
      gsl_vector_set (&dv, dx, gsl_vector_get (&sv, sx));
    }
  index_vector_uninit (&iv);

  return dm;
}

static gsl_matrix *
matrix_expr_evaluate_mat_index (gsl_matrix *sm,
                                gsl_matrix *im0, const struct matrix_expr *eim0,
                                gsl_matrix *im1, const struct matrix_expr *eim1)
{
  struct index_vector iv0;
  if (!matrix_normalize_index_vector (im0, eim0, sm->size1,
                                      IV_ROW, sm->size2, &iv0))
    return NULL;

  struct index_vector iv1;
  if (!matrix_normalize_index_vector (im1, eim1, sm->size2,
                                      IV_COLUMN, sm->size1, &iv1))
    {
      index_vector_uninit (&iv0);
      return NULL;
    }

  gsl_matrix *dm = gsl_matrix_alloc (iv0.n, iv1.n);
  for (size_t dy = 0; dy < iv0.n; dy++)
    {
      size_t sy = iv0.indexes[dy];

      for (size_t dx = 0; dx < iv1.n; dx++)
        {
          size_t sx = iv1.indexes[dx];
          gsl_matrix_set (dm, dy, dx, gsl_matrix_get (sm, sy, sx));
        }
    }
  index_vector_uninit (&iv0);
  index_vector_uninit (&iv1);
  return dm;
}

#define F(ENUM, STRING, PROTO, CONSTRAINTS)                     \
  static gsl_matrix *matrix_expr_evaluate_##PROTO (             \
    const struct matrix_function_properties *, gsl_matrix *[],  \
    const struct matrix_expr *, matrix_proto_##PROTO *);
MATRIX_FUNCTIONS
#undef F

static bool
check_scalar_arg (const char *name, gsl_matrix *subs[],
                  const struct matrix_expr *e, size_t index)
{
  if (!is_scalar (subs[index]))
    {
      msg_at (SE, matrix_expr_location (e->subs[index]),
              _("Function %s argument %zu must be a scalar, "
                "not a %zu×%zu matrix."),
              name, index + 1, subs[index]->size1, subs[index]->size2);
      return false;
    }
  return true;
}

static bool
check_vector_arg (const char *name, gsl_matrix *subs[],
                  const struct matrix_expr *e, size_t index)
{
  if (!is_vector (subs[index]))
    {
      msg_at (SE, matrix_expr_location (e->subs[index]),
              _("Function %s argument %zu must be a vector, "
                "not a %zu×%zu matrix."),
              name, index + 1, subs[index]->size1, subs[index]->size2);
      return false;
    }
  return true;
}

static bool
to_scalar_args (const char *name, gsl_matrix *subs[],
                const struct matrix_expr *e, double d[])
{
  for (size_t i = 0; i < e->n_subs; i++)
    {
      if (!check_scalar_arg (name, subs, e, i))
        return false;
      d[i] = to_scalar (subs[i]);
    }
  return true;
}

static int
parse_constraint_value (const char **constraintsp)
{
  char *tail;
  long retval = strtol (*constraintsp, &tail, 10);
  assert (tail > *constraintsp);
  *constraintsp = tail;
  return retval;
}

enum matrix_argument_relop
  {
    MRR_GT,                 /* > */
    MRR_GE,                 /* >= */
    MRR_LT,                 /* < */
    MRR_LE,                 /* <= */
    MRR_NE,                 /* <> */
  };

static void
argument_inequality_error (
  const struct matrix_function_properties *props, const struct matrix_expr *e,
  size_t ai, gsl_matrix *a, size_t y, size_t x,
  size_t bi, double b,
  enum matrix_argument_relop relop)
{
  const struct msg_location *loc = matrix_expr_location (e);
  switch (relop)
    {
    case MRR_GE:
      msg_at (ME, loc, _("Argument %zu to matrix function %s must be greater "
                         "than or equal to argument %zu."),
              ai + 1, props->name, bi + 1);
      break;

    case MRR_GT:
      msg_at (ME, loc, _("Argument %zu to matrix function %s must be greater "
                         "than argument %zu."),
              ai + 1, props->name, bi + 1);
      break;

    case MRR_LE:
      msg_at (ME, loc, _("Argument %zu to matrix function %s must be less than "
                         "or equal to argument %zu."),
              ai + 1, props->name, bi + 1);
      break;

    case MRR_LT:
      msg_at (ME, loc, _("Argument %zu to matrix function %s must be less than "
                         "argument %zu."),
              ai + 1, props->name, bi + 1);
      break;

    case MRR_NE:
      msg_at (ME, loc, _("Argument %zu to matrix function %s must not be equal "
                         "to argument %zu."),
              ai + 1, props->name, bi + 1);
      break;
    }

  const struct msg_location *a_loc = matrix_expr_location (e->subs[ai]);
  if (is_scalar (a))
    msg_at (SN, a_loc, _("Argument %zu is %g."),
            ai + 1, gsl_matrix_get (a, y, x));
  else
    msg_at (SN, a_loc, _("Row %zu, column %zu of argument %zu is %g."),
            y + 1, x + 1, ai + 1, gsl_matrix_get (a, y, x));

  msg_at (SN, matrix_expr_location (e->subs[bi]),
          _("Argument %zu is %g."), bi + 1, b);
}

static void
argument_value_error (
  const struct matrix_function_properties *props, const struct matrix_expr *e,
  size_t ai, gsl_matrix *a, size_t y, size_t x,
  double b,
  enum matrix_argument_relop relop)
{
  const struct msg_location *loc = matrix_expr_location (e);
  switch (relop)
    {
    case MRR_GE:
      msg_at (SE, loc, _("Argument %zu to matrix function %s must be greater "
                         "than or equal to %g."),
              ai + 1, props->name, b);
      break;

    case MRR_GT:
      msg_at (SE, loc, _("Argument %zu to matrix function %s must be greater "
                         "than %g."),
              ai + 1, props->name, b);
      break;

    case MRR_LE:
      msg_at (SE, loc, _("Argument %zu to matrix function %s must be less than "
                         "or equal to %g."),
              ai + 1, props->name, b);
      break;

    case MRR_LT:
      msg_at (SE, loc, _("Argument %zu to matrix function %s must be less than "
                         "%g."),
              ai + 1, props->name, b);
      break;

    case MRR_NE:
      msg_at (SE, loc, _("Argument %zu to matrix function %s must not be equal "
                         "to %g."),
              ai + 1, props->name, b);
      break;
    }

  const struct msg_location *a_loc = matrix_expr_location (e->subs[ai]);
  if (is_scalar (a))
    {
      if (relop != MRR_NE)
        msg_at (SN, a_loc, _("Argument %zu is %g."),
                ai + 1, gsl_matrix_get (a, y, x));
    }
  else
    msg_at (SN, a_loc, _("Row %zu, column %zu of argument %zu is %g."),
            y + 1, x + 1, ai + 1, gsl_matrix_get (a, y, x));
}

static bool
matrix_argument_relop_is_satisfied (double a, double b,
                                    enum matrix_argument_relop relop)
{
  switch (relop)
    {
    case MRR_GE: return a >= b;
    case MRR_GT: return a > b;
    case MRR_LE: return a <= b;
    case MRR_LT: return a < b;
    case MRR_NE: return a != b;
    }

  NOT_REACHED ();
}

static enum matrix_argument_relop
matrix_argument_relop_flip (enum matrix_argument_relop relop)
{
  switch (relop)
    {
    case MRR_GE: return MRR_LE;
    case MRR_GT: return MRR_LT;
    case MRR_LE: return MRR_GE;
    case MRR_LT: return MRR_GT;
    case MRR_NE: return MRR_NE;
    }

  NOT_REACHED ();
}

static bool
check_constraints (const struct matrix_function_properties *props,
                   gsl_matrix *args[], const struct matrix_expr *e)
{
  size_t n_args = e->n_subs;
  const char *constraints = props->constraints;
  if (!constraints)
    return true;

  size_t arg_index = SIZE_MAX;
  while (*constraints)
    {
      if (*constraints >= 'a' && *constraints <= 'd')
        {
          arg_index = *constraints++ - 'a';
          assert (arg_index < n_args);
        }
      else if (*constraints == '[' || *constraints == '(')
        {
          assert (arg_index < n_args);
          bool open_lower = *constraints++ == '(';
          int minimum = parse_constraint_value (&constraints);
          assert (*constraints == ',');
          constraints++;
          int maximum = parse_constraint_value (&constraints);
          assert (*constraints == ']' || *constraints == ')');
          bool open_upper = *constraints++ == ')';

          MATRIX_FOR_ALL_ELEMENTS (d, y, x, args[arg_index])
            if ((open_lower ? *d <= minimum : *d < minimum)
                || (open_upper ? *d >= maximum : *d > maximum))
              {
                if (!is_scalar (args[arg_index]))
                  msg_at (SE, matrix_expr_location (e->subs[arg_index]),
                          _("Row %zu, column %zu of argument %zu to matrix "
                            "function %s is %g, which is outside "
                            "the valid range %c%d,%d%c."),
                          y + 1, x + 1, arg_index + 1, props->name, *d,
                          open_lower ? '(' : '[',
                          minimum, maximum,
                          open_upper ? ')' : ']');
                else
                  msg_at (SE, matrix_expr_location (e->subs[arg_index]),
                          _("Argument %zu to matrix function %s is %g, "
                            "which is outside the valid range %c%d,%d%c."),
                          arg_index + 1, props->name, *d,
                          open_lower ? '(' : '[',
                          minimum, maximum,
                          open_upper ? ')' : ']');
                return false;
              }
        }
      else if (*constraints == 'i')
        {
          constraints++;
          MATRIX_FOR_ALL_ELEMENTS (d, y, x, args[arg_index])
            if (*d != floor (*d))
              {
                if (!is_scalar (args[arg_index]))
                  msg_at (SE, matrix_expr_location (e->subs[arg_index]),
                          _("Argument %zu to matrix function %s, which must be "
                            "integer, contains non-integer value %g in "
                            "row %zu, column %zu."),
                          arg_index + 1, props->name, *d, y + 1, x + 1);
                else
                  msg_at (SE, matrix_expr_location (e->subs[arg_index]),
                          _("Argument %zu to matrix function %s, which must be "
                            "integer, has non-integer value %g."),
                          arg_index + 1, props->name, *d);
                return false;
              }
        }
      else if (*constraints == '>'
               || *constraints == '<'
               || *constraints == '!')
        {
          enum matrix_argument_relop relop;
          switch (*constraints++)
            {
            case '>':
              if (*constraints == '=')
                {
                  constraints++;
                  relop = MRR_GE;
                }
              else
                relop = MRR_GT;
              break;

            case '<':
              if (*constraints == '=')
                {
                  constraints++;
                  relop = MRR_LE;
                }
              else
                relop = MRR_LT;
              break;

            case '!':
              assert (*constraints == '=');
              constraints++;
              relop = MRR_NE;
              break;

            default:
              NOT_REACHED ();
            }

          if (*constraints >= 'a' && *constraints <= 'd')
            {
              size_t a_index = arg_index;
              size_t b_index = *constraints - 'a';
              assert (a_index < n_args);
              assert (b_index < n_args);

              /* We only support one of the two arguments being non-scalar.
                 It's easier to support only the first one being non-scalar, so
                 flip things around if it's the other way. */
              if (!is_scalar (args[b_index]))
                {
                  assert (is_scalar (args[a_index]));
                  size_t tmp_index = a_index;
                  a_index = b_index;
                  b_index = tmp_index;
                  relop = matrix_argument_relop_flip (relop);
                }

              double b = to_scalar (args[b_index]);
              MATRIX_FOR_ALL_ELEMENTS (a, y, x, args[a_index])
                if (!matrix_argument_relop_is_satisfied (*a, b, relop))
                  {
                    argument_inequality_error (
                      props, e,
                      a_index, args[a_index], y, x,
                      b_index, b,
                      relop);
                    return false;
                  }
            }
          else
            {
              int comparand = parse_constraint_value (&constraints);

              MATRIX_FOR_ALL_ELEMENTS (d, y, x, args[arg_index])
                if (!matrix_argument_relop_is_satisfied (*d, comparand, relop))
                  {
                    argument_value_error (
                      props, e,
                      arg_index, args[arg_index], y, x,
                      comparand,
                      relop);
                    return false;
                  }
            }
        }
      else
        {
          assert (*constraints == ' ');
          constraints++;
          arg_index = SIZE_MAX;
        }
    }
  return true;
}

static gsl_matrix *
matrix_expr_evaluate_d_none (const struct matrix_function_properties *props,
                             gsl_matrix *subs[], const struct matrix_expr *e,
                             matrix_proto_d_none *f)
{
  assert (e->n_subs == 0);

  if (!check_constraints (props, subs, e))
    return NULL;

  gsl_matrix *m = gsl_matrix_alloc (1, 1);
  gsl_matrix_set (m, 0, 0, f ());
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_d_d (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_d_d *f)
{
  assert (e->n_subs == 1);

  double d;
  if (!to_scalar_args (props->name, subs, e, &d)
      || !check_constraints (props, subs, e))
    return NULL;

  gsl_matrix *m = gsl_matrix_alloc (1, 1);
  gsl_matrix_set (m, 0, 0, f (d));
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_d_dd (const struct matrix_function_properties *props,
                           gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_d_dd *f)
{
  assert (e->n_subs == 2);

  double d[2];
  if (!to_scalar_args (props->name, subs, e, d)
      && !check_constraints (props, subs, e))
    return NULL;

  gsl_matrix *m = gsl_matrix_alloc (1, 1);
  gsl_matrix_set (m, 0, 0, f (d[0], d[1]));
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_d_ddd (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_d_ddd *f)
{
  assert (e->n_subs == 3);

  double d[3];
  if (!to_scalar_args (props->name, subs, e, d)
      || !check_constraints (props, subs, e))
    return NULL;

  gsl_matrix *m = gsl_matrix_alloc (1, 1);
  gsl_matrix_set (m, 0, 0, f (d[0], d[1], d[2]));
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_m_d (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_m_d *f)
{
  assert (e->n_subs == 1);

  double d;
  return (to_scalar_args (props->name, subs, e, &d)
          && check_constraints (props, subs, e)
          ? f(d)
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate_m_ddd (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_m_ddd *f)
{
  assert (e->n_subs == 3);

  double d[3];
  return (to_scalar_args (props->name, subs, e, d)
          && check_constraints (props, subs, e)
          ? f(d[0], d[1], d[2])
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate_m_ddn (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_ddn *f)
{
  assert (e->n_subs == 2);

  double d[2];
  return (to_scalar_args (props->name, subs, e, d)
          && check_constraints (props, subs, e)
          ? f(d[0], d[1], e)
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate_m_m (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_m_m *f)
{
  assert (e->n_subs == 1);
  return check_constraints (props, subs, e) ? f (subs[0]) : NULL;
}

static gsl_matrix *
matrix_expr_evaluate_m_mn (const struct matrix_function_properties *props,
                           gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_m_mn *f)
{
  assert (e->n_subs == 1);
  return check_constraints (props, subs, e) ? f (subs[0], e) : NULL;
}

static gsl_matrix *
matrix_expr_evaluate_m_e (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_m_e *f)
{
  assert (e->n_subs == 1);

  if (!check_constraints (props, subs, e))
    return NULL;

  MATRIX_FOR_ALL_ELEMENTS (a, y, x, subs[0])
      *a = f (*a);
  return subs[0];
}

static gsl_matrix *
matrix_expr_evaluate_m_md (const struct matrix_function_properties *props,
                           gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_m_md *f)
{
  assert (e->n_subs == 2);
  return (check_scalar_arg (props->name, subs, e, 1)
          && check_constraints (props, subs, e)
          ? f (subs[0], to_scalar (subs[1]))
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate_m_mdn (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_mdn *f)
{
  assert (e->n_subs == 2);
  return (check_scalar_arg (props->name, subs, e, 1)
          && check_constraints (props, subs, e)
          ? f (subs[0], to_scalar (subs[1]), e)
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate_m_ed (const struct matrix_function_properties *props,
                           gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_m_ed *f)
{
  assert (e->n_subs == 2);
  if (!check_scalar_arg (props->name, subs, e, 1)
      || !check_constraints (props, subs, e))
    return NULL;

  double b = to_scalar (subs[1]);
  MATRIX_FOR_ALL_ELEMENTS (a, y, x, subs[0])
    *a = f (*a, b);
  return subs[0];
}

static gsl_matrix *
matrix_expr_evaluate_m_mddn (const struct matrix_function_properties *props,
                             gsl_matrix *subs[], const struct matrix_expr *e,
                             matrix_proto_m_mddn *f)
{
  assert (e->n_subs == 3);
  if (!check_scalar_arg (props->name, subs, e, 1)
      || !check_scalar_arg (props->name, subs, e, 2)
      || !check_constraints (props, subs, e))
    return NULL;
  return f (subs[0], to_scalar (subs[1]), to_scalar (subs[2]), e);
}

static gsl_matrix *
matrix_expr_evaluate_m_edd (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_edd *f)
{
  assert (e->n_subs == 3);
  if (!check_scalar_arg (props->name, subs, e, 1)
      || !check_scalar_arg (props->name, subs, e, 2)
      || !check_constraints (props, subs, e))
    return NULL;

  double b = to_scalar (subs[1]);
  double c = to_scalar (subs[2]);
  MATRIX_FOR_ALL_ELEMENTS (a, y, x, subs[0])
    *a = f (*a, b, c);
  return subs[0];
}

static gsl_matrix *
matrix_expr_evaluate_m_eddd (const struct matrix_function_properties *props,
                             gsl_matrix *subs[], const struct matrix_expr *e,
                             matrix_proto_m_eddd *f)
{
  assert (e->n_subs == 4);
  for (size_t i = 1; i < 4; i++)
    if (!check_scalar_arg (props->name, subs, e, i))
    return NULL;

  if (!check_constraints (props, subs, e))
    return NULL;

  double b = to_scalar (subs[1]);
  double c = to_scalar (subs[2]);
  double d = to_scalar (subs[3]);
  MATRIX_FOR_ALL_ELEMENTS (a, y, x, subs[0])
    *a = f (*a, b, c, d);
  return subs[0];
}

static gsl_matrix *
matrix_expr_evaluate_m_eed (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_eed *f)
{
  assert (e->n_subs == 3);
  if (!check_scalar_arg (props->name, subs, e, 2))
    return NULL;

  if (!is_scalar (subs[0]) && !is_scalar (subs[1])
      && (subs[0]->size1 != subs[1]->size1 || subs[0]->size2 != subs[1]->size2))
    {
      struct msg_location *loc = msg_location_dup (e->subs[0]->location);
      loc->end = e->subs[1]->location->end;

      msg_at (ME, loc,
              _("Arguments 1 and 2 to %s have dimensions %zu×%zu and "
                "%zu×%zu, but %s requires these arguments either to have "
                "the same dimensions or for one of them to be a scalar."),
              props->name,
              subs[0]->size1, subs[0]->size2,
              subs[1]->size1, subs[1]->size2,
              props->name);

      msg_location_destroy (loc);
      return NULL;
    }

  if (!check_constraints (props, subs, e))
    return NULL;

  double c = to_scalar (subs[2]);

  if (is_scalar (subs[0]))
    {
      double a = to_scalar (subs[0]);
      MATRIX_FOR_ALL_ELEMENTS (b, y, x, subs[1])
        *b = f (a, *b, c);
      return subs[1];
    }
  else
    {
      double b = to_scalar (subs[1]);
      MATRIX_FOR_ALL_ELEMENTS (a, y, x, subs[0])
        *a = f (*a, b, c);
      return subs[0];
    }
}

static gsl_matrix *
matrix_expr_evaluate_m_mm (const struct matrix_function_properties *props,
                           gsl_matrix *subs[], const struct matrix_expr *e,
                           matrix_proto_m_mm *f)
{
  assert (e->n_subs == 2);
  return check_constraints (props, subs, e) ? f (subs[0], subs[1]) : NULL;
}

static gsl_matrix *
matrix_expr_evaluate_m_mmn (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_mmn *f)
{
  assert (e->n_subs == 2);
  return check_constraints (props, subs, e) ? f (subs[0], subs[1], e) : NULL;
}

static gsl_matrix *
matrix_expr_evaluate_m_v (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_m_v *f)
{
  assert (e->n_subs == 1);
  if (!check_vector_arg (props->name, subs, e, 0)
      || !check_constraints (props, subs, e))
    return NULL;
  gsl_vector v = to_vector (subs[0]);
  return f (&v);
}

static gsl_matrix *
matrix_expr_evaluate_d_m (const struct matrix_function_properties *props,
                          gsl_matrix *subs[], const struct matrix_expr *e,
                          matrix_proto_d_m *f)
{
  assert (e->n_subs == 1);

  if (!check_constraints (props, subs, e))
    return NULL;

  gsl_matrix *m = gsl_matrix_alloc (1, 1);
  gsl_matrix_set (m, 0, 0, f (subs[0]));
  return m;
}

static gsl_matrix *
matrix_expr_evaluate_m_any (const struct matrix_function_properties *props,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_m_any *f)
{
  return check_constraints (props, subs, e) ? f (subs, e->n_subs) : NULL;
}

static gsl_matrix *
matrix_expr_evaluate_IDENT (const struct matrix_function_properties *props_ UNUSED,
                            gsl_matrix *subs[], const struct matrix_expr *e,
                            matrix_proto_IDENT *f)
{
  static const struct matrix_function_properties p1 = {
    .name = "IDENT",
    .constraints = "ai>=0"
  };
  static const struct matrix_function_properties p2 = {
    .name = "IDENT",
    .constraints = "ai>=0 bi>=0"
  };
  const struct matrix_function_properties *props = e->n_subs == 1 ? &p1 : &p2;

  assert (e->n_subs <= 2);

  double d[2];
  return (to_scalar_args (props->name, subs, e, d)
          && check_constraints (props, subs, e)
          ? f (d[0], d[e->n_subs - 1])
          : NULL);
}

static gsl_matrix *
matrix_expr_evaluate (const struct matrix_expr *e)
{
  if (e->op == MOP_NUMBER)
    {
      gsl_matrix *m = gsl_matrix_alloc (1, 1);
      gsl_matrix_set (m, 0, 0, e->number);
      return m;
    }
  else if (e->op == MOP_VARIABLE)
    {
      const gsl_matrix *src = e->variable->value;
      if (!src)
        {
          msg_at (SE, e->location,
                  _("Uninitialized variable %s used in expression."),
                  e->variable->name);
          return NULL;
        }

      gsl_matrix *dst = gsl_matrix_alloc (src->size1, src->size2);
      gsl_matrix_memcpy (dst, src);
      return dst;
    }
  else if (e->op == MOP_EOF)
    {
      struct dfm_reader *reader = read_file_open (e->eof);
      gsl_matrix *m = gsl_matrix_alloc (1, 1);
      gsl_matrix_set (m, 0, 0, !reader || dfm_eof (reader));
      return m;
    }

  enum { N_LOCAL = 3 };
  gsl_matrix *local_subs[N_LOCAL];
  gsl_matrix **subs = (e->n_subs < N_LOCAL
                       ? local_subs
                       : xmalloc (e->n_subs * sizeof *subs));

  for (size_t i = 0; i < e->n_subs; i++)
    {
      subs[i] = matrix_expr_evaluate (e->subs[i]);
      if (!subs[i])
        {
          for (size_t j = 0; j < i; j++)
            gsl_matrix_free (subs[j]);
          if (subs != local_subs)
            free (subs);
          return NULL;
        }
    }

  gsl_matrix *result = NULL;
  switch (e->op)
    {
#define F(ENUM, STRING, PROTO, CONSTRAINTS)                             \
      case MOP_F_##ENUM:                                                \
        {                                                               \
          static const struct matrix_function_properties props = {      \
            .name = STRING,                                             \
            .constraints = CONSTRAINTS,                                 \
          };                                                            \
          result = matrix_expr_evaluate_##PROTO (&props, subs, e,       \
                                                 matrix_eval_##ENUM);   \
        }                                                               \
      break;
      MATRIX_FUNCTIONS
#undef F

    case MOP_NEGATE:
      gsl_matrix_scale (subs[0], -1.0);
      result = subs[0];
      break;

    case MOP_ADD_ELEMS:
    case MOP_SUB_ELEMS:
    case MOP_MUL_ELEMS:
    case MOP_DIV_ELEMS:
    case MOP_EXP_ELEMS:
    case MOP_GT:
    case MOP_GE:
    case MOP_LT:
    case MOP_LE:
    case MOP_EQ:
    case MOP_NE:
    case MOP_AND:
    case MOP_OR:
    case MOP_XOR:
      result = matrix_expr_evaluate_elementwise (e, e->op, subs[0], subs[1]);
      break;

    case MOP_NOT:
      result = matrix_expr_evaluate_not (subs[0]);
      break;

    case MOP_SEQ:
      result = matrix_expr_evaluate_seq (e, subs[0], subs[1], NULL);
      break;

    case MOP_SEQ_BY:
      result = matrix_expr_evaluate_seq (e, subs[0], subs[1], subs[2]);
      break;

    case MOP_MUL_MAT:
      result = matrix_expr_evaluate_mul_mat (e, subs[0], subs[1]);
      break;

    case MOP_EXP_MAT:
      result = matrix_expr_evaluate_exp_mat (e, subs[0], subs[1]);
      break;

    case MOP_PASTE_HORZ:
      result = matrix_expr_evaluate_paste_horz (e, subs[0], subs[1]);
      break;

    case MOP_PASTE_VERT:
      result = matrix_expr_evaluate_paste_vert (e, subs[0], subs[1]);
      break;

    case MOP_EMPTY:
      result = gsl_matrix_alloc (0, 0);
      break;

    case MOP_VEC_INDEX:
      result = matrix_expr_evaluate_vec_index (e, subs[0], subs[1]);
      break;

    case MOP_VEC_ALL:
      result = matrix_expr_evaluate_vec_all (e, subs[0]);
      break;

    case MOP_MAT_INDEX:
      result = matrix_expr_evaluate_mat_index (subs[0],
                                               subs[1], e->subs[1],
                                               subs[2], e->subs[2]);
      break;

    case MOP_ROW_INDEX:
      result = matrix_expr_evaluate_mat_index (subs[0],
                                               subs[1], e->subs[1],
                                               NULL, NULL);
      break;

    case MOP_COL_INDEX:
      result = matrix_expr_evaluate_mat_index (subs[0],
                                               NULL, NULL,
                                               subs[1], e->subs[1]);
      break;

    case MOP_NUMBER:
    case MOP_VARIABLE:
    case MOP_EOF:
      NOT_REACHED ();
    }

  for (size_t i = 0; i < e->n_subs; i++)
    if (subs[i] != result)
      gsl_matrix_free (subs[i]);
  if (subs != local_subs)
    free (subs);
  return result;
}

static bool
matrix_expr_evaluate_scalar (const struct matrix_expr *e, const char *context,
                             double *d)
{
  gsl_matrix *m = matrix_expr_evaluate (e);
  if (!m)
    return false;

  if (!is_scalar (m))
    {
      msg_at (SE, matrix_expr_location (e),
              _("Expression for %s must evaluate to scalar, "
                "not a %zu×%zu matrix."),
           context, m->size1, m->size2);
      gsl_matrix_free (m);
      return false;
    }

  *d = to_scalar (m);
  gsl_matrix_free (m);
  return true;
}

static bool
matrix_expr_evaluate_integer (const struct matrix_expr *e, const char *context,
                              long int *integer)
{
  double d;
  if (!matrix_expr_evaluate_scalar (e, context, &d))
    return false;

  d = trunc (d);
  if (d < DBL_UNIT_LONG_MIN || d > DBL_UNIT_LONG_MAX)
    {
      msg_at (SE, matrix_expr_location (e),
              _("Expression for %s is outside the supported integer range."),
              context);
      return false;
    }
  *integer = d;
  return true;
}

/* Matrix lvalues.

   An lvalue is an expression that can appear on the left side of a COMPUTE
   command and in other contexts that assign values.

   An lvalue is parsed once, with matrix_lvalue_parse().  It can then be
   evaluated (with matrix_lvalue_evaluate()) and assigned (with
   matrix_lvalue_assign()).

   There are three kinds of lvalues:

   - A variable name.  A variable used as an lvalue need not be initialized,
     since the assignment will initialize.

   - A subvector, e.g. "var(index0)".  The variable must be initialized and
     must have the form of a vector (it must have 1 column or 1 row).

   - A submatrix, e.g. "var(index0, index1)".  The variable must be
     initialized. */
struct matrix_lvalue
  {
    struct matrix_var *var;         /* Destination variable. */
    struct matrix_expr *indexes[2]; /* Index expressions, if any. */
    size_t n_indexes;               /* Number of indexes. */

    struct msg_location *var_location; /* Variable name. */
    struct msg_location *full_location; /* Variable name plus indexing. */
    struct msg_location *index_locations[2]; /* Index expressions. */
  };

/* Frees LVALUE. */
static void
matrix_lvalue_destroy (struct matrix_lvalue *lvalue)
{
  if (lvalue)
    {
      msg_location_destroy (lvalue->var_location);
      msg_location_destroy (lvalue->full_location);
      for (size_t i = 0; i < lvalue->n_indexes; i++)
        {
          matrix_expr_destroy (lvalue->indexes[i]);
          msg_location_destroy (lvalue->index_locations[i]);
        }
      free (lvalue);
    }
}

/* Parses and returns an lvalue at the current position in S's lexer.  Returns
   null on parse failure.  On success, the caller must eventually free the
   lvalue. */
static struct matrix_lvalue *
matrix_lvalue_parse (struct matrix_state *s)
{
  if (!lex_force_id (s->lexer))
    return NULL;

  struct matrix_lvalue *lvalue = xzalloc (sizeof *lvalue);
  int start_ofs = lex_ofs (s->lexer);
  lvalue->var_location = lex_get_location (s->lexer, 0, 0);
  lvalue->var = matrix_var_lookup (s, lex_tokss (s->lexer));
  if (lex_next_token (s->lexer, 1) == T_LPAREN)
    {
      if (!lvalue->var)
        {
          lex_error (s->lexer, _("Undefined variable %s."),
                     lex_tokcstr (s->lexer));
          goto error;
        }

      lex_get_n (s->lexer, 2);

      if (!matrix_parse_index_expr (s, &lvalue->indexes[0],
                                    &lvalue->index_locations[0]))
        goto error;
      lvalue->n_indexes++;

      if (lex_match (s->lexer, T_COMMA))
        {
          if (!matrix_parse_index_expr (s, &lvalue->indexes[1],
                                        &lvalue->index_locations[1]))
            goto error;
          lvalue->n_indexes++;
        }
      if (!lex_force_match (s->lexer, T_RPAREN))
        goto error;

      lvalue->full_location = lex_ofs_location (s->lexer, start_ofs,
                                                lex_ofs (s->lexer) - 1);
    }
  else
    {
      if (!lvalue->var)
        lvalue->var = matrix_var_create (s, lex_tokss (s->lexer));
      lex_get (s->lexer);
    }
  return lvalue;

error:
  matrix_lvalue_destroy (lvalue);
  return NULL;
}

static bool
matrix_lvalue_evaluate_vector (struct matrix_expr *e, size_t size,
                               enum index_type index_type, size_t other_size,
                               struct index_vector *iv)
{
  gsl_matrix *m;
  if (e)
    {
      m = matrix_expr_evaluate (e);
      if (!m)
        return false;
    }
  else
    m = NULL;

  bool ok = matrix_normalize_index_vector (m, e, size, index_type,
                                           other_size, iv);
  gsl_matrix_free (m);
  return ok;
}

/* Evaluates the indexes in LVALUE into IV0 and IV1, owned by the caller.
   Returns true if successful, false if evaluating the expressions failed or if
   LVALUE otherwise can't be used for an assignment.

   On success, the caller retains ownership of the index vectors, which are
   suitable for passing to matrix_lvalue_assign().  If not used for that
   purpose then they need to eventually be freed (with
   index_vector_uninit()). */
static bool
matrix_lvalue_evaluate (struct matrix_lvalue *lvalue,
                        struct index_vector *iv0,
                        struct index_vector *iv1)
{
  *iv0 = INDEX_VECTOR_INIT;
  *iv1 = INDEX_VECTOR_INIT;
  if (!lvalue->n_indexes)
    return true;

  /* Validate destination matrix exists and has the right shape. */
  gsl_matrix *dm = lvalue->var->value;
  if (!dm)
    {
      msg_at (SE, lvalue->var_location,
              _("Undefined variable %s."), lvalue->var->name);
      return false;
    }
  else if (dm->size1 == 0 || dm->size2 == 0)
    {
      msg_at (SE, lvalue->full_location, _("Cannot index %zu×%zu matrix %s."),
              dm->size1, dm->size2, lvalue->var->name);
      return false;
    }
  else if (lvalue->n_indexes == 1)
    {
      if (!is_vector (dm))
        {
          msg_at (SE, lvalue->full_location,
                  _("Can't use vector indexing on %zu×%zu matrix %s."),
                  dm->size1, dm->size2, lvalue->var->name);
          return false;
        }
      return matrix_lvalue_evaluate_vector (lvalue->indexes[0],
                                            MAX (dm->size1, dm->size2),
                                            IV_VECTOR, 0, iv0);
    }
  else
    {
      assert (lvalue->n_indexes == 2);
      if (!matrix_lvalue_evaluate_vector (lvalue->indexes[0], dm->size1,
                                          IV_ROW, dm->size2, iv0))
        return false;

      if (!matrix_lvalue_evaluate_vector (lvalue->indexes[1], dm->size2,
                                          IV_COLUMN, dm->size1, iv1))
        {
          index_vector_uninit (iv0);
          return false;
        }
      return true;
    }
}

static bool
matrix_lvalue_assign_vector (struct matrix_lvalue *lvalue,
                             struct index_vector *iv,
                             gsl_matrix *sm, const struct msg_location *lsm)
{
  /* Convert source matrix 'sm' to source vector 'sv'. */
  if (!is_vector (sm))
    {
      msg_at (SE, lvalue->full_location,
              _("Only an %zu-element vector may be assigned to this "
                "%zu-element subvector of %s."),
              iv->n, iv->n, lvalue->var->name);
      msg_at (SE, lsm,
              _("The source is an %zu×%zu matrix."),
              sm->size1, sm->size2);
      return false;
    }
  gsl_vector sv = to_vector (sm);
  if (iv->n != sv.size)
    {
      msg_at (SE, lvalue->full_location,
              _("Only an %zu-element vector may be assigned to this "
                "%zu-element subvector of %s."),
              iv->n, iv->n, lvalue->var->name);
      msg_at (SE, lsm, ngettext ("The source vector has %zu element.",
                                 "The source vector has %zu elements.",
                                 sv.size),
              sv.size);
      return false;
    }

  /* Assign elements. */
  gsl_vector dv = to_vector (lvalue->var->value);
  for (size_t x = 0; x < iv->n; x++)
    gsl_vector_set (&dv, iv->indexes[x], gsl_vector_get (&sv, x));
  return true;
}

static bool
matrix_lvalue_assign_matrix (struct matrix_lvalue *lvalue,
                             struct index_vector *iv0,
                             struct index_vector *iv1,
                             gsl_matrix *sm, const struct msg_location *lsm)
{
  gsl_matrix *dm = lvalue->var->value;

  /* Convert source matrix 'sm' to source vector 'sv'. */
  bool wrong_rows = iv0->n != sm->size1;
  bool wrong_columns = iv1->n != sm->size2;
  if (wrong_rows || wrong_columns)
    {
      if (wrong_rows && wrong_columns)
        msg_at (SE, lvalue->full_location,
                _("Numbers of indexes for assigning to %s differ from the "
                  "size of the source matrix."),
                lvalue->var->name);
      else if (wrong_rows)
        msg_at (SE, lvalue->full_location,
                _("Number of row indexes for assigning to %s differs from "
                  "number of rows in source matrix."),
                lvalue->var->name);
      else
        msg_at (SE, lvalue->full_location,
                _("Number of column indexes for assigning to %s differs from "
                  "number of columns in source matrix."),
                lvalue->var->name);

      if (wrong_rows)
        {
          if (lvalue->indexes[0])
            msg_at (SN, lvalue->index_locations[0],
                    ngettext ("There is %zu row index.",
                              "There are %zu row indexes.",
                              iv0->n),
                    iv0->n);
          else
            msg_at (SN, lvalue->index_locations[0],
                    ngettext ("Destination matrix %s has %zu row.",
                              "Destination matrix %s has %zu rows.",
                              iv0->n),
                    lvalue->var->name, iv0->n);
        }

      if (wrong_columns)
        {
          if (lvalue->indexes[1])
            msg_at (SN, lvalue->index_locations[1],
                    ngettext ("There is %zu column index.",
                              "There are %zu column indexes.",
                              iv1->n),
                    iv1->n);
          else
            msg_at (SN, lvalue->index_locations[1],
                    ngettext ("Destination matrix %s has %zu column.",
                              "Destination matrix %s has %zu columns.",
                              iv1->n),
                    lvalue->var->name, iv1->n);
        }

      msg_at (SN, lsm, _("The source matrix is %zu×%zu."),
              sm->size1, sm->size2);
      return false;
    }

  /* Assign elements. */
  for (size_t y = 0; y < iv0->n; y++)
    {
      size_t f0 = iv0->indexes[y];
      for (size_t x = 0; x < iv1->n; x++)
        {
          size_t f1 = iv1->indexes[x];
          gsl_matrix_set (dm, f0, f1, gsl_matrix_get (sm, y, x));
        }
    }
  return true;
}

/* Assigns rvalue SM to LVALUE, whose index vectors IV0 and IV1 were previously
   obtained with matrix_lvalue_evaluate().  Returns true if successful, false
   on error.  Always takes ownership of M.  LSM should be the source location
   to use for errors related to SM. */
static bool
matrix_lvalue_assign (struct matrix_lvalue *lvalue,
                      struct index_vector *iv0, struct index_vector *iv1,
                      gsl_matrix *sm, const struct msg_location *lsm)
{
  if (!lvalue->n_indexes)
    {
      gsl_matrix_free (lvalue->var->value);
      lvalue->var->value = sm;
      return true;
    }
  else
    {
      bool ok = (lvalue->n_indexes == 1
                 ? matrix_lvalue_assign_vector (lvalue, iv0, sm, lsm)
                 : matrix_lvalue_assign_matrix (lvalue, iv0, iv1, sm, lsm));
      index_vector_uninit (iv0);
      index_vector_uninit (iv1);
      gsl_matrix_free (sm);
      return ok;
    }
}

/* Evaluates and then assigns SM to LVALUE.  Always takes ownership of M.  LSM
   should be the source location to use for errors related to SM.*/
static bool
matrix_lvalue_evaluate_and_assign (struct matrix_lvalue *lvalue,
                                   gsl_matrix *sm,
                                   const struct msg_location *lsm)
{
  struct index_vector iv0, iv1;
  if (!matrix_lvalue_evaluate (lvalue, &iv0, &iv1))
    {
      gsl_matrix_free (sm);
      return false;
    }

  return matrix_lvalue_assign (lvalue, &iv0, &iv1, sm, lsm);
}

/* Matrix command data structure. */

/* An array of matrix commands. */
struct matrix_commands
  {
    struct matrix_command **commands;
    size_t n;
  };

static bool matrix_commands_parse (struct matrix_state *,
                                   struct matrix_commands *,
                                   const char *command_name,
                                   const char *stop1, const char *stop2);
static void matrix_commands_uninit (struct matrix_commands *);

/* A single matrix command. */
struct matrix_command
  {
    /* The type of command. */
    enum matrix_command_type
      {
        MCMD_COMPUTE,
        MCMD_PRINT,
        MCMD_DO_IF,
        MCMD_LOOP,
        MCMD_BREAK,
        MCMD_DISPLAY,
        MCMD_RELEASE,
        MCMD_SAVE,
        MCMD_READ,
        MCMD_WRITE,
        MCMD_GET,
        MCMD_MSAVE,
        MCMD_MGET,
        MCMD_EIGEN,
        MCMD_SETDIAG,
        MCMD_SVD,
      }
    type;

    /* Source lines for this command. */
    struct msg_location *location;

    union
      {
        struct matrix_compute
          {
            struct matrix_lvalue *lvalue;
            struct matrix_expr *rvalue;
          }
        compute;

        struct matrix_print
          {
            struct matrix_expr *expression;
            bool use_default_format;
            struct fmt_spec format;
            char *title;
            int space;          /* -1 means NEWPAGE. */

            struct print_labels
              {
                struct string_array literals; /* CLABELS/RLABELS. */
                struct matrix_expr *expr;     /* CNAMES/RNAMES. */
              }
            *rlabels, *clabels;
          }
        print;

        struct matrix_do_if
          {
            struct do_if_clause
              {
                struct matrix_expr *condition;
                struct matrix_commands commands;
              }
            *clauses;
            size_t n_clauses;
          }
        do_if;

        struct matrix_loop
          {
            /* Index. */
            struct matrix_var *var;
            struct matrix_expr *start;
            struct matrix_expr *end;
            struct matrix_expr *increment;

            /* Loop conditions. */
            struct matrix_expr *top_condition;
            struct matrix_expr *bottom_condition;

            /* Commands. */
            struct matrix_commands commands;
          }
        loop;

        struct matrix_display
          {
            struct matrix_state *state;
          }
        display;

        struct matrix_release
          {
            struct matrix_var **vars;
            size_t n_vars;
          }
        release;

        struct matrix_save
          {
            struct matrix_expr *expression;
            struct save_file *sf;
          }
        save;

        struct matrix_read
          {
            struct read_file *rf;
            struct matrix_lvalue *dst;
            struct matrix_expr *size;
            int c1, c2;
            enum fmt_type format;
            int w;
            bool symmetric;
            bool reread;
          }
        read;

        struct matrix_write
          {
            struct write_file *wf;
            struct matrix_expr *expression;
            int c1, c2;

            /* If this is nonnull, WRITE uses this format.

               If this is NULL, WRITE uses free-field format with as many
               digits of precision as needed. */
            struct fmt_spec *format;

            bool triangular;
            bool hold;
          }
        write;

        struct matrix_get
          {
            struct lexer *lexer;
            struct matrix_lvalue *dst;
            struct dataset *dataset;
            struct file_handle *file;
            char *encoding;
            struct var_syntax *vars;
            size_t n_vars;
            struct matrix_var *names;

            /* Treatment of missing values. */
            struct
              {
                enum
                  {
                    MGET_ERROR,  /* Flag error on command. */
                    MGET_ACCEPT, /* Accept without change, user-missing only. */
                    MGET_OMIT,   /* Drop this case. */
                    MGET_RECODE  /* Recode to 'substitute'. */
                  }
                treatment;
                double substitute; /* MGET_RECODE only. */
              }
            user, system;
          }
        get;

        struct matrix_msave
          {
            struct msave_common *common;
            struct matrix_expr *expr;
            const char *rowtype;
            const struct matrix_expr *factors;
            const struct matrix_expr *splits;
          }
         msave;

        struct matrix_mget
          {
            struct matrix_state *state;
            struct file_handle *file;
            char *encoding;
            struct stringi_set rowtypes;
          }
        mget;

        struct matrix_eigen
          {
            struct matrix_expr *expr;
            struct matrix_var *evec;
            struct matrix_var *eval;
          }
        eigen;

        struct matrix_setdiag
          {
            struct matrix_var *dst;
            struct matrix_expr *expr;
          }
        setdiag;

        struct matrix_svd
          {
            struct matrix_expr *expr;
            struct matrix_var *u;
            struct matrix_var *s;
            struct matrix_var *v;
          }
        svd;
      };
  };

static struct matrix_command *matrix_command_parse (struct matrix_state *);
static bool matrix_command_execute (struct matrix_command *);
static void matrix_command_destroy (struct matrix_command *);

/* COMPUTE. */

static struct matrix_command *
matrix_compute_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_COMPUTE,
    .compute = { .lvalue = NULL },
  };

  struct matrix_compute *compute = &cmd->compute;
  compute->lvalue = matrix_lvalue_parse (s);
  if (!compute->lvalue)
    goto error;

  if (!lex_force_match (s->lexer, T_EQUALS))
    goto error;

  compute->rvalue = matrix_expr_parse (s);
  if (!compute->rvalue)
    goto error;

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_compute_execute (struct matrix_command *cmd)
{
  struct matrix_compute *compute = &cmd->compute;
  gsl_matrix *value = matrix_expr_evaluate (compute->rvalue);
  if (!value)
    return;

  matrix_lvalue_evaluate_and_assign (compute->lvalue, value,
                                     matrix_expr_location (compute->rvalue));
}

/* PRINT. */

static void
print_labels_destroy (struct print_labels *labels)
{
  if (labels)
    {
      string_array_destroy (&labels->literals);
      matrix_expr_destroy (labels->expr);
      free (labels);
    }
}

static void
parse_literal_print_labels (struct matrix_state *s,
                            struct print_labels **labelsp)
{
  lex_match (s->lexer, T_EQUALS);
  print_labels_destroy (*labelsp);
  *labelsp = xzalloc (sizeof **labelsp);
  while (lex_token (s->lexer) != T_SLASH
         && lex_token (s->lexer) != T_ENDCMD
         && lex_token (s->lexer) != T_STOP)
    {
      struct string label = DS_EMPTY_INITIALIZER;
      while (lex_token (s->lexer) != T_COMMA
             && lex_token (s->lexer) != T_SLASH
             && lex_token (s->lexer) != T_ENDCMD
             && lex_token (s->lexer) != T_STOP)
        {
          if (!ds_is_empty (&label))
            ds_put_byte (&label, ' ');

          if (lex_token (s->lexer) == T_STRING)
            ds_put_cstr (&label, lex_tokcstr (s->lexer));
          else
            {
              char *rep = lex_next_representation (s->lexer, 0, 0);
              ds_put_cstr (&label, rep);
              free (rep);
            }
          lex_get (s->lexer);
        }
      string_array_append_nocopy (&(*labelsp)->literals,
                                  ds_steal_cstr (&label));

      if (!lex_match (s->lexer, T_COMMA))
        break;
    }
}

static bool
parse_expr_print_labels (struct matrix_state *s, struct print_labels **labelsp)
{
  lex_match (s->lexer, T_EQUALS);
  struct matrix_expr *e = matrix_parse_exp (s);
  if (!e)
    return false;

  print_labels_destroy (*labelsp);
  *labelsp = xzalloc (sizeof **labelsp);
  (*labelsp)->expr = e;
  return true;
}

static struct matrix_command *
matrix_print_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_PRINT,
    .print = {
      .use_default_format = true,
    }
  };

  if (lex_token (s->lexer) != T_SLASH && lex_token (s->lexer) != T_ENDCMD)
    {
      int start_ofs = lex_ofs (s->lexer);
      cmd->print.expression = matrix_parse_exp (s);
      if (!cmd->print.expression)
        goto error;
      cmd->print.title = lex_ofs_representation (s->lexer, start_ofs,
                                                 lex_ofs (s->lexer) - 1);
    }

  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "FORMAT"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!parse_format_specifier (s->lexer, &cmd->print.format))
            goto error;

          char *error = fmt_check_output__ (cmd->print.format);
          if (error)
            {
              lex_next_error (s->lexer, -1, -1, "%s", error);
              free (error);
              goto error;
            }

          cmd->print.use_default_format = false;
        }
      else if (lex_match_id (s->lexer, "TITLE"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_string (s->lexer))
            goto error;
          free (cmd->print.title);
          cmd->print.title = ss_xstrdup (lex_tokss (s->lexer));
          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "SPACE"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (lex_match_id (s->lexer, "NEWPAGE"))
            cmd->print.space = -1;
          else if (lex_force_int_range (s->lexer, "SPACE", 1, INT_MAX))
            {
              cmd->print.space = lex_integer (s->lexer);
              lex_get (s->lexer);
            }
          else
            goto error;
        }
      else if (lex_match_id (s->lexer, "RLABELS"))
        parse_literal_print_labels (s, &cmd->print.rlabels);
      else if (lex_match_id (s->lexer, "CLABELS"))
        parse_literal_print_labels (s, &cmd->print.clabels);
      else if (lex_match_id (s->lexer, "RNAMES"))
        {
          if (!parse_expr_print_labels (s, &cmd->print.rlabels))
            goto error;
        }
      else if (lex_match_id (s->lexer, "CNAMES"))
        {
          if (!parse_expr_print_labels (s, &cmd->print.clabels))
            goto error;
        }
      else
        {
          lex_error_expecting (s->lexer, "FORMAT", "TITLE", "SPACE",
                               "RLABELS", "CLABELS", "RNAMES", "CNAMES");
          goto error;
        }

    }
  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static bool
matrix_is_integer (const gsl_matrix *m)
{
  for (size_t y = 0; y < m->size1; y++)
    for (size_t x = 0; x < m->size2; x++)
      {
        double d = gsl_matrix_get (m, y, x);
        if (d != floor (d))
          return false;
      }
  return true;
}

static double
matrix_max_magnitude (const gsl_matrix *m)
{
  double max = 0.0;
  for (size_t y = 0; y < m->size1; y++)
    for (size_t x = 0; x < m->size2; x++)
      {
        double d = fabs (gsl_matrix_get (m, y, x));
        if (d > max)
          max = d;
      }
  return max;
}

static bool
format_fits (struct fmt_spec format, double x)
{
  char *s = data_out (&(union value) { .f = x }, NULL,
                      format, settings_get_fmt_settings ());
  bool fits = *s != '*' && !strchr (s, 'E');
  free (s);
  return fits;
}

static struct fmt_spec
default_format (const gsl_matrix *m, int *log_scale)
{
  *log_scale = 0;

  double max = matrix_max_magnitude (m);

  if (matrix_is_integer (m))
    for (int w = 1; w <= 12; w++)
      {
        struct fmt_spec format = { .type = FMT_F, .w = w };
        if (format_fits (format, -max))
          return format;
      }

  if (max >= 1e9 || max <= 1e-4)
    {
      char s[64];
      snprintf (s, sizeof s, "%.1e", max);

      const char *e = strchr (s, 'e');
      if (e)
        *log_scale = atoi (e + 1);
    }

  return (struct fmt_spec) { .type = FMT_F, .w = 13, .d = 10 };
}

static char *
trimmed_string (double d)
{
  struct substring s = ss_buffer ((char *) &d, sizeof d);
  ss_rtrim (&s, ss_cstr (" "));
  return ss_xstrdup (s);
}

static struct string_array *
print_labels_get (const struct print_labels *labels, size_t n,
                  const char *prefix, bool truncate)
{
  if (!labels)
    return NULL;

  struct string_array *out = xzalloc (sizeof *out);
  if (labels->literals.n)
    string_array_clone (out, &labels->literals);
  else if (labels->expr)
    {
      gsl_matrix *m = matrix_expr_evaluate (labels->expr);
      if (m && is_vector (m))
        {
          gsl_vector v = to_vector (m);
          for (size_t i = 0; i < v.size; i++)
            string_array_append_nocopy (out, trimmed_string (
                                          gsl_vector_get (&v, i)));
        }
      gsl_matrix_free (m);
    }

  while (out->n < n)
    {
      if (labels->expr)
        string_array_append_nocopy (out, xasprintf ("%s%zu", prefix,
                                                    out->n + 1));
      else
        string_array_append (out, "");
    }
  while (out->n > n)
    string_array_delete (out, out->n - 1);

  if (truncate)
    for (size_t i = 0; i < out->n; i++)
      {
        char *s = out->strings[i];
        s[strnlen (s, 8)] = '\0';
      }

  return out;
}

static void
matrix_print_space (int space)
{
  if (space < 0)
    output_item_submit (page_break_item_create ());
  for (int i = 0; i < space; i++)
    output_log ("%s", "");
}

static void
matrix_print_text (const struct matrix_print *print, const gsl_matrix *m,
                   struct fmt_spec format, int log_scale)
{
  matrix_print_space (print->space);
  if (print->title)
    output_log ("%s", print->title);
  if (log_scale != 0)
    output_log ("  10 ** %d   X", log_scale);

  struct string_array *clabels = print_labels_get (print->clabels,
                                                   m->size2, "col", true);
  if (clabels && format.w < 8)
    format.w = 8;
  struct string_array *rlabels = print_labels_get (print->rlabels,
                                                   m->size1, "row", true);

  if (clabels)
    {
      struct string line = DS_EMPTY_INITIALIZER;
      if (rlabels)
        ds_put_byte_multiple (&line, ' ', 8);
      for (size_t x = 0; x < m->size2; x++)
        ds_put_format (&line, " %*s", format.w, clabels->strings[x]);
      output_log_nocopy (ds_steal_cstr (&line));
    }

  double scale = pow (10.0, log_scale);
  bool numeric = fmt_is_numeric (format.type);
  for (size_t y = 0; y < m->size1; y++)
    {
      struct string line = DS_EMPTY_INITIALIZER;
      if (rlabels)
        ds_put_format (&line, "%-8s", rlabels->strings[y]);

      for (size_t x = 0; x < m->size2; x++)
        {
          double f = gsl_matrix_get (m, y, x);
          char *s = (numeric
                     ? data_out (&(union value) { .f = f / scale}, NULL,
                                 format, settings_get_fmt_settings ())
                     : trimmed_string (f));
          ds_put_format (&line, " %s", s);
          free (s);
        }
      output_log_nocopy (ds_steal_cstr (&line));
    }

  string_array_destroy (rlabels);
  free (rlabels);
  string_array_destroy (clabels);
  free (clabels);
}

static void
create_print_dimension (struct pivot_table *table, enum pivot_axis_type axis,
                        const struct print_labels *print_labels, size_t n,
                        const char *name, const char *prefix)
{
  struct string_array *labels = print_labels_get (print_labels, n, prefix,
                                                  false);
  struct pivot_dimension *d = pivot_dimension_create (table, axis, name);
  for (size_t i = 0; i < n; i++)
    pivot_category_create_leaf (
      d->root, (labels
                ? pivot_value_new_user_text (labels->strings[i], SIZE_MAX)
                : pivot_value_new_integer (i + 1)));
  if (!labels)
    d->hide_all_labels = true;
  string_array_destroy (labels);
  free (labels);
}

static void
matrix_print_tables (const struct matrix_print *print, const gsl_matrix *m,
                     struct fmt_spec format, int log_scale)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_user_text (print->title ? print->title : "", SIZE_MAX),
    "Matrix Print");

  create_print_dimension (table, PIVOT_AXIS_ROW, print->rlabels, m->size1,
                          N_("Rows"), "row");
  create_print_dimension (table, PIVOT_AXIS_COLUMN, print->clabels, m->size2,
                          N_("Columns"), "col");

  struct pivot_footnote *footnote = NULL;
  if (log_scale != 0)
    {
      char *s = xasprintf ("× 10**%d", log_scale);
      footnote = pivot_table_create_footnote (
        table, pivot_value_new_user_text_nocopy (s));
    }

  double scale = pow (10.0, log_scale);
  bool numeric = fmt_is_numeric (format.type);
  for (size_t y = 0; y < m->size1; y++)
    for (size_t x = 0; x < m->size2; x++)
      {
        double f = gsl_matrix_get (m, y, x);
        struct pivot_value *v;
        if (numeric)
          {
            v = pivot_value_new_number (f / scale);
            v->numeric.format = format;
          }
        else
          v = pivot_value_new_user_text_nocopy (trimmed_string (f));
        if (footnote)
          pivot_value_add_footnote (v, footnote);
        pivot_table_put2 (table, y, x, v);
      }

  pivot_table_submit (table);
}

static void
matrix_print_execute (const struct matrix_print *print)
{
  if (print->expression)
    {
      gsl_matrix *m = matrix_expr_evaluate (print->expression);
      if (!m)
        return;

      int log_scale = 0;
      struct fmt_spec format = (print->use_default_format
                                ? default_format (m, &log_scale)
                                : print->format);

      if (settings_get_mdisplay () == SETTINGS_MDISPLAY_TEXT)
        matrix_print_text (print, m, format, log_scale);
      else
        matrix_print_tables (print, m, format, log_scale);

      gsl_matrix_free (m);
    }
  else
    {
      matrix_print_space (print->space);
      if (print->title)
        output_log ("%s", print->title);
    }
}

/* DO IF. */

static bool
matrix_do_if_clause_parse (struct matrix_state *s,
                           struct matrix_do_if *ifc,
                           bool parse_condition,
                           size_t *allocated_clauses)
{
  if (ifc->n_clauses >= *allocated_clauses)
    ifc->clauses = x2nrealloc (ifc->clauses, allocated_clauses,
                               sizeof *ifc->clauses);
  struct do_if_clause *c = &ifc->clauses[ifc->n_clauses++];
  *c = (struct do_if_clause) { .condition = NULL };

  if (parse_condition)
    {
      c->condition = matrix_expr_parse (s);
      if (!c->condition)
        return false;
    }

  return matrix_commands_parse (s, &c->commands, "DO IF", "ELSE", "END IF");
}

static struct matrix_command *
matrix_do_if_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_DO_IF,
    .do_if = { .n_clauses = 0 }
  };

  size_t allocated_clauses = 0;
  do
    {
      if (!matrix_do_if_clause_parse (s, &cmd->do_if, true, &allocated_clauses))
        goto error;
    }
  while (lex_match_phrase (s->lexer, "ELSE IF"));

  if (lex_match_id (s->lexer, "ELSE")
      && !matrix_do_if_clause_parse (s, &cmd->do_if, false, &allocated_clauses))
    goto error;

  if (!lex_match_phrase (s->lexer, "END IF"))
    NOT_REACHED ();
  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static bool
matrix_do_if_execute (struct matrix_do_if *cmd)
{
  for (size_t i = 0; i < cmd->n_clauses; i++)
    {
      struct do_if_clause *c = &cmd->clauses[i];
      if (c->condition)
        {
          double d;
          if (!matrix_expr_evaluate_scalar (c->condition,
                                            i ? "ELSE IF" : "DO IF",
                                            &d) || d <= 0)
            continue;
        }

      for (size_t j = 0; j < c->commands.n; j++)
        if (!matrix_command_execute (c->commands.commands[j]))
          return false;
      return true;
    }
  return true;
}

/* LOOP. */

static struct matrix_command *
matrix_loop_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) { .type = MCMD_LOOP, .loop = { .var = NULL } };

  struct matrix_loop *loop = &cmd->loop;
  if (lex_token (s->lexer) == T_ID && lex_next_token (s->lexer, 1) == T_EQUALS)
    {
      struct substring name = lex_tokss (s->lexer);
      loop->var = matrix_var_lookup (s, name);
      if (!loop->var)
        loop->var = matrix_var_create (s, name);

      lex_get (s->lexer);
      lex_get (s->lexer);

      loop->start = matrix_expr_parse (s);
      if (!loop->start || !lex_force_match (s->lexer, T_TO))
        goto error;

      loop->end = matrix_expr_parse (s);
      if (!loop->end)
        goto error;

      if (lex_match (s->lexer, T_BY))
        {
          loop->increment = matrix_expr_parse (s);
          if (!loop->increment)
            goto error;
        }
    }

  if (lex_match_id (s->lexer, "IF"))
    {
      loop->top_condition = matrix_expr_parse (s);
      if (!loop->top_condition)
        goto error;
    }

  bool was_in_loop = s->in_loop;
  s->in_loop = true;
  bool ok = matrix_commands_parse (s, &loop->commands, "LOOP",
                                   "END LOOP", NULL);
  s->in_loop = was_in_loop;
  if (!ok)
    goto error;

  if (!lex_match_phrase (s->lexer, "END LOOP"))
    NOT_REACHED ();

  if (lex_match_id (s->lexer, "IF"))
    {
      loop->bottom_condition = matrix_expr_parse (s);
      if (!loop->bottom_condition)
        goto error;
    }

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_loop_execute (struct matrix_loop *cmd)
{
  long int value = 0;
  long int end = 0;
  long int increment = 1;
  if (cmd->var)
    {
      if (!matrix_expr_evaluate_integer (cmd->start, "LOOP", &value)
          || !matrix_expr_evaluate_integer (cmd->end, "TO", &end)
          || (cmd->increment
              && !matrix_expr_evaluate_integer (cmd->increment, "BY",
                                                &increment)))
        return;

      if (increment > 0 ? value > end
          : increment < 0 ? value < end
          : true)
        return;
    }

  int mxloops = settings_get_mxloops ();
  for (int i = 0; i < mxloops; i++)
    {
      if (cmd->var)
        {
          if (cmd->var->value
              && (cmd->var->value->size1 != 1 || cmd->var->value->size2 != 1))
            {
              gsl_matrix_free (cmd->var->value);
              cmd->var->value = NULL;
            }
          if (!cmd->var->value)
            cmd->var->value = gsl_matrix_alloc (1, 1);

          gsl_matrix_set (cmd->var->value, 0, 0, value);
        }

      if (cmd->top_condition)
        {
          double d;
          if (!matrix_expr_evaluate_scalar (cmd->top_condition, "LOOP IF",
                                            &d) || d <= 0)
            return;
        }

      for (size_t j = 0; j < cmd->commands.n; j++)
        if (!matrix_command_execute (cmd->commands.commands[j]))
          return;

      if (cmd->bottom_condition)
        {
          double d;
          if (!matrix_expr_evaluate_scalar (cmd->bottom_condition,
                                            "END LOOP IF", &d) || d > 0)
            return;
        }

      if (cmd->var)
        {
          value += increment;
          if (increment > 0 ? value > end : value < end)
            return;
        }
    }
}

/* BREAK. */

static struct matrix_command *
matrix_break_parse (struct matrix_state *s)
{
  if (!s->in_loop)
    {
      lex_next_error (s->lexer, -1, -1, _("BREAK not inside LOOP."));
      return NULL;
    }

  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) { .type = MCMD_BREAK };
  return cmd;
}

/* DISPLAY. */

static struct matrix_command *
matrix_display_parse (struct matrix_state *s)
{
  while (lex_token (s->lexer) != T_ENDCMD)
    {
      if (!lex_match_id (s->lexer, "DICTIONARY")
          && !lex_match_id (s->lexer, "STATUS"))
        {
          lex_error_expecting (s->lexer, "DICTIONARY", "STATUS");
          return NULL;
        }
    }

  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) { .type = MCMD_DISPLAY, .display = { s } };
  return cmd;
}

static int
compare_matrix_var_pointers (const void *a_, const void *b_)
{
  const struct matrix_var *const *ap = a_;
  const struct matrix_var *const *bp = b_;
  const struct matrix_var *a = *ap;
  const struct matrix_var *b = *bp;
  return strcmp (a->name, b->name);
}

static void
matrix_display_execute (struct matrix_display *cmd)
{
  const struct matrix_state *s = cmd->state;

  struct pivot_table *table = pivot_table_create (N_("Matrix Variables"));
  struct pivot_dimension *attr_dimension
    = pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Attribute"));
  pivot_category_create_group (attr_dimension->root, N_("Dimension"),
                               N_("Rows"), N_("Columns"));
  pivot_category_create_leaves (attr_dimension->root, N_("Size (kB)"));

  const struct matrix_var **vars = xmalloc (hmap_count (&s->vars) * sizeof *vars);
  size_t n_vars = 0;

  const struct matrix_var *var;
  HMAP_FOR_EACH (var, struct matrix_var, hmap_node, &s->vars)
    if (var->value)
      vars[n_vars++] = var;
  qsort (vars, n_vars, sizeof *vars, compare_matrix_var_pointers);

  struct pivot_dimension *rows = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));
  for (size_t i = 0; i < n_vars; i++)
    {
      const struct matrix_var *var = vars[i];
      pivot_category_create_leaf (
        rows->root, pivot_value_new_user_text (var->name, SIZE_MAX));

      size_t r = var->value->size1;
      size_t c = var->value->size2;
      double values[] = { r, c, r * c * sizeof (double) / 1024 };
      for (size_t j = 0; j < sizeof values / sizeof *values; j++)
        pivot_table_put2 (table, j, i, pivot_value_new_integer (values[j]));
    }
  free (vars);
  pivot_table_submit (table);
}

/* RELEASE. */

static struct matrix_command *
matrix_release_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) { .type = MCMD_RELEASE };

  size_t allocated_vars = 0;
  while (lex_token (s->lexer) == T_ID)
    {
      struct matrix_var *var = matrix_var_lookup (s, lex_tokss (s->lexer));
      if (var)
        {
          if (cmd->release.n_vars >= allocated_vars)
            cmd->release.vars = x2nrealloc (cmd->release.vars, &allocated_vars,
                                            sizeof *cmd->release.vars);
          cmd->release.vars[cmd->release.n_vars++] = var;
        }
      else
        lex_error (s->lexer, _("Syntax error expecting variable name."));
      lex_get (s->lexer);

      if (!lex_match (s->lexer, T_COMMA))
        break;
    }

  return cmd;
}

static void
matrix_release_execute (struct matrix_release *cmd)
{
  for (size_t i = 0; i < cmd->n_vars; i++)
    {
      struct matrix_var *var = cmd->vars[i];
      gsl_matrix_free (var->value);
      var->value = NULL;
    }
}

/* SAVE. */

static struct save_file *
save_file_create (struct matrix_state *s, struct file_handle *fh,
                  struct string_array *variables,
                  struct matrix_expr *names,
                  struct stringi_set *strings)
{
  for (size_t i = 0; i < s->n_save_files; i++)
    {
      struct save_file *sf = s->save_files[i];
      if (fh_equal (sf->file, fh))
        {
          fh_unref (fh);

          string_array_destroy (variables);
          matrix_expr_destroy (names);
          stringi_set_destroy (strings);

          return sf;
        }
    }

  struct save_file *sf = xmalloc (sizeof *sf);
  *sf = (struct save_file) {
    .file = fh,
    .dataset = s->dataset,
    .variables = *variables,
    .names = names,
    .strings = STRINGI_SET_INITIALIZER (sf->strings),
  };

  stringi_set_swap (&sf->strings, strings);
  stringi_set_destroy (strings);

  s->save_files = xrealloc (s->save_files,
                             (s->n_save_files + 1) * sizeof *s->save_files);
  s->save_files[s->n_save_files++] = sf;
  return sf;
}

static struct casewriter *
save_file_open (struct save_file *sf, gsl_matrix *m,
                const struct msg_location *save_location)
{
  if (sf->writer || sf->error)
    {
      if (sf->writer)
        {
          size_t n_variables = caseproto_get_n_widths (
            casewriter_get_proto (sf->writer));
          if (m->size2 != n_variables)
            {
              const char *file_name = (sf->file == fh_inline_file () ? "*"
                                       : fh_get_name (sf->file));
              msg_at (SE, save_location,
                      _("Cannot save %zu×%zu matrix to %s because the "
                        "first SAVE to %s in this matrix program wrote a "
                        "%zu-column matrix."),
                      m->size1, m->size2, file_name, file_name, n_variables);
              msg_at (SE, sf->location,
                      _("This is the location of the first SAVE to %s."),
                      file_name);
              return NULL;
            }
        }
      return sf->writer;
    }

  bool ok = true;
  struct dictionary *dict = dict_create (get_default_encoding ());

  /* Fill 'names' with user-specified names if there were any, either from
     sf->variables or sf->names. */
  struct string_array names = { .n = 0 };
  if (sf->variables.n)
    string_array_clone (&names, &sf->variables);
  else if (sf->names)
    {
      gsl_matrix *nm = matrix_expr_evaluate (sf->names);
      if (nm && is_vector (nm))
        {
          gsl_vector nv = to_vector (nm);
          for (size_t i = 0; i < nv.size; i++)
            {
              char *name = trimmed_string (gsl_vector_get (&nv, i));
              char *error = dict_id_is_valid__ (dict, name, DC_ORDINARY);
              if (!error)
                string_array_append_nocopy (&names, name);
              else
                {
                  msg_at (SE, save_location, "%s", error);
                  free (error);
                  ok = false;
                }
            }
        }
      gsl_matrix_free (nm);
    }

  struct stringi_set strings;
  stringi_set_clone (&strings, &sf->strings);

  for (size_t i = 0; dict_get_n_vars (dict) < m->size2; i++)
    {
      char tmp_name[64];
      const char *name;
      if (i < names.n)
        name = names.strings[i];
      else
        {
          snprintf (tmp_name, sizeof tmp_name, "COL%zu", i + 1);
          name = tmp_name;
        }

      int width = stringi_set_delete (&strings, name) ? 8 : 0;
      struct variable *var = dict_create_var (dict, name, width);
      if (!var)
        {
          msg_at (ME, save_location,
                  _("Duplicate variable name %s in SAVE statement."), name);
          ok = false;
        }
    }

  if (!stringi_set_is_empty (&strings))
    {
      size_t count = stringi_set_count (&strings);
      const char *example = stringi_set_node_get_string (
        stringi_set_first (&strings));

      if (count == 1)
        msg_at (ME, save_location,
                _("The SAVE command STRINGS subcommand specifies an unknown "
                  "variable %s."), example);
      else
        msg_at (ME, save_location,
                ngettext ("The SAVE command STRINGS subcommand specifies %zu "
                          "unknown variable: %s.",
                          "The SAVE command STRINGS subcommand specifies %zu "
                          "unknown variables, including %s.",
                          count),
                count, example);
      ok = false;
    }
  stringi_set_destroy (&strings);
  string_array_destroy (&names);

  if (!ok)
    {
      dict_unref (dict);
      sf->error = true;
      return NULL;
    }

  if (sf->file == fh_inline_file ())
    sf->writer = autopaging_writer_create (dict_get_proto (dict));
  else
    sf->writer = any_writer_open (sf->file, dict);
  if (sf->writer)
    {
      sf->dict = dict;
      sf->location = msg_location_dup (save_location);
    }
  else
    {
      dict_unref (dict);
      sf->error = true;
    }
  return sf->writer;
}

static void
save_file_destroy (struct save_file *sf)
{
  if (sf)
    {
      if (sf->file == fh_inline_file () && sf->writer && sf->dict)
        {
          dataset_set_dict (sf->dataset, sf->dict);
          dataset_set_source (sf->dataset, casewriter_make_reader (sf->writer));
        }
      else
        {
          casewriter_destroy (sf->writer);
          dict_unref (sf->dict);
        }
      fh_unref (sf->file);
      string_array_destroy (&sf->variables);
      matrix_expr_destroy (sf->names);
      stringi_set_destroy (&sf->strings);
      msg_location_destroy (sf->location);
      free (sf);
    }
}

static struct matrix_command *
matrix_save_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_SAVE,
    .save = { .expression = NULL },
  };

  struct file_handle *fh = NULL;
  struct matrix_save *save = &cmd->save;

  struct string_array variables = STRING_ARRAY_INITIALIZER;
  struct matrix_expr *names = NULL;
  struct stringi_set strings = STRINGI_SET_INITIALIZER (strings);

  save->expression = matrix_parse_exp (s);
  if (!save->expression)
    goto error;

  int names_start = 0;
  int names_end = 0;
  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "OUTFILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (fh);
          fh = (lex_match (s->lexer, T_ASTERISK)
                ? fh_inline_file ()
                : fh_parse (s->lexer, FH_REF_FILE, s->session));
          if (!fh)
            goto error;
        }
      else if (lex_match_id (s->lexer, "VARIABLES"))
        {
          lex_match (s->lexer, T_EQUALS);

          char **names;
          size_t n;
          struct dictionary *d = dict_create (get_default_encoding ());
          bool ok = parse_DATA_LIST_vars (s->lexer, d, &names, &n,
                                          PV_NO_SCRATCH | PV_NO_DUPLICATE);
          dict_unref (d);
          if (!ok)
            goto error;

          string_array_clear (&variables);
          variables = (struct string_array) {
            .strings = names,
            .n = n,
            .allocated = n,
          };
        }
      else if (lex_match_id (s->lexer, "NAMES"))
        {
          lex_match (s->lexer, T_EQUALS);
          matrix_expr_destroy (names);
          names_start = lex_ofs (s->lexer);
          names = matrix_parse_exp (s);
          names_end = lex_ofs (s->lexer) - 1;
          if (!names)
            goto error;
        }
      else if (lex_match_id (s->lexer, "STRINGS"))
        {
          lex_match (s->lexer, T_EQUALS);
          while (lex_token (s->lexer) == T_ID)
            {
              stringi_set_insert (&strings, lex_tokcstr (s->lexer));
              lex_get (s->lexer);
              if (!lex_match (s->lexer, T_COMMA))
                break;
            }
        }
      else
        {
          lex_error_expecting (s->lexer, "OUTFILE", "VARIABLES", "NAMES",
                               "STRINGS");
          goto error;
        }
    }

  if (!fh)
    {
      if (s->prev_save_file)
        fh = fh_ref (s->prev_save_file);
      else
        {
          lex_sbc_missing (s->lexer, "OUTFILE");
          goto error;
        }
    }
  fh_unref (s->prev_save_file);
  s->prev_save_file = fh_ref (fh);

  if (variables.n && names)
    {
      lex_ofs_msg (s->lexer, SW, names_start, names_end,
                   _("Ignoring NAMES because VARIABLES was also specified."));
      matrix_expr_destroy (names);
      names = NULL;
    }

  save->sf = save_file_create (s, fh, &variables, names, &strings);
  return cmd;

error:
  string_array_destroy (&variables);
  matrix_expr_destroy (names);
  stringi_set_destroy (&strings);
  fh_unref (fh);
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_save_execute (const struct matrix_command *cmd)
{
  const struct matrix_save *save = &cmd->save;

  gsl_matrix *m = matrix_expr_evaluate (save->expression);
  if (!m)
    return;

  struct casewriter *writer = save_file_open (save->sf, m, cmd->location);
  if (!writer)
    {
      gsl_matrix_free (m);
      return;
    }

  const struct caseproto *proto = casewriter_get_proto (writer);
  for (size_t y = 0; y < m->size1; y++)
    {
      struct ccase *c = case_create (proto);
      for (size_t x = 0; x < m->size2; x++)
        {
          double d = gsl_matrix_get (m, y, x);
          int width = caseproto_get_width (proto, x);
          union value *value = case_data_rw_idx (c, x);
          if (width == 0)
            value->f = d;
          else
            memcpy (value->s, &d, width);
        }
      casewriter_write (writer, c);
    }
  gsl_matrix_free (m);
}

/* READ. */

static struct read_file *
read_file_create (struct matrix_state *s, struct file_handle *fh)
{
  for (size_t i = 0; i < s->n_read_files; i++)
    {
      struct read_file *rf = s->read_files[i];
      if (rf->file == fh)
        {
          fh_unref (fh);
          return rf;
        }
    }

  struct read_file *rf = xmalloc (sizeof *rf);
  *rf = (struct read_file) { .file = fh };

  s->read_files = xrealloc (s->read_files,
                            (s->n_read_files + 1) * sizeof *s->read_files);
  s->read_files[s->n_read_files++] = rf;
  return rf;
}

static struct dfm_reader *
read_file_open (struct read_file *rf)
{
  if (!rf->reader)
    rf->reader = dfm_open_reader (rf->file, NULL, rf->encoding);
  return rf->reader;
}

static void
read_file_destroy (struct read_file *rf)
{
  if (rf)
    {
      fh_unref (rf->file);
      dfm_close_reader (rf->reader);
      free (rf->encoding);
      free (rf);
    }
}

static struct matrix_command *
matrix_read_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_READ,
    .read = { .format = FMT_F },
  };

  struct file_handle *fh = NULL;
  char *encoding = NULL;
  struct matrix_read *read = &cmd->read;
  read->dst = matrix_lvalue_parse (s);
  if (!read->dst)
    goto error;

  int by_ofs = 0;
  int format_ofs = 0;
  int record_width_start = 0, record_width_end = 0;

  int by = 0;
  int repetitions = 0;
  int record_width = 0;
  bool seen_format = false;
  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "FILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (fh);
          fh = fh_parse (s->lexer, FH_REF_FILE, NULL);
          if (!fh)
            goto error;
        }
      else if (lex_match_id (s->lexer, "ENCODING"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_string (s->lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (s->lexer));

          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "FIELD"))
        {
          lex_match (s->lexer, T_EQUALS);

          record_width_start = lex_ofs (s->lexer);
          if (!lex_force_int_range (s->lexer, "FIELD", 1, INT_MAX))
            goto error;
          read->c1 = lex_integer (s->lexer);
          lex_get (s->lexer);
          if (!lex_force_match (s->lexer, T_TO)
              || !lex_force_int_range (s->lexer, "TO", read->c1, INT_MAX))
            goto error;
          read->c2 = lex_integer (s->lexer) + 1;
          record_width_end = lex_ofs (s->lexer);
          lex_get (s->lexer);

          record_width = read->c2 - read->c1;
          if (lex_match (s->lexer, T_BY))
            {
              if (!lex_force_int_range (s->lexer, "BY", 1,
                                        read->c2 - read->c1))
                goto error;
              by = lex_integer (s->lexer);
              by_ofs = lex_ofs (s->lexer);
              int field_end = lex_ofs (s->lexer);
              lex_get (s->lexer);

              if (record_width % by)
                {
                  lex_ofs_error (
                    s->lexer, record_width_start, field_end,
                    _("Field width %d does not evenly divide record width %d."),
                    by, record_width);
                  lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                               _("This syntax designates the record width."));
                  lex_ofs_msg (s->lexer, SN, by_ofs, by_ofs,
                               _("This syntax specifies the field width."));
                  goto error;
                }
            }
          else
            by = 0;
        }
      else if (lex_match_id (s->lexer, "SIZE"))
        {
          lex_match (s->lexer, T_EQUALS);
          matrix_expr_destroy (read->size);
          read->size = matrix_parse_exp (s);
          if (!read->size)
            goto error;
        }
      else if (lex_match_id (s->lexer, "MODE"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (lex_match_id (s->lexer, "RECTANGULAR"))
            read->symmetric = false;
          else if (lex_match_id (s->lexer, "SYMMETRIC"))
            read->symmetric = true;
          else
            {
              lex_error_expecting (s->lexer, "RECTANGULAR", "SYMMETRIC");
              goto error;
            }
        }
      else if (lex_match_id (s->lexer, "REREAD"))
        read->reread = true;
      else if (lex_match_id (s->lexer, "FORMAT"))
        {
          if (seen_format)
            {
              lex_sbc_only_once (s->lexer, "FORMAT");
              goto error;
            }
          seen_format = true;

          lex_match (s->lexer, T_EQUALS);

          if (lex_token (s->lexer) != T_STRING && !lex_force_id (s->lexer))
            goto error;

          format_ofs = lex_ofs (s->lexer);
          const char *p = lex_tokcstr (s->lexer);
          if (c_isdigit (p[0]))
            {
              repetitions = atoi (p);
              p += strspn (p, "0123456789");
              if (!fmt_from_name (p, &read->format))
                {
                  lex_error (s->lexer, _("Unknown format %s."), p);
                  goto error;
                }
              lex_get (s->lexer);
            }
          else if (fmt_from_name (p, &read->format))
            lex_get (s->lexer);
          else
            {
              struct fmt_spec format;
              if (!parse_format_specifier (s->lexer, &format))
                goto error;
              read->format = format.type;
              read->w = format.w;
            }
        }
      else
        {
          lex_error_expecting (s->lexer, "FILE", "FIELD", "MODE",
                               "REREAD", "FORMAT");
          goto error;
        }
    }

  if (!read->c1)
    {
      lex_sbc_missing (s->lexer, "FIELD");
      goto error;
    }

  if (!read->dst->n_indexes && !read->size)
    {
      msg (SE, _("SIZE is required for reading data into a full matrix "
                 "(as opposed to a submatrix)."));
      msg_at (SN, read->dst->var_location,
              _("This expression designates a full matrix."));
      goto error;
    }

  if (!fh)
    {
      if (s->prev_read_file)
        fh = fh_ref (s->prev_read_file);
      else
        {
          lex_sbc_missing (s->lexer, "FILE");
          goto error;
        }
    }
  fh_unref (s->prev_read_file);
  s->prev_read_file = fh_ref (fh);

  read->rf = read_file_create (s, fh);
  fh = NULL;
  if (encoding)
    {
      free (read->rf->encoding);
      read->rf->encoding = encoding;
      encoding = NULL;
    }

  /* Field width may be specified in multiple ways:

     1. BY on FIELD.
     2. The format on FORMAT.
     3. The repetition factor on FORMAT.

     (2) and (3) are mutually exclusive.

     If more than one of these is present, they must agree.  If none of them is
     present, then free-field format is used.
   */
  if (repetitions > record_width)
    {
      msg (SE, _("%d repetitions cannot fit in record width %d."),
           repetitions, record_width);
      lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                   _("This syntax designates the number of repetitions."));
      lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                   _("This syntax designates the record width."));
      goto error;
    }
  int w = (repetitions ? record_width / repetitions
           : read->w ? read->w
           : by);
  if (by && w != by)
    {
      msg (SE, _("This command specifies two different field widths."));
      if (repetitions)
        {
          lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                       ngettext ("This syntax specifies %d repetition.",
                                 "This syntax specifies %d repetitions.",
                                 repetitions),
                       repetitions);
          lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                       _("This syntax designates record width %d, "
                         "which divided by %d repetitions implies "
                         "field width %d."),
                       record_width, repetitions, w);
        }
      else
        lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                     _("This syntax specifies field width %d."), w);

      lex_ofs_msg (s->lexer, SN, by_ofs, by_ofs,
                   _("This syntax specifies field width %d."), by);
      goto error;
    }
  read->w = w;
  return cmd;

error:
  fh_unref (fh);
  matrix_command_destroy (cmd);
  free (encoding);
  return NULL;
}

static void
parse_error (const struct dfm_reader *reader, enum fmt_type format,
             struct substring data, size_t y, size_t x,
             int first_column, int last_column, char *error)
{
  int line_number = dfm_get_line_number (reader);
  struct msg_location location = {
    .file_name = intern_new (dfm_get_file_name (reader)),
    .start = { .line = line_number, .column = first_column },
    .end = { .line = line_number, .column = last_column },
  };
  msg_at (DW, &location, _("Error reading \"%.*s\" as format %s "
                           "for matrix row %zu, column %zu: %s"),
          (int) data.length, data.string, fmt_name (format),
          y + 1, x + 1, error);
  msg_location_uninit (&location);
  free (error);
}

static void
matrix_read_set_field (struct matrix_read *read, struct dfm_reader *reader,
                       gsl_matrix *m, struct substring p, size_t y, size_t x,
                       const char *line_start)
{
  const char *input_encoding = dfm_reader_get_encoding (reader);
  char *error;
  double f;
  if (fmt_is_numeric (read->format))
    {
      union value v;
      error = data_in (p, input_encoding, read->format,
                       settings_get_fmt_settings (), &v, 0, NULL);
      if (!error && v.f == SYSMIS)
        error = xstrdup (_("Matrix data may not contain missing value."));
      f = v.f;
    }
    else
      {
        uint8_t s[sizeof (double)];
        union value v = { .s = s };
        error = data_in (p, input_encoding, read->format,
                         settings_get_fmt_settings (), &v, sizeof s, "UTF-8");
        memcpy (&f, s, sizeof f);
      }

  if (error)
    {
      int c1 = utf8_count_columns (line_start, p.string - line_start) + 1;
      int nc = ss_utf8_count_columns (p);
      int c2 = c1 + MAX (1, nc) - 1;
      parse_error (reader, read->format, p, y, x, c1, c2, error);
    }
  else
    {
      gsl_matrix_set (m, y, x, f);
      if (read->symmetric && x != y)
        gsl_matrix_set (m, x, y, f);
    }
}

static bool
matrix_read_line (struct matrix_command *cmd, struct dfm_reader *reader,
                  struct substring *line, const char **startp)
{
  struct matrix_read *read = &cmd->read;
  if (dfm_eof (reader))
    {
      msg_at (SE, cmd->location,
              _("Unexpected end of file reading matrix data."));
      return false;
    }
  dfm_expand_tabs (reader);
  struct substring record = dfm_get_record (reader);
  /* XXX need to recode record into UTF-8 */
  *startp = record.string;
  *line = ss_utf8_columns (record, read->c1 - 1, read->c2 - read->c1);
  return true;
}

static void
matrix_read (struct matrix_command *cmd, struct dfm_reader *reader,
             gsl_matrix *m)
{
  struct matrix_read *read = &cmd->read;
  for (size_t y = 0; y < m->size1; y++)
    {
      size_t nx = read->symmetric ? y + 1 : m->size2;

      struct substring line = ss_empty ();
      const char *line_start = line.string;
      for (size_t x = 0; x < nx; x++)
        {
          struct substring p;
          if (!read->w)
            {
              for (;;)
                {
                  ss_ltrim (&line, ss_cstr (" ,"));
                  if (!ss_is_empty (line))
                    break;
                  if (!matrix_read_line (cmd, reader, &line, &line_start))
                    return;
                  dfm_forward_record (reader);
                }

              ss_get_bytes (&line, ss_cspan (line, ss_cstr (" ,")), &p);
            }
          else
            {
              if (!matrix_read_line (cmd, reader, &line, &line_start))
                return;
              size_t fields_per_line = (read->c2 - read->c1) / read->w;
              int f = x % fields_per_line;
              if (f == fields_per_line - 1)
                dfm_forward_record (reader);

              p = ss_substr (line, read->w * f, read->w);
            }

          matrix_read_set_field (read, reader, m, p, y, x, line_start);
        }

      if (read->w)
        dfm_forward_record (reader);
      else
        {
          ss_ltrim (&line, ss_cstr (" ,"));
          if (!ss_is_empty (line))
            {
              int line_number = dfm_get_line_number (reader);
              int c1 = utf8_count_columns (line_start,
                                           line.string - line_start) + 1;
              int c2 = c1 + ss_utf8_count_columns (line) - 1;
              struct msg_location location = {
                .file_name = intern_new (dfm_get_file_name (reader)),
                .start = { .line = line_number, .column = c1 },
                .end = { .line = line_number, .column = c2 },
              };
              msg_at (DW, &location,
                      _("Trailing garbage following data for matrix row %zu."),
                      y + 1);
              msg_location_uninit (&location);
            }
        }
    }
}

static void
matrix_read_execute (struct matrix_command *cmd)
{
  struct matrix_read *read = &cmd->read;
  struct index_vector iv0, iv1;
  if (!matrix_lvalue_evaluate (read->dst, &iv0, &iv1))
    return;

  size_t size[2] = { SIZE_MAX, SIZE_MAX };
  if (read->size)
    {
      gsl_matrix *m = matrix_expr_evaluate (read->size);
      if (!m)
        return;

      if (!is_vector (m))
        {
          msg_at (SE, matrix_expr_location (read->size),
                  _("SIZE must evaluate to a scalar or a 2-element vector, "
                    "not a %zu×%zu matrix."), m->size1, m->size2);
          gsl_matrix_free (m);
          index_vector_uninit (&iv0);
          index_vector_uninit (&iv1);
          return;
        }

      gsl_vector v = to_vector (m);
      double d[2];
      if (v.size == 1)
        {
          d[0] = gsl_vector_get (&v, 0);
          d[1] = 1;
        }
      else if (v.size == 2)
        {
          d[0] = gsl_vector_get (&v, 0);
          d[1] = gsl_vector_get (&v, 1);
        }
      else
        {
          msg_at (SE, matrix_expr_location (read->size),
                  _("SIZE must evaluate to a scalar or a 2-element vector, "
                    "not a %zu×%zu matrix."),
                  m->size1, m->size2),
          gsl_matrix_free (m);
          index_vector_uninit (&iv0);
          index_vector_uninit (&iv1);
          return;
        }
      gsl_matrix_free (m);

      if (d[0] < 0 || d[0] > SIZE_MAX || d[1] < 0 || d[1] > SIZE_MAX)
        {
          msg_at (SE, matrix_expr_location (read->size),
                  _("Matrix dimensions %g×%g specified on SIZE "
                    "are outside valid range."),
                  d[0], d[1]);
          index_vector_uninit (&iv0);
          index_vector_uninit (&iv1);
          return;
        }

      size[0] = d[0];
      size[1] = d[1];
    }

  if (read->dst->n_indexes)
    {
      size_t submatrix_size[2];
      if (read->dst->n_indexes == 2)
        {
          submatrix_size[0] = iv0.n;
          submatrix_size[1] = iv1.n;
        }
      else if (read->dst->var->value->size1 == 1)
        {
          submatrix_size[0] = 1;
          submatrix_size[1] = iv0.n;
        }
      else
        {
          submatrix_size[0] = iv0.n;
          submatrix_size[1] = 1;
        }

      if (read->size)
        {
          if (size[0] != submatrix_size[0] || size[1] != submatrix_size[1])
            {
              msg_at (SE, cmd->location,
                      _("Dimensions specified on SIZE differ from dimensions "
                        "of destination submatrix."));
              msg_at (SN, matrix_expr_location (read->size),
                      _("SIZE specifies dimensions %zu×%zu."),
                      size[0], size[1]);
              msg_at (SN, read->dst->full_location,
                      _("Destination submatrix has dimensions %zu×%zu."),
                      submatrix_size[0], submatrix_size[1]);
              index_vector_uninit (&iv0);
              index_vector_uninit (&iv1);
              return;
            }
        }
      else
        {
          size[0] = submatrix_size[0];
          size[1] = submatrix_size[1];
        }
    }

  struct dfm_reader *reader = read_file_open (read->rf);
  if (read->reread)
    dfm_reread_record (reader, 1);

  if (read->symmetric && size[0] != size[1])
    {
      msg_at (SE, cmd->location,
              _("Cannot read non-square %zu×%zu matrix "
                "using READ with MODE=SYMMETRIC."),
              size[0], size[1]);
      index_vector_uninit (&iv0);
      index_vector_uninit (&iv1);
      return;
    }
  gsl_matrix *tmp = gsl_matrix_calloc (size[0], size[1]);
  matrix_read (cmd, reader, tmp);
  matrix_lvalue_assign (read->dst, &iv0, &iv1, tmp, cmd->location);
}

/* WRITE. */

static struct write_file *
write_file_create (struct matrix_state *s, struct file_handle *fh)
{
  for (size_t i = 0; i < s->n_write_files; i++)
    {
      struct write_file *wf = s->write_files[i];
      if (wf->file == fh)
        {
          fh_unref (fh);
          return wf;
        }
    }

  struct write_file *wf = xmalloc (sizeof *wf);
  *wf = (struct write_file) { .file = fh };

  s->write_files = xrealloc (s->write_files,
                             (s->n_write_files + 1) * sizeof *s->write_files);
  s->write_files[s->n_write_files++] = wf;
  return wf;
}

static struct dfm_writer *
write_file_open (struct write_file *wf)
{
  if (!wf->writer)
    wf->writer = dfm_open_writer (wf->file, wf->encoding);
  return wf->writer;
}

static void
write_file_destroy (struct write_file *wf)
{
  if (wf)
    {
      if (wf->held)
        {
          dfm_put_record_utf8 (wf->writer, wf->held->s.ss.string,
                               wf->held->s.ss.length);
          u8_line_destroy (wf->held);
          free (wf->held);
        }

      fh_unref (wf->file);
      dfm_close_writer (wf->writer);
      free (wf->encoding);
      free (wf);
    }
}

static struct matrix_command *
matrix_write_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_WRITE,
  };

  struct file_handle *fh = NULL;
  char *encoding = NULL;
  struct matrix_write *write = &cmd->write;
  write->expression = matrix_parse_exp (s);
  if (!write->expression)
    goto error;

  int by_ofs = 0;
  int format_ofs = 0;
  int record_width_start = 0, record_width_end = 0;

  int by = 0;
  int repetitions = 0;
  int record_width = 0;
  enum fmt_type format = FMT_F;
  bool has_format = false;
  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "OUTFILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (fh);
          fh = fh_parse (s->lexer, FH_REF_FILE, NULL);
          if (!fh)
            goto error;
        }
      else if (lex_match_id (s->lexer, "ENCODING"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_string (s->lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (s->lexer));

          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "FIELD"))
        {
          lex_match (s->lexer, T_EQUALS);

          record_width_start = lex_ofs (s->lexer);

          if (!lex_force_int_range (s->lexer, "FIELD", 1, INT_MAX))
            goto error;
          write->c1 = lex_integer (s->lexer);
          lex_get (s->lexer);
          if (!lex_force_match (s->lexer, T_TO)
              || !lex_force_int_range (s->lexer, "TO", write->c1, INT_MAX))
            goto error;
          write->c2 = lex_integer (s->lexer) + 1;
          record_width_end = lex_ofs (s->lexer);
          lex_get (s->lexer);

          record_width = write->c2 - write->c1;
          if (lex_match (s->lexer, T_BY))
            {
              if (!lex_force_int_range (s->lexer, "BY", 1,
                                        write->c2 - write->c1))
                goto error;
              by_ofs = lex_ofs (s->lexer);
              int field_end = lex_ofs (s->lexer);
              by = lex_integer (s->lexer);
              lex_get (s->lexer);

              if (record_width % by)
                {
                  lex_ofs_error (
                    s->lexer, record_width_start, field_end,
                    _("Field width %d does not evenly divide record width %d."),
                    by, record_width);
                  lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                               _("This syntax designates the record width."));
                  lex_ofs_msg (s->lexer, SN, by_ofs, by_ofs,
                               _("This syntax specifies the field width."));
                  goto error;
                }
            }
          else
            by = 0;
        }
      else if (lex_match_id (s->lexer, "MODE"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (lex_match_id (s->lexer, "RECTANGULAR"))
            write->triangular = false;
          else if (lex_match_id (s->lexer, "TRIANGULAR"))
            write->triangular = true;
          else
            {
              lex_error_expecting (s->lexer, "RECTANGULAR", "TRIANGULAR");
              goto error;
            }
        }
      else if (lex_match_id (s->lexer, "HOLD"))
        write->hold = true;
      else if (lex_match_id (s->lexer, "FORMAT"))
        {
          if (has_format || write->format)
            {
              lex_sbc_only_once (s->lexer, "FORMAT");
              goto error;
            }

          lex_match (s->lexer, T_EQUALS);

          if (lex_token (s->lexer) != T_STRING && !lex_force_id (s->lexer))
            goto error;

          format_ofs = lex_ofs (s->lexer);
          const char *p = lex_tokcstr (s->lexer);
          if (c_isdigit (p[0]))
            {
              repetitions = atoi (p);
              p += strspn (p, "0123456789");
              if (!fmt_from_name (p, &format))
                {
                  lex_error (s->lexer, _("Unknown format %s."), p);
                  goto error;
                }
              has_format = true;
              lex_get (s->lexer);
            }
          else if (fmt_from_name (p, &format))
            {
              has_format = true;
              lex_get (s->lexer);
            }
          else
            {
              struct fmt_spec spec;
              if (!parse_format_specifier (s->lexer, &spec))
                goto error;
              write->format = xmemdup (&spec, sizeof spec);
            }
        }
      else
        {
          lex_error_expecting (s->lexer, "OUTFILE", "FIELD", "MODE",
                               "HOLD", "FORMAT");
          goto error;
        }
    }

  if (!write->c1)
    {
      lex_sbc_missing (s->lexer, "FIELD");
      goto error;
    }

  if (!fh)
    {
      if (s->prev_write_file)
        fh = fh_ref (s->prev_write_file);
      else
        {
          lex_sbc_missing (s->lexer, "OUTFILE");
          goto error;
        }
    }
  fh_unref (s->prev_write_file);
  s->prev_write_file = fh_ref (fh);

  write->wf = write_file_create (s, fh);
  fh = NULL;
  if (encoding)
    {
      free (write->wf->encoding);
      write->wf->encoding = encoding;
      encoding = NULL;
    }

  /* Field width may be specified in multiple ways:

     1. BY on FIELD.
     2. The format on FORMAT.
     3. The repetition factor on FORMAT.

     (2) and (3) are mutually exclusive.

     If more than one of these is present, they must agree.  If none of them is
     present, then free-field format is used.
   */
  if (repetitions > record_width)
    {
      lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                   _("This syntax designates the number of repetitions."));
      lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                   _("This syntax designates the record width."));
      goto error;
    }
  int w = (repetitions ? record_width / repetitions
           : write->format ? write->format->w
           : by);
  if (by && w != by)
    {
      msg (SE, _("This command specifies two different field widths."));
      if (repetitions)
        {
          lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                       ngettext ("This syntax specifies %d repetition.",
                                 "This syntax specifies %d repetitions.",
                                 repetitions),
                       repetitions);
          lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                       _("This syntax designates record width %d, "
                         "which divided by %d repetitions implies "
                         "field width %d."),
                       record_width, repetitions, w);
        }
      else
        lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                     _("This syntax specifies field width %d."), w);

      lex_ofs_msg (s->lexer, SN, by_ofs, by_ofs,
                   _("This syntax specifies field width %d."), by);
      goto error;
    }
  if (w && !write->format)
    {
      write->format = xmalloc (sizeof *write->format);
      *write->format = (struct fmt_spec) { .type = format, .w = w };

      char *error = fmt_check_output__ (*write->format);
      if (error)
        {
          msg (SE, "%s", error);
          free (error);

          if (has_format)
            lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                         _("This syntax specifies format %s."),
                         fmt_name (format));

          if (repetitions)
            {
              lex_ofs_msg (s->lexer, SN, format_ofs, format_ofs,
                           ngettext ("This syntax specifies %d repetition.",
                                     "This syntax specifies %d repetitions.",
                                     repetitions),
                           repetitions);
              lex_ofs_msg (s->lexer, SN, record_width_start, record_width_end,
                           _("This syntax designates record width %d, "
                             "which divided by %d repetitions implies "
                             "field width %d."),
                           record_width, repetitions, w);
            }

          if (by)
            lex_ofs_msg (s->lexer, SN, by_ofs, by_ofs,
                         _("This syntax specifies field width %d."), by);

          goto error;
        }
    }

  if (write->format && fmt_var_width (*write->format) > sizeof (double))
    {
      char fs[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (*write->format, fs);
      lex_ofs_error (s->lexer, format_ofs, format_ofs,
                     _("Format %s is too wide for %zu-byte matrix elements."),
                     fs, sizeof (double));
      goto error;
    }

  return cmd;

error:
  fh_unref (fh);
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_write_execute (struct matrix_write *write)
{
  gsl_matrix *m = matrix_expr_evaluate (write->expression);
  if (!m)
    return;

  if (write->triangular && m->size1 != m->size2)
    {
      msg_at (SE, matrix_expr_location (write->expression),
              _("WRITE with MODE=TRIANGULAR requires a square matrix but "
                "the matrix to be written has dimensions %zu×%zu."),
              m->size1, m->size2);
      gsl_matrix_free (m);
      return;
    }

  struct dfm_writer *writer = write_file_open (write->wf);
  if (!writer || !m->size1)
    {
      gsl_matrix_free (m);
      return;
    }

  const struct fmt_settings *settings = settings_get_fmt_settings ();
  struct u8_line *line = write->wf->held;
  for (size_t y = 0; y < m->size1; y++)
    {
      if (!line)
        {
          line = xmalloc (sizeof *line);
          u8_line_init (line);
        }
      size_t nx = write->triangular ? y + 1 : m->size2;
      int x0 = write->c1;
      for (size_t x = 0; x < nx; x++)
        {
          char *s;
          double f = gsl_matrix_get (m, y, x);
          if (write->format)
            {
              union value v;
              if (fmt_is_numeric (write->format->type))
                v.f = f;
              else
                v.s = (uint8_t *) &f;
              s = data_out (&v, NULL, *write->format, settings);
            }
          else
            {
              s = xmalloc (DBL_BUFSIZE_BOUND);
              if (c_dtoastr (s, DBL_BUFSIZE_BOUND, FTOASTR_UPPER_E, 0, f)
                  >= DBL_BUFSIZE_BOUND)
                abort ();
            }
          size_t len = strlen (s);
          int width = u8_width (CHAR_CAST (const uint8_t *, s), len, UTF8);
          if (width + x0 > write->c2)
            {
              dfm_put_record_utf8 (writer, line->s.ss.string,
                                   line->s.ss.length);
              u8_line_clear (line);
              x0 = write->c1;
            }
          u8_line_put (line, x0, x0 + width, s, len);
          free (s);

          x0 += write->format ? write->format->w : width + 1;
        }

      if (y + 1 >= m->size1 && write->hold)
        break;
      dfm_put_record_utf8 (writer, line->s.ss.string, line->s.ss.length);
      u8_line_clear (line);
    }
  if (!write->hold)
    {
      u8_line_destroy (line);
      free (line);
      line = NULL;
    }
  write->wf->held = line;

  gsl_matrix_free (m);
}

/* GET. */

static struct matrix_command *
matrix_get_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_GET,
    .get = {
      .lexer = s->lexer,
      .dataset = s->dataset,
      .user = { .treatment = MGET_ERROR },
      .system = { .treatment = MGET_ERROR },
    }
  };

  struct matrix_get *get = &cmd->get;
  get->dst = matrix_lvalue_parse (s);
  if (!get->dst)
    goto error;

  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "FILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (get->file);
          if (lex_match (s->lexer, T_ASTERISK))
            get->file = NULL;
          else
            {
              get->file = fh_parse (s->lexer, FH_REF_FILE, s->session);
              if (!get->file)
                goto error;
            }
        }
      else if (lex_match_id (s->lexer, "ENCODING"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_string (s->lexer))
            goto error;

          free (get->encoding);
          get->encoding = ss_xstrdup (lex_tokss (s->lexer));

          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "VARIABLES"))
        {
          lex_match (s->lexer, T_EQUALS);

          if (get->n_vars)
            {
              lex_sbc_only_once (s->lexer, "VARIABLES");
              goto error;
            }

          if (!var_syntax_parse (s->lexer, &get->vars, &get->n_vars))
            goto error;
        }
      else if (lex_match_id (s->lexer, "NAMES"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_id (s->lexer))
            goto error;

          struct substring name = lex_tokss (s->lexer);
          get->names = matrix_var_lookup (s, name);
          if (!get->names)
            get->names = matrix_var_create (s, name);
          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "MISSING"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (lex_match_id (s->lexer, "ACCEPT"))
            get->user.treatment = MGET_ACCEPT;
          else if (lex_match_id (s->lexer, "OMIT"))
            get->user.treatment = MGET_OMIT;
          else if (lex_is_number (s->lexer))
            {
              get->user.treatment = MGET_RECODE;
              get->user.substitute = lex_number (s->lexer);
              lex_get (s->lexer);
            }
          else
            {
              lex_error (s->lexer, _("Syntax error expecting ACCEPT or OMIT or "
                                     "a number for MISSING."));
              goto error;
            }
        }
      else if (lex_match_id (s->lexer, "SYSMIS"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (lex_match_id (s->lexer, "OMIT"))
            get->system.treatment = MGET_OMIT;
          else if (lex_is_number (s->lexer))
            {
              get->system.treatment = MGET_RECODE;
              get->system.substitute = lex_number (s->lexer);
              lex_get (s->lexer);
            }
          else
            {
              lex_error (s->lexer, _("Syntax error expecting OMIT or a number "
                                     "for SYSMIS."));
              goto error;
            }
        }
      else
        {
          lex_error_expecting (s->lexer, "FILE", "VARIABLES", "NAMES",
                               "MISSING", "SYSMIS");
          goto error;
        }
    }

  if (get->user.treatment != MGET_ACCEPT)
    get->system.treatment = MGET_ERROR;

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_get_execute__ (struct matrix_command *cmd, struct casereader *reader,
                      const struct dictionary *dict)
{
  struct matrix_get *get = &cmd->get;
  struct variable **vars;
  size_t n_vars = 0;

  if (get->n_vars)
    {
      if (!var_syntax_evaluate (get->lexer, get->vars, get->n_vars, dict,
                                &vars, &n_vars, PV_NUMERIC))
        return;
    }
  else
    {
      n_vars = dict_get_n_vars (dict);
      vars = xnmalloc (n_vars, sizeof *vars);
      for (size_t i = 0; i < n_vars; i++)
        {
          struct variable *var = dict_get_var (dict, i);
          if (!var_is_numeric (var))
            {
              msg_at (SE, cmd->location, _("Variable %s is not numeric."),
                      var_get_name (var));
              free (vars);
              return;
            }
          vars[i] = var;
        }
    }

  if (get->names)
    {
      gsl_matrix *names = gsl_matrix_alloc (n_vars, 1);
      for (size_t i = 0; i < n_vars; i++)
        {
          char s[sizeof (double)];
          double f;
          buf_copy_str_rpad (s, sizeof s, var_get_name (vars[i]), ' ');
          memcpy (&f, s, sizeof f);
          gsl_matrix_set (names, i, 0, f);
        }

      gsl_matrix_free (get->names->value);
      get->names->value = names;
    }

  size_t n_rows = 0;
  gsl_matrix *m = gsl_matrix_alloc (4, n_vars);
  long long int casenum = 1;
  bool error = false;
  for (struct ccase *c = casereader_read (reader); c;
       c = casereader_read (reader), casenum++)
    {
      if (n_rows >= m->size1)
        {
          gsl_matrix *p = gsl_matrix_alloc (m->size1 * 2, n_vars);
          for (size_t y = 0; y < n_rows; y++)
            for (size_t x = 0; x < n_vars; x++)
              gsl_matrix_set (p, y, x, gsl_matrix_get (m, y, x));
          gsl_matrix_free (m);
          m = p;
        }

      bool keep = true;
      for (size_t x = 0; x < n_vars; x++)
        {
          const struct variable *var = vars[x];
          double d = case_num (c, var);
          if (d == SYSMIS)
            {
              if (get->system.treatment == MGET_RECODE)
                d = get->system.substitute;
              else if (get->system.treatment == MGET_OMIT)
                keep = false;
              else
                {
                  msg_at (SE, cmd->location, _("Variable %s in case %lld "
                                               "is system-missing."),
                          var_get_name (var), casenum);
                  error = true;
                }
            }
          else if (var_is_num_missing (var, d) == MV_USER)
            {
              if (get->user.treatment == MGET_RECODE)
                d = get->user.substitute;
              else if (get->user.treatment == MGET_OMIT)
                keep = false;
              else if (get->user.treatment != MGET_ACCEPT)
                {
                  msg_at (SE, cmd->location,
                          _("Variable %s in case %lld has user-missing "
                             "value %g."),
                          var_get_name (var), casenum, d);
                  error = true;
                }
            }
          gsl_matrix_set (m, n_rows, x, d);
        }
      case_unref (c);
      if (error)
        break;
      if (keep)
        n_rows++;
    }
  if (!error)
    {
      m->size1 = n_rows;
      matrix_lvalue_evaluate_and_assign (get->dst, m, cmd->location);
    }
  else
    gsl_matrix_free (m);
  free (vars);
}

static bool
matrix_open_casereader (const struct matrix_command *cmd,
                        const char *command_name, struct file_handle *file,
                        const char *encoding, struct dataset *dataset,
                        struct casereader **readerp, struct dictionary **dictp)
{
  if (file)
    {
       *readerp = any_reader_open_and_decode (file, encoding, dictp, NULL);
       return *readerp != NULL;
    }
  else
    {
      if (dict_get_n_vars (dataset_dict (dataset)) == 0)
        {
          msg_at (SE, cmd->location,
                  _("The %s command cannot read an empty active file."),
                  command_name);
          return false;
        }
      *readerp = proc_open (dataset);
      *dictp = dict_ref (dataset_dict (dataset));
      return true;
    }
}

static void
matrix_close_casereader (struct file_handle *file, struct dataset *dataset,
                         struct casereader *reader, struct dictionary *dict)
{
  dict_unref (dict);
  casereader_destroy (reader);
  if (!file)
    proc_commit (dataset);
}

static void
matrix_get_execute (struct matrix_command *cmd)
{
  struct matrix_get *get = &cmd->get;
  struct casereader *r;
  struct dictionary *d;
  if (matrix_open_casereader (cmd, "GET", get->file, get->encoding,
                              get->dataset, &r, &d))
    {
      matrix_get_execute__ (cmd, r, d);
      matrix_close_casereader (get->file, get->dataset, r, d);
    }
}

/* MSAVE. */

static bool
variables_changed (const char *keyword,
                   const struct string_array *new_vars,
                   const struct msg_location *new_vars_location,
                   const struct msg_location *new_location,
                   const struct string_array *old_vars,
                   const struct msg_location *old_vars_location,
                   const struct msg_location *old_location)
{
  if (new_vars->n)
    {
      if (!old_vars->n)
        {
          msg_at (SE, new_location,
                  _("%s may only be specified on MSAVE if it was specified "
                    "on the first MSAVE within MATRIX."), keyword);
          msg_at (SN, old_location,
                  _("The first MSAVE in MATRIX did not specify %s."),
                  keyword);
          msg_at (SN, new_vars_location,
                  _("This is the specification of %s on a later MSAVE."),
                  keyword);
          return true;
        }
      if (!string_array_equal_case (old_vars, new_vars))
        {
          msg_at (SE, new_location,
                  _("%s must specify the same variables on each MSAVE "
                    "within a given MATRIX."), keyword);
          msg_at (SE, old_vars_location,
                  _("This is the specification of %s on the first MSAVE."),
                  keyword);
          msg_at (SE, new_vars_location,
                  _("This is a different specification of %s on a later MSAVE."),
                  keyword);
          return true;
        }
    }
  return false;
}

static bool
msave_common_changed (const struct msave_common *old,
                      const struct msave_common *new)
{
  if (new->outfile && !fh_equal (old->outfile, new->outfile))
    {
      msg (SE, _("OUTFILE must name the same file on each MSAVE "
                 "within a single MATRIX command."));
      msg_at (SN, old->outfile_location,
              _("This is the OUTFILE on the first MSAVE command."));
      msg_at (SN, new->outfile_location,
              _("This is the OUTFILE on a later MSAVE command."));
      return false;
    }

  if (!variables_changed ("VARIABLES",
                          &new->variables, new->variables_location, new->location,
                          &old->variables, old->variables_location, old->location)
      && !variables_changed ("FNAMES",
                             &new->fnames, new->fnames_location, new->location,
                             &old->fnames, old->fnames_location, old->location)
      && !variables_changed ("SNAMES",
                             &new->snames, new->snames_location, new->location,
                             &old->snames, old->snames_location, old->location))
    return false;

  return true;
}

static void
msave_common_destroy (struct msave_common *common)
{
  if (common)
    {
      msg_location_destroy (common->location);
      fh_unref (common->outfile);
      msg_location_destroy (common->outfile_location);
      string_array_destroy (&common->variables);
      msg_location_destroy (common->variables_location);
      string_array_destroy (&common->fnames);
      msg_location_destroy (common->fnames_location);
      string_array_destroy (&common->snames);
      msg_location_destroy (common->snames_location);

      for (size_t i = 0; i < common->n_factors; i++)
        matrix_expr_destroy (common->factors[i]);
      free (common->factors);

      for (size_t i = 0; i < common->n_splits; i++)
        matrix_expr_destroy (common->splits[i]);
      free (common->splits);

      dict_unref (common->dict);
      casewriter_destroy (common->writer);

      free (common);
    }
}

static const char *
match_rowtype (struct lexer *lexer)
{
  static const char *rowtypes[] = {
    "COV", "CORR", "MEAN", "STDDEV", "N", "COUNT"
  };
  size_t n_rowtypes = sizeof rowtypes / sizeof *rowtypes;

  for (size_t i = 0; i < n_rowtypes; i++)
    if (lex_match_id (lexer, rowtypes[i]))
      return rowtypes[i];

  lex_error_expecting_array (lexer, rowtypes, n_rowtypes);
  return NULL;
}

static bool
parse_var_names (struct lexer *lexer, struct string_array *sa,
                 struct msg_location **locationp)
{
  lex_match (lexer, T_EQUALS);

  string_array_clear (sa);
  msg_location_destroy (*locationp);
  *locationp = NULL;

  struct dictionary *dict = dict_create (get_default_encoding ());
  char **names;
  size_t n_names;
  int start_ofs = lex_ofs (lexer);
  bool ok = parse_DATA_LIST_vars (lexer, dict, &names, &n_names,
                                  PV_NO_DUPLICATE | PV_NO_SCRATCH);
  int end_ofs = lex_ofs (lexer) - 1;
  dict_unref (dict);

  if (ok)
    {
      for (size_t i = 0; i < n_names; i++)
        if (ss_equals_case (ss_cstr (names[i]), ss_cstr ("ROWTYPE_"))
            || ss_equals_case (ss_cstr (names[i]), ss_cstr ("VARNAME_")))
          {
            lex_ofs_error (lexer, start_ofs, end_ofs,
                           _("Variable name %s is reserved."), names[i]);
            for (size_t j = 0; j < n_names; j++)
              free (names[i]);
            free (names);
            return false;
          }

      sa->strings = names;
      sa->n = sa->allocated = n_names;
      *locationp = lex_ofs_location (lexer, start_ofs, end_ofs);
    }
  return ok;
}

static struct matrix_command *
matrix_msave_parse (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);

  struct msave_common *common = xmalloc (sizeof *common);
  *common = (struct msave_common) { .outfile = NULL };

  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) { .type = MCMD_MSAVE, .msave = { .expr = NULL } };

  struct matrix_expr *splits = NULL;
  struct matrix_expr *factors = NULL;

  struct matrix_msave *msave = &cmd->msave;
  msave->expr = matrix_parse_exp (s);
  if (!msave->expr)
    goto error;

  while (lex_match (s->lexer, T_SLASH))
    {
      if (lex_match_id (s->lexer, "TYPE"))
        {
          lex_match (s->lexer, T_EQUALS);

          msave->rowtype = match_rowtype (s->lexer);
          if (!msave->rowtype)
            goto error;
        }
      else if (lex_match_id (s->lexer, "OUTFILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (common->outfile);
          int start_ofs = lex_ofs (s->lexer);
          common->outfile = fh_parse (s->lexer, FH_REF_FILE, NULL);
          if (!common->outfile)
            goto error;
          msg_location_destroy (common->outfile_location);
          common->outfile_location = lex_ofs_location (s->lexer, start_ofs,
                                                       lex_ofs (s->lexer) - 1);
        }
      else if (lex_match_id (s->lexer, "VARIABLES"))
        {
          if (!parse_var_names (s->lexer, &common->variables,
                                &common->variables_location))
            goto error;
        }
      else if (lex_match_id (s->lexer, "FNAMES"))
        {
          if (!parse_var_names (s->lexer, &common->fnames,
                                &common->fnames_location))
            goto error;
        }
      else if (lex_match_id (s->lexer, "SNAMES"))
        {
          if (!parse_var_names (s->lexer, &common->snames,
                                &common->snames_location))
            goto error;
        }
      else if (lex_match_id (s->lexer, "SPLIT"))
        {
          lex_match (s->lexer, T_EQUALS);

          matrix_expr_destroy (splits);
          splits = matrix_parse_exp (s);
          if (!splits)
            goto error;
        }
      else if (lex_match_id (s->lexer, "FACTOR"))
        {
          lex_match (s->lexer, T_EQUALS);

          matrix_expr_destroy (factors);
          factors = matrix_parse_exp (s);
          if (!factors)
            goto error;
        }
      else
        {
          lex_error_expecting (s->lexer, "TYPE", "OUTFILE", "VARIABLES",
                               "FNAMES", "SNAMES", "SPLIT", "FACTOR");
          goto error;
        }
    }
  if (!msave->rowtype)
    {
      lex_sbc_missing (s->lexer, "TYPE");
      goto error;
    }

  if (!s->msave_common)
    {
      if (common->fnames.n && !factors)
        {
          msg_at (SE, common->fnames_location, _("FNAMES requires FACTOR."));
          goto error;
        }
      if (common->snames.n && !splits)
        {
          msg_at (SE, common->snames_location, _("SNAMES requires SPLIT."));
          goto error;
        }
      if (!common->outfile)
        {
          lex_sbc_missing (s->lexer, "OUTFILE");
          goto error;
        }
      common->location = lex_ofs_location (s->lexer, start_ofs,
                                           lex_ofs (s->lexer));
      msg_location_remove_columns (common->location);
      s->msave_common = common;
    }
  else
    {
      if (msave_common_changed (s->msave_common, common))
        goto error;
      msave_common_destroy (common);
    }
  msave->common = s->msave_common;

  struct msave_common *c = s->msave_common;
  if (factors)
    {
      if (c->n_factors >= c->allocated_factors)
        c->factors = x2nrealloc (c->factors, &c->allocated_factors,
                                 sizeof *c->factors);
      c->factors[c->n_factors++] = factors;
    }
  if (c->n_factors > 0)
    msave->factors = c->factors[c->n_factors - 1];

  if (splits)
    {
      if (c->n_splits >= c->allocated_splits)
        c->splits = x2nrealloc (c->splits, &c->allocated_splits,
                                sizeof *c->splits);
      c->splits[c->n_splits++] = splits;
    }
  if (c->n_splits > 0)
    msave->splits = c->splits[c->n_splits - 1];

  return cmd;

error:
  matrix_expr_destroy (splits);
  matrix_expr_destroy (factors);
  msave_common_destroy (common);
  matrix_command_destroy (cmd);
  return NULL;
}

static gsl_vector *
matrix_expr_evaluate_vector (const struct matrix_expr *e, const char *name)
{
  gsl_matrix *m = matrix_expr_evaluate (e);
  if (!m)
    return NULL;

  if (!is_vector (m))
    {
      msg_at (SE, matrix_expr_location (e),
              _("%s expression must evaluate to vector, "
                "not a %zu×%zu matrix."),
              name, m->size1, m->size2);
      gsl_matrix_free (m);
      return NULL;
    }

  return matrix_to_vector (m);
}

static const char *
msave_add_vars (struct dictionary *d, const struct string_array *vars)
{
  for (size_t i = 0; i < vars->n; i++)
    if (!dict_create_var (d, vars->strings[i], 0))
      return vars->strings[i];
  return NULL;
}

static struct dictionary *
msave_create_dict (const struct msave_common *common)
{
  struct dictionary *dict = dict_create (get_default_encoding ());

  const char *dup_split = msave_add_vars (dict, &common->snames);
  if (dup_split)
    {
      /* Should not be possible because the parser ensures that the names are
         unique. */
      NOT_REACHED ();
    }

  dict_create_var_assert (dict, "ROWTYPE_", 8);

  const char *dup_factor = msave_add_vars (dict, &common->fnames);
  if (dup_factor)
    {
      msg_at (SE, common->fnames_location,
              _("Duplicate or invalid FACTOR variable name %s."),
              dup_factor);
      goto error;
    }

  dict_create_var_assert (dict, "VARNAME_", 8);

  const char *dup_var = msave_add_vars (dict, &common->variables);
  if (dup_var)
    {
      msg_at (SE, common->variables_location,
              _("Duplicate or invalid variable name %s."),
              dup_var);
      goto error;
    }

  return dict;

error:
  dict_unref (dict);
  return NULL;
}

static void
matrix_msave_execute (struct matrix_command *cmd)
{
  struct matrix_msave *msave = &cmd->msave;
  struct msave_common *common = msave->common;
  gsl_matrix *m = NULL;
  gsl_vector *factors = NULL;
  gsl_vector *splits = NULL;

  m = matrix_expr_evaluate (msave->expr);
  if (!m)
    goto error;

  if (!common->variables.n)
    for (size_t i = 0; i < m->size2; i++)
      string_array_append_nocopy (&common->variables,
                                  xasprintf ("COL%zu", i + 1));
  else if (m->size2 != common->variables.n)
    {
      msg_at (SE, matrix_expr_location (msave->expr),
              _("Matrix on MSAVE has %zu columns but there are %zu variables."),
              m->size2, common->variables.n);
      goto error;
    }

  if (msave->factors)
    {
      factors = matrix_expr_evaluate_vector (msave->factors, "FACTOR");
      if (!factors)
        goto error;

      if (!common->fnames.n)
        for (size_t i = 0; i < factors->size; i++)
          string_array_append_nocopy (&common->fnames,
                                      xasprintf ("FAC%zu", i + 1));
      else if (factors->size != common->fnames.n)
        {
          msg_at (SE, matrix_expr_location (msave->factors),
                  _("There are %zu factor variables, "
                    "but %zu factor values were supplied."),
                  common->fnames.n, factors->size);
          goto error;
        }
    }

  if (msave->splits)
    {
      splits = matrix_expr_evaluate_vector (msave->splits, "SPLIT");
      if (!splits)
        goto error;

      if (!common->snames.n)
        for (size_t i = 0; i < splits->size; i++)
          string_array_append_nocopy (&common->snames,
                                      xasprintf ("SPL%zu", i + 1));
      else if (splits->size != common->snames.n)
        {
          msg_at (SE, matrix_expr_location (msave->splits),
                  _("There are %zu split variables, "
                    "but %zu split values were supplied."),
                  common->snames.n, splits->size);
          goto error;
        }
    }

  if (!common->writer)
    {
      struct dictionary *dict = msave_create_dict (common);
      if (!dict)
        goto error;

      common->writer = any_writer_open (common->outfile, dict);
      if (!common->writer)
        {
          dict_unref (dict);
          goto error;
        }

      common->dict = dict;
    }

  bool matrix = (!strcmp (msave->rowtype, "COV")
                 || !strcmp (msave->rowtype, "CORR"));
  for (size_t y = 0; y < m->size1; y++)
    {
      struct ccase *c = case_create (dict_get_proto (common->dict));
      size_t idx = 0;

      /* Split variables */
      if (splits)
        for (size_t i = 0; i < splits->size; i++)
          case_data_rw_idx (c, idx++)->f = gsl_vector_get (splits, i);

      /* ROWTYPE_. */
      buf_copy_str_rpad (CHAR_CAST (char *, case_data_rw_idx (c, idx++)->s), 8,
                         msave->rowtype, ' ');

      /* Factors. */
      if (factors)
        for (size_t i = 0; i < factors->size; i++)
          *case_num_rw_idx (c, idx++) = gsl_vector_get (factors, i);

      /* VARNAME_. */
      const char *varname_ = (matrix && y < common->variables.n
                              ? common->variables.strings[y]
                              : "");
      buf_copy_str_rpad (CHAR_CAST (char *, case_data_rw_idx (c, idx++)->s), 8,
                         varname_, ' ');

      /* Continuous variables. */
      for (size_t x = 0; x < m->size2; x++)
        case_data_rw_idx (c, idx++)->f = gsl_matrix_get (m, y, x);
      casewriter_write (common->writer, c);
    }

error:
  gsl_matrix_free (m);
  gsl_vector_free (factors);
  gsl_vector_free (splits);
}

/* MGET. */

static struct matrix_command *
matrix_mget_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_MGET,
    .mget = {
      .state = s,
      .rowtypes = STRINGI_SET_INITIALIZER (cmd->mget.rowtypes),
    },
  };

  struct matrix_mget *mget = &cmd->mget;

  lex_match (s->lexer, T_SLASH);
  while (lex_token (s->lexer) != T_ENDCMD)
    {
      if (lex_match_id (s->lexer, "FILE"))
        {
          lex_match (s->lexer, T_EQUALS);

          fh_unref (mget->file);
          mget->file = fh_parse (s->lexer, FH_REF_FILE, s->session);
          if (!mget->file)
            goto error;
        }
      else if (lex_match_id (s->lexer, "ENCODING"))
        {
          lex_match (s->lexer, T_EQUALS);
          if (!lex_force_string (s->lexer))
            goto error;

          free (mget->encoding);
          mget->encoding = ss_xstrdup (lex_tokss (s->lexer));

          lex_get (s->lexer);
        }
      else if (lex_match_id (s->lexer, "TYPE"))
        {
          lex_match (s->lexer, T_EQUALS);
          while (lex_token (s->lexer) != T_SLASH
                 && lex_token (s->lexer) != T_ENDCMD)
            {
              const char *rowtype = match_rowtype (s->lexer);
              if (!rowtype)
                goto error;

              stringi_set_insert (&mget->rowtypes, rowtype);
            }
        }
      else
        {
          lex_error_expecting (s->lexer, "FILE", "TYPE");
          goto error;
        }
      lex_match (s->lexer, T_SLASH);
    }
  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static const struct variable *
get_a8_var (const struct msg_location *loc,
            const struct dictionary *d, const char *name)
{
  const struct variable *v = dict_lookup_var (d, name);
  if (!v)
    {
      msg_at (SE, loc, _("Matrix data file lacks %s variable."), name);
      return NULL;
    }
  if (var_get_width (v) != 8)
    {
      msg_at (SE, loc, _("%s variable in matrix data file must be "
                         "8-byte string, but it has width %d."),
              name, var_get_width (v));
      return NULL;
    }
  return v;
}

static bool
var_changed (const struct ccase *ca, const struct ccase *cb,
             const struct variable *var)
{
  return (ca && cb
          ? !value_equal (case_data (ca, var), case_data (cb, var),
                          var_get_width (var))
          : ca || cb);
}

static bool
vars_changed (const struct ccase *ca, const struct ccase *cb,
              const struct dictionary *d,
              size_t first_var, size_t n_vars)
{
  for (size_t i = 0; i < n_vars; i++)
    {
      const struct variable *v = dict_get_var (d, first_var + i);
      if (var_changed (ca, cb, v))
        return true;
    }
  return false;
}

static bool
vars_all_missing (const struct ccase *c, const struct dictionary *d,
                  size_t first_var, size_t n_vars)
{
  for (size_t i = 0; i < n_vars; i++)
    if (case_num (c, dict_get_var (d, first_var + i)) != SYSMIS)
      return false;
  return true;
}

static void
matrix_mget_commit_var (struct ccase **rows, size_t n_rows,
                        const struct dictionary *d,
                        const struct variable *rowtype_var,
                        const struct stringi_set *accepted_rowtypes,
                        struct matrix_state *s,
                        size_t ss, size_t sn, size_t si,
                        size_t fs, size_t fn, size_t fi,
                        size_t cs, size_t cn,
                        struct pivot_table *pt,
                        struct pivot_dimension *var_dimension)
{
  if (!n_rows)
    goto exit;

  /* Is this a matrix for pooled data, either where there are no factor
     variables or the factor variables are missing? */
  bool pooled = !fn || vars_all_missing (rows[0], d, fs, fn);

  struct substring rowtype = case_ss (rows[0], rowtype_var);
  ss_rtrim (&rowtype, ss_cstr (" "));
  if (!stringi_set_is_empty (accepted_rowtypes)
      && !stringi_set_contains_len (accepted_rowtypes,
                                    rowtype.string, rowtype.length))
    goto exit;

  const char *prefix = (ss_equals_case (rowtype, ss_cstr ("COV")) ? "CV"
                        : ss_equals_case (rowtype, ss_cstr ("CORR")) ? "CR"
                        : ss_equals_case (rowtype, ss_cstr ("MEAN")) ? "MN"
                        : ss_equals_case (rowtype, ss_cstr ("STDDEV")) ? "SD"
                        : ss_equals_case (rowtype, ss_cstr ("N")) ? "NC"
                        : ss_equals_case (rowtype, ss_cstr ("COUNT")) ? "CN"
                        : NULL);
  if (!prefix)
    {
      msg (SE, _("Matrix data file contains unknown ROWTYPE_ \"%.*s\"."),
           (int) rowtype.length, rowtype.string);
      goto exit;
    }

  struct string name = DS_EMPTY_INITIALIZER;
  ds_put_cstr (&name, prefix);
  if (!pooled)
    ds_put_format (&name, "F%zu", fi);
  if (si > 0)
    ds_put_format (&name, "S%zu", si);

  struct matrix_var *mv = matrix_var_lookup (s, ds_ss (&name));
  if (!mv)
    mv = matrix_var_create (s, ds_ss (&name));
  else if (mv->value)
    {
      msg (SW, _("Matrix data file contains variable with existing name %s."),
           ds_cstr (&name));
      goto exit_free_name;
    }

  gsl_matrix *m = gsl_matrix_alloc (n_rows, cn);
  size_t n_missing = 0;
  for (size_t y = 0; y < n_rows; y++)
    {
      for (size_t x = 0; x < cn; x++)
        {
          struct variable *var = dict_get_var (d, cs + x);
          double value = case_num (rows[y], var);
          if (var_is_num_missing (var, value))
            {
              n_missing++;
              value = 0.0;
            }
          gsl_matrix_set (m, y, x, value);
        }
    }

  int var_index = pivot_category_create_leaf (
    var_dimension->root, pivot_value_new_user_text (ds_cstr (&name), SIZE_MAX));
  double values[] = { n_rows, cn };
  for (size_t j = 0; j < sn; j++)
    {
      struct variable *var = dict_get_var (d, ss + j);
      const union value *value = case_data (rows[0], var);
      pivot_table_put2 (pt, j, var_index,
                        pivot_value_new_var_value (var, value));
    }
  for (size_t j = 0; j < fn; j++)
    {
      struct variable *var = dict_get_var (d, fs + j);
      const union value sysmis = { .f = SYSMIS };
      const union value *value = pooled ? &sysmis : case_data (rows[0], var);
      pivot_table_put2 (pt, j + sn, var_index,
                        pivot_value_new_var_value (var, value));
    }
  for (size_t j = 0; j < sizeof values / sizeof *values; j++)
    pivot_table_put2 (pt, j + sn + fn, var_index,
                      pivot_value_new_integer (values[j]));

  if (n_missing)
    msg (SE, ngettext ("Matrix data file variable %s contains a missing "
                       "value, which was treated as zero.",
                       "Matrix data file variable %s contains %zu missing "
                       "values, which were treated as zero.", n_missing),
         ds_cstr (&name), n_missing);
  mv->value = m;

exit_free_name:
  ds_destroy (&name);

exit:
  for (size_t y = 0; y < n_rows; y++)
    case_unref (rows[y]);
}

static void
matrix_mget_execute__ (struct matrix_command *cmd, struct casereader *r,
                       const struct dictionary *d)
{
  struct matrix_mget *mget = &cmd->mget;
  const struct msg_location *loc = cmd->location;
  const struct variable *rowtype_ = get_a8_var (loc, d, "ROWTYPE_");
  const struct variable *varname_ = get_a8_var (loc, d, "VARNAME_");
  if (!rowtype_ || !varname_)
    return;

  if (var_get_dict_index (rowtype_) >= var_get_dict_index (varname_))
    {
      msg_at (SE, loc,
              _("ROWTYPE_ must precede VARNAME_ in matrix data file."));
      return;
    }
  if (var_get_dict_index (varname_) + 1 >= dict_get_n_vars (d))
    {
      msg_at (SE, loc, _("Matrix data file contains no continuous variables."));
      return;
    }

  for (size_t i = 0; i < dict_get_n_vars (d); i++)
    {
      const struct variable *v = dict_get_var (d, i);
      if (v != rowtype_ && v != varname_ && var_get_width (v) != 0)
        {
          msg_at (SE, loc,
                  _("Matrix data file contains unexpected string variable %s."),
                  var_get_name (v));
          return;
        }
    }

  /* SPLIT variables. */
  size_t ss = 0;
  size_t sn = var_get_dict_index (rowtype_);
  struct ccase *sc = NULL;
  size_t si = 0;

  /* FACTOR variables. */
  size_t fs = var_get_dict_index (rowtype_) + 1;
  size_t fn = var_get_dict_index (varname_) - var_get_dict_index (rowtype_) - 1;
  struct ccase *fc = NULL;
  size_t fi = 0;

  /* Continuous variables. */
  size_t cs = var_get_dict_index (varname_) + 1;
  size_t cn = dict_get_n_vars (d) - cs;
  struct ccase *cc = NULL;

  /* Pivot table. */
  struct pivot_table *pt = pivot_table_create (
    N_("Matrix Variables Created by MGET"));
  struct pivot_dimension *attr_dimension = pivot_dimension_create (
    pt, PIVOT_AXIS_COLUMN, N_("Attribute"));
  struct pivot_dimension *var_dimension = pivot_dimension_create (
    pt, PIVOT_AXIS_ROW, N_("Variable"));
  if (sn > 0)
    {
      struct pivot_category *splits = pivot_category_create_group (
        attr_dimension->root, N_("Split Values"));
      for (size_t i = 0; i < sn; i++)
        pivot_category_create_leaf (splits, pivot_value_new_variable (
                                      dict_get_var (d, ss + i)));
    }
  if (fn > 0)
    {
      struct pivot_category *factors = pivot_category_create_group (
        attr_dimension->root, N_("Factors"));
      for (size_t i = 0; i < fn; i++)
        pivot_category_create_leaf (factors, pivot_value_new_variable (
                                      dict_get_var (d, fs + i)));
    }
  pivot_category_create_group (attr_dimension->root, N_("Dimensions"),
                                N_("Rows"), N_("Columns"));

  /* Matrix. */
  struct ccase **rows = NULL;
  size_t allocated_rows = 0;
  size_t n_rows = 0;

  struct ccase *c;
  while ((c = casereader_read (r)) != NULL)
    {
      bool row_has_factors = fn && !vars_all_missing (c, d, fs, fn);

      enum
        {
          SPLITS_CHANGED,
          FACTORS_CHANGED,
          ROWTYPE_CHANGED,
          NOTHING_CHANGED
        }
      change
        = (sn && (!sc || vars_changed (sc, c, d, ss, sn)) ? SPLITS_CHANGED
           : fn && (!fc || vars_changed (fc, c, d, fs, fn)) ? FACTORS_CHANGED
           : !cc || var_changed (cc, c, rowtype_) ? ROWTYPE_CHANGED
           : NOTHING_CHANGED);

      if (change != NOTHING_CHANGED)
        {
          matrix_mget_commit_var (rows, n_rows, d,
                                  rowtype_, &mget->rowtypes,
                                  mget->state,
                                  ss, sn, si,
                                  fs, fn, fi,
                                  cs, cn,
                                  pt, var_dimension);
          n_rows = 0;
          case_unref (cc);
          cc = case_ref (c);
        }

      if (n_rows >= allocated_rows)
        rows = x2nrealloc (rows, &allocated_rows, sizeof *rows);
      rows[n_rows++] = c;

      if (change == SPLITS_CHANGED)
        {
          si++;
          case_unref (sc);
          sc = case_ref (c);

          /* Reset the factor number, if there are factors. */
          if (fn)
            {
              fi = 0;
              if (row_has_factors)
                fi++;
              case_unref (fc);
              fc = case_ref (c);
            }
        }
      else if (change == FACTORS_CHANGED)
        {
          if (row_has_factors)
            fi++;
          case_unref (fc);
          fc = case_ref (c);
        }
    }
  matrix_mget_commit_var (rows, n_rows, d,
                          rowtype_, &mget->rowtypes,
                          mget->state,
                          ss, sn, si,
                          fs, fn, fi,
                          cs, cn,
                          pt, var_dimension);
  free (rows);

  case_unref (sc);
  case_unref (fc);
  case_unref (cc);

  if (var_dimension->n_leaves)
    pivot_table_submit (pt);
  else
    pivot_table_unref (pt);
}

static void
matrix_mget_execute (struct matrix_command *cmd)
{
  struct matrix_mget *mget = &cmd->mget;
  struct casereader *r;
  struct dictionary *d;
  if (matrix_open_casereader (cmd, "MGET", mget->file, mget->encoding,
                              mget->state->dataset, &r, &d))
    {
      matrix_mget_execute__ (cmd, r, d);
      matrix_close_casereader (mget->file, mget->state->dataset, r, d);
    }
}

/* CALL EIGEN. */

static bool
matrix_parse_dst_var (struct matrix_state *s, struct matrix_var **varp)
{
  if (!lex_force_id (s->lexer))
    return false;

  *varp = matrix_var_lookup (s, lex_tokss (s->lexer));
  if (!*varp)
    *varp = matrix_var_create (s, lex_tokss (s->lexer));
  lex_get (s->lexer);
  return true;
}

static struct matrix_command *
matrix_eigen_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_EIGEN,
    .eigen = { .expr = NULL }
  };

  struct matrix_eigen *eigen = &cmd->eigen;
  if (!lex_force_match (s->lexer, T_LPAREN))
    goto error;
  eigen->expr = matrix_expr_parse (s);
  if (!eigen->expr
      || !lex_force_match (s->lexer, T_COMMA)
      || !matrix_parse_dst_var (s, &eigen->evec)
      || !lex_force_match (s->lexer, T_COMMA)
      || !matrix_parse_dst_var (s, &eigen->eval)
      || !lex_force_match (s->lexer, T_RPAREN))
    goto error;

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_eigen_execute (struct matrix_command *cmd)
{
  struct matrix_eigen *eigen = &cmd->eigen;
  gsl_matrix *A = matrix_expr_evaluate (eigen->expr);
  if (!A)
    return;
  if (!is_symmetric (A))
    {
      msg_at (SE, cmd->location, _("Argument of EIGEN must be symmetric."));
      gsl_matrix_free (A);
      return;
    }

  gsl_eigen_symmv_workspace *w = gsl_eigen_symmv_alloc (A->size1);
  gsl_matrix *eval = gsl_matrix_alloc (A->size1, 1);
  gsl_vector v_eval = to_vector (eval);
  gsl_matrix *evec = gsl_matrix_alloc (A->size1, A->size2);
  gsl_eigen_symmv (A, &v_eval, evec, w);
  gsl_eigen_symmv_free (w);

  gsl_eigen_symmv_sort (&v_eval, evec, GSL_EIGEN_SORT_VAL_DESC);

  gsl_matrix_free (A);

  gsl_matrix_free (eigen->eval->value);
  eigen->eval->value = eval;

  gsl_matrix_free (eigen->evec->value);
  eigen->evec->value = evec;
}

/* CALL SETDIAG. */

static struct matrix_command *
matrix_setdiag_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_SETDIAG,
    .setdiag = { .dst = NULL }
  };

  struct matrix_setdiag *setdiag = &cmd->setdiag;
  if (!lex_force_match (s->lexer, T_LPAREN) || !lex_force_id (s->lexer))
    goto error;

  setdiag->dst = matrix_var_lookup (s, lex_tokss (s->lexer));
  if (!setdiag->dst)
    {
      lex_error (s->lexer, _("Unknown variable %s."), lex_tokcstr (s->lexer));
      goto error;
    }
  lex_get (s->lexer);

  if (!lex_force_match (s->lexer, T_COMMA))
    goto error;

  setdiag->expr = matrix_expr_parse (s);
  if (!setdiag->expr)
    goto error;

  if (!lex_force_match (s->lexer, T_RPAREN))
    goto error;

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_setdiag_execute (struct matrix_command *cmd)
{
  struct matrix_setdiag *setdiag = &cmd->setdiag;
  gsl_matrix *dst = setdiag->dst->value;
  if (!dst)
    {
      msg_at (SE, cmd->location,
              _("SETDIAG destination matrix %s is uninitialized."),
              setdiag->dst->name);
      return;
    }

  gsl_matrix *src = matrix_expr_evaluate (setdiag->expr);
  if (!src)
    return;

  size_t n = MIN (dst->size1, dst->size2);
  if (is_scalar (src))
    {
      double d = to_scalar (src);
      for (size_t i = 0; i < n; i++)
        gsl_matrix_set (dst, i, i, d);
    }
  else if (is_vector (src))
    {
      gsl_vector v = to_vector (src);
      for (size_t i = 0; i < n && i < v.size; i++)
        gsl_matrix_set (dst, i, i, gsl_vector_get (&v, i));
    }
  else
    msg_at (SE, matrix_expr_location (setdiag->expr),
            _("SETDIAG argument 2 must be a scalar or a vector, "
              "not a %zu×%zu matrix."),
            src->size1, src->size2);
  gsl_matrix_free (src);
}

/* CALL SVD. */

static struct matrix_command *
matrix_svd_parse (struct matrix_state *s)
{
  struct matrix_command *cmd = xmalloc (sizeof *cmd);
  *cmd = (struct matrix_command) {
    .type = MCMD_SVD,
    .svd = { .expr = NULL }
  };

  struct matrix_svd *svd = &cmd->svd;
  if (!lex_force_match (s->lexer, T_LPAREN))
    goto error;
  svd->expr = matrix_expr_parse (s);
  if (!svd->expr
      || !lex_force_match (s->lexer, T_COMMA)
      || !matrix_parse_dst_var (s, &svd->u)
      || !lex_force_match (s->lexer, T_COMMA)
      || !matrix_parse_dst_var (s, &svd->s)
      || !lex_force_match (s->lexer, T_COMMA)
      || !matrix_parse_dst_var (s, &svd->v)
      || !lex_force_match (s->lexer, T_RPAREN))
    goto error;

  return cmd;

error:
  matrix_command_destroy (cmd);
  return NULL;
}

static void
matrix_svd_execute (struct matrix_svd *svd)
{
  gsl_matrix *m = matrix_expr_evaluate (svd->expr);
  if (!m)
    return;

  if (m->size1 >= m->size2)
    {
      gsl_matrix *A = m;
      gsl_matrix *V = gsl_matrix_alloc (A->size2, A->size2);
      gsl_matrix *S = gsl_matrix_calloc (A->size2, A->size2);
      gsl_vector Sv = gsl_matrix_diagonal (S).vector;
      gsl_vector *work = gsl_vector_alloc (A->size2);
      gsl_linalg_SV_decomp (A, V, &Sv, work);
      gsl_vector_free (work);

      matrix_var_set (svd->u, A);
      matrix_var_set (svd->s, S);
      matrix_var_set (svd->v, V);
    }
  else
    {
      gsl_matrix *At = gsl_matrix_alloc (m->size2, m->size1);
      gsl_matrix_transpose_memcpy (At, m);
      gsl_matrix_free (m);

      gsl_matrix *Vt = gsl_matrix_alloc (At->size2, At->size2);
      gsl_matrix *St = gsl_matrix_calloc (At->size2, At->size2);
      gsl_vector Stv = gsl_matrix_diagonal (St).vector;
      gsl_vector *work = gsl_vector_alloc (At->size2);
      gsl_linalg_SV_decomp (At, Vt, &Stv, work);
      gsl_vector_free (work);

      matrix_var_set (svd->v, At);
      matrix_var_set (svd->s, St);
      matrix_var_set (svd->u, Vt);
    }
}

/* The main MATRIX command logic. */

static bool
matrix_command_execute (struct matrix_command *cmd)
{
  switch (cmd->type)
    {
    case MCMD_COMPUTE:
      matrix_compute_execute (cmd);
      break;

    case MCMD_PRINT:
      matrix_print_execute (&cmd->print);
      break;

    case MCMD_DO_IF:
      return matrix_do_if_execute (&cmd->do_if);

    case MCMD_LOOP:
      matrix_loop_execute (&cmd->loop);
      break;

    case MCMD_BREAK:
      return false;

    case MCMD_DISPLAY:
      matrix_display_execute (&cmd->display);
      break;

    case MCMD_RELEASE:
      matrix_release_execute (&cmd->release);
      break;

    case MCMD_SAVE:
      matrix_save_execute (cmd);
      break;

    case MCMD_READ:
      matrix_read_execute (cmd);
      break;

    case MCMD_WRITE:
      matrix_write_execute (&cmd->write);
      break;

    case MCMD_GET:
      matrix_get_execute (cmd);
      break;

    case MCMD_MSAVE:
      matrix_msave_execute (cmd);
      break;

    case MCMD_MGET:
      matrix_mget_execute (cmd);
      break;

    case MCMD_EIGEN:
      matrix_eigen_execute (cmd);
      break;

    case MCMD_SETDIAG:
      matrix_setdiag_execute (cmd);
      break;

    case MCMD_SVD:
      matrix_svd_execute (&cmd->svd);
      break;
    }

  return true;
}

static void
matrix_command_destroy (struct matrix_command *cmd)
{
  if (!cmd)
    return;

  msg_location_destroy (cmd->location);

  switch (cmd->type)
    {
    case MCMD_COMPUTE:
      matrix_lvalue_destroy (cmd->compute.lvalue);
      matrix_expr_destroy (cmd->compute.rvalue);
      break;

    case MCMD_PRINT:
      matrix_expr_destroy (cmd->print.expression);
      free (cmd->print.title);
      print_labels_destroy (cmd->print.rlabels);
      print_labels_destroy (cmd->print.clabels);
      break;

    case MCMD_DO_IF:
      for (size_t i = 0; i < cmd->do_if.n_clauses; i++)
        {
          matrix_expr_destroy (cmd->do_if.clauses[i].condition);
          matrix_commands_uninit (&cmd->do_if.clauses[i].commands);
        }
      free (cmd->do_if.clauses);
      break;

    case MCMD_LOOP:
      matrix_expr_destroy (cmd->loop.start);
      matrix_expr_destroy (cmd->loop.end);
      matrix_expr_destroy (cmd->loop.increment);
      matrix_expr_destroy (cmd->loop.top_condition);
      matrix_expr_destroy (cmd->loop.bottom_condition);
      matrix_commands_uninit (&cmd->loop.commands);
      break;

    case MCMD_BREAK:
      break;

    case MCMD_DISPLAY:
      break;

    case MCMD_RELEASE:
      free (cmd->release.vars);
      break;

    case MCMD_SAVE:
      matrix_expr_destroy (cmd->save.expression);
      break;

    case MCMD_READ:
      matrix_lvalue_destroy (cmd->read.dst);
      matrix_expr_destroy (cmd->read.size);
      break;

    case MCMD_WRITE:
      matrix_expr_destroy (cmd->write.expression);
      free (cmd->write.format);
      break;

    case MCMD_GET:
      matrix_lvalue_destroy (cmd->get.dst);
      fh_unref (cmd->get.file);
      free (cmd->get.encoding);
      var_syntax_destroy (cmd->get.vars, cmd->get.n_vars);
      break;

    case MCMD_MSAVE:
      matrix_expr_destroy (cmd->msave.expr);
      break;

    case MCMD_MGET:
      fh_unref (cmd->mget.file);
      stringi_set_destroy (&cmd->mget.rowtypes);
      break;

    case MCMD_EIGEN:
      matrix_expr_destroy (cmd->eigen.expr);
      break;

    case MCMD_SETDIAG:
      matrix_expr_destroy (cmd->setdiag.expr);
      break;

    case MCMD_SVD:
      matrix_expr_destroy (cmd->svd.expr);
      break;
    }
  free (cmd);
}

static bool
matrix_commands_parse (struct matrix_state *s, struct matrix_commands *c,
                       const char *command_name,
                       const char *stop1, const char *stop2)
{
  lex_end_of_command (s->lexer);
  lex_discard_rest_of_command (s->lexer);

  size_t allocated = 0;
  for (;;)
    {
      while (lex_token (s->lexer) == T_ENDCMD)
        lex_get (s->lexer);

      if (lex_at_phrase (s->lexer, stop1)
          || (stop2 && lex_at_phrase (s->lexer, stop2)))
        return true;

      if (lex_at_phrase (s->lexer, "END MATRIX"))
        {
          lex_next_error (s->lexer, 0, 1,
                          _("Premature END MATRIX within %s."), command_name);
          return false;
        }

      struct matrix_command *cmd = matrix_command_parse (s);
      if (!cmd)
        return false;

      if (c->n >= allocated)
        c->commands = x2nrealloc (c->commands, &allocated, sizeof *c->commands);
      c->commands[c->n++] = cmd;
    }
}

static void
matrix_commands_uninit (struct matrix_commands *cmds)
{
  for (size_t i = 0; i < cmds->n; i++)
    matrix_command_destroy (cmds->commands[i]);
  free (cmds->commands);
}

struct matrix_command_name
  {
    const char *name;
    struct matrix_command *(*parse) (struct matrix_state *);
  };

static const struct matrix_command_name *
matrix_command_name_parse (struct lexer *lexer)
{
  static const struct matrix_command_name commands[] = {
    { "COMPUTE", matrix_compute_parse },
    { "CALL EIGEN", matrix_eigen_parse },
    { "CALL SETDIAG", matrix_setdiag_parse },
    { "CALL SVD", matrix_svd_parse },
    { "PRINT", matrix_print_parse },
    { "DO IF", matrix_do_if_parse },
    { "LOOP", matrix_loop_parse },
    { "BREAK", matrix_break_parse },
    { "READ", matrix_read_parse },
    { "WRITE", matrix_write_parse },
    { "GET", matrix_get_parse },
    { "SAVE", matrix_save_parse },
    { "MGET", matrix_mget_parse },
    { "MSAVE", matrix_msave_parse },
    { "DISPLAY", matrix_display_parse },
    { "RELEASE", matrix_release_parse },
  };
  static size_t n = sizeof commands / sizeof *commands;

  for (const struct matrix_command_name *c = commands; c < &commands[n]; c++)
    if (lex_match_phrase (lexer, c->name))
      return c;
  return NULL;
}

static struct matrix_command *
matrix_command_parse (struct matrix_state *s)
{
  int start_ofs = lex_ofs (s->lexer);
  size_t nesting_level = SIZE_MAX;

  struct matrix_command *c = NULL;
  const struct matrix_command_name *cmd = matrix_command_name_parse (s->lexer);
  if (!cmd)
    lex_error (s->lexer, _("Unknown matrix command."));
  else if (!cmd->parse)
    lex_error (s->lexer, _("Matrix command %s is not yet implemented."),
               cmd->name);
  else
    {
      nesting_level = output_open_group (
        group_item_create_nocopy (utf8_to_title (cmd->name),
                                  utf8_to_title (cmd->name)));
      c = cmd->parse (s);
    }

  if (c)
    {
      c->location = lex_ofs_location (s->lexer, start_ofs, lex_ofs (s->lexer));
      msg_location_remove_columns (c->location);
      lex_end_of_command (s->lexer);
    }
  lex_discard_rest_of_command (s->lexer);
  if (nesting_level != SIZE_MAX)
    output_close_groups (nesting_level);

  return c;
}

int
cmd_matrix (struct lexer *lexer, struct dataset *ds)
{
  if (!lex_force_match (lexer, T_ENDCMD))
    return CMD_FAILURE;

  struct matrix_state state = {
    .dataset = ds,
    .session = dataset_session (ds),
    .lexer = lexer,
    .vars = HMAP_INITIALIZER (state.vars),
  };

  for (;;)
    {
      while (lex_match (lexer, T_ENDCMD))
        continue;
      if (lex_token (lexer) == T_STOP)
        {
          msg (SE, _("Unexpected end of input expecting matrix command."));
          break;
        }

      if (lex_match_phrase (lexer, "END MATRIX"))
        break;

      struct matrix_command *c = matrix_command_parse (&state);
      if (c)
        {
          matrix_command_execute (c);
          matrix_command_destroy (c);
        }
    }

  struct matrix_var *var, *next;
  HMAP_FOR_EACH_SAFE (var, next, struct matrix_var, hmap_node, &state.vars)
    {
      free (var->name);
      gsl_matrix_free (var->value);
      hmap_delete (&state.vars, &var->hmap_node);
      free (var);
    }
  hmap_destroy (&state.vars);
  msave_common_destroy (state.msave_common);
  fh_unref (state.prev_read_file);
  for (size_t i = 0; i < state.n_read_files; i++)
    read_file_destroy (state.read_files[i]);
  free (state.read_files);
  fh_unref (state.prev_write_file);
  for (size_t i = 0; i < state.n_write_files; i++)
    write_file_destroy (state.write_files[i]);
  free (state.write_files);
  fh_unref (state.prev_save_file);
  for (size_t i = 0; i < state.n_save_files; i++)
    save_file_destroy (state.save_files[i]);
  free (state.save_files);

  return CMD_SUCCESS;
}
