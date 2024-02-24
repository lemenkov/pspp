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

#include <math.h>
#include <errno.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/mrset.h"
#include "data/subcase.h"
#include "data/value-labels.h"
#include "language/command.h"
#include "language/commands/split-file.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/token.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/string-array.h"
#include "math/mode.h"
#include "math/moments.h"
#include "math/percentiles.h"
#include "math/sort.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct ctables;

/* The three forms of weighting supported by CTABLES. */
enum ctables_weighting
  {
    CTW_EFFECTIVE,             /* Effective base weight (WEIGHT subcommand). */
    CTW_DICTIONARY,            /* Dictionary weight. */
    CTW_UNWEIGHTED             /* No weight. */
#define N_CTWS 3
  };

/* CTABLES table areas. */

enum ctables_area_type
  {
    /* Within a section, where stacked variables divide one section from
       another.

       Keep CTAT_LAYER after CTAT_LAYERROW and CTAT_LAYERCOL so that
       parse_ctables_summary_function() parses correctly. */
    CTAT_TABLE,                  /* All layers of a whole section. */
    CTAT_LAYERROW,               /* Row in one layer within a section. */
    CTAT_LAYERCOL,               /* Column in one layer within a section. */
    CTAT_LAYER,                  /* One layer within a section. */

    /* Within a subtable, where a subtable pairs an innermost row variable with
       an innermost column variable within a single layer.  */
    CTAT_SUBTABLE,               /* Whole subtable. */
    CTAT_ROW,                    /* Row within a subtable. */
    CTAT_COL,                    /* Column within a subtable. */
#define N_CTATS 7
  };

static const char *ctables_area_type_name[N_CTATS] = {
  [CTAT_TABLE] = "TABLE",
  [CTAT_LAYER] = "LAYER",
  [CTAT_LAYERROW] = "LAYERROW",
  [CTAT_LAYERCOL] = "LAYERCOL",
  [CTAT_SUBTABLE] = "SUBTABLE",
  [CTAT_ROW] = "ROW",
  [CTAT_COL] = "COL",
};

/* Summary statistics for an area. */
struct ctables_area
  {
    struct hmap_node node;
    const struct ctables_cell *example;

    /* Sequence number used for CTSF_ID. */
    size_t sequence;

    /* Weights for CTSF_areaPCT_COUNT, CTSF_areaPCT_VALIDN, and
       CTSF_areaPCT_TOTALN. */
    double count[N_CTWS];
    double valid[N_CTWS];
    double total[N_CTWS];

    /* Sums for CTSF_areaPCT_SUM. */
    struct ctables_sum *sums;
  };

struct ctables_sum
  {
    double sum[N_CTWS];
  };

/* CTABLES summary functions. */

enum ctables_function_type
  {
    /* A function that operates on data in a single cell.  It operates on
       effective weights.  It does not have an unweighted version. */
    CTFT_CELL,

    /* A function that operates on data in a single cell.  The function
       operates on effective weights and has a U-prefixed unweighted
       version. */
    CTFT_UCELL,

    /* A function that operates on data in a single cell.  It operates on
       dictionary weights, and has U-prefixed unweighted version and an
       E-prefixed effective weight version. */
    CTFT_UECELL,

    /* A function that operates on an area of cells.  It operates on effective
       weights and has a U-prefixed unweighted version. */
    CTFT_AREA,
  };

enum ctables_format
  {
    CTF_COUNT,                  /* F40.0. */
    CTF_PERCENT,                /* PCT40.1. */
    CTF_GENERAL                 /* Variable's print format. */
  };

enum ctables_function_availability
  {
    CTFA_ALL,                /* Any variables. */
    CTFA_SCALE,              /* Only scale variables, totals, and subtotals. */
    //CTFA_MRSETS,             /* Only multiple-response sets */
  };

enum ctables_summary_function
  {
#define S(ENUM, NAME, TYPE, FORMAT, AVAILABILITY) ENUM,
#include "ctables.inc"
#undef S
  };

enum {
#define S(ENUM, NAME, TYPE, FORMAT, AVAILABILITY) +1
  N_CTSF_FUNCTIONS =
#include "ctables.inc"
#undef S
};

struct ctables_function_info
  {
    struct substring basename;
    enum ctables_function_type type;
    enum ctables_format format;
    enum ctables_function_availability availability;

    bool u_prefix;              /* Accepts a 'U' prefix (for unweighted)? */
    bool e_prefix;              /* Accepts an 'E' prefix (for effective)? */
    bool is_area;               /* Needs an area prefix. */
  };
static const struct ctables_function_info ctables_function_info[N_CTSF_FUNCTIONS] = {
#define S(ENUM, NAME, TYPE, FORMAT, AVAILABILITY)                       \
  [ENUM] = {                                                            \
    .basename = SS_LITERAL_INITIALIZER (NAME),                          \
    .type = TYPE,                                                       \
    .format = FORMAT,                                                   \
    .availability = AVAILABILITY,                                       \
    .u_prefix = (TYPE) == CTFT_UCELL || (TYPE) == CTFT_UECELL || (TYPE) == CTFT_AREA, \
    .e_prefix = (TYPE) == CTFT_UECELL,                                  \
    .is_area = (TYPE) == CTFT_AREA                                      \
  },
#include "ctables.inc"
#undef S
};

static struct fmt_spec
ctables_summary_default_format (enum ctables_summary_function function,
                                const struct variable *var)
{
  static const enum ctables_format default_formats[] = {
#define S(ENUM, NAME, TYPE, FORMAT, AVAILABILITY) [ENUM] = FORMAT,
#include "ctables.inc"
#undef S
  };
  switch (default_formats[function])
    {
    case CTF_COUNT:
      return (struct fmt_spec) { .type = FMT_F, .w = 40 };

    case CTF_PERCENT:
      return (struct fmt_spec) { .type = FMT_PCT, .w = 40, .d = 1 };

    case CTF_GENERAL:
      return var_get_print_format (var);

    default:
      NOT_REACHED ();
    }
}

static enum ctables_function_availability
ctables_function_availability (enum ctables_summary_function f)
{
  static enum ctables_function_availability availability[] = {
#define S(ENUM, NAME, TYPE, FORMAT, AVAILABILITY) [ENUM] = AVAILABILITY,
#include "ctables.inc"
#undef S
  };

  return availability[f];
}

static bool
parse_ctables_summary_function (struct lexer *lexer,
                                enum ctables_summary_function *function,
                                enum ctables_weighting *weighting,
                                enum ctables_area_type *area)
{
  if (!lex_force_id (lexer))
    return false;

  struct substring name = lex_tokss (lexer);
  if (ss_ends_with_case (name, ss_cstr (".LCL"))
      || ss_ends_with_case (name, ss_cstr (".UCL"))
      || ss_ends_with_case (name, ss_cstr (".SE")))
    {
      lex_error (lexer, _("Support for LCL, UCL, and SE summary functions "
                          "is not yet implemented."));
      return false;
    }

  bool u = ss_match_byte (&name, 'U') || ss_match_byte (&name, 'u');
  bool e = !u && (ss_match_byte (&name, 'E') || ss_match_byte (&name, 'e'));

  bool has_area = false;
  *area = 0;
  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    if (ss_match_string_case (&name, ss_cstr (ctables_area_type_name[at])))
      {
        has_area = true;
        *area = at;

        if (ss_equals_case (name, ss_cstr ("PCT")))
          {
            /* Special case where .COUNT suffix is omitted. */
            *function = CTSF_areaPCT_COUNT;
            *weighting = CTW_EFFECTIVE;
            lex_get (lexer);
            return true;
          }
        break;
      }

  for (int f = 0; f < N_CTSF_FUNCTIONS; f++)
    {
      const struct ctables_function_info *cfi = &ctables_function_info[f];
      if (ss_equals_case (cfi->basename, name))
        {
          *function = f;
          if ((u && !cfi->u_prefix) || (e && !cfi->e_prefix) || (has_area != cfi->is_area))
            break;

          *weighting = (e ? CTW_EFFECTIVE
                        : u ? CTW_UNWEIGHTED
                        : cfi->e_prefix ? CTW_DICTIONARY
                        : CTW_EFFECTIVE);
          lex_get (lexer);
          return true;
        }
    }

  lex_error (lexer, _("Syntax error expecting summary function name."));
  return false;
}

static const char *
ctables_summary_function_name (enum ctables_summary_function function,
                               enum ctables_weighting weighting,
                               enum ctables_area_type area,
                               char *buffer, size_t bufsize)
{
  const struct ctables_function_info *cfi = &ctables_function_info[function];
  snprintf (buffer, bufsize, "%s%s%s",
            (weighting == CTW_UNWEIGHTED ? "U"
             : weighting == CTW_DICTIONARY ? ""
             : cfi->e_prefix ? "E"
             : ""),
            cfi->is_area ? ctables_area_type_name[area] : "",
            cfi->basename.string);
  return buffer;
}

static const char *
ctables_summary_function_label__ (enum ctables_summary_function function,
                                  enum ctables_weighting weighting,
                                  enum ctables_area_type area)
{
  bool w = weighting != CTW_UNWEIGHTED;
  bool d = weighting == CTW_DICTIONARY;
  enum ctables_area_type a = area;
  switch (function)
    {
    case CTSF_COUNT:
      return d ? N_("Count") : w ? N_("Adjusted Count") : N_("Unweighted Count");

    case CTSF_areaPCT_COUNT:
      switch (a)
        {
        case CTAT_TABLE: return w ? N_("Table %") : N_("Unweighted Table %");
        case CTAT_LAYER: return w ? N_("Layer %") : N_("Unweighted Layer %");
        case CTAT_LAYERROW: return w ? N_("Layer Row %") : N_("Unweighted Layer Row %");
        case CTAT_LAYERCOL: return w ? N_("Layer Column %") : N_("Unweighted Layer Column %");
        case CTAT_SUBTABLE: return w ? N_("Subtable %") : N_("Unweighted Subtable %");
        case CTAT_ROW: return w ? N_("Row %") : N_("Unweighted Row %");
        case CTAT_COL: return w ? N_("Column %") : N_("Unweighted Column %");
        }
      NOT_REACHED ();

    case CTSF_areaPCT_VALIDN:
      switch (a)
        {
        case CTAT_TABLE: return w ? N_("Table Valid N %") : N_("Unweighted Table Valid N %");
        case CTAT_LAYER: return w ? N_("Layer Valid N %") : N_("Unweighted Layer Valid N %");
        case CTAT_LAYERROW: return w ? N_("Layer Row Valid N %") : N_("Unweighted Layer Row Valid N %");
        case CTAT_LAYERCOL: return w ? N_("Layer Column Valid N %") : N_("Unweighted Layer Column Valid N %");
        case CTAT_SUBTABLE: return w ? N_("Subtable Valid N %") : N_("Unweighted Subtable Valid N %");
        case CTAT_ROW: return w ? N_("Row Valid N %") : N_("Unweighted Row Valid N %");
        case CTAT_COL: return w ? N_("Column Valid N %") : N_("Unweighted Column Valid N %");
        }
      NOT_REACHED ();

    case CTSF_areaPCT_TOTALN:
      switch (a)
        {
        case CTAT_TABLE: return w ? N_("Table Total N %") : N_("Unweighted Table Total N %");
        case CTAT_LAYER: return w ? N_("Layer Total N %") : N_("Unweighted Layer Total N %");
        case CTAT_LAYERROW: return w ? N_("Layer Row Total N %") : N_("Unweighted Layer Row Total N %");
        case CTAT_LAYERCOL: return w ? N_("Layer Column Total N %") : N_("Unweighted Layer Column Total N %");
        case CTAT_SUBTABLE: return w ? N_("Subtable Total N %") : N_("Unweighted Subtable Total N %");
        case CTAT_ROW: return w ? N_("Row Total N %") : N_("Unweighted Row Total N %");
        case CTAT_COL: return w ? N_("Column Total N %") : N_("Unweighted Column Total N %");
        }
      NOT_REACHED ();

    case CTSF_MAXIMUM: return N_("Maximum");
    case CTSF_MEAN: return w ? N_("Mean") : N_("Unweighted Mean");
    case CTSF_MEDIAN: return w ? N_("Median") : N_("Unweighted Median");
    case CTSF_MINIMUM: return N_("Minimum");
    case CTSF_MISSING: return w ? N_("Missing") : N_("Unweighted Missing");
    case CTSF_MODE: return w ? N_("Mode") : N_("Unweighted Mode");
    case CTSF_PTILE: NOT_REACHED ();
    case CTSF_RANGE: return N_("Range");
    case CTSF_SEMEAN: return w ? N_("Std Error of Mean") : N_("Unweighted Std Error of Mean");
    case CTSF_STDDEV: return w ? N_("Std Deviation") : N_("Unweighted Std Deviation");
    case CTSF_SUM: return w ? N_("Sum") : N_("Unweighted Sum");
    case CTSF_TOTALN: return (d ? N_("Total N")
                              : w ? N_("Adjusted Total N")
                              : N_("Unweighted Total N"));
    case CTSF_VALIDN: return (d ? N_("Valid N")
                              : w ? N_("Adjusted Valid N")
                              : N_("Unweighted Valid N"));
    case CTSF_VARIANCE: return w ? N_("Variance") : N_("Unweighted Variance");
    case CTSF_areaPCT_SUM:
      switch (a)
        {
        case CTAT_TABLE: return w ? N_("Table Sum %") : N_("Unweighted Table Sum %");
        case CTAT_LAYER: return w ? N_("Layer Sum %") : N_("Unweighted Layer Sum %");
        case CTAT_LAYERROW: return w ? N_("Layer Row Sum %") : N_("Unweighted Layer Row Sum %");
        case CTAT_LAYERCOL: return w ? N_("Layer Column Sum %") : N_("Unweighted Layer Column Sum %");
        case CTAT_SUBTABLE: return w ? N_("Subtable Sum %") : N_("Unweighted Subtable Sum %");
        case CTAT_ROW: return w ? N_("Row Sum %") : N_("Unweighted Row Sum %");
        case CTAT_COL: return w ? N_("Column Sum %") : N_("Unweighted Column Sum %");
        }
      NOT_REACHED ();

    case CTSF_areaID:
      switch (a)
        {
        /* Don't bother translating these: they are for developers only. */
        case CTAT_TABLE: return "Table ID";
        case CTAT_LAYER: return "Layer ID";
        case CTAT_LAYERROW: return "Layer Row ID";
        case CTAT_LAYERCOL: return "Layer Column ID";
        case CTAT_SUBTABLE: return "Subtable ID";
        case CTAT_ROW: return "Row ID";
        case CTAT_COL: return "Column ID";
        }
      NOT_REACHED ();
    }

  NOT_REACHED ();
}

static struct pivot_value *
ctables_summary_function_label (enum ctables_summary_function function,
                                enum ctables_weighting weighting,
                                enum ctables_area_type area,
                                double percentile)
{
  if (function == CTSF_PTILE)
    {
      char *s = (weighting != CTW_UNWEIGHTED
                 ? xasprintf (_("Percentile %.2f"), percentile)
                 : xasprintf (_("Unweighted Percentile %.2f"), percentile));
      return pivot_value_new_user_text_nocopy (s);
    }
  else
    return pivot_value_new_text (ctables_summary_function_label__ (
                                   function, weighting, area));
}

/* CTABLES summaries. */

struct ctables_summary_spec
  {
    /* The calculation to be performed.

       'function' is the function to calculate.  'weighted' specifies whether
       to use weighted or unweighted data (for functions that do not support a
       choice, it must be true).  'calc_area' is the area over which the
       calculation takes place (for functions that target only an individual
       cell, it must be 0).  For CTSF_PTILE only, 'percentile' is the
       percentile between 0 and 100 (for other functions it must be 0). */
    enum ctables_summary_function function;
    enum ctables_weighting weighting;
    enum ctables_area_type calc_area;
    double percentile;          /* CTSF_PTILE only. */

    /* How to display the result of the calculation.

       'label' is a user-specified label, NULL if the user didn't specify
       one.

       'user_area' is usually the same as 'calc_area', but when category labels
       are rotated from one axis to another it swaps rows and columns.

       'format' is the format for displaying the output.  If
       'is_ctables_format' is true, then 'format.type' is one of the special
       CTEF_* formats instead of the standard ones. */
    char *label;
    enum ctables_area_type user_area;
    struct fmt_spec format;
    bool is_ctables_format;       /* Is 'format' one of CTEF_*? */

    size_t axis_idx;            /* Leaf index if summary dimension in use. */
    size_t sum_var_idx;         /* Offset into 'sums' in ctables_area. */
  };

static void
ctables_summary_spec_clone (struct ctables_summary_spec *dst,
                            const struct ctables_summary_spec *src)
{
  *dst = *src;
  dst->label = xstrdup_if_nonnull (src->label);
}

static void
ctables_summary_spec_uninit (struct ctables_summary_spec *s)
{
  if (s)
    free (s->label);
}

/* Collections of summary functions. */

struct ctables_summary_spec_set
  {
    struct ctables_summary_spec *specs;
    size_t n;
    size_t allocated;

    /* The variable to which the summary specs are applied. */
    struct variable *var;

    /* Whether the variable to which the summary specs are applied is a scale
       variable for the purpose of summarization.

       (VALIDN and TOTALN act differently for summarizing scale and categorical
       variables.) */
    bool is_scale;

    /* If any of these optional additional scale variables are missing, then
       treat 'var' as if it's missing too.  This is for implementing
       SMISSING=LISTWISE. */
    struct variable **listwise_vars;
    size_t n_listwise_vars;
  };

static void
ctables_summary_spec_set_clone (struct ctables_summary_spec_set *dst,
                                const struct ctables_summary_spec_set *src)
{
  struct ctables_summary_spec *specs
    = (src->n ? xnmalloc (src->n, sizeof *specs) : NULL);
  for (size_t i = 0; i < src->n; i++)
    ctables_summary_spec_clone (&specs[i], &src->specs[i]);

  *dst = (struct ctables_summary_spec_set) {
    .specs = specs,
    .n = src->n,
    .allocated = src->n,
    .var = src->var,
    .is_scale = src->is_scale,
  };
}

static void
ctables_summary_spec_set_uninit (struct ctables_summary_spec_set *set)
{
  for (size_t i = 0; i < set->n; i++)
    ctables_summary_spec_uninit (&set->specs[i]);
  free (set->listwise_vars);
  free (set->specs);
}

static bool
is_listwise_missing (const struct ctables_summary_spec_set *specs,
                     const struct ccase *c)
{
  for (size_t i = 0; i < specs->n_listwise_vars; i++)
    {
      const struct variable *var = specs->listwise_vars[i];
      if (var_is_num_missing (var, case_num (c, var)))
        return true;
    }

  return false;
}

/* CTABLES postcompute expressions. */

struct ctables_postcompute
  {
    struct hmap_node hmap_node; /* In struct ctables's 'pcompute' hmap. */
    char *name;                 /* Name, without leading &. */

    struct msg_location *location; /* Location of definition. */
    struct ctables_pcexpr *expr;
    char *label;
    struct ctables_summary_spec_set *specs;
    bool hide_source_cats;
  };

struct ctables_pcexpr
  {
    /* Precedence table:

       ()
       **
       -
       * /
       - +
    */
    enum ctables_pcexpr_op
      {
        /* Terminals. */
        CTPO_CONSTANT,          /* 5 */
        CTPO_CAT_NUMBER,        /* [5] */
        CTPO_CAT_STRING,        /* ["STRING"] */
        CTPO_CAT_NRANGE,        /* [LO THRU 5] */
        CTPO_CAT_SRANGE,        /* ["A" THRU "B"] */
        CTPO_CAT_MISSING,       /* MISSING */
        CTPO_CAT_OTHERNM,       /* OTHERNM */
        CTPO_CAT_SUBTOTAL,      /* SUBTOTAL */
        CTPO_CAT_TOTAL,         /* TOTAL */

        /* Nonterminals. */
        CTPO_ADD,
        CTPO_SUB,
        CTPO_MUL,
        CTPO_DIV,
        CTPO_POW,
        CTPO_NEG,
      }
    op;

    union
      {
        /* CTPO_CAT_NUMBER. */
        double number;

        /* CTPO_CAT_STRING, in dictionary encoding. */
        struct substring string;

        /* CTPO_CAT_NRANGE. */
        double nrange[2];

        /* CTPO_CAT_SRANGE. */
        struct substring srange[2];

        /* CTPO_CAT_SUBTOTAL. */
        size_t subtotal_index;

        /* Two elements: CTPO_ADD, CTPO_SUB, CTPO_MUL, CTPO_DIV, CTPO_POW.
           One element: CTPO_NEG. */
        struct ctables_pcexpr *subs[2];
      };

    /* Source location. */
    struct msg_location *location;
  };

static struct ctables_postcompute *ctables_find_postcompute (struct ctables *,
                                                             const char *name);

static struct ctables_pcexpr *ctables_pcexpr_allocate_binary (
  enum ctables_pcexpr_op, struct ctables_pcexpr *sub0,
  struct ctables_pcexpr *sub1);

typedef struct ctables_pcexpr *parse_recursively_func (struct lexer *,
                                                       struct dictionary *);

static void
ctables_pcexpr_destroy (struct ctables_pcexpr *e)
{
  if (e)
    {
      switch (e->op)
        {
        case CTPO_CAT_STRING:
          ss_dealloc (&e->string);
          break;

        case CTPO_CAT_SRANGE:
          for (size_t i = 0; i < 2; i++)
            ss_dealloc (&e->srange[i]);
          break;

        case CTPO_ADD:
        case CTPO_SUB:
        case CTPO_MUL:
        case CTPO_DIV:
        case CTPO_POW:
        case CTPO_NEG:
          for (size_t i = 0; i < 2; i++)
            ctables_pcexpr_destroy (e->subs[i]);
          break;

        case CTPO_CONSTANT:
        case CTPO_CAT_NUMBER:
        case CTPO_CAT_NRANGE:
        case CTPO_CAT_MISSING:
        case CTPO_CAT_OTHERNM:
        case CTPO_CAT_SUBTOTAL:
        case CTPO_CAT_TOTAL:
          break;
        }

      msg_location_destroy (e->location);
      free (e);
    }
}

static struct ctables_pcexpr *
ctables_pcexpr_allocate_binary (enum ctables_pcexpr_op op,
                                struct ctables_pcexpr *sub0,
                                struct ctables_pcexpr *sub1)
{
  struct ctables_pcexpr *e = xmalloc (sizeof *e);
  *e = (struct ctables_pcexpr) {
    .op = op,
    .subs = { sub0, sub1 },
    .location = msg_location_merged (sub0->location, sub1->location),
  };
  return e;
}

/* How to parse an operator. */
struct operator
  {
    enum token_type token;
    enum ctables_pcexpr_op op;
  };

static const struct operator *
ctables_pcexpr_match_operator (struct lexer *lexer,
                              const struct operator ops[], size_t n_ops)
{
  for (const struct operator *op = ops; op < ops + n_ops; op++)
    if (lex_token (lexer) == op->token)
      {
        if (op->token != T_NEG_NUM)
          lex_get (lexer);

        return op;
      }

  return NULL;
}

static struct ctables_pcexpr *
ctables_pcexpr_parse_binary_operators__ (
  struct lexer *lexer, struct dictionary *dict,
  const struct operator ops[], size_t n_ops,
  parse_recursively_func *parse_next_level,
  const char *chain_warning, struct ctables_pcexpr *lhs)
{
  for (int op_count = 0; ; op_count++)
    {
      const struct operator *op
        = ctables_pcexpr_match_operator (lexer, ops, n_ops);
      if (!op)
        {
          if (op_count > 1 && chain_warning)
            msg_at (SW, lhs->location, "%s", chain_warning);

          return lhs;
        }

      struct ctables_pcexpr *rhs = parse_next_level (lexer, dict);
      if (!rhs)
        {
          ctables_pcexpr_destroy (lhs);
          return NULL;
        }

      lhs = ctables_pcexpr_allocate_binary (op->op, lhs, rhs);
    }
}

static struct ctables_pcexpr *
ctables_pcexpr_parse_binary_operators (
  struct lexer *lexer, struct dictionary *dict,
  const struct operator ops[], size_t n_ops,
  parse_recursively_func *parse_next_level, const char *chain_warning)
{
  struct ctables_pcexpr *lhs = parse_next_level (lexer, dict);
  if (!lhs)
    return NULL;

  return ctables_pcexpr_parse_binary_operators__ (lexer, dict, ops, n_ops,
                                                 parse_next_level,
                                                 chain_warning, lhs);
}

static struct ctables_pcexpr *ctables_pcexpr_parse_add (struct lexer *,
                                                        struct dictionary *);

static struct ctables_pcexpr
ctpo_cat_nrange (double low, double high)
{
  return (struct ctables_pcexpr) {
    .op = CTPO_CAT_NRANGE,
    .nrange = { low, high },
  };
}

static struct ctables_pcexpr
ctpo_cat_srange (struct substring low, struct substring high)
{
  return (struct ctables_pcexpr) {
    .op = CTPO_CAT_SRANGE,
    .srange = { low, high },
  };
}

static struct substring
parse_substring (struct lexer *lexer, struct dictionary *dict)
{
  struct substring s = recode_substring_pool (
    dict_get_encoding (dict), "UTF-8", lex_tokss (lexer), NULL);
  ss_rtrim (&s, ss_cstr (" "));
  lex_get (lexer);
  return s;
}

static struct ctables_pcexpr *
ctables_pcexpr_parse_primary (struct lexer *lexer, struct dictionary *dict)
{
  int start_ofs = lex_ofs (lexer);
  struct ctables_pcexpr e;
  if (lex_is_number (lexer))
    {
      e = (struct ctables_pcexpr) { .op = CTPO_CONSTANT,
                                    .number = lex_number (lexer) };
      lex_get (lexer);
    }
  else if (lex_match_id (lexer, "MISSING"))
    e = (struct ctables_pcexpr) { .op = CTPO_CAT_MISSING };
  else if (lex_match_id (lexer, "OTHERNM"))
    e = (struct ctables_pcexpr) { .op = CTPO_CAT_OTHERNM };
  else if (lex_match_id (lexer, "TOTAL"))
    e = (struct ctables_pcexpr) { .op = CTPO_CAT_TOTAL };
  else if (lex_match_id (lexer, "SUBTOTAL"))
    {
      size_t subtotal_index = 0;
      if (lex_match (lexer, T_LBRACK))
        {
          if (!lex_force_int_range (lexer, "SUBTOTAL", 1, LONG_MAX))
            return NULL;
          subtotal_index = lex_integer (lexer);
          lex_get (lexer);
          if (!lex_force_match (lexer, T_RBRACK))
            return NULL;
        }
      e = (struct ctables_pcexpr) { .op = CTPO_CAT_SUBTOTAL,
                                    .subtotal_index = subtotal_index };
    }
  else if (lex_match (lexer, T_LBRACK))
    {
      if (lex_match_id (lexer, "LO"))
        {
          if (!lex_force_match_id (lexer, "THRU"))
            return false;

          if (lex_is_string (lexer))
            {
              struct substring low = { .string = NULL };
              struct substring high = parse_substring (lexer, dict);
              e = ctpo_cat_srange (low, high);
            }
          else
            {
              if (!lex_force_num (lexer))
                return false;
              e = ctpo_cat_nrange (-DBL_MAX, lex_number (lexer));
              lex_get (lexer);
            }
        }
      else if (lex_is_number (lexer))
        {
          double number = lex_number (lexer);
          lex_get (lexer);
          if (lex_match_id (lexer, "THRU"))
            {
              if (lex_match_id (lexer, "HI"))
                e = ctpo_cat_nrange (number, DBL_MAX);
              else
                {
                  if (!lex_force_num (lexer))
                    return false;
                  e = ctpo_cat_nrange (number, lex_number (lexer));
                  lex_get (lexer);
                }
            }
          else
            e = (struct ctables_pcexpr) { .op = CTPO_CAT_NUMBER,
                                          .number = number };
        }
      else if (lex_is_string (lexer))
        {
          struct substring s = parse_substring (lexer, dict);

          if (lex_match_id (lexer, "THRU"))
            {
              struct substring high;

              if (lex_match_id (lexer, "HI"))
                high = (struct substring) { .string = NULL };
              else
                {
                  if (!lex_force_string (lexer))
                    {
                      ss_dealloc (&s);
                      return false;
                    }
                  high = parse_substring (lexer, dict);
                }

              e = ctpo_cat_srange (s, high);
            }
          else
            e = (struct ctables_pcexpr) { .op = CTPO_CAT_STRING, .string = s };
        }
      else
        {
          lex_error (lexer,
                     _("Syntax error expecting number or string or range."));
          return NULL;
        }

      if (!lex_force_match (lexer, T_RBRACK))
        {
          if (e.op == CTPO_CAT_STRING)
            ss_dealloc (&e.string);
          else if (e.op == CTPO_CAT_SRANGE)
            {
              ss_dealloc (&e.srange[0]);
              ss_dealloc (&e.srange[1]);
            }
          return NULL;
        }
    }
  else if (lex_match (lexer, T_LPAREN))
    {
      struct ctables_pcexpr *ep = ctables_pcexpr_parse_add (lexer, dict);
      if (!ep)
        return NULL;
      if (!lex_force_match (lexer, T_RPAREN))
        {
          ctables_pcexpr_destroy (ep);
          return NULL;
        }
      return ep;
    }
  else
    {
      lex_error (lexer, _("Syntax error in postcompute expression."));
      return NULL;
    }

  e.location = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer) - 1);
  return xmemdup (&e, sizeof e);
}

static struct ctables_pcexpr *
ctables_pcexpr_allocate_neg (struct ctables_pcexpr *sub,
                             struct lexer *lexer, int start_ofs)
{
  struct ctables_pcexpr *e = xmalloc (sizeof *e);
  *e = (struct ctables_pcexpr) {
    .op = CTPO_NEG,
    .subs = { sub },
    .location = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer) - 1),
  };
  return e;
}

static struct ctables_pcexpr *
ctables_pcexpr_parse_exp (struct lexer *lexer, struct dictionary *dict)
{
  static const struct operator op = { T_EXP, CTPO_POW };

  const char *chain_warning =
    _("The exponentiation operator (`**') is left-associative: "
      "`a**b**c' equals `(a**b)**c', not `a**(b**c)'.  "
      "To disable this warning, insert parentheses.");

  if (lex_token (lexer) != T_NEG_NUM || lex_next_token (lexer, 1) != T_EXP)
    return ctables_pcexpr_parse_binary_operators (lexer, dict, &op, 1,
                                                  ctables_pcexpr_parse_primary,
                                                  chain_warning);

  /* Special case for situations like "-5**6", which must be parsed as
     -(5**6). */

  int start_ofs = lex_ofs (lexer);
  struct ctables_pcexpr *lhs = xmalloc (sizeof *lhs);
  *lhs = (struct ctables_pcexpr) {
    .op = CTPO_CONSTANT,
    .number = -lex_tokval (lexer),
    .location = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer)),
  };
  lex_get (lexer);

  struct ctables_pcexpr *node = ctables_pcexpr_parse_binary_operators__ (
    lexer, dict, &op, 1,
    ctables_pcexpr_parse_primary, chain_warning, lhs);
  if (!node)
    return NULL;

  return ctables_pcexpr_allocate_neg (node, lexer, start_ofs);
}

/* Parses the unary minus level. */
static struct ctables_pcexpr *
ctables_pcexpr_parse_neg (struct lexer *lexer, struct dictionary *dict)
{
  int start_ofs = lex_ofs (lexer);
  if (!lex_match (lexer, T_DASH))
    return ctables_pcexpr_parse_exp (lexer, dict);

  struct ctables_pcexpr *inner = ctables_pcexpr_parse_neg (lexer, dict);
  if (!inner)
    return NULL;

  return ctables_pcexpr_allocate_neg (inner, lexer, start_ofs);
}

/* Parses the multiplication and division level. */
static struct ctables_pcexpr *
ctables_pcexpr_parse_mul (struct lexer *lexer, struct dictionary *dict)
{
  static const struct operator ops[] =
    {
      { T_ASTERISK, CTPO_MUL },
      { T_SLASH, CTPO_DIV },
    };

  return ctables_pcexpr_parse_binary_operators (lexer, dict, ops,
                                               sizeof ops / sizeof *ops,
                                               ctables_pcexpr_parse_neg, NULL);
}

/* Parses the addition and subtraction level. */
static struct ctables_pcexpr *
ctables_pcexpr_parse_add (struct lexer *lexer, struct dictionary *dict)
{
  static const struct operator ops[] =
    {
      { T_PLUS, CTPO_ADD },
      { T_DASH, CTPO_SUB },
      { T_NEG_NUM, CTPO_ADD },
    };

  return ctables_pcexpr_parse_binary_operators (lexer, dict,
                                               ops, sizeof ops / sizeof *ops,
                                               ctables_pcexpr_parse_mul, NULL);
}

/* CTABLES axis expressions. */

/* CTABLES has a number of extra formats that we implement via custom
   currency specifications on an alternate fmt_settings. */
#define CTEF_NEGPAREN FMT_CCA
#define CTEF_NEQUAL   FMT_CCB
#define CTEF_PAREN    FMT_CCC
#define CTEF_PCTPAREN FMT_CCD

enum ctables_summary_variant
  {
    CSV_CELL,
    CSV_TOTAL
#define N_CSVS 2
  };

struct ctables_axis
  {
    enum ctables_axis_op
      {
        /* Terminals. */
        CTAO_VAR,

        /* Nonterminals. */
        CTAO_STACK,             /* + */
        CTAO_NEST,              /* > */
      }
    op;

    union
      {
        /* Terminals. */
        struct
          {
            struct variable *var;
            bool scale;
            struct ctables_summary_spec_set specs[N_CSVS];
          };

        /* Nonterminals. */
        struct ctables_axis *subs[2];
      };

    struct msg_location *loc;
  };

static void
ctables_axis_destroy (struct ctables_axis *axis)
{
  if (!axis)
    return;

  switch (axis->op)
    {
    case CTAO_VAR:
      for (size_t i = 0; i < N_CSVS; i++)
        ctables_summary_spec_set_uninit (&axis->specs[i]);
      break;

    case CTAO_STACK:
    case CTAO_NEST:
      ctables_axis_destroy (axis->subs[0]);
      ctables_axis_destroy (axis->subs[1]);
      break;
    }
  msg_location_destroy (axis->loc);
  free (axis);
}

static struct ctables_axis *
ctables_axis_new_nonterminal (enum ctables_axis_op op,
                              struct ctables_axis *sub0,
                              struct ctables_axis *sub1,
                              struct lexer *lexer, int start_ofs)
{
  struct ctables_axis *axis = xmalloc (sizeof *axis);
  *axis = (struct ctables_axis) {
    .op = op,
    .subs = { sub0, sub1 },
    .loc = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer) - 1),
  };
  return axis;
}

struct ctables_axis_parse_ctx
  {
    struct lexer *lexer;
    struct dictionary *dict;
  };

static struct pivot_value *
ctables_summary_label (const struct ctables_summary_spec *spec, double cilevel)
{
  if (!spec->label)
    return ctables_summary_function_label (spec->function, spec->weighting,
                                           spec->user_area, spec->percentile);
  else
    {
      struct substring in = ss_cstr (spec->label);
      struct substring target = ss_cstr (")CILEVEL");

      struct string out = DS_EMPTY_INITIALIZER;
      for (;;)
        {
          size_t chunk = ss_find_substring (in, target);
          ds_put_substring (&out, ss_head (in, chunk));
          ss_advance (&in, chunk);
          if (!in.length)
            return pivot_value_new_user_text_nocopy (ds_steal_cstr (&out));

          ss_advance (&in, target.length);
          ds_put_format (&out, "%g", cilevel);
        }
    }
}

static bool
add_summary_spec (struct ctables_axis *axis,
                  enum ctables_summary_function function,
                  enum ctables_weighting weighting,
                  enum ctables_area_type area, double percentile,
                  const char *label, const struct fmt_spec *format,
                  bool is_ctables_format, const struct msg_location *loc,
                  enum ctables_summary_variant sv)
{
  if (axis->op == CTAO_VAR)
    {
      char function_name[128];
      ctables_summary_function_name (function, weighting, area,
                                     function_name, sizeof function_name);
      const char *var_name = var_get_name (axis->var);
      switch (ctables_function_availability (function))
        {
#if 0
        case CTFA_MRSETS:
          msg_at (SE, loc, _("Summary function %s applies only to multiple "
                             "response sets."), function_name);
          msg_at (SN, axis->loc, _("'%s' is not a multiple response set."),
                  var_name);
          return false;
#endif

        case CTFA_SCALE:
          if (!axis->scale && sv != CSV_TOTAL)
            {
              msg_at (SE, loc,
                      _("Summary function %s applies only to scale variables."),
                      function_name);
              msg_at (SN, axis->loc, _("'%s' is not a scale variable."),
                      var_name);
              return false;
            }
          break;

        case CTFA_ALL:
          break;
        }

      struct ctables_summary_spec_set *set = &axis->specs[sv];
      if (set->n >= set->allocated)
        set->specs = x2nrealloc (set->specs, &set->allocated,
                                 sizeof *set->specs);

      struct ctables_summary_spec *dst = &set->specs[set->n++];
      *dst = (struct ctables_summary_spec) {
        .function = function,
        .weighting = weighting,
        .calc_area = area,
        .user_area = area,
        .percentile = percentile,
        .label = xstrdup_if_nonnull (label),
        .format = (format ? *format
                   : ctables_summary_default_format (function, axis->var)),
        .is_ctables_format = is_ctables_format,
      };
      return true;
    }
  else if (axis->op == CTAO_NEST)
    return add_summary_spec (axis->subs[1], function, weighting, area,
                             percentile, label, format, is_ctables_format,
                             loc, sv);
  else
    {
      assert (axis->op == CTAO_STACK);
      for (size_t i = 0; i < 2; i++)
        if (!add_summary_spec (axis->subs[i], function, weighting, area,
                               percentile, label, format, is_ctables_format,
                               loc, sv))
          return false;
      return true;
    }
}

static struct ctables_axis *ctables_axis_parse_stack (
  struct ctables_axis_parse_ctx *);

static struct ctables_axis *
ctables_axis_parse_primary (struct ctables_axis_parse_ctx *ctx)
{
  if (lex_match (ctx->lexer, T_LPAREN))
    {
      struct ctables_axis *sub = ctables_axis_parse_stack (ctx);
      if (!sub || !lex_force_match (ctx->lexer, T_RPAREN))
        {
          ctables_axis_destroy (sub);
          return NULL;
        }
      return sub;
    }

  if (!lex_force_id (ctx->lexer))
    return NULL;

  if (lex_tokcstr (ctx->lexer)[0] == '$')
    {
      lex_error (ctx->lexer,
                 _("Multiple response set support not implemented."));
      return NULL;
    }

  int start_ofs = lex_ofs (ctx->lexer);
  struct variable *var = parse_variable (ctx->lexer, ctx->dict);
  if (!var)
    return NULL;

  struct ctables_axis *axis = xmalloc (sizeof *axis);
  *axis = (struct ctables_axis) { .op = CTAO_VAR, .var = var };

  axis->scale = (lex_match_phrase (ctx->lexer, "[S]") ? true
                 : lex_match_phrase (ctx->lexer, "[C]") ? false
                 : var_get_measure (var) == MEASURE_SCALE);
  axis->loc = lex_ofs_location (ctx->lexer, start_ofs,
                                lex_ofs (ctx->lexer) - 1);
  if (axis->scale && var_is_alpha (var))
    {
      msg_at (SE, axis->loc, _("Cannot use string variable %s as a scale "
                               "variable."),
              var_get_name (var));
      ctables_axis_destroy (axis);
      return NULL;
    }

  return axis;
}

static bool
has_digit (const char *s)
{
  return s[strcspn (s, "0123456789")] != '\0';
}

static bool
parse_ctables_format_specifier (struct lexer *lexer, struct fmt_spec *format,
                                bool *is_ctables_format)
{
  char type[FMT_TYPE_LEN_MAX + 1];
  if (!parse_abstract_format_specifier__ (lexer, type, &format->w, &format->d))
    return false;

  if (!strcasecmp (type, "NEGPAREN"))
    format->type = CTEF_NEGPAREN;
  else if (!strcasecmp (type, "NEQUAL"))
    format->type = CTEF_NEQUAL;
  else if (!strcasecmp (type, "PAREN"))
    format->type = CTEF_PAREN;
  else if (!strcasecmp (type, "PCTPAREN"))
    format->type = CTEF_PCTPAREN;
  else
    {
      *is_ctables_format = false;
      if (!parse_format_specifier (lexer, format))
        return false;

      char *error = fmt_check_output__ (*format);
      if (!error)
        error = fmt_check_type_compat__ (*format, NULL, VAL_NUMERIC);
      if (error)
        {
          lex_next_error (lexer, -1, -1, "%s", error);
          free (error);
          return false;
        }

      return true;
    }

  lex_get (lexer);
  if (format->w < 2)
    {
      lex_next_error (lexer, -1, -1,
                      _("Output format %s requires width 2 or greater."), type);
      return false;
    }
  else if (format->d > format->w - 1)
    {
      lex_next_error (lexer, -1, -1, _("Output format %s requires width "
                                       "greater than decimals."), type);
      return false;
    }
  else
    {
      *is_ctables_format = true;
      return true;
    }
}

static struct ctables_axis *
ctables_axis_parse_postfix (struct ctables_axis_parse_ctx *ctx)
{
  struct ctables_axis *sub = ctables_axis_parse_primary (ctx);
  if (!sub || !lex_match (ctx->lexer, T_LBRACK))
    return sub;

  enum ctables_summary_variant sv = CSV_CELL;
  for (;;)
    {
      int start_ofs = lex_ofs (ctx->lexer);

      /* Parse function. */
      enum ctables_summary_function function;
      enum ctables_weighting weighting;
      enum ctables_area_type area;
      if (!parse_ctables_summary_function (ctx->lexer, &function, &weighting,
                                           &area))
        goto error;

      /* Parse percentile. */
      double percentile = 0;
      if (function == CTSF_PTILE)
        {
          if (!lex_force_num_range_closed (ctx->lexer, "PTILE", 0, 100))
            goto error;
          percentile = lex_number (ctx->lexer);
          lex_get (ctx->lexer);
        }

      /* Parse label. */
      char *label = NULL;
      if (lex_is_string (ctx->lexer))
        {
          label = ss_xstrdup (lex_tokss (ctx->lexer));
          lex_get (ctx->lexer);
        }

      /* Parse format. */
      struct fmt_spec format;
      const struct fmt_spec *formatp;
      bool is_ctables_format = false;
      if (lex_token (ctx->lexer) == T_ID
          && has_digit (lex_tokcstr (ctx->lexer)))
        {
          if (!parse_ctables_format_specifier (ctx->lexer, &format,
                                               &is_ctables_format))
            {
              free (label);
              goto error;
            }
          formatp = &format;
        }
      else
        formatp = NULL;

      struct msg_location *loc = lex_ofs_location (ctx->lexer, start_ofs,
                                                   lex_ofs (ctx->lexer) - 1);
      bool ok = add_summary_spec (sub, function, weighting, area, percentile,
                                  label, formatp, is_ctables_format, loc, sv);
      free (label);
      msg_location_destroy (loc);

      if (!ok)
        goto error;

      lex_match (ctx->lexer, T_COMMA);
      if (sv == CSV_CELL && lex_match_id (ctx->lexer, "TOTALS"))
        {
          if (!lex_force_match (ctx->lexer, T_LBRACK))
            goto error;
          sv = CSV_TOTAL;
        }
      else if (lex_match (ctx->lexer, T_RBRACK))
        {
          if (sv == CSV_TOTAL && !lex_force_match (ctx->lexer, T_RBRACK))
            goto error;
          return sub;
        }
    }

error:
  ctables_axis_destroy (sub);
  return NULL;
}

static const struct ctables_axis *
find_scale (const struct ctables_axis *axis)
{
  if (!axis)
    return NULL;
  else if (axis->op == CTAO_VAR)
    return axis->scale ? axis : NULL;
  else
    {
      for (size_t i = 0; i < 2; i++)
        {
          const struct ctables_axis *scale = find_scale (axis->subs[i]);
          if (scale)
            return scale;
        }
      return NULL;
    }
}

static const struct ctables_axis *
find_categorical_summary_spec (const struct ctables_axis *axis)
{
  if (!axis)
    return NULL;
  else if (axis->op == CTAO_VAR)
    return !axis->scale && axis->specs[CSV_CELL].n ? axis : NULL;
  else
    {
      for (size_t i = 0; i < 2; i++)
        {
          const struct ctables_axis *sum
            = find_categorical_summary_spec (axis->subs[i]);
          if (sum)
            return sum;
        }
      return NULL;
    }
}

static struct ctables_axis *
ctables_axis_parse_nest (struct ctables_axis_parse_ctx *ctx)
{
  int start_ofs = lex_ofs (ctx->lexer);
  struct ctables_axis *lhs = ctables_axis_parse_postfix (ctx);
  if (!lhs)
    return NULL;

  while (lex_match (ctx->lexer, T_GT))
    {
      struct ctables_axis *rhs = ctables_axis_parse_postfix (ctx);
      if (!rhs)
        {
          ctables_axis_destroy (lhs);
          return NULL;
        }

      struct ctables_axis *nest = ctables_axis_new_nonterminal (
        CTAO_NEST, lhs, rhs, ctx->lexer, start_ofs);

      const struct ctables_axis *outer_scale = find_scale (lhs);
      const struct ctables_axis *inner_scale = find_scale (rhs);
      if (outer_scale && inner_scale)
        {
          msg_at (SE, nest->loc, _("Cannot nest scale variables."));
          msg_at (SN, outer_scale->loc, _("This is an outer scale variable."));
          msg_at (SN, inner_scale->loc, _("This is an inner scale variable."));
          ctables_axis_destroy (nest);
          return NULL;
        }

      const struct ctables_axis *outer_sum = find_categorical_summary_spec (lhs);
      if (outer_sum)
        {
          msg_at (SE, nest->loc,
                  _("Summaries may only be requested for categorical variables "
                    "at the innermost nesting level."));
          msg_at (SN, outer_sum->loc,
                  _("This outer categorical variable has a summary."));
          ctables_axis_destroy (nest);
          return NULL;
        }

      lhs = nest;
    }

  return lhs;
}

static struct ctables_axis *
ctables_axis_parse_stack (struct ctables_axis_parse_ctx *ctx)
{
  int start_ofs = lex_ofs (ctx->lexer);
  struct ctables_axis *lhs = ctables_axis_parse_nest (ctx);
  if (!lhs)
    return NULL;

  while (lex_match (ctx->lexer, T_PLUS))
    {
      struct ctables_axis *rhs = ctables_axis_parse_nest (ctx);
      if (!rhs)
        {
          ctables_axis_destroy (lhs);
          return NULL;
        }

      lhs = ctables_axis_new_nonterminal (CTAO_STACK, lhs, rhs,
                                          ctx->lexer, start_ofs);
    }

  return lhs;
}

static bool
ctables_axis_parse (struct lexer *lexer, struct dictionary *dict,
                    struct ctables_axis **axisp)
{
  *axisp = NULL;
  if (lex_token (lexer) == T_BY
      || lex_token (lexer) == T_SLASH
      || lex_token (lexer) == T_ENDCMD)
    return true;

  struct ctables_axis_parse_ctx ctx = {
    .lexer = lexer,
    .dict = dict,
  };
  *axisp = ctables_axis_parse_stack (&ctx);
  return *axisp;
}

/* CTABLES categories. */

struct ctables_categories
  {
    size_t n_refs;
    struct ctables_category *cats;
    size_t n_cats;
  };

struct ctables_category
  {
    enum ctables_category_type
      {
        /* Explicit category lists. */
        CCT_NUMBER,
        CCT_STRING,
        CCT_NRANGE,             /* Numerical range. */
        CCT_SRANGE,             /* String range. */
        CCT_MISSING,
        CCT_OTHERNM,
        CCT_POSTCOMPUTE,

        /* Totals and subtotals. */
        CCT_SUBTOTAL,
        CCT_TOTAL,

        /* Implicit category lists. */
        CCT_VALUE,
        CCT_LABEL,
        CCT_FUNCTION,

        /* For contributing to TOTALN. */
        CCT_EXCLUDED_MISSING,
      }
    type;

    struct ctables_category *subtotal;

    bool hide;

    union
      {
        double number;           /* CCT_NUMBER. */
        struct substring string; /* CCT_STRING, in dictionary encoding. */
        double nrange[2];        /* CCT_NRANGE. */
        struct substring srange[2]; /* CCT_SRANGE. */

        struct
          {
            char *total_label;      /* CCT_SUBTOTAL, CCT_TOTAL. */
            bool hide_subcategories; /* CCT_SUBTOTAL. */
          };

        /* CCT_POSTCOMPUTE. */
        struct
          {
            const struct ctables_postcompute *pc;
            enum fmt_type parse_format;
          };

        /* CCT_VALUE, CCT_LABEL, CCT_FUNCTION. */
        struct
          {
            bool include_missing;
            bool sort_ascending;

            /* CCT_FUNCTION. */
            enum ctables_summary_function sort_function;
            enum ctables_weighting weighting;
            enum ctables_area_type area;
            struct variable *sort_var;
            double percentile;
          };
      };

    /* Source location (sometimes NULL). */
    struct msg_location *location;
  };

static void
ctables_category_uninit (struct ctables_category *cat)
{
  if (!cat)
    return;

  msg_location_destroy (cat->location);
  switch (cat->type)
    {
    case CCT_NUMBER:
    case CCT_NRANGE:
    case CCT_MISSING:
    case CCT_OTHERNM:
    case CCT_POSTCOMPUTE:
      break;

    case CCT_STRING:
      ss_dealloc (&cat->string);
      break;

    case CCT_SRANGE:
      ss_dealloc (&cat->srange[0]);
      ss_dealloc (&cat->srange[1]);
      break;

    case CCT_SUBTOTAL:
    case CCT_TOTAL:
      free (cat->total_label);
      break;

    case CCT_VALUE:
    case CCT_LABEL:
    case CCT_FUNCTION:
      break;

    case CCT_EXCLUDED_MISSING:
      break;
    }
}

static bool
nullable_substring_equal (const struct substring *a,
                          const struct substring *b)
{
  return !a->string ? !b->string : b->string && ss_equals (*a, *b);
}

static bool
ctables_category_equal (const struct ctables_category *a,
                        const struct ctables_category *b)
{
  if (a->type != b->type)
    return false;

  switch (a->type)
    {
    case CCT_NUMBER:
      return a->number == b->number;

    case CCT_STRING:
      return ss_equals (a->string, b->string);

    case CCT_NRANGE:
      return a->nrange[0] == b->nrange[0] && a->nrange[1] == b->nrange[1];

    case CCT_SRANGE:
      return (nullable_substring_equal (&a->srange[0], &b->srange[0])
              && nullable_substring_equal (&a->srange[1], &b->srange[1]));

    case CCT_MISSING:
    case CCT_OTHERNM:
      return true;

    case CCT_POSTCOMPUTE:
      return a->pc == b->pc;

    case CCT_SUBTOTAL:
    case CCT_TOTAL:
      return !strcmp (a->total_label, b->total_label);

    case CCT_VALUE:
    case CCT_LABEL:
    case CCT_FUNCTION:
      return (a->include_missing == b->include_missing
              && a->sort_ascending == b->sort_ascending
              && a->sort_function == b->sort_function
              && a->sort_var == b->sort_var
              && a->percentile == b->percentile);

    case CCT_EXCLUDED_MISSING:
      return true;
    }

  NOT_REACHED ();
}

static void
ctables_categories_unref (struct ctables_categories *c)
{
  if (!c)
    return;

  assert (c->n_refs > 0);
  if (--c->n_refs)
    return;

  for (size_t i = 0; i < c->n_cats; i++)
    ctables_category_uninit (&c->cats[i]);
  free (c->cats);
  free (c);
}

static bool
ctables_categories_equal (const struct ctables_categories *a,
                          const struct ctables_categories *b)
{
  if (a->n_cats != b->n_cats)
    return false;

  for (size_t i = 0; i < a->n_cats; i++)
    if (!ctables_category_equal (&a->cats[i], &b->cats[i]))
      return false;

  return true;
}

static struct ctables_category
cct_nrange (double low, double high)
{
  return (struct ctables_category) {
    .type = CCT_NRANGE,
    .nrange = { low, high }
  };
}

static struct ctables_category
cct_srange (struct substring low, struct substring high)
{
  return (struct ctables_category) {
    .type = CCT_SRANGE,
    .srange = { low, high }
  };
}

static bool
ctables_table_parse_subtotal (struct lexer *lexer, bool hide_subcategories,
                              struct ctables_category *cat)
{
  char *total_label;
  if (lex_match (lexer, T_EQUALS))
    {
      if (!lex_force_string (lexer))
        return false;

      total_label = ss_xstrdup (lex_tokss (lexer));
      lex_get (lexer);
    }
  else
    total_label = xstrdup (_("Subtotal"));

  *cat = (struct ctables_category) {
    .type = CCT_SUBTOTAL,
    .hide_subcategories = hide_subcategories,
    .total_label = total_label
  };
  return true;
}

static bool
ctables_table_parse_explicit_category (struct lexer *lexer,
                                       struct dictionary *dict,
                                       struct ctables *ct,
                                       struct ctables_category *cat)
{
  if (lex_match_id (lexer, "OTHERNM"))
    *cat = (struct ctables_category) { .type = CCT_OTHERNM };
  else if (lex_match_id (lexer, "MISSING"))
    *cat = (struct ctables_category) { .type = CCT_MISSING };
  else if (lex_match_id (lexer, "SUBTOTAL"))
    return ctables_table_parse_subtotal (lexer, false, cat);
  else if (lex_match_id (lexer, "HSUBTOTAL"))
    return ctables_table_parse_subtotal (lexer, true, cat);
  else if (lex_match_id (lexer, "LO"))
    {
      if (!lex_force_match_id (lexer, "THRU"))
        return false;
      if (lex_is_string (lexer))
        {
          struct substring sr0 = { .string = NULL };
          struct substring sr1 = parse_substring (lexer, dict);
          *cat = cct_srange (sr0, sr1);
        }
      else if (lex_force_num (lexer))
        {
          *cat = cct_nrange (-DBL_MAX, lex_number (lexer));
          lex_get (lexer);
        }
      else
        return false;
    }
  else if (lex_is_number (lexer))
    {
      double number = lex_number (lexer);
      lex_get (lexer);
      if (lex_match_id (lexer, "THRU"))
        {
          if (lex_match_id (lexer, "HI"))
            *cat = cct_nrange (number, DBL_MAX);
          else
            {
              if (!lex_force_num (lexer))
                return false;
              *cat = cct_nrange (number, lex_number (lexer));
              lex_get (lexer);
            }
        }
      else
        *cat = (struct ctables_category) {
          .type = CCT_NUMBER,
          .number = number
        };
    }
  else if (lex_is_string (lexer))
    {
      struct substring s = parse_substring (lexer, dict);
      if (lex_match_id (lexer, "THRU"))
        {
          if (lex_match_id (lexer, "HI"))
            {
              struct substring sr1 = { .string = NULL };
              *cat = cct_srange (s, sr1);
            }
          else
            {
              if (!lex_force_string (lexer))
                {
                  ss_dealloc (&s);
                  return false;
                }
              struct substring sr1 = parse_substring (lexer, dict);
              *cat = cct_srange (s, sr1);
            }
        }
      else
        *cat = (struct ctables_category) { .type = CCT_STRING, .string = s };
    }
  else if (lex_match (lexer, T_AND))
    {
      if (!lex_force_id (lexer))
        return false;
      struct ctables_postcompute *pc = ctables_find_postcompute (
        ct, lex_tokcstr (lexer));
      if (!pc)
        {
          struct msg_location *loc = lex_get_location (lexer, -1, 0);
          msg_at (SE, loc, _("Unknown postcompute &%s."),
                  lex_tokcstr (lexer));
          msg_location_destroy (loc);
          return false;
        }
      lex_get (lexer);

      *cat = (struct ctables_category) { .type = CCT_POSTCOMPUTE, .pc = pc };
    }
  else
    {
      lex_error (lexer, _("Syntax error expecting category specification."));
      return false;
    }

  return true;
}

static bool
parse_category_string (struct msg_location *location,
                       struct substring s, const struct dictionary *dict,
                       enum fmt_type format, double *n)
{
  union value v;
  char *error = data_in (s, dict_get_encoding (dict), format,
                         settings_get_fmt_settings (), &v, 0, NULL);
  if (error)
    {
      msg_at (SE, location,
              _("Failed to parse category specification as format %s: %s."),
              fmt_name (format), error);
      free (error);
      return false;
    }

  *n = v.f;
  return true;
}

static struct ctables_category *
ctables_find_category_for_postcompute__ (const struct ctables_categories *cats,
                                         const struct ctables_pcexpr *e)
{
  struct ctables_category *best = NULL;
  size_t n_subtotals = 0;
  for (size_t i = 0; i < cats->n_cats; i++)
    {
      struct ctables_category *cat = &cats->cats[i];
      switch (e->op)
        {
        case CTPO_CAT_NUMBER:
          if (cat->type == CCT_NUMBER && cat->number == e->number)
            best = cat;
          break;

        case CTPO_CAT_STRING:
          if (cat->type == CCT_STRING && ss_equals (cat->string, e->string))
            best = cat;
          break;

        case CTPO_CAT_NRANGE:
          if (cat->type == CCT_NRANGE
              && cat->nrange[0] == e->nrange[0]
              && cat->nrange[1] == e->nrange[1])
            best = cat;
          break;

        case CTPO_CAT_SRANGE:
          if (cat->type == CCT_SRANGE
              && nullable_substring_equal (&cat->srange[0], &e->srange[0])
              && nullable_substring_equal (&cat->srange[1], &e->srange[1]))
            best = cat;
          break;

        case CTPO_CAT_MISSING:
          if (cat->type == CCT_MISSING)
            best = cat;
          break;

        case CTPO_CAT_OTHERNM:
          if (cat->type == CCT_OTHERNM)
            best = cat;
          break;

        case CTPO_CAT_SUBTOTAL:
          if (cat->type == CCT_SUBTOTAL)
            {
              n_subtotals++;
              if (e->subtotal_index == n_subtotals)
                return cat;
              else if (e->subtotal_index == 0)
                best = cat;
            }
          break;

        case CTPO_CAT_TOTAL:
          if (cat->type == CCT_TOTAL)
            return cat;
          break;

        case CTPO_CONSTANT:
        case CTPO_ADD:
        case CTPO_SUB:
        case CTPO_MUL:
        case CTPO_DIV:
        case CTPO_POW:
        case CTPO_NEG:
          NOT_REACHED ();
        }
    }
  if (e->op == CTPO_CAT_SUBTOTAL && e->subtotal_index == 0 && n_subtotals > 1)
    return NULL;
  return best;
}

static struct ctables_category *
ctables_find_category_for_postcompute (const struct dictionary *dict,
                                       const struct ctables_categories *cats,
                                       enum fmt_type parse_format,
                                       const struct ctables_pcexpr *e)
{
  if (parse_format != FMT_F)
    {
      if (e->op == CTPO_CAT_STRING)
        {
          double number;
          if (!parse_category_string (e->location, e->string, dict,
                                      parse_format, &number))
            return NULL;

          struct ctables_pcexpr e2 = {
            .op = CTPO_CAT_NUMBER,
            .number = number,
            .location = e->location,
          };
          return ctables_find_category_for_postcompute__ (cats, &e2);
        }
      else if (e->op == CTPO_CAT_SRANGE)
        {
          double nrange[2];
          if (!e->srange[0].string)
            nrange[0] = -DBL_MAX;
          else if (!parse_category_string (e->location, e->srange[0], dict,
                                           parse_format, &nrange[0]))
            return NULL;

          if (!e->srange[1].string)
            nrange[1] = DBL_MAX;
          else if (!parse_category_string (e->location, e->srange[1], dict,
                                           parse_format, &nrange[1]))
            return NULL;

          struct ctables_pcexpr e2 = {
            .op = CTPO_CAT_NRANGE,
            .nrange = { nrange[0], nrange[1] },
            .location = e->location,
          };
          return ctables_find_category_for_postcompute__ (cats, &e2);
        }
    }
  return ctables_find_category_for_postcompute__ (cats, e);
}

static struct substring
rtrim_value (const union value *v, const struct variable *var)
{
  struct substring s = ss_buffer (CHAR_CAST (char *, v->s),
                                  var_get_width (var));
  ss_rtrim (&s, ss_cstr (" "));
  return s;
}

static bool
in_string_range (const union value *v, const struct variable *var,
                 const struct substring *srange)
{
  struct substring s = rtrim_value (v, var);
  return ((!srange[0].string || ss_compare (s, srange[0]) >= 0)
          && (!srange[1].string || ss_compare (s, srange[1]) <= 0));
}

static const struct ctables_category *
ctables_categories_match (const struct ctables_categories *c,
                          const union value *v, const struct variable *var)
{
  if (var_is_numeric (var) && v->f == SYSMIS)
    return NULL;

  const struct ctables_category *othernm = NULL;
  for (size_t i = c->n_cats; i-- > 0; )
    {
      const struct ctables_category *cat = &c->cats[i];
      switch (cat->type)
        {
        case CCT_NUMBER:
          if (cat->number == v->f)
            return cat;
          break;

        case CCT_STRING:
          if (ss_equals (cat->string, rtrim_value (v, var)))
            return cat;
          break;

        case CCT_NRANGE:
          if ((cat->nrange[0] == -DBL_MAX || v->f >= cat->nrange[0])
              && (cat->nrange[1] == DBL_MAX || v->f <= cat->nrange[1]))
            return cat;
          break;

        case CCT_SRANGE:
          if (in_string_range (v, var, cat->srange))
            return cat;
          break;

        case CCT_MISSING:
          if (var_is_value_missing (var, v))
            return cat;
          break;

        case CCT_POSTCOMPUTE:
          break;

        case CCT_OTHERNM:
          if (!othernm)
            othernm = cat;
          break;

        case CCT_SUBTOTAL:
        case CCT_TOTAL:
          break;

        case CCT_VALUE:
        case CCT_LABEL:
        case CCT_FUNCTION:
          return (cat->include_missing || !var_is_value_missing (var, v) ? cat
                  : NULL);

        case CCT_EXCLUDED_MISSING:
          break;
        }
    }

  return var_is_value_missing (var, v) ? NULL : othernm;
}

static const struct ctables_category *
ctables_categories_total (const struct ctables_categories *c)
{
  const struct ctables_category *first = &c->cats[0];
  const struct ctables_category *last = &c->cats[c->n_cats - 1];
  return (first->type == CCT_TOTAL ? first
          : last->type == CCT_TOTAL ? last
          : NULL);
}

static void
ctables_category_format_number (double number, const struct variable *var,
                                struct string *s)
{
  struct pivot_value *pv = pivot_value_new_var_value (
    var, &(union value) { .f = number });
  pivot_value_format (pv, NULL, s);
  pivot_value_destroy (pv);
}

static void
ctables_category_format_string (struct substring string,
                                const struct variable *var, struct string *out)
{
  int width = var_get_width (var);
  char *s = xmalloc (width);
  buf_copy_rpad (s, width, string.string, string.length, ' ');
  struct pivot_value *pv = pivot_value_new_var_value (
    var, &(union value) { .s = CHAR_CAST (uint8_t *, s) });
  pivot_value_format (pv, NULL, out);
  pivot_value_destroy (pv);
  free (s);
}

static bool
ctables_category_format_label (const struct ctables_category *cat,
                               const struct variable *var,
                               struct string *s)
{
  switch (cat->type)
    {
    case CCT_NUMBER:
      ctables_category_format_number (cat->number, var, s);
      return true;

    case CCT_STRING:
      ctables_category_format_string (cat->string, var, s);
      return true;

    case CCT_NRANGE:
      ctables_category_format_number (cat->nrange[0], var, s);
      ds_put_format (s, " THRU ");
      ctables_category_format_number (cat->nrange[1], var, s);
      return true;

    case CCT_SRANGE:
      ctables_category_format_string (cat->srange[0], var, s);
      ds_put_format (s, " THRU ");
      ctables_category_format_string (cat->srange[1], var, s);
      return true;

    case CCT_MISSING:
      ds_put_cstr (s, "MISSING");
      return true;

    case CCT_OTHERNM:
      ds_put_cstr (s, "OTHERNM");
      return true;

    case CCT_POSTCOMPUTE:
      ds_put_format (s, "&%s", cat->pc->name);
      return true;

    case CCT_TOTAL:
    case CCT_SUBTOTAL:
      ds_put_cstr (s, cat->total_label);
      return true;

    case CCT_VALUE:
    case CCT_LABEL:
    case CCT_FUNCTION:
    case CCT_EXCLUDED_MISSING:
      return false;
    }

  return false;
}

static bool
ctables_recursive_check_postcompute (struct dictionary *dict,
                                     const struct ctables_pcexpr *e,
                                     struct ctables_category *pc_cat,
                                     const struct ctables_categories *cats,
                                     const struct msg_location *cats_location)
{
  switch (e->op)
    {
    case CTPO_CAT_NUMBER:
    case CTPO_CAT_STRING:
    case CTPO_CAT_NRANGE:
    case CTPO_CAT_SRANGE:
    case CTPO_CAT_MISSING:
    case CTPO_CAT_OTHERNM:
    case CTPO_CAT_SUBTOTAL:
    case CTPO_CAT_TOTAL:
      {
        struct ctables_category *cat = ctables_find_category_for_postcompute (
          dict, cats, pc_cat->parse_format, e);
        if (!cat)
          {
            if (e->op == CTPO_CAT_SUBTOTAL && e->subtotal_index == 0)
              {
                size_t n_subtotals = 0;
                for (size_t i = 0; i < cats->n_cats; i++)
                  n_subtotals += cats->cats[i].type == CCT_SUBTOTAL;
                if (n_subtotals > 1)
                  {
                    msg_at (SE, cats_location,
                            ngettext ("These categories include %zu instance "
                                      "of SUBTOTAL or HSUBTOTAL, so references "
                                      "from computed categories must refer to "
                                      "subtotals by position, "
                                      "e.g. SUBTOTAL[1].",
                                      "These categories include %zu instances "
                                      "of SUBTOTAL or HSUBTOTAL, so references "
                                      "from computed categories must refer to "
                                      "subtotals by position, "
                                      "e.g. SUBTOTAL[1].",
                                      n_subtotals),
                            n_subtotals);
                    msg_at (SN, e->location,
                            _("This is the reference that lacks a position."));
                    return NULL;
                  }
              }

            msg_at (SE, pc_cat->location,
                    _("Computed category &%s references a category not included "
                      "in the category list."),
                    pc_cat->pc->name);
            msg_at (SN, e->location, _("This is the missing category."));
            if (e->op == CTPO_CAT_SUBTOTAL)
              msg_at (SN, cats_location,
                      _("To fix the problem, add subtotals to the "
                        "list of categories here."));
            else if (e->op == CTPO_CAT_TOTAL)
              msg (SN, _("To fix the problem, add TOTAL=YES to the variable's "
                         "CATEGORIES specification."));
            else
              msg_at (SN, cats_location,
                      _("To fix the problem, add the missing category to the "
                        "list of categories here."));
            return false;
          }
        if (pc_cat->pc->hide_source_cats)
          cat->hide = true;
        return true;
      }

    case CTPO_CONSTANT:
      return true;

    case CTPO_ADD:
    case CTPO_SUB:
    case CTPO_MUL:
    case CTPO_DIV:
    case CTPO_POW:
    case CTPO_NEG:
      for (size_t i = 0; i < 2; i++)
        if (e->subs[i] && !ctables_recursive_check_postcompute (
              dict, e->subs[i], pc_cat, cats, cats_location))
          return false;
      return true;
    }

  NOT_REACHED ();
}

static struct pivot_value *
ctables_postcompute_label (const struct ctables_categories *cats,
                           const struct ctables_category *cat,
                           const struct variable *var)
{
  struct substring in = ss_cstr (cat->pc->label);
  struct substring target = ss_cstr (")LABEL[");

  struct string out = DS_EMPTY_INITIALIZER;
  for (;;)
    {
      size_t chunk = ss_find_substring (in, target);
      if (chunk == SIZE_MAX)
        {
          if (ds_is_empty (&out))
            return pivot_value_new_user_text (in.string, in.length);
          else
            {
              ds_put_substring (&out, in);
              return pivot_value_new_user_text_nocopy (ds_steal_cstr (&out));
            }
        }

      ds_put_substring (&out, ss_head (in, chunk));
      ss_advance (&in, chunk + target.length);

      struct substring idx_s;
      if (!ss_get_until (&in, ']', &idx_s))
        goto error;
      char *tail;
      long int idx = strtol (idx_s.string, &tail, 10);
      if (idx < 1 || idx > cats->n_cats || tail != ss_end (idx_s))
        goto error;

      struct ctables_category *cat2 = &cats->cats[idx - 1];
      if (!ctables_category_format_label (cat2, var, &out))
        goto error;
    }

error:
  ds_destroy (&out);
  return pivot_value_new_user_text (cat->pc->label, SIZE_MAX);
}

static struct pivot_value *
ctables_category_create_value_label (const struct ctables_categories *cats,
                                     const struct ctables_category *cat,
                                     const struct variable *var,
                                     const union value *value)
{
  return (cat->type == CCT_POSTCOMPUTE && cat->pc->label
          ? ctables_postcompute_label (cats, cat, var)
          : cat->type == CCT_TOTAL || cat->type == CCT_SUBTOTAL
          ? pivot_value_new_user_text (cat->total_label, SIZE_MAX)
          : pivot_value_new_var_value (var, value));
}

/* CTABLES variable nesting and stacking. */

/* A nested sequence of variables, e.g. a > b > c. */
struct ctables_nest
  {
    struct variable **vars;
    size_t n;
    size_t scale_idx;
    size_t summary_idx;
    size_t *areas[N_CTATS];
    size_t n_areas[N_CTATS];
    size_t group_head;

    struct ctables_summary_spec_set specs[N_CSVS];
  };

/* A stack of nestings, e.g. nest1 + nest2 + ... + nestN. */
struct ctables_stack
  {
    struct ctables_nest *nests;
    size_t n;
  };

static void
ctables_nest_uninit (struct ctables_nest *nest)
{
  free (nest->vars);
  for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
    ctables_summary_spec_set_uninit (&nest->specs[sv]);
  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    free (nest->areas[at]);
}

static void
ctables_stack_uninit (struct ctables_stack *stack)
{
  if (stack)
    {
      for (size_t i = 0; i < stack->n; i++)
        ctables_nest_uninit (&stack->nests[i]);
      free (stack->nests);
    }
}

static struct ctables_stack
nest_fts (struct ctables_stack s0, struct ctables_stack s1)
{
  if (!s0.n)
    return s1;
  else if (!s1.n)
    return s0;

  struct ctables_stack stack = { .nests = xnmalloc (s0.n, s1.n * sizeof *stack.nests) };
  for (size_t i = 0; i < s0.n; i++)
    for (size_t j = 0; j < s1.n; j++)
      {
        const struct ctables_nest *a = &s0.nests[i];
        const struct ctables_nest *b = &s1.nests[j];

        size_t allocate = a->n + b->n;
        struct variable **vars = xnmalloc (allocate, sizeof *vars);
        size_t n = 0;
        for (size_t k = 0; k < a->n; k++)
          vars[n++] = a->vars[k];
        for (size_t k = 0; k < b->n; k++)
          vars[n++] = b->vars[k];
        assert (n == allocate);

        const struct ctables_nest *summary_src;
        if (!a->specs[CSV_CELL].var)
          summary_src = b;
        else if (!b->specs[CSV_CELL].var)
          summary_src = a;
        else
          NOT_REACHED ();

        struct ctables_nest *new = &stack.nests[stack.n++];
        *new = (struct ctables_nest) {
          .vars = vars,
          .scale_idx = (a->scale_idx != SIZE_MAX ? a->scale_idx
                        : b->scale_idx != SIZE_MAX ? a->n + b->scale_idx
                        : SIZE_MAX),
          .summary_idx = (a->summary_idx != SIZE_MAX ? a->summary_idx
                          : b->summary_idx != SIZE_MAX ? a->n + b->summary_idx
                          : SIZE_MAX),
          .n = n,
        };
        for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
          ctables_summary_spec_set_clone (&new->specs[sv], &summary_src->specs[sv]);
      }
  ctables_stack_uninit (&s0);
  ctables_stack_uninit (&s1);
  return stack;
}

static struct ctables_stack
stack_fts (struct ctables_stack s0, struct ctables_stack s1)
{
  struct ctables_stack stack = { .nests = xnmalloc (s0.n + s1.n, sizeof *stack.nests) };
  for (size_t i = 0; i < s0.n; i++)
    stack.nests[stack.n++] = s0.nests[i];
  for (size_t i = 0; i < s1.n; i++)
    {
      stack.nests[stack.n] = s1.nests[i];
      stack.nests[stack.n].group_head += s0.n;
      stack.n++;
    }
  assert (stack.n == s0.n + s1.n);
  free (s0.nests);
  free (s1.nests);
  return stack;
}

static struct ctables_stack
var_fts (const struct ctables_axis *a)
{
  struct variable **vars = xmalloc (sizeof *vars);
  *vars = a->var;

  bool is_summary = a->specs[CSV_CELL].n || a->scale;
  struct ctables_nest *nest = xmalloc (sizeof *nest);
  *nest = (struct ctables_nest) {
    .vars = vars,
    .n = 1,
    .scale_idx = a->scale ? 0 : SIZE_MAX,
    .summary_idx = is_summary ? 0 : SIZE_MAX,
  };
  if (is_summary)
    for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
      {
        ctables_summary_spec_set_clone (&nest->specs[sv], &a->specs[sv]);
        nest->specs[sv].var = a->var;
        nest->specs[sv].is_scale = a->scale;
      }
  return (struct ctables_stack) { .nests = nest, .n = 1 };
}

static struct ctables_stack
enumerate_fts (enum pivot_axis_type axis_type, const struct ctables_axis *a)
{
  if (!a)
    return (struct ctables_stack) { .n = 0 };

  switch (a->op)
    {
    case CTAO_VAR:
      return var_fts (a);

    case CTAO_STACK:
      return stack_fts (enumerate_fts (axis_type, a->subs[0]),
                        enumerate_fts (axis_type, a->subs[1]));

    case CTAO_NEST:
      /* This should consider any of the scale variables found in the result to
         be linked to each other listwise for SMISSING=LISTWISE. */
      return nest_fts (enumerate_fts (axis_type, a->subs[0]),
                       enumerate_fts (axis_type, a->subs[1]));
    }

  NOT_REACHED ();
}

/* CTABLES summary calculation. */

union ctables_summary
  {
    /* COUNT, VALIDN, TOTALN. */
    double count;

    /* MINIMUM, MAXIMUM, RANGE. */
    struct
      {
        double min;
        double max;
      };

    /* MEAN, SEMEAN, STDDEV, SUM, VARIANCE, *.SUM. */
    struct moments1 *moments;

    /* MEDIAN, MODE, PTILE. */
    struct
      {
        struct casewriter *writer;
        double ovalid;
        double ovalue;
      };
  };

static void
ctables_summary_init (union ctables_summary *s,
                      const struct ctables_summary_spec *ss)
{
  switch (ss->function)
    {
    case CTSF_COUNT:
    case CTSF_areaPCT_COUNT:
    case CTSF_areaPCT_VALIDN:
    case CTSF_areaPCT_TOTALN:
    case CTSF_MISSING:
    case CTSF_TOTALN:
    case CTSF_VALIDN:
      s->count = 0;
      break;

    case CTSF_areaID:
      break;

    case CTSF_MAXIMUM:
    case CTSF_MINIMUM:
    case CTSF_RANGE:
      s->min = s->max = SYSMIS;
      break;

    case CTSF_MEAN:
    case CTSF_SUM:
    case CTSF_areaPCT_SUM:
      s->moments = moments1_create (MOMENT_MEAN);
      break;

    case CTSF_SEMEAN:
    case CTSF_STDDEV:
    case CTSF_VARIANCE:
      s->moments = moments1_create (MOMENT_VARIANCE);
      break;

    case CTSF_MEDIAN:
    case CTSF_MODE:
    case CTSF_PTILE:
      {
        struct caseproto *proto = caseproto_create ();
        proto = caseproto_add_width (proto, 0);
        proto = caseproto_add_width (proto, 0);

        struct subcase ordering;
        subcase_init (&ordering, 0, 0, SC_ASCEND);
        s->writer = sort_create_writer (&ordering, proto);
        subcase_uninit (&ordering);
        caseproto_unref (proto);

        s->ovalid = 0;
        s->ovalue = SYSMIS;
      }
      break;
    }
}

static void
ctables_summary_uninit (union ctables_summary *s,
                        const struct ctables_summary_spec *ss)
{
  switch (ss->function)
    {
    case CTSF_COUNT:
    case CTSF_areaPCT_COUNT:
    case CTSF_areaPCT_VALIDN:
    case CTSF_areaPCT_TOTALN:
    case CTSF_MISSING:
    case CTSF_TOTALN:
    case CTSF_VALIDN:
      break;

    case CTSF_areaID:
      break;

    case CTSF_MAXIMUM:
    case CTSF_MINIMUM:
    case CTSF_RANGE:
      break;

    case CTSF_MEAN:
    case CTSF_SEMEAN:
    case CTSF_STDDEV:
    case CTSF_SUM:
    case CTSF_VARIANCE:
    case CTSF_areaPCT_SUM:
      moments1_destroy (s->moments);
      break;

    case CTSF_MEDIAN:
    case CTSF_MODE:
    case CTSF_PTILE:
      casewriter_destroy (s->writer);
      break;
    }
}

static void
ctables_summary_add (union ctables_summary *s,
                     const struct ctables_summary_spec *ss,
                     const union value *value,
                     bool is_missing, bool is_included,
                     double weight)
{
  /* To determine whether a case is included in a given table for a particular
     kind of summary, consider the following charts for the variable being
     summarized.  Only if "yes" appears is the case counted.

     Categorical variables:                    VALIDN   other   TOTALN
       Valid values in included categories       yes     yes      yes
       Missing values in included categories     ---     yes      yes
       Missing values in excluded categories     ---     ---      yes
       Valid values in excluded categories       ---     ---      ---

     Scale variables:                          VALIDN   other   TOTALN
       Valid value                               yes     yes      yes
       Missing value                             ---     yes      yes

     Missing values include both user- and system-missing.  (The system-missing
     value is always in an excluded category.)

     One way to interpret the above table is that scale variables are like
     categorical variables in which all values are in included categories.
  */
  switch (ss->function)
    {
    case CTSF_TOTALN:
    case CTSF_areaPCT_TOTALN:
      s->count += weight;
      break;

    case CTSF_COUNT:
    case CTSF_areaPCT_COUNT:
      if (is_included)
        s->count += weight;
      break;

    case CTSF_VALIDN:
    case CTSF_areaPCT_VALIDN:
      if (!is_missing)
        s->count += weight;
      break;

    case CTSF_areaID:
      break;

    case CTSF_MISSING:
      if (is_missing)
        s->count += weight;
      break;

    case CTSF_MAXIMUM:
    case CTSF_MINIMUM:
    case CTSF_RANGE:
      if (!is_missing)
        {
          if (s->min == SYSMIS || value->f < s->min)
            s->min = value->f;
          if (s->max == SYSMIS || value->f > s->max)
            s->max = value->f;
        }
      break;

    case CTSF_MEAN:
    case CTSF_SEMEAN:
    case CTSF_STDDEV:
    case CTSF_SUM:
    case CTSF_VARIANCE:
      if (!is_missing)
        moments1_add (s->moments, value->f, weight);
      break;

    case CTSF_areaPCT_SUM:
      if (!is_missing)
        moments1_add (s->moments, value->f, weight);
      break;

    case CTSF_MEDIAN:
    case CTSF_MODE:
    case CTSF_PTILE:
      if (!is_missing)
        {
          s->ovalid += weight;

          struct ccase *c = case_create (casewriter_get_proto (s->writer));
          *case_num_rw_idx (c, 0) = value->f;
          *case_num_rw_idx (c, 1) = weight;
          casewriter_write (s->writer, c);
        }
      break;
    }
}

static double
ctables_summary_value (struct ctables_area *areas[N_CTATS],
                       union ctables_summary *s,
                       const struct ctables_summary_spec *ss)
{
  switch (ss->function)
    {
    case CTSF_COUNT:
      return s->count;

    case CTSF_areaID:
      return areas[ss->calc_area]->sequence;

    case CTSF_areaPCT_COUNT:
      {
        const struct ctables_area *a = areas[ss->calc_area];
        double a_count = a->count[ss->weighting];
        return a_count ? s->count / a_count * 100 : SYSMIS;
      }

    case CTSF_areaPCT_VALIDN:
      {
        const struct ctables_area *a = areas[ss->calc_area];
        double a_valid = a->valid[ss->weighting];
        return a_valid ? s->count / a_valid * 100 : SYSMIS;
      }

    case CTSF_areaPCT_TOTALN:
      {
        const struct ctables_area *a = areas[ss->calc_area];
        double a_total = a->total[ss->weighting];
        return a_total ? s->count / a_total * 100 : SYSMIS;
      }

    case CTSF_MISSING:
    case CTSF_TOTALN:
    case CTSF_VALIDN:
      return s->count;

    case CTSF_MAXIMUM:
      return s->max;

    case CTSF_MINIMUM:
      return s->min;

    case CTSF_RANGE:
      return s->max != SYSMIS && s->min != SYSMIS ? s->max - s->min : SYSMIS;

    case CTSF_MEAN:
      {
        double mean;
        moments1_calculate (s->moments, NULL, &mean, NULL, NULL, NULL);
        return mean;
      }

    case CTSF_SEMEAN:
      {
        double weight, variance;
        moments1_calculate (s->moments, &weight, NULL, &variance, NULL, NULL);
        return calc_semean (variance, weight);
      }

    case CTSF_STDDEV:
      {
        double variance;
        moments1_calculate (s->moments, NULL, NULL, &variance, NULL, NULL);
        return variance != SYSMIS ? sqrt (variance) : SYSMIS;
      }

    case CTSF_SUM:
      {
        double weight, mean;
        moments1_calculate (s->moments, &weight, &mean, NULL, NULL, NULL);
        return weight != SYSMIS && mean != SYSMIS ? weight * mean : SYSMIS;
      }

    case CTSF_VARIANCE:
      {
        double variance;
        moments1_calculate (s->moments, NULL, NULL, &variance, NULL, NULL);
        return variance;
      }

    case CTSF_areaPCT_SUM:
      {
        double weight, mean;
        moments1_calculate (s->moments, &weight, &mean, NULL, NULL, NULL);
        if (weight == SYSMIS || mean == SYSMIS)
          return SYSMIS;

        const struct ctables_area *a = areas[ss->calc_area];
        const struct ctables_sum *sum = &a->sums[ss->sum_var_idx];
        double denom = sum->sum[ss->weighting];
        return denom != 0 ? weight * mean / denom * 100 : SYSMIS;
      }

    case CTSF_MEDIAN:
    case CTSF_PTILE:
      if (s->writer)
        {
          struct casereader *reader = casewriter_make_reader (s->writer);
          s->writer = NULL;

          struct percentile *ptile = percentile_create (
            ss->function == CTSF_PTILE ? ss->percentile : 0.5, s->ovalid);
          struct order_stats *os = &ptile->parent;
          order_stats_accumulate_idx (&os, 1, reader, 1, 0);
          s->ovalue = percentile_calculate (ptile, PC_HAVERAGE);
          statistic_destroy (&ptile->parent.parent);
        }
      return s->ovalue;

    case CTSF_MODE:
      if (s->writer)
        {
          struct casereader *reader = casewriter_make_reader (s->writer);
          s->writer = NULL;

          struct mode *mode = mode_create ();
          struct order_stats *os = &mode->parent;
          order_stats_accumulate_idx (&os, 1, reader, 1, 0);
          s->ovalue = mode->mode;
          statistic_destroy (&mode->parent.parent);
        }
      return s->ovalue;
    }

  NOT_REACHED ();
}

/* CTABLES occurrences. */

struct ctables_occurrence
  {
    struct hmap_node node;
    union value value;
  };

static void
ctables_add_occurrence (const struct variable *var,
                        const union value *value,
                        struct hmap *occurrences)
{
  int width = var_get_width (var);
  unsigned int hash = value_hash (value, width, 0);

  struct ctables_occurrence *o;
  HMAP_FOR_EACH_WITH_HASH (o, struct ctables_occurrence, node, hash,
                           occurrences)
    if (value_equal (value, &o->value, width))
      return;

  o = xmalloc (sizeof *o);
  value_clone (&o->value, value, width);
  hmap_insert (occurrences, &o->node, hash);
}

enum ctables_vlabel
  {
    CTVL_NONE = SETTINGS_VALUE_SHOW_DEFAULT,
    CTVL_NAME = SETTINGS_VALUE_SHOW_VALUE,
    CTVL_LABEL = SETTINGS_VALUE_SHOW_LABEL,
    CTVL_BOTH = SETTINGS_VALUE_SHOW_BOTH,
  };

struct ctables_cell
  {
    /* In struct ctables_section's 'cells' hmap.  Indexed by all the values in
       all the axes (except the scalar variable, if any). */
    struct hmap_node node;
    struct ctables_section *section;

    /* The areas that contain this cell. */
    uint32_t omit_areas;
    struct ctables_area *areas[N_CTATS];

    bool hide;

    bool postcompute;
    enum ctables_summary_variant sv;

    struct ctables_cell_axis
      {
        struct ctables_cell_value
          {
            const struct ctables_category *category;
            union value value;
          }
        *cvs;
        int leaf;
      }
    axes[PIVOT_N_AXES];

    union ctables_summary *summaries;
  };

struct ctables_section
  {
    /* Settings. */
    struct ctables_table *table;
    struct ctables_nest *nests[PIVOT_N_AXES];

    /* Data. */
    struct hmap *occurrences[PIVOT_N_AXES]; /* "struct ctables_occurrence"s. */
    struct hmap cells;            /* Contains "struct ctables_cell"s. */
    struct hmap areas[N_CTATS];   /* Contains "struct ctables_area"s. */
  };

static void ctables_section_uninit (struct ctables_section *);

struct ctables_table
  {
    struct ctables *ctables;
    struct ctables_axis *axes[PIVOT_N_AXES];
    struct ctables_stack stacks[PIVOT_N_AXES];
    struct ctables_section *sections;
    size_t n_sections;
    enum pivot_axis_type summary_axis;
    struct ctables_summary_spec_set summary_specs;
    struct variable **sum_vars;
    size_t n_sum_vars;

    enum pivot_axis_type slabels_axis;
    bool slabels_visible;

    /* The innermost category labels for axis 'a' appear on axis label_axis[a].

       Most commonly, label_axis[a] == a, and in particular we always have
       label_axis{PIVOT_AXIS_LAYER] == PIVOT_AXIS_LAYER.

       If ROWLABELS or COLLABELS is specified, then one of
       label_axis[PIVOT_AXIS_ROW] or label_axis[PIVOT_AXIS_COLUMN] can be the
       opposite axis or PIVOT_AXIS_LAYER.  Only one of them will differ.

       If any category labels are moved, then 'clabels_example' is one of the
       variables being moved (and it is otherwise NULL).  All of the variables
       being moved have the same width, value labels, and categories, so this
       example variable can be used to find those out.

       The remaining members in this group are relevant only if category labels
       are moved.

       'clabels_values_map' holds a "struct ctables_value" for all the values
       that appear in all of the variables in the moved categories.  It is
       accumulated as the data is read.  Once the data is fully read, its
       sorted values are put into 'clabels_values' and 'n_clabels_values'.
    */
    enum pivot_axis_type label_axis[PIVOT_N_AXES];
    enum pivot_axis_type clabels_from_axis;
    enum pivot_axis_type clabels_to_axis;
    int clabels_start_ofs, clabels_end_ofs;
    const struct variable *clabels_example;
    struct hmap clabels_values_map;
    struct ctables_value **clabels_values;
    size_t n_clabels_values;

    /* Indexed by variable dictionary index. */
    struct ctables_categories **categories;
    size_t n_categories;
    bool *show_empty;

    double cilevel;

    char *caption;
    char *corner;
    char *title;

    struct ctables_chisq *chisq;
    struct ctables_pairwise *pairwise;
  };

struct ctables_cell_sort_aux
  {
    const struct ctables_nest *nest;
    enum pivot_axis_type a;
  };

static int
ctables_cell_compare_3way (const void *a_, const void *b_, const void *aux_)
{
  const struct ctables_cell_sort_aux *aux = aux_;
  struct ctables_cell *const *ap = a_;
  struct ctables_cell *const *bp = b_;
  const struct ctables_cell *a = *ap;
  const struct ctables_cell *b = *bp;

  const struct ctables_nest *nest = aux->nest;
  for (size_t i = 0; i < nest->n; i++)
    if (i != nest->scale_idx)
      {
        const struct variable *var = nest->vars[i];
        const struct ctables_cell_value *a_cv = &a->axes[aux->a].cvs[i];
        const struct ctables_cell_value *b_cv = &b->axes[aux->a].cvs[i];
        if (a_cv->category != b_cv->category)
          return a_cv->category > b_cv->category ? 1 : -1;

        const union value *a_val = &a_cv->value;
        const union value *b_val = &b_cv->value;
        switch (a_cv->category->type)
          {
          case CCT_NUMBER:
          case CCT_STRING:
          case CCT_SUBTOTAL:
          case CCT_TOTAL:
          case CCT_POSTCOMPUTE:
          case CCT_EXCLUDED_MISSING:
            /* Must be equal. */
            continue;

          case CCT_NRANGE:
          case CCT_SRANGE:
          case CCT_MISSING:
          case CCT_OTHERNM:
            {
              int cmp = value_compare_3way (a_val, b_val, var_get_width (var));
              if (cmp)
                return cmp;
            }
            break;

          case CCT_VALUE:
            {
              int cmp = value_compare_3way (a_val, b_val, var_get_width (var));
              if (cmp)
                return a_cv->category->sort_ascending ? cmp : -cmp;
            }
            break;

          case CCT_LABEL:
            {
              const char *a_label = var_lookup_value_label (var, a_val);
              const char *b_label = var_lookup_value_label (var, b_val);
              int cmp;
              if (a_label)
                {
                  if (!b_label)
                    return -1;
                  cmp = strcmp (a_label, b_label);
                }
              else
                {
                  if (b_label)
                    return 1;
                  cmp = value_compare_3way (a_val, b_val, var_get_width (var));
                }
              if (cmp)
                return a_cv->category->sort_ascending ? cmp : -cmp;
            }
            break;

          case CCT_FUNCTION:
            NOT_REACHED ();
          }
      }
  return 0;
}

static struct ctables_area *
ctables_area_insert (struct ctables_cell *cell, enum ctables_area_type area)
{
  struct ctables_section *s = cell->section;
  size_t hash = 0;
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n_areas[area]; i++)
        {
          size_t v_idx = nest->areas[area][i];
          struct ctables_cell_value *cv = &cell->axes[a].cvs[v_idx];
          hash = hash_pointer (cv->category, hash);
          if (cv->category->type != CCT_TOTAL
              && cv->category->type != CCT_SUBTOTAL
              && cv->category->type != CCT_POSTCOMPUTE)
            hash = value_hash (&cv->value,
                               var_get_width (nest->vars[v_idx]), hash);
        }
    }

  struct ctables_area *a;
  HMAP_FOR_EACH_WITH_HASH (a, struct ctables_area, node, hash, &s->areas[area])
    {
      const struct ctables_cell *df = a->example;
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          const struct ctables_nest *nest = s->nests[a];
          for (size_t i = 0; i < nest->n_areas[area]; i++)
            {
              size_t v_idx = nest->areas[area][i];
              struct ctables_cell_value *cv1 = &df->axes[a].cvs[v_idx];
              struct ctables_cell_value *cv2 = &cell->axes[a].cvs[v_idx];
              if (cv1->category != cv2->category
                  || (cv1->category->type != CCT_TOTAL
                      && cv1->category->type != CCT_SUBTOTAL
                      && cv1->category->type != CCT_POSTCOMPUTE
                      && !value_equal (&cv1->value, &cv2->value,
                                       var_get_width (nest->vars[v_idx]))))
                goto not_equal;
            }
        }
      return a;

    not_equal: ;
    }

  struct ctables_sum *sums = (s->table->n_sum_vars
                              ? xzalloc (s->table->n_sum_vars * sizeof *sums)
                              : NULL);

  a = xmalloc (sizeof *a);
  *a = (struct ctables_area) { .example = cell, .sums = sums };
  hmap_insert (&s->areas[area], &a->node, hash);
  return a;
}

static struct ctables_cell *
ctables_cell_insert__ (struct ctables_section *s, const struct ccase *c,
                       const struct ctables_category **cats[PIVOT_N_AXES])
{
  size_t hash = 0;
  enum ctables_summary_variant sv = CSV_CELL;
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n; i++)
        if (i != nest->scale_idx)
          {
            hash = hash_pointer (cats[a][i], hash);
            if (cats[a][i]->type != CCT_TOTAL
                && cats[a][i]->type != CCT_SUBTOTAL
                && cats[a][i]->type != CCT_POSTCOMPUTE)
              hash = value_hash (case_data (c, nest->vars[i]),
                                 var_get_width (nest->vars[i]), hash);
            else
              sv = CSV_TOTAL;
          }
    }

  struct ctables_cell *cell;
  HMAP_FOR_EACH_WITH_HASH (cell, struct ctables_cell, node, hash, &s->cells)
    {
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          const struct ctables_nest *nest = s->nests[a];
          for (size_t i = 0; i < nest->n; i++)
            if (i != nest->scale_idx
                && (cats[a][i] != cell->axes[a].cvs[i].category
                    || (cats[a][i]->type != CCT_TOTAL
                        && cats[a][i]->type != CCT_SUBTOTAL
                        && cats[a][i]->type != CCT_POSTCOMPUTE
                        && !value_equal (case_data (c, nest->vars[i]),
                                         &cell->axes[a].cvs[i].value,
                                         var_get_width (nest->vars[i])))))
                goto not_equal;
        }

      return cell;

    not_equal: ;
    }

  cell = xmalloc (sizeof *cell);
  cell->section = s;
  cell->hide = false;
  cell->sv = sv;
  cell->omit_areas = 0;
  cell->postcompute = false;
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      cell->axes[a].cvs = (nest->n
                           ? xnmalloc (nest->n, sizeof *cell->axes[a].cvs)
                           : NULL);
      for (size_t i = 0; i < nest->n; i++)
        {
          const struct ctables_category *cat = cats[a][i];
          const struct variable *var = nest->vars[i];
          const union value *value = case_data (c, var);
          if (i != nest->scale_idx)
            {
              const struct ctables_category *subtotal = cat->subtotal;
              if (cat->hide || (subtotal && subtotal->hide_subcategories))
                cell->hide = true;

              if (cat->type == CCT_TOTAL
                  || cat->type == CCT_SUBTOTAL
                  || cat->type == CCT_POSTCOMPUTE)
                {
                  switch (a)
                    {
                    case PIVOT_AXIS_COLUMN:
                      cell->omit_areas |= ((1u << CTAT_TABLE) |
                                           (1u << CTAT_LAYER) |
                                           (1u << CTAT_LAYERCOL) |
                                           (1u << CTAT_SUBTABLE) |
                                           (1u << CTAT_COL));
                      break;
                    case PIVOT_AXIS_ROW:
                      cell->omit_areas |= ((1u << CTAT_TABLE) |
                                           (1u << CTAT_LAYER) |
                                           (1u << CTAT_LAYERROW) |
                                           (1u << CTAT_SUBTABLE) |
                                           (1u << CTAT_ROW));
                      break;
                    case PIVOT_AXIS_LAYER:
                      cell->omit_areas |= ((1u << CTAT_TABLE) |
                                           (1u << CTAT_LAYER));
                      break;
                    }
                }
              if (cat->type == CCT_POSTCOMPUTE)
                cell->postcompute = true;
            }

          cell->axes[a].cvs[i].category = cat;
          value_clone (&cell->axes[a].cvs[i].value, value, var_get_width (var));
        }
    }

  const struct ctables_nest *ss = s->nests[s->table->summary_axis];
  const struct ctables_summary_spec_set *specs = &ss->specs[cell->sv];
  cell->summaries = xmalloc (specs->n * sizeof *cell->summaries);
  for (size_t i = 0; i < specs->n; i++)
    ctables_summary_init (&cell->summaries[i], &specs->specs[i]);
  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    cell->areas[at] = ctables_area_insert (cell, at);
  hmap_insert (&s->cells, &cell->node, hash);
  return cell;
}

static void
add_weight (double dst[N_CTWS], const double src[N_CTWS])
{
  for (enum ctables_weighting wt = 0; wt < N_CTWS; wt++)
    dst[wt] += src[wt];
}

static void
ctables_cell_add__ (struct ctables_section *s, const struct ccase *c,
                    const struct ctables_category **cats[PIVOT_N_AXES],
                    bool is_included, double weight[N_CTWS])
{
  struct ctables_cell *cell = ctables_cell_insert__ (s, c, cats);
  const struct ctables_nest *ss = s->nests[s->table->summary_axis];

  const struct ctables_summary_spec_set *specs = &ss->specs[cell->sv];
  const union value *value = case_data (c, specs->var);
  bool is_missing = var_is_value_missing (specs->var, value);
  bool is_scale_missing
    = is_missing || (specs->is_scale && is_listwise_missing (specs, c));

  for (size_t i = 0; i < specs->n; i++)
     ctables_summary_add (&cell->summaries[i], &specs->specs[i], value,
                          is_scale_missing, is_included,
                          weight[specs->specs[i].weighting]);
  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    if (!(cell->omit_areas && (1u << at)))
      {
        struct ctables_area *a = cell->areas[at];

        add_weight (a->total, weight);
        if (is_included)
          add_weight (a->count, weight);
        if (!is_missing)
          {
            add_weight (a->valid, weight);

            if (!is_scale_missing)
              for (size_t i = 0; i < s->table->n_sum_vars; i++)
                {
                  const struct variable *var = s->table->sum_vars[i];
                  double addend = case_num (c, var);
                  if (!var_is_num_missing (var, addend))
                    for (enum ctables_weighting wt = 0; wt < N_CTWS; wt++)
                      a->sums[i].sum[wt] += addend * weight[wt];
                }
          }
      }
}

static void
recurse_totals (struct ctables_section *s, const struct ccase *c,
                const struct ctables_category **cats[PIVOT_N_AXES],
                bool is_included, double weight[N_CTWS],
                enum pivot_axis_type start_axis, size_t start_nest)
{
  for (enum pivot_axis_type a = start_axis; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = start_nest; i < nest->n; i++)
        {
          if (i == nest->scale_idx)
            continue;

          const struct variable *var = nest->vars[i];

          const struct ctables_category *total = ctables_categories_total (
            s->table->categories[var_get_dict_index (var)]);
          if (total)
            {
              const struct ctables_category *save = cats[a][i];
              cats[a][i] = total;
              ctables_cell_add__ (s, c, cats, is_included, weight);
              recurse_totals (s, c, cats, is_included, weight, a, i + 1);
              cats[a][i] = save;
            }
        }
      start_nest = 0;
    }
}

static void
recurse_subtotals (struct ctables_section *s, const struct ccase *c,
                   const struct ctables_category **cats[PIVOT_N_AXES],
                   bool is_included, double weight[N_CTWS],
                   enum pivot_axis_type start_axis, size_t start_nest)
{
  for (enum pivot_axis_type a = start_axis; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = start_nest; i < nest->n; i++)
        {
          if (i == nest->scale_idx)
            continue;

          const struct ctables_category *save = cats[a][i];
          if (save->subtotal)
            {
              cats[a][i] = save->subtotal;
              ctables_cell_add__ (s, c, cats, is_included, weight);
              recurse_subtotals (s, c, cats, is_included, weight, a, i + 1);
              cats[a][i] = save;
            }
        }
      start_nest = 0;
    }
}

static void
ctables_cell_insert (struct ctables_section *s, const struct ccase *c,
                     double weight[N_CTWS])
{
  const struct ctables_category *layer_cats[s->nests[PIVOT_AXIS_LAYER]->n];
  const struct ctables_category *row_cats[s->nests[PIVOT_AXIS_ROW]->n];
  const struct ctables_category *column_cats[s->nests[PIVOT_AXIS_COLUMN]->n];
  const struct ctables_category **cats[PIVOT_N_AXES] =
    {
      [PIVOT_AXIS_LAYER] = layer_cats,
      [PIVOT_AXIS_ROW] = row_cats,
      [PIVOT_AXIS_COLUMN] = column_cats,
    };

  bool is_included = true;

  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n; i++)
        if (i != nest->scale_idx)
          {
            const struct variable *var = nest->vars[i];
            const union value *value = case_data (c, var);

            cats[a][i] = ctables_categories_match (
              s->table->categories[var_get_dict_index (var)], value, var);
            if (!cats[a][i])
              {
                if (i != nest->summary_idx)
                  return;

                if (!var_is_value_missing (var, value))
                  return;

                static const struct ctables_category cct_excluded_missing = {
                  .type = CCT_EXCLUDED_MISSING,
                  .hide = true,
                };
                cats[a][i] = &cct_excluded_missing;
                is_included = false;
              }
        }
    }

  if (is_included)
    for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
      {
        const struct ctables_nest *nest = s->nests[a];
        for (size_t i = 0; i < nest->n; i++)
          if (i != nest->scale_idx)
            {
              const struct variable *var = nest->vars[i];
              const union value *value = case_data (c, var);
              ctables_add_occurrence (var, value, &s->occurrences[a][i]);
            }
      }

  ctables_cell_add__ (s, c, cats, is_included, weight);
  recurse_totals (s, c, cats, is_included, weight, 0, 0);
  recurse_subtotals (s, c, cats, is_included, weight, 0, 0);
}

struct ctables_value
  {
    struct hmap_node node;
    union value value;
    int leaf;
  };

static struct ctables_value *
ctables_value_find__ (const struct ctables_table *t, const union value *value,
                      int width, unsigned int hash)
{
  struct ctables_value *clv;
  HMAP_FOR_EACH_WITH_HASH (clv, struct ctables_value, node,
                           hash, &t->clabels_values_map)
    if (value_equal (value, &clv->value, width))
      return clv;
  return NULL;
}

static void
ctables_value_insert (struct ctables_table *t, const union value *value,
                      int width)
{
  unsigned int hash = value_hash (value, width, 0);
  struct ctables_value *clv = ctables_value_find__ (t, value, width, hash);
  if (!clv)
    {
      clv = xmalloc (sizeof *clv);
      value_clone (&clv->value, value, width);
      hmap_insert (&t->clabels_values_map, &clv->node, hash);
    }
}

static const struct ctables_value *
ctables_value_find (const struct ctables_cell *cell)
{
  const struct ctables_section *s = cell->section;
  const struct ctables_table *t = s->table;
  if (!t->clabels_example)
    return NULL;

  const struct ctables_nest *clabels_nest = s->nests[t->clabels_from_axis];
  const struct variable *var = clabels_nest->vars[clabels_nest->n - 1];
  const union value *value
    = &cell->axes[t->clabels_from_axis].cvs[clabels_nest->n - 1].value;
  int width = var_get_width (var);
  const struct ctables_value *ctv = ctables_value_find__ (
    t, value, width, value_hash (value, width, 0));
  assert (ctv != NULL);
  return ctv;
}

static int
compare_ctables_values_3way (const void *a_, const void *b_, const void *width_)
{
  const struct ctables_value *const *ap = a_;
  const struct ctables_value *const *bp = b_;
  const struct ctables_value *a = *ap;
  const struct ctables_value *b = *bp;
  const int *width = width_;
  return value_compare_3way (&a->value, &b->value, *width);
}

static void
ctables_sort_clabels_values (struct ctables_table *t)
{
  const struct variable *v0 = t->clabels_example;
  int width = var_get_width (v0);

  size_t i0 = var_get_dict_index (v0);
  struct ctables_categories *c0 = t->categories[i0];
  if (t->show_empty[i0])
    {
      const struct val_labs *val_labs = var_get_value_labels (v0);
      for (const struct val_lab *vl = val_labs_first (val_labs); vl;
           vl = val_labs_next (val_labs, vl))
        if (ctables_categories_match (c0, &vl->value, v0))
          ctables_value_insert (t, &vl->value, width);
    }

  size_t n = hmap_count (&t->clabels_values_map);
  t->clabels_values = xnmalloc (n, sizeof *t->clabels_values);

  struct ctables_value *clv;
  size_t i = 0;
  HMAP_FOR_EACH (clv, struct ctables_value, node, &t->clabels_values_map)
    t->clabels_values[i++] = clv;
  t->n_clabels_values = n;
  assert (i == n);

  sort (t->clabels_values, n, sizeof *t->clabels_values,
        compare_ctables_values_3way, &width);

  for (size_t i = 0; i < n; i++)
    t->clabels_values[i]->leaf = i;
}

struct ctables
  {
    const struct dictionary *dict;
    struct pivot_table_look *look;

    /* For CTEF_* formats. */
    struct fmt_settings ctables_formats;

    /* If this is NULL, zeros are displayed using the normal print format.
       Otherwise, this string is displayed. */
    char *zero;

    /* If this is NULL, missing values are displayed using the normal print
       format.  Otherwise, this string is displayed. */
    char *missing;

    /* Indexed by variable dictionary index. */
    enum ctables_vlabel *vlabels;

    struct hmap postcomputes;   /* Contains "struct ctables_postcompute"s. */

    bool mrsets_count_duplicates; /* MRSETS. */
    bool smissing_listwise;       /* SMISSING. */
    struct variable *e_weight;    /* WEIGHT. */
    int hide_threshold;           /* HIDESMALLCOUNTS. */

    struct ctables_table **tables;
    size_t n_tables;
  };

static double
ctpo_add (double a, double b)
{
  return a + b;
}

static double
ctpo_sub (double a, double b)
{
  return a - b;
}

static double
ctpo_mul (double a, double b)
{
  return a * b;
}

static double
ctpo_div (double a, double b)
{
  return b ? a / b : SYSMIS;
}

static double
ctpo_pow (double a, double b)
{
  int save_errno = errno;
  errno = 0;
  double result = pow (a, b);
  if (errno)
    result = SYSMIS;
  errno = save_errno;
  return result;
}

static double
ctpo_neg (double a, double b UNUSED)
{
  return -a;
}

struct ctables_pcexpr_evaluate_ctx
  {
    const struct ctables_cell *cell;
    const struct ctables_section *section;
    const struct ctables_categories *cats;
    enum pivot_axis_type pc_a;
    size_t pc_a_idx;
    size_t summary_idx;
    enum fmt_type parse_format;
  };

static double ctables_pcexpr_evaluate (
  const struct ctables_pcexpr_evaluate_ctx *, const struct ctables_pcexpr *);

static double
ctables_pcexpr_evaluate_nonterminal (
  const struct ctables_pcexpr_evaluate_ctx *ctx,
  const struct ctables_pcexpr *e, size_t n_args,
  double evaluate (double, double))
{
  double args[2] = { 0, 0 };
  for (size_t i = 0; i < n_args; i++)
    {
      args[i] = ctables_pcexpr_evaluate (ctx, e->subs[i]);
      if (!isfinite (args[i]) || args[i] == SYSMIS)
        return SYSMIS;
    }
  return evaluate (args[0], args[1]);
}

static double
ctables_pcexpr_evaluate_category (const struct ctables_pcexpr_evaluate_ctx *ctx,
                                  const struct ctables_cell_value *pc_cv)
{
  const struct ctables_section *s = ctx->section;

  size_t hash = 0;
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n; i++)
        if (i != nest->scale_idx)
          {
            const struct ctables_cell_value *cv
              = (a == ctx->pc_a && i == ctx->pc_a_idx ? pc_cv
                 : &ctx->cell->axes[a].cvs[i]);
            hash = hash_pointer (cv->category, hash);
            if (cv->category->type != CCT_TOTAL
                && cv->category->type != CCT_SUBTOTAL
                && cv->category->type != CCT_POSTCOMPUTE)
              hash = value_hash (&cv->value,
                                 var_get_width (nest->vars[i]), hash);
          }
    }

  struct ctables_cell *tc;
  HMAP_FOR_EACH_WITH_HASH (tc, struct ctables_cell, node, hash, &s->cells)
    {
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          const struct ctables_nest *nest = s->nests[a];
          for (size_t i = 0; i < nest->n; i++)
            if (i != nest->scale_idx)
              {
                const struct ctables_cell_value *p_cv
                  = (a == ctx->pc_a && i == ctx->pc_a_idx ? pc_cv
                     : &ctx->cell->axes[a].cvs[i]);
                const struct ctables_cell_value *t_cv = &tc->axes[a].cvs[i];
                if (p_cv->category != t_cv->category
                    || (p_cv->category->type != CCT_TOTAL
                        && p_cv->category->type != CCT_SUBTOTAL
                        && p_cv->category->type != CCT_POSTCOMPUTE
                        && !value_equal (&p_cv->value,
                                         &t_cv->value,
                                         var_get_width (nest->vars[i]))))
                  goto not_equal;
              }
        }

      goto found;

    not_equal: ;
    }
  return 0;

found: ;
  const struct ctables_table *t = s->table;
  const struct ctables_nest *specs_nest = s->nests[t->summary_axis];
  const struct ctables_summary_spec_set *specs = &specs_nest->specs[tc->sv];
  return ctables_summary_value (tc->areas, &tc->summaries[ctx->summary_idx],
                                &specs->specs[ctx->summary_idx]);
}

static double
ctables_pcexpr_evaluate (const struct ctables_pcexpr_evaluate_ctx *ctx,
                         const struct ctables_pcexpr *e)
{
  switch (e->op)
    {
    case CTPO_CONSTANT:
      return e->number;

    case CTPO_CAT_NRANGE:
    case CTPO_CAT_SRANGE:
    case CTPO_CAT_MISSING:
    case CTPO_CAT_OTHERNM:
      {
        struct ctables_cell_value cv = {
          .category = ctables_find_category_for_postcompute (ctx->section->table->ctables->dict, ctx->cats, ctx->parse_format, e)
        };
        assert (cv.category != NULL);

        struct hmap *occurrences = &ctx->section->occurrences[ctx->pc_a][ctx->pc_a_idx];
        const struct ctables_occurrence *o;

        double sum = 0.0;
        const struct variable *var = ctx->section->nests[ctx->pc_a]->vars[ctx->pc_a_idx];
        HMAP_FOR_EACH (o, struct ctables_occurrence, node, occurrences)
          if (ctables_categories_match (ctx->cats, &o->value, var) == cv.category)
            {
              cv.value = o->value;
              sum += ctables_pcexpr_evaluate_category (ctx, &cv);
            }
        return sum;
      }

    case CTPO_CAT_NUMBER:
    case CTPO_CAT_SUBTOTAL:
    case CTPO_CAT_TOTAL:
      {
        struct ctables_cell_value cv = {
          .category = ctables_find_category_for_postcompute (ctx->section->table->ctables->dict, ctx->cats, ctx->parse_format, e),
          .value = { .f = e->number },
        };
        assert (cv.category != NULL);
        return ctables_pcexpr_evaluate_category (ctx, &cv);
      }

    case CTPO_CAT_STRING:
      {
        int width = var_get_width (ctx->section->nests[ctx->pc_a]->vars[ctx->pc_a_idx]);
        char *s = NULL;
        if (width > e->string.length)
          {
            s = xmalloc (width);
            buf_copy_rpad (s, width, e->string.string, e->string.length, ' ');
          }

        const struct ctables_category *category
          = ctables_find_category_for_postcompute (
            ctx->section->table->ctables->dict,
            ctx->cats, ctx->parse_format, e);
        assert (category != NULL);

        struct ctables_cell_value cv = { .category = category };
        if (category->type == CCT_NUMBER)
          cv.value.f = category->number;
        else if (category->type == CCT_STRING)
          cv.value.s = CHAR_CAST (uint8_t *, s ? s : e->string.string);
        else
          NOT_REACHED ();

        double retval = ctables_pcexpr_evaluate_category (ctx, &cv);
        free (s);
        return retval;
      }

    case CTPO_ADD:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 2, ctpo_add);

    case CTPO_SUB:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 2, ctpo_sub);

    case CTPO_MUL:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 2, ctpo_mul);

    case CTPO_DIV:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 2, ctpo_div);

    case CTPO_POW:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 2, ctpo_pow);

    case CTPO_NEG:
      return ctables_pcexpr_evaluate_nonterminal (ctx, e, 1, ctpo_neg);
    }

  NOT_REACHED ();
}

static const struct ctables_category *
ctables_cell_postcompute (const struct ctables_section *s,
                          const struct ctables_cell *cell,
                          enum pivot_axis_type *pc_a_p,
                          size_t *pc_a_idx_p)
{
  assert (cell->postcompute);
  const struct ctables_category *pc_cat = NULL;
  for (enum pivot_axis_type pc_a = 0; pc_a < PIVOT_N_AXES; pc_a++)
    for (size_t pc_a_idx = 0; pc_a_idx < s->nests[pc_a]->n; pc_a_idx++)
      {
        const struct ctables_cell_value *cv = &cell->axes[pc_a].cvs[pc_a_idx];
        if (cv->category->type == CCT_POSTCOMPUTE)
          {
            if (pc_cat)
              {
                /* Multiple postcomputes cross each other.  The value is
                   undefined. */
                return NULL;
              }

            pc_cat = cv->category;
            if (pc_a_p)
              *pc_a_p = pc_a;
            if (pc_a_idx_p)
              *pc_a_idx_p = pc_a_idx;
          }
      }

  assert (pc_cat != NULL);
  return pc_cat;
}

static double
ctables_cell_calculate_postcompute (const struct ctables_section *s,
                                    const struct ctables_cell *cell,
                                    const struct ctables_summary_spec *ss,
                                    struct fmt_spec *format,
                                    bool *is_ctables_format,
                                    size_t summary_idx)
{
  enum pivot_axis_type pc_a = 0;
  size_t pc_a_idx = 0;
  const struct ctables_category *pc_cat = ctables_cell_postcompute (
    s, cell, &pc_a, &pc_a_idx);
  if (!pc_cat)
    return SYSMIS;

  const struct ctables_postcompute *pc = pc_cat->pc;
  if (pc->specs)
    {
      for (size_t i = 0; i < pc->specs->n; i++)
        {
          const struct ctables_summary_spec *ss2 = &pc->specs->specs[i];
          if (ss->function == ss2->function
              && ss->weighting == ss2->weighting
              && ss->calc_area == ss2->calc_area
              && ss->percentile == ss2->percentile)
            {
              *format = ss2->format;
              *is_ctables_format = ss2->is_ctables_format;
              break;
            }
        }
    }

  const struct variable *var = s->nests[pc_a]->vars[pc_a_idx];
  const struct ctables_categories *cats = s->table->categories[
    var_get_dict_index (var)];
  struct ctables_pcexpr_evaluate_ctx ctx = {
    .cell = cell,
    .section = s,
    .cats = cats,
    .pc_a = pc_a,
    .pc_a_idx = pc_a_idx,
    .summary_idx = summary_idx,
    .parse_format = pc_cat->parse_format,
  };
  return ctables_pcexpr_evaluate (&ctx, pc->expr);
}

/* Chi-square test (SIGTEST). */
struct ctables_chisq
  {
    double alpha;
    bool include_mrsets;
    bool all_visible;
  };

/* Pairwise comparison test (COMPARETEST). */
struct ctables_pairwise
  {
    enum { PROP, MEAN } type;
    double alpha[2];
    bool include_mrsets;
    bool meansvariance_allcats;
    bool all_visible;
    enum { BONFERRONI = 1, BH } adjust;
    bool merge;
    bool apa_style;
    bool show_sig;
  };



static bool
parse_col_width (struct lexer *lexer, const char *name, double *width)
{
  lex_match (lexer, T_EQUALS);
  if (lex_match_id (lexer, "DEFAULT"))
    *width = SYSMIS;
  else if (lex_force_num_range_closed (lexer, name, 0, DBL_MAX))
    {
      *width = lex_number (lexer);
      lex_get (lexer);
    }
  else
    return false;

  return true;
}

static bool
parse_bool (struct lexer *lexer, bool *b)
{
  if (lex_match_id (lexer, "NO"))
    *b = false;
  else if (lex_match_id (lexer, "YES"))
    *b = true;
  else
    {
      lex_error_expecting (lexer, "YES", "NO");
      return false;
    }
  return true;
}

static void
ctables_chisq_destroy (struct ctables_chisq *chisq)
{
  free (chisq);
}

static void
ctables_pairwise_destroy (struct ctables_pairwise *pairwise)
{
  free (pairwise);
}

static void
ctables_table_destroy (struct ctables_table *t)
{
  if (!t)
    return;

  for (size_t i = 0; i < t->n_sections; i++)
    ctables_section_uninit (&t->sections[i]);
  free (t->sections);

  for (size_t i = 0; i < t->n_categories; i++)
    ctables_categories_unref (t->categories[i]);
  free (t->categories);
  free (t->show_empty);

  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      ctables_axis_destroy (t->axes[a]);
      ctables_stack_uninit (&t->stacks[a]);
    }
  free (t->summary_specs.specs);

  struct ctables_value *ctv, *next_ctv;
  HMAP_FOR_EACH_SAFE (ctv, next_ctv, struct ctables_value, node,
                      &t->clabels_values_map)
    {
      value_destroy (&ctv->value, var_get_width (t->clabels_example));
      hmap_delete (&t->clabels_values_map, &ctv->node);
      free (ctv);
    }
  hmap_destroy (&t->clabels_values_map);
  free (t->clabels_values);

  free (t->sum_vars);
  free (t->caption);
  free (t->corner);
  free (t->title);
  ctables_chisq_destroy (t->chisq);
  ctables_pairwise_destroy (t->pairwise);
  free (t);
}

static void
ctables_destroy (struct ctables *ct)
{
  if (!ct)
    return;

  struct ctables_postcompute *pc, *next_pc;
  HMAP_FOR_EACH_SAFE (pc, next_pc, struct ctables_postcompute, hmap_node,
                      &ct->postcomputes)
    {
      free (pc->name);
      msg_location_destroy (pc->location);
      ctables_pcexpr_destroy (pc->expr);
      free (pc->label);
      if (pc->specs)
        {
          ctables_summary_spec_set_uninit (pc->specs);
          free (pc->specs);
        }
      hmap_delete (&ct->postcomputes, &pc->hmap_node);
      free (pc);
    }
  hmap_destroy (&ct->postcomputes);

  fmt_settings_uninit (&ct->ctables_formats);
  pivot_table_look_unref (ct->look);
  free (ct->zero);
  free (ct->missing);
  free (ct->vlabels);
  for (size_t i = 0; i < ct->n_tables; i++)
    ctables_table_destroy (ct->tables[i]);
  free (ct->tables);
  free (ct);
}

static bool
all_strings (struct variable **vars, size_t n_vars,
             const struct ctables_category *cat)
{
  for (size_t j = 0; j < n_vars; j++)
    if (var_is_numeric (vars[j]))
      {
        msg_at (SE, cat->location,
                _("This category specification may be applied only to string "
                  "variables, but this subcommand tries to apply it to "
                  "numeric variable %s."),
                var_get_name (vars[j]));
        return false;
      }
  return true;
}

static bool
ctables_table_parse_categories (struct lexer *lexer, struct dictionary *dict,
                                struct ctables *ct, struct ctables_table *t)
{
  if (!lex_force_match_id (lexer, "VARIABLES"))
    return false;
  lex_match (lexer, T_EQUALS);

  struct variable **vars;
  size_t n_vars;
  if (!parse_variables (lexer, dict, &vars, &n_vars, PV_NO_SCRATCH))
    return false;

  struct fmt_spec common_format = var_get_print_format (vars[0]);
  bool has_common_format = true;
  for (size_t i = 1; i < n_vars; i++)
    {
      struct fmt_spec f = var_get_print_format (vars[i]);
      if (f.type != common_format.type)
        {
          has_common_format = false;
          break;
        }
    }
  bool parse_strings
    = (has_common_format
       && (fmt_get_category (common_format.type)
           & (FMT_CAT_DATE | FMT_CAT_TIME | FMT_CAT_DATE_COMPONENT)));

  struct ctables_categories *c = xmalloc (sizeof *c);
  *c = (struct ctables_categories) { .n_refs = 1 };

  bool set_categories = false;

  size_t allocated_cats = 0;
  int cats_start_ofs = -1;
  int cats_end_ofs = -1;
  if (lex_match (lexer, T_LBRACK))
    {
      set_categories = true;
      cats_start_ofs = lex_ofs (lexer);
      do
        {
          if (c->n_cats >= allocated_cats)
            c->cats = x2nrealloc (c->cats, &allocated_cats, sizeof *c->cats);

          int start_ofs = lex_ofs (lexer);
          struct ctables_category *cat = &c->cats[c->n_cats];
          if (!ctables_table_parse_explicit_category (lexer, dict, ct, cat))
            goto error;
          cat->location = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer) - 1);
          c->n_cats++;

          lex_match (lexer, T_COMMA);
        }
      while (!lex_match (lexer, T_RBRACK));
      cats_end_ofs = lex_ofs (lexer) - 1;
    }

  struct ctables_category cat = {
    .type = CCT_VALUE,
    .include_missing = false,
    .sort_ascending = true,
  };
  bool show_totals = false;
  char *total_label = NULL;
  bool totals_before = false;
  int key_start_ofs = 0;
  int key_end_ofs = 0;
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (!c->n_cats && lex_match_id (lexer, "ORDER"))
        {
          set_categories = true;
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "A"))
            cat.sort_ascending = true;
          else if (lex_match_id (lexer, "D"))
            cat.sort_ascending = false;
          else
            {
              lex_error_expecting (lexer, "A", "D");
              goto error;
            }
        }
      else if (!c->n_cats && lex_match_id (lexer, "KEY"))
        {
          set_categories = true;
          key_start_ofs = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "VALUE"))
            cat.type = CCT_VALUE;
          else if (lex_match_id (lexer, "LABEL"))
            cat.type = CCT_LABEL;
          else
            {
              cat.type = CCT_FUNCTION;
              if (!parse_ctables_summary_function (lexer, &cat.sort_function,
                                                   &cat.weighting, &cat.area))
                goto error;

              if (lex_match (lexer, T_LPAREN))
                {
                  cat.sort_var = parse_variable (lexer, dict);
                  if (!cat.sort_var)
                    goto error;

                  if (cat.sort_function == CTSF_PTILE)
                    {
                      lex_match (lexer, T_COMMA);
                      if (!lex_force_num_range_closed (lexer, "PTILE", 0, 100))
                        goto error;
                      cat.percentile = lex_number (lexer);
                      lex_get (lexer);
                    }

                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
              else if (ctables_function_availability (cat.sort_function)
                       == CTFA_SCALE)
                {
                  bool UNUSED b = lex_force_match (lexer, T_LPAREN);
                  goto error;
                }
            }
          key_end_ofs = lex_ofs (lexer) - 1;

          if (cat.type == CCT_FUNCTION)
            {
              lex_ofs_error (lexer, key_start_ofs, key_end_ofs,
                             _("Data-dependent sorting is not implemented."));
              goto error;
            }
        }
      else if (!c->n_cats && lex_match_id (lexer, "MISSING"))
        {
          set_categories = true;
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "INCLUDE"))
            cat.include_missing = true;
          else if (lex_match_id (lexer, "EXCLUDE"))
            cat.include_missing = false;
          else
            {
              lex_error_expecting (lexer, "INCLUDE", "EXCLUDE");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "TOTAL"))
        {
          set_categories = true;
          lex_match (lexer, T_EQUALS);
          if (!parse_bool (lexer, &show_totals))
            goto error;
        }
      else if (lex_match_id (lexer, "LABEL"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto error;
          free (total_label);
          total_label = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "POSITION"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "BEFORE"))
            totals_before = true;
          else if (lex_match_id (lexer, "AFTER"))
            totals_before = false;
          else
            {
              lex_error_expecting (lexer, "BEFORE", "AFTER");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "EMPTY"))
        {
          lex_match (lexer, T_EQUALS);

          bool show_empty;
          if (lex_match_id (lexer, "INCLUDE"))
            show_empty = true;
          else if (lex_match_id (lexer, "EXCLUDE"))
            show_empty = false;
          else
            {
              lex_error_expecting (lexer, "INCLUDE", "EXCLUDE");
              goto error;
            }

          for (size_t i = 0; i < n_vars; i++)
            t->show_empty[var_get_dict_index (vars[i])] = show_empty;
        }
      else
        {
          if (!c->n_cats)
            lex_error_expecting (lexer, "ORDER", "KEY", "MISSING",
                                 "TOTAL", "LABEL", "POSITION", "EMPTY");
          else
            lex_error_expecting (lexer, "TOTAL", "LABEL", "POSITION", "EMPTY");
          goto error;
        }
    }

  if (!c->n_cats)
    {
      if (key_start_ofs)
        cat.location = lex_ofs_location (lexer, key_start_ofs, key_end_ofs);

      if (c->n_cats >= allocated_cats)
        c->cats = x2nrealloc (c->cats, &allocated_cats, sizeof *c->cats);
      c->cats[c->n_cats++] = cat;
    }

  if (show_totals)
    {
      if (c->n_cats >= allocated_cats)
        c->cats = x2nrealloc (c->cats, &allocated_cats, sizeof *c->cats);

      struct ctables_category *totals;
      if (totals_before)
        {
          insert_element (c->cats, c->n_cats, sizeof *c->cats, 0);
          totals = &c->cats[0];
        }
      else
        totals = &c->cats[c->n_cats];
      c->n_cats++;

      *totals = (struct ctables_category) {
        .type = CCT_TOTAL,
        .total_label = total_label ? total_label : xstrdup (_("Total")),
      };
    }

  struct ctables_category *subtotal = NULL;
  for (size_t i = totals_before ? 0 : c->n_cats;
       totals_before ? i < c->n_cats : i-- > 0;
       totals_before ? i++ : 0)
    {
      struct ctables_category *cat = &c->cats[i];
      switch (cat->type)
        {
        case CCT_NUMBER:
        case CCT_STRING:
        case CCT_NRANGE:
        case CCT_SRANGE:
        case CCT_MISSING:
        case CCT_OTHERNM:
          cat->subtotal = subtotal;
          break;

        case CCT_POSTCOMPUTE:
          break;

        case CCT_SUBTOTAL:
          subtotal = cat;
          break;

        case CCT_TOTAL:
        case CCT_VALUE:
        case CCT_LABEL:
        case CCT_FUNCTION:
        case CCT_EXCLUDED_MISSING:
          break;
        }
    }

  if (cats_start_ofs != -1)
    {
      for (size_t i = 0; i < c->n_cats; i++)
        {
          struct ctables_category *cat = &c->cats[i];
          switch (cat->type)
            {
            case CCT_POSTCOMPUTE:
              cat->parse_format = parse_strings ? common_format.type : FMT_F;
              struct msg_location *cats_location
                = lex_ofs_location (lexer, cats_start_ofs, cats_end_ofs);
              bool ok = ctables_recursive_check_postcompute (
                dict, cat->pc->expr, cat, c, cats_location);
              msg_location_destroy (cats_location);
              if (!ok)
                goto error;
              break;

            case CCT_NUMBER:
            case CCT_NRANGE:
              for (size_t j = 0; j < n_vars; j++)
                if (var_is_alpha (vars[j]))
                  {
                    msg_at (SE, cat->location,
                            _("This category specification may be applied "
                              "only to numeric variables, but this "
                              "subcommand tries to apply it to string "
                              "variable %s."),
                            var_get_name (vars[j]));
                    goto error;
                  }
              break;

            case CCT_STRING:
              if (parse_strings)
                {
                  double n;
                  if (!parse_category_string (cat->location, cat->string, dict,
                                              common_format.type, &n))
                    goto error;

                  ss_dealloc (&cat->string);

                  cat->type = CCT_NUMBER;
                  cat->number = n;
                }
              else if (!all_strings (vars, n_vars, cat))
                goto error;
              break;

            case CCT_SRANGE:
              if (parse_strings)
                {
                  double n[2];

                  if (!cat->srange[0].string)
                    n[0] = -DBL_MAX;
                  else if (!parse_category_string (cat->location,
                                                   cat->srange[0], dict,
                                                   common_format.type, &n[0]))
                    goto error;

                  if (!cat->srange[1].string)
                    n[1] = DBL_MAX;
                  else if (!parse_category_string (cat->location,
                                                   cat->srange[1], dict,
                                                   common_format.type, &n[1]))
                    goto error;

                  ss_dealloc (&cat->srange[0]);
                  ss_dealloc (&cat->srange[1]);

                  cat->type = CCT_NRANGE;
                  cat->nrange[0] = n[0];
                  cat->nrange[1] = n[1];
                }
              else if (!all_strings (vars, n_vars, cat))
                goto error;
              break;

            case CCT_MISSING:
            case CCT_OTHERNM:
            case CCT_SUBTOTAL:
            case CCT_TOTAL:
            case CCT_VALUE:
            case CCT_LABEL:
            case CCT_FUNCTION:
            case CCT_EXCLUDED_MISSING:
              break;
            }
        }
    }

  if (set_categories)
    for (size_t i = 0; i < n_vars; i++)
      {
        struct ctables_categories **cp
          = &t->categories[var_get_dict_index (vars[i])];
        ctables_categories_unref (*cp);
        *cp = c;
        c->n_refs++;
      }

  ctables_categories_unref (c);
  free (vars);
  return true;

error:
  ctables_categories_unref (c);
  free (vars);
  return false;
}


struct merge_item
  {
    const struct ctables_summary_spec_set *set;
    size_t ofs;
  };

static int
merge_item_compare_3way (const struct merge_item *a, const struct merge_item *b)
{
  const struct ctables_summary_spec *as = &a->set->specs[a->ofs];
  const struct ctables_summary_spec *bs = &b->set->specs[b->ofs];
  if (as->function != bs->function)
    return as->function > bs->function ? 1 : -1;
  else if (as->weighting != bs->weighting)
    return as->weighting > bs->weighting ? 1 : -1;
  else if (as->calc_area != bs->calc_area)
    return as->calc_area > bs->calc_area ? 1 : -1;
  else if (as->percentile != bs->percentile)
    return as->percentile < bs->percentile ? 1 : -1;

  const char *as_label = as->label ? as->label : "";
  const char *bs_label = bs->label ? bs->label : "";
  return strcmp (as_label, bs_label);
}

static void
ctables_table_add_section (struct ctables_table *t, enum pivot_axis_type a,
                           size_t ix[PIVOT_N_AXES])
{
  if (a < PIVOT_N_AXES)
    {
      size_t limit = MAX (t->stacks[a].n, 1);
      for (ix[a] = 0; ix[a] < limit; ix[a]++)
        ctables_table_add_section (t, a + 1, ix);
    }
  else
    {
      struct ctables_section *s = &t->sections[t->n_sections++];
      *s = (struct ctables_section) {
        .table = t,
        .cells = HMAP_INITIALIZER (s->cells),
      };
      for (a = 0; a < PIVOT_N_AXES; a++)
        if (t->stacks[a].n)
          {
            struct ctables_nest *nest = &t->stacks[a].nests[ix[a]];
            s->nests[a] = nest;
            s->occurrences[a] = xnmalloc (nest->n, sizeof *s->occurrences[a]);
            for (size_t i = 0; i < nest->n; i++)
              hmap_init (&s->occurrences[a][i]);
        }
      for (enum ctables_area_type at = 0; at < N_CTATS; at++)
        hmap_init (&s->areas[at]);
    }
}

static char *
ctables_format (double d, struct fmt_spec format,
                const struct fmt_settings *settings)
{
  const union value v = { .f = d };
  char *s = data_out_stretchy (&v, "UTF-8", format, settings, NULL);

  /* The custom-currency specifications for NEQUAL, PAREN, and PCTPAREN don't
     produce the results we want for negative numbers, putting the negative
     sign in the wrong spot, before the prefix instead of after it.  We can't,
     in fact, produce the desired results using a custom-currency
     specification.  Instead, we postprocess the output, moving the negative
     sign into place:

         NEQUAL:   "-N=3"  => "N=-3"
         PAREN:    "-(3)"  => "(-3)"
         PCTPAREN: "-(3%)" => "(-3%)"

     This transformation doesn't affect NEGPAREN. */
  char *minus_src = strchr (s, '-');
  if (minus_src && (minus_src == s || minus_src[-1] != 'E'))
    {
      char *n_equals = strstr (s, "N=");
      char *lparen = strchr (s, '(');
      char *minus_dst = n_equals ? n_equals + 1 : lparen;
      if (minus_dst)
        move_element (s, minus_dst - s + 1, 1, minus_src - s, minus_dst - s);
    }
  return s;
}

static bool
all_hidden_vlabels (const struct ctables_table *t, enum pivot_axis_type a)
{
  for (size_t i = 0; i < t->stacks[a].n; i++)
    {
      struct ctables_nest *nest = &t->stacks[a].nests[i];
      if (nest->n != 1 || nest->scale_idx != 0)
        return false;

      enum ctables_vlabel vlabel
        = t->ctables->vlabels[var_get_dict_index (nest->vars[0])];
      if (vlabel != CTVL_NONE)
        return false;
    }
  return true;
}

static int
compare_ints_3way (int a, int b)
{
  return a < b ? -1 : a > b;
}

static int
ctables_cell_compare_leaf_3way (const void *a_, const void *b_,
                                const void *aux UNUSED)
{
  struct ctables_cell *const *ap = a_;
  struct ctables_cell *const *bp = b_;
  const struct ctables_cell *a = *ap;
  const struct ctables_cell *b = *bp;

  if (a == b)
    {
      assert (a_ == b_);
      return 0;
    }

  for (enum pivot_axis_type axis = 0; axis < PIVOT_N_AXES; axis++)
    {
      int cmp = compare_ints_3way (a->axes[axis].leaf, b->axes[axis].leaf);
      if (cmp)
        return cmp;
    }

  const struct ctables_value *a_ctv = ctables_value_find (a);
  const struct ctables_value *b_ctv = ctables_value_find (b);
  if (a_ctv && b_ctv)
    {
      int cmp = compare_ints_3way (a_ctv->leaf, b_ctv->leaf);
      if (cmp)
        return cmp;
    }
  else
    assert (!a_ctv && !b_ctv);
  return 0;
}

static void
ctables_table_output (struct ctables *ct, struct ctables_table *t)
{
  struct pivot_table *pt = pivot_table_create__ (
    (t->title
     ? pivot_value_new_user_text (t->title, SIZE_MAX)
     : pivot_value_new_text (N_("Custom Tables"))),
    "Custom Tables");
  if (t->caption)
    pivot_table_set_caption (
      pt, pivot_value_new_user_text (t->caption, SIZE_MAX));
  if (t->corner)
    pivot_table_set_corner_text (
      pt, pivot_value_new_user_text (t->corner, SIZE_MAX));

  bool summary_dimension = (t->summary_axis != t->slabels_axis
                            || (!t->slabels_visible
                                && t->summary_specs.n > 1));
  if (summary_dimension)
    {
      struct pivot_dimension *d = pivot_dimension_create (
        pt, t->slabels_axis, N_("Statistics"));
      const struct ctables_summary_spec_set *specs = &t->summary_specs;
      if (!t->slabels_visible)
        d->hide_all_labels = true;
      for (size_t i = 0; i < specs->n; i++)
        pivot_category_create_leaf (
          d->root, ctables_summary_label (&specs->specs[i], t->cilevel));
    }

  bool categories_dimension = t->clabels_example != NULL;
  if (categories_dimension)
    {
      struct pivot_dimension *d = pivot_dimension_create (
        pt, t->label_axis[t->clabels_from_axis],
        t->clabels_from_axis == PIVOT_AXIS_ROW
        ? N_("Row Categories")
        : N_("Column Categories"));
      const struct variable *var = t->clabels_example;
      const struct ctables_categories *c = t->categories[var_get_dict_index (var)];
      for (size_t i = 0; i < t->n_clabels_values; i++)
        {
          const struct ctables_value *value = t->clabels_values[i];
          const struct ctables_category *cat = ctables_categories_match (c, &value->value, var);
          assert (cat != NULL);
          pivot_category_create_leaf (
            d->root, ctables_category_create_value_label (c, cat,
                                                          t->clabels_example,
                                                          &value->value));
        }
    }

  pivot_table_set_look (pt, ct->look);
  struct pivot_dimension *d[PIVOT_N_AXES];
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      static const char *names[] = {
        [PIVOT_AXIS_ROW] = N_("Rows"),
        [PIVOT_AXIS_COLUMN] = N_("Columns"),
        [PIVOT_AXIS_LAYER] = N_("Layers"),
      };
      d[a] = (t->axes[a] || a == t->summary_axis
              ? pivot_dimension_create (pt, a, names[a])
              : NULL);
      if (!d[a])
        continue;

      assert (t->axes[a]);

      for (size_t i = 0; i < t->stacks[a].n; i++)
        {
          struct ctables_nest *nest = &t->stacks[a].nests[i];
          struct ctables_section **sections = xnmalloc (t->n_sections,
                                                        sizeof *sections);
          size_t n_sections = 0;

          size_t n_total_cells = 0;
          size_t max_depth = 0;
          for (size_t j = 0; j < t->n_sections; j++)
            if (t->sections[j].nests[a] == nest)
              {
                struct ctables_section *s = &t->sections[j];
                sections[n_sections++] = s;
                n_total_cells += hmap_count (&s->cells);

                size_t depth = s->nests[a]->n;
                max_depth = MAX (depth, max_depth);
              }

          struct ctables_cell **sorted = xnmalloc (n_total_cells,
                                                   sizeof *sorted);
          size_t n_sorted = 0;

          for (size_t j = 0; j < n_sections; j++)
            {
              struct ctables_section *s = sections[j];

              struct ctables_cell *cell;
              HMAP_FOR_EACH (cell, struct ctables_cell, node, &s->cells)
                if (!cell->hide)
                  sorted[n_sorted++] = cell;
              assert (n_sorted <= n_total_cells);
            }

          struct ctables_cell_sort_aux aux = { .nest = nest, .a = a };
          sort (sorted, n_sorted, sizeof *sorted, ctables_cell_compare_3way, &aux);

          struct ctables_level
            {
              enum ctables_level_type
                {
                  CTL_VAR,          /* Variable label for nest->vars[var_idx]. */
                  CTL_CATEGORY,     /* Category for nest->vars[var_idx]. */
                  CTL_SUMMARY,      /* Summary functions. */
                }
                type;

              enum settings_value_show vlabel; /* CTL_VAR only. */
              size_t var_idx;
            };
          struct ctables_level *levels = xnmalloc (1 + 2 * max_depth, sizeof *levels);
          size_t n_levels = 0;
          for (size_t k = 0; k < nest->n; k++)
            {
              enum ctables_vlabel vlabel = ct->vlabels[var_get_dict_index (nest->vars[k])];
              if (vlabel == CTVL_NONE
                  && (nest->scale_idx == k
                      || (
                        /* There's a single nesting level on this axis and the
                           labels are moved to a different axis.  We need to
                           have something to stick into the dimension.  It's
                           hard to see what that should be, so just force a
                           variable name to be shown. */
                        nest->n == 1 && t->label_axis[a] != a)))
                vlabel = CTVL_NAME;
              if (vlabel != CTVL_NONE)
                {
                  levels[n_levels++] = (struct ctables_level) {
                    .type = CTL_VAR,
                    .vlabel = (enum settings_value_show) vlabel,
                    .var_idx = k,
                  };
                }

              if (nest->scale_idx != k
                  && (k != nest->n - 1 || t->label_axis[a] == a))
                {
                  levels[n_levels++] = (struct ctables_level) {
                    .type = CTL_CATEGORY,
                    .var_idx = k,
                  };
                }
            }

          if (!summary_dimension && a == t->slabels_axis)
            {
              levels[n_levels++] = (struct ctables_level) {
                .type = CTL_SUMMARY,
                .var_idx = SIZE_MAX,
              };
            }

          /* Pivot categories:

             - variable label for nest->vars[0], if vlabel != CTVL_NONE
             - category for nest->vars[0], if nest->scale_idx != 0
             - variable label for nest->vars[1], if vlabel != CTVL_NONE
             - category for nest->vars[1], if nest->scale_idx != 1
             ...
             - variable label for nest->vars[n - 1], if vlabel != CTVL_NONE
             - category for nest->vars[n - 1], if t->label_axis[a] == a && nest->scale_idx != n - 1.
             - summary function, if 'a == t->slabels_axis && a ==
             t->summary_axis'.

             Additional dimensions:

             - If 'a == t->slabels_axis && a != t->summary_axis', add a summary
             dimension.
             - If 't->label_axis[b] == a' for some 'b != a', add a category
             dimension to 'a'.
          */


          struct pivot_category **groups = xnmalloc (1 + 2 * max_depth, sizeof *groups);
          int prev_leaf = 0;
          for (size_t j = 0; j < n_sorted; j++)
            {
              struct ctables_cell *cell = sorted[j];
              struct ctables_cell *prev = j > 0 ? sorted[j - 1] : NULL;

              size_t n_common = 0;
              if (j > 0)
                {
                  for (; n_common < n_levels; n_common++)
                    {
                      const struct ctables_level *level = &levels[n_common];
                      if (level->type == CTL_CATEGORY)
                        {
                          size_t var_idx = level->var_idx;
                          const struct ctables_category *c = cell->axes[a].cvs[var_idx].category;
                          if (prev->axes[a].cvs[var_idx].category != c)
                            break;
                          else if (c->type != CCT_SUBTOTAL
                                   && c->type != CCT_TOTAL
                                   && c->type != CCT_POSTCOMPUTE
                                   && !value_equal (&prev->axes[a].cvs[var_idx].value,
                                                    &cell->axes[a].cvs[var_idx].value,
                                                    var_get_width (nest->vars[var_idx])))
                            break;
                        }
                    }
                }

              for (size_t k = n_common; k < n_levels; k++)
                {
                  const struct ctables_level *level = &levels[k];
                  struct pivot_category *parent = k ? groups[k - 1] : d[a]->root;
                  if (level->type == CTL_SUMMARY)
                    {
                      assert (k == n_levels - 1);

                      const struct ctables_summary_spec_set *specs = &t->summary_specs;
                      for (size_t m = 0; m < specs->n; m++)
                        {
                          int leaf = pivot_category_create_leaf (
                            parent, ctables_summary_label (&specs->specs[m],
                                                           t->cilevel));
                          if (!m)
                            prev_leaf = leaf;
                        }
                    }
                  else
                    {
                      const struct variable *var = nest->vars[level->var_idx];
                      struct pivot_value *label;
                      if (level->type == CTL_VAR)
                        {
                          label = pivot_value_new_variable (var);
                          label->variable.show = level->vlabel;
                        }
                      else if (level->type == CTL_CATEGORY)
                        {
                          const struct ctables_cell_value *cv = &cell->axes[a].cvs[level->var_idx];
                          label = ctables_category_create_value_label (
                            t->categories[var_get_dict_index (var)],
                            cv->category, var, &cv->value);
                        }
                      else
                        NOT_REACHED ();

                      if (k == n_levels - 1)
                        prev_leaf = pivot_category_create_leaf (parent, label);
                      else
                        groups[k] = pivot_category_create_group__ (parent, label);
                    }
                }

              cell->axes[a].leaf = prev_leaf;
            }
          free (sorted);
          free (groups);
          free (levels);
          free (sections);

        }

      d[a]->hide_all_labels = all_hidden_vlabels (t, a);
    }

  {
    size_t n_total_cells = 0;
    for (size_t j = 0; j < t->n_sections; j++)
      n_total_cells += hmap_count (&t->sections[j].cells);

    struct ctables_cell **sorted = xnmalloc (n_total_cells, sizeof *sorted);
    size_t n_sorted = 0;
    for (size_t j = 0; j < t->n_sections; j++)
      {
        const struct ctables_section *s = &t->sections[j];
        struct ctables_cell *cell;
        HMAP_FOR_EACH (cell, struct ctables_cell, node, &s->cells)
          if (!cell->hide)
            sorted[n_sorted++] = cell;
      }
    assert (n_sorted <= n_total_cells);
    sort (sorted, n_sorted, sizeof *sorted, ctables_cell_compare_leaf_3way,
          NULL);
    size_t ids[N_CTATS];
    memset (ids, 0, sizeof ids);
    for (size_t j = 0; j < n_sorted; j++)
      {
        struct ctables_cell *cell = sorted[j];
        for (enum ctables_area_type at = 0; at < N_CTATS; at++)
          {
            struct ctables_area *area = cell->areas[at];
            if (!area->sequence)
              area->sequence = ++ids[at];
          }
      }

    free (sorted);
  }

  for (size_t i = 0; i < t->n_sections; i++)
    {
      struct ctables_section *s = &t->sections[i];

      struct ctables_cell *cell;
      HMAP_FOR_EACH (cell, struct ctables_cell, node, &s->cells)
        {
          if (cell->hide)
            continue;

          const struct ctables_value *ctv = ctables_value_find (cell);
          const struct ctables_nest *specs_nest = s->nests[t->summary_axis];
          const struct ctables_summary_spec_set *specs = &specs_nest->specs[cell->sv];
          for (size_t j = 0; j < specs->n; j++)
            {
              size_t dindexes[5];
              size_t n_dindexes = 0;

              if (summary_dimension)
                dindexes[n_dindexes++] = specs->specs[j].axis_idx;

              if (ctv)
                dindexes[n_dindexes++] = ctv->leaf;

              for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
                if (d[a])
                  {
                    int leaf = cell->axes[a].leaf;
                    if (a == t->summary_axis && !summary_dimension)
                      leaf += specs->specs[j].axis_idx;
                    dindexes[n_dindexes++] = leaf;
                  }

              const struct ctables_summary_spec *ss = &specs->specs[j];

              struct fmt_spec format = specs->specs[j].format;
              bool is_ctables_format = ss->is_ctables_format;
              double d = (cell->postcompute
                          ? ctables_cell_calculate_postcompute (
                            s, cell, ss, &format, &is_ctables_format, j)
                          : ctables_summary_value (cell->areas,
                                                   &cell->summaries[j], ss));

              struct pivot_value *value;
              if (ct->hide_threshold != 0
                  && d < ct->hide_threshold
                  && ss->function == CTSF_COUNT)
                {
                  value = pivot_value_new_user_text_nocopy (
                    xasprintf ("<%d", ct->hide_threshold));
                }
              else if (d == 0 && ct->zero)
                value = pivot_value_new_user_text (ct->zero, SIZE_MAX);
              else if (d == SYSMIS && ct->missing)
                value = pivot_value_new_user_text (ct->missing, SIZE_MAX);
              else if (is_ctables_format)
                value = pivot_value_new_user_text_nocopy (
                  ctables_format (d, format, &ct->ctables_formats));
              else
                {
                  value = pivot_value_new_number (d);
                  value->numeric.format = format;
                }
              /* XXX should text values be right-justified? */
              pivot_table_put (pt, dindexes, n_dindexes, value);
            }
        }
    }

  pivot_table_submit (pt);
}

static bool
ctables_check_label_position (struct ctables_table *t, struct lexer *lexer,
                              enum pivot_axis_type a)
{
  enum pivot_axis_type label_pos = t->label_axis[a];
  if (label_pos == a)
    return true;

  const struct ctables_stack *stack = &t->stacks[a];
  if (!stack->n)
    return true;

  const struct ctables_nest *n0 = &stack->nests[0];
  if (n0->n == 0)
    {
      assert (stack->n == 1);
      return true;
    }

  const struct variable *v0 = n0->vars[n0->n - 1];
  struct ctables_categories *c0 = t->categories[var_get_dict_index (v0)];
  t->clabels_example = v0;

  for (size_t i = 0; i < c0->n_cats; i++)
    if (c0->cats[i].type == CCT_FUNCTION)
      {
        msg (SE, _("Category labels may not be moved to another axis when "
                   "sorting by a summary function."));
        lex_ofs_msg (lexer, SN, t->clabels_start_ofs, t->clabels_end_ofs,
                     _("This syntax moves category labels to another axis."));
        msg_at (SN, c0->cats[i].location,
                _("This syntax requests sorting by a summary function."));
        return false;
      }

  for (size_t i = 0; i < stack->n; i++)
    {
      const struct ctables_nest *ni = &stack->nests[i];
      assert (ni->n > 0);
      const struct variable *vi = ni->vars[ni->n - 1];
      if (n0->n - 1 == ni->scale_idx)
        {
          msg (SE, _("To move category labels from one axis to another, "
                     "the variables whose labels are to be moved must be "
                     "categorical, but %s is scale."), var_get_name (vi));
          lex_ofs_msg (lexer, SN, t->clabels_start_ofs, t->clabels_end_ofs,
                       _("This syntax moves category labels to another axis."));
          return false;
        }
    }

  for (size_t i = 1; i < stack->n; i++)
    {
      const struct ctables_nest *ni = &stack->nests[i];
      assert (ni->n > 0);
      const struct variable *vi = ni->vars[ni->n - 1];
      struct ctables_categories *ci = t->categories[var_get_dict_index (vi)];

      if (var_get_width (v0) != var_get_width (vi))
        {
          msg (SE, _("To move category labels from one axis to another, "
                     "the variables whose labels are to be moved must all "
                     "have the same width, but %s has width %d and %s has "
                     "width %d."),
               var_get_name (v0), var_get_width (v0),
               var_get_name (vi), var_get_width (vi));
          lex_ofs_msg (lexer, SN, t->clabels_start_ofs, t->clabels_end_ofs,
                       _("This syntax moves category labels to another axis."));
          return false;
        }
      if (!val_labs_equal (var_get_value_labels (v0),
                           var_get_value_labels (vi)))
        {
          msg (SE, _("To move category labels from one axis to another, "
                     "the variables whose labels are to be moved must all "
                     "have the same value labels, but %s and %s have "
                     "different value labels."),
               var_get_name (v0), var_get_name (vi));
          lex_ofs_msg (lexer, SN, t->clabels_start_ofs, t->clabels_end_ofs,
                       _("This syntax moves category labels to another axis."));
          return false;
        }
      if (!ctables_categories_equal (c0, ci))
        {
          msg (SE, _("To move category labels from one axis to another, "
                     "the variables whose labels are to be moved must all "
                     "have the same category specifications, but %s and %s "
                     "have different category specifications."),
               var_get_name (v0), var_get_name (vi));
          lex_ofs_msg (lexer, SN, t->clabels_start_ofs, t->clabels_end_ofs,
                       _("This syntax moves category labels to another axis."));
          return false;
        }
    }

  return true;
}

static size_t
add_sum_var (struct variable *var,
             struct variable ***sum_vars, size_t *n, size_t *allocated)
{
  for (size_t i = 0; i < *n; i++)
    if (var == (*sum_vars)[i])
      return i;

  if (*n >= *allocated)
    *sum_vars = x2nrealloc (*sum_vars, allocated, sizeof **sum_vars);
  (*sum_vars)[*n] = var;
  return (*n)++;
}

static enum ctables_area_type
rotate_area (enum ctables_area_type area)
{
  return area;
  switch (area)
    {
    case CTAT_TABLE:
    case CTAT_LAYER:
    case CTAT_SUBTABLE:
      return area;

    case CTAT_LAYERROW:
      return CTAT_LAYERCOL;

    case CTAT_LAYERCOL:
      return CTAT_LAYERROW;

    case CTAT_ROW:
      return CTAT_COL;

    case CTAT_COL:
      return CTAT_ROW;
    }

  NOT_REACHED ();
}

static void
enumerate_sum_vars (const struct ctables_axis *a,
                    struct variable ***sum_vars, size_t *n, size_t *allocated)
{
  if (!a)
    return;

  switch (a->op)
    {
    case CTAO_VAR:
      for (size_t i = 0; i < N_CSVS; i++)
        for (size_t j = 0; j < a->specs[i].n; j++)
          {
            struct ctables_summary_spec *spec = &a->specs[i].specs[j];
            if (spec->function == CTSF_areaPCT_SUM)
              spec->sum_var_idx = add_sum_var (a->var, sum_vars, n, allocated);
          }
      break;

    case CTAO_STACK:
    case CTAO_NEST:
      for (size_t i = 0; i < 2; i++)
        enumerate_sum_vars (a->subs[i], sum_vars, n, allocated);
      break;
    }
}

static bool
ctables_prepare_table (struct ctables_table *t, struct lexer *lexer)
{
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    if (t->axes[a])
      {
        t->stacks[a] = enumerate_fts (a, t->axes[a]);

        for (size_t j = 0; j < t->stacks[a].n; j++)
          {
            struct ctables_nest *nest = &t->stacks[a].nests[j];
            for (enum ctables_area_type at = 0; at < N_CTATS; at++)
              {
                nest->areas[at] = xmalloc (nest->n * sizeof *nest->areas[at]);
                nest->n_areas[at] = 0;

                enum pivot_axis_type ata, atb;
                if (at == CTAT_ROW || at == CTAT_LAYERROW)
                  {
                    ata = PIVOT_AXIS_ROW;
                    atb = PIVOT_AXIS_COLUMN;
                  }
                else /* at == CTAT_COL || at == CTAT_LAYERCOL */
                  {
                    ata = PIVOT_AXIS_COLUMN;
                    atb = PIVOT_AXIS_ROW;
                  }

                if (at == CTAT_LAYER
                    ? a != PIVOT_AXIS_LAYER && t->label_axis[a] == PIVOT_AXIS_LAYER
                    : at == CTAT_LAYERCOL || at == CTAT_LAYERROW
                    ? a == atb && t->label_axis[a] != a
                    : false)
                  {
                    for (size_t k = nest->n - 1; k < nest->n; k--)
                      if (k != nest->scale_idx)
                        {
                          nest->areas[at][nest->n_areas[at]++] = k;
                          break;
                        }
                    continue;
                  }

                if (at == CTAT_LAYER ? a != PIVOT_AXIS_LAYER
                    : at == CTAT_LAYERROW || at == CTAT_LAYERCOL ? a == atb
                    : at == CTAT_TABLE ? true
                    : false)
                  continue;

                for (size_t k = 0; k < nest->n; k++)
                  if (k != nest->scale_idx)
                    nest->areas[at][nest->n_areas[at]++] = k;

                int n_drop;
                switch (at)
                  {
                  case CTAT_SUBTABLE:
#define L PIVOT_AXIS_LAYER
                    n_drop = (t->clabels_from_axis == L ? a != L
                              : t->clabels_to_axis == L ? (t->clabels_from_axis == a ? -1 : a != L)
                              : t->clabels_from_axis == a ? 2
                              : 0);
#undef L
                    break;

                  case CTAT_LAYERROW:
                  case CTAT_LAYERCOL:
                    n_drop = a == ata && t->label_axis[ata] == atb;
                    break;

                  case CTAT_ROW:
                  case CTAT_COL:
                    n_drop = (a == ata ? t->label_axis[ata] == atb
                              : a != atb ? 0
                              : t->clabels_from_axis == atb ? -1
                              : t->clabels_to_axis != atb ? 1
                              : 0);
                    break;

                  case CTAT_LAYER:
                  case CTAT_TABLE:
                    n_drop = 0;
                    break;
                  }

                if (n_drop < 0)
                  {
                    size_t n = nest->n_areas[at];
                    if (n > 1)
                      {
                        nest->areas[at][n - 2] = nest->areas[at][n - 1];
                        nest->n_areas[at]--;
                      }
                  }
                else
                  {
                    for (int i = 0; i < n_drop; i++)
                      if (nest->n_areas[at] > 0)
                        nest->n_areas[at]--;
                  }
              }
          }
      }
    else
      {
        struct ctables_nest *nest = xmalloc (sizeof *nest);
        *nest = (struct ctables_nest) {
          .n = 0,
          .scale_idx = SIZE_MAX,
          .summary_idx = SIZE_MAX
        };
        t->stacks[a] = (struct ctables_stack) { .nests = nest, .n = 1 };

        /* There's no point in moving labels away from an axis that has no
           labels, so avoid dealing with the special cases around that. */
        t->label_axis[a] = a;
      }

  struct ctables_stack *stack = &t->stacks[t->summary_axis];
  for (size_t i = 0; i < stack->n; i++)
    {
      struct ctables_nest *nest = &stack->nests[i];
      if (!nest->specs[CSV_CELL].n)
        {
          struct ctables_summary_spec_set *ss = &nest->specs[CSV_CELL];
          ss->specs = xmalloc (sizeof *ss->specs);
          ss->n = 1;

          enum ctables_summary_function function
            = ss->is_scale ? CTSF_MEAN : CTSF_COUNT;

          if (!ss->var)
            {
              nest->summary_idx = nest->n - 1;
              ss->var = nest->vars[nest->summary_idx];
            }
          *ss->specs = (struct ctables_summary_spec) {
            .function = function,
            .weighting = ss->is_scale ? CTW_EFFECTIVE : CTW_DICTIONARY,
            .format = ctables_summary_default_format (function, ss->var),
          };

          ctables_summary_spec_set_clone (&nest->specs[CSV_TOTAL],
                                          &nest->specs[CSV_CELL]);
        }
      else if (!nest->specs[CSV_TOTAL].n)
        ctables_summary_spec_set_clone (&nest->specs[CSV_TOTAL],
                                        &nest->specs[CSV_CELL]);

      if (t->label_axis[PIVOT_AXIS_ROW] == PIVOT_AXIS_COLUMN
          || t->label_axis[PIVOT_AXIS_COLUMN] == PIVOT_AXIS_ROW)
        {
          for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
            for (size_t i = 0; i < nest->specs[sv].n; i++)
              {
                struct ctables_summary_spec *ss = &nest->specs[sv].specs[i];
                const struct ctables_function_info *cfi =
                  &ctables_function_info[ss->function];
                if (cfi->is_area)
                  ss->calc_area = rotate_area (ss->calc_area);
              }
        }

      if (t->ctables->smissing_listwise)
        {
          struct variable **listwise_vars = NULL;
          size_t n = 0;
          size_t allocated = 0;

          for (size_t j = nest->group_head; j < stack->n; j++)
            {
              const struct ctables_nest *other_nest = &stack->nests[j];
              if (other_nest->group_head != nest->group_head)
                break;

              if (nest != other_nest && other_nest->scale_idx < other_nest->n)
                {
                  if (n >= allocated)
                    listwise_vars = x2nrealloc (listwise_vars, &allocated,
                                                sizeof *listwise_vars);
                  listwise_vars[n++] = other_nest->vars[other_nest->scale_idx];
                }
            }
          for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
            {
              if (sv > 0)
                listwise_vars = xmemdup (listwise_vars,
                                         n * sizeof *listwise_vars);
              nest->specs[sv].listwise_vars = listwise_vars;
              nest->specs[sv].n_listwise_vars = n;
            }
        }
    }

  struct ctables_summary_spec_set *merged = &t->summary_specs;
  struct merge_item *items = xnmalloc (N_CSVS * stack->n, sizeof *items);
  size_t n_left = 0;
  for (size_t j = 0; j < stack->n; j++)
    {
      const struct ctables_nest *nest = &stack->nests[j];
      if (nest->n)
        for (enum ctables_summary_variant sv = 0; sv < N_CSVS; sv++)
          items[n_left++] = (struct merge_item) { .set = &nest->specs[sv] };
    }

  while (n_left > 0)
    {
      struct merge_item min = items[0];
      for (size_t j = 1; j < n_left; j++)
        if (merge_item_compare_3way (&items[j], &min) < 0)
          min = items[j];

      if (merged->n >= merged->allocated)
        merged->specs = x2nrealloc (merged->specs, &merged->allocated,
                                    sizeof *merged->specs);
      merged->specs[merged->n++] = min.set->specs[min.ofs];

      for (size_t j = 0; j < n_left; )
        {
          if (merge_item_compare_3way (&items[j], &min) == 0)
            {
              struct merge_item *item = &items[j];
              item->set->specs[item->ofs++].axis_idx = merged->n - 1;
              if (item->ofs >= item->set->n)
                {
                  items[j] = items[--n_left];
                  continue;
                }
            }
          j++;
        }
    }
  free (items);

  size_t allocated_sum_vars = 0;
  enumerate_sum_vars (t->axes[t->summary_axis],
                      &t->sum_vars, &t->n_sum_vars, &allocated_sum_vars);

  return (ctables_check_label_position (t, lexer, PIVOT_AXIS_ROW)
          && ctables_check_label_position (t, lexer, PIVOT_AXIS_COLUMN));
}

static void
ctables_insert_clabels_values (struct ctables_table *t, const struct ccase *c,
                               enum pivot_axis_type a)
{
  struct ctables_stack *stack = &t->stacks[a];
  for (size_t i = 0; i < stack->n; i++)
    {
      const struct ctables_nest *nest = &stack->nests[i];
      const struct variable *var = nest->vars[nest->n - 1];
      const union value *value = case_data (c, var);

      if (var_is_numeric (var) && value->f == SYSMIS)
        continue;

      if (ctables_categories_match (t->categories [var_get_dict_index (var)],
                                    value, var))
        ctables_value_insert (t, value, var_get_width (var));
    }
}

static void
ctables_add_category_occurrences (const struct variable *var,
                                  struct hmap *occurrences,
                                  const struct ctables_categories *cats)
{
  const struct val_labs *val_labs = var_get_value_labels (var);

  for (size_t i = 0; i < cats->n_cats; i++)
    {
      const struct ctables_category *c = &cats->cats[i];
      switch (c->type)
        {
        case CCT_NUMBER:
          ctables_add_occurrence (var, &(const union value) { .f = c->number },
                                  occurrences);
          break;

        case CCT_STRING:
          {
            int width = var_get_width (var);
            union value value;
            value_init (&value, width);
            value_copy_buf_rpad (&value, width,
                                 CHAR_CAST (uint8_t *, c->string.string),
                                 c->string.length, ' ');
            ctables_add_occurrence (var, &value, occurrences);
            value_destroy (&value, width);
          }
          break;

        case CCT_NRANGE:
          assert (var_is_numeric (var));
          for (const struct val_lab *vl = val_labs_first (val_labs); vl;
               vl = val_labs_next (val_labs, vl))
            if (vl->value.f >= c->nrange[0] && vl->value.f <= c->nrange[1])
              ctables_add_occurrence (var, &vl->value, occurrences);
          break;

        case CCT_SRANGE:
          assert (var_is_alpha (var));
          for (const struct val_lab *vl = val_labs_first (val_labs); vl;
               vl = val_labs_next (val_labs, vl))
            if (in_string_range (&vl->value, var, c->srange))
              ctables_add_occurrence (var, &vl->value, occurrences);
          break;

        case CCT_MISSING:
          for (const struct val_lab *vl = val_labs_first (val_labs); vl;
               vl = val_labs_next (val_labs, vl))
            if (var_is_value_missing (var, &vl->value))
              ctables_add_occurrence (var, &vl->value, occurrences);
          break;

        case CCT_OTHERNM:
          for (const struct val_lab *vl = val_labs_first (val_labs); vl;
               vl = val_labs_next (val_labs, vl))
            ctables_add_occurrence (var, &vl->value, occurrences);
          break;

        case CCT_POSTCOMPUTE:
          break;

        case CCT_SUBTOTAL:
        case CCT_TOTAL:
          break;

        case CCT_VALUE:
        case CCT_LABEL:
        case CCT_FUNCTION:
          for (const struct val_lab *vl = val_labs_first (val_labs); vl;
               vl = val_labs_next (val_labs, vl))
            if (c->include_missing || !var_is_value_missing (var, &vl->value))
              ctables_add_occurrence (var, &vl->value, occurrences);
          break;

        case CCT_EXCLUDED_MISSING:
          break;
        }
    }
}

static void
ctables_section_recurse_add_empty_categories (
  struct ctables_section *s,
  const struct ctables_category **cats[PIVOT_N_AXES], struct ccase *c,
  enum pivot_axis_type a, size_t a_idx, bool add)
{
  if (a >= PIVOT_N_AXES)
    {
      if (add)
        ctables_cell_insert__ (s, c, cats);
    }
  else if (!s->nests[a] || a_idx >= s->nests[a]->n)
    ctables_section_recurse_add_empty_categories (s, cats, c, a + 1, 0, add);
  else
    {
      const struct variable *var = s->nests[a]->vars[a_idx];
      size_t idx = var_get_dict_index (var);
      bool show_empty = s->table->show_empty[idx];
      if (show_empty)
        add = true;

      const struct ctables_categories *categories = s->table->categories[idx];
      int width = var_get_width (var);
      const struct hmap *occurrences = &s->occurrences[a][a_idx];
      const struct ctables_occurrence *o;
      HMAP_FOR_EACH (o, struct ctables_occurrence, node, occurrences)
        {
          union value *value = case_data_rw (c, var);
          value_destroy (value, width);
          value_clone (value, &o->value, width);
          cats[a][a_idx] = ctables_categories_match (categories, value, var);
          assert (cats[a][a_idx] != NULL);
          ctables_section_recurse_add_empty_categories (s, cats, c,
                                                        a, a_idx + 1, add);
        }

      for (size_t i = 0; i < categories->n_cats; i++)
        {
          const struct ctables_category *cat = &categories->cats[i];
          if (cat->type == CCT_POSTCOMPUTE
              || (show_empty && cat->type == CCT_SUBTOTAL))
            {
              cats[a][a_idx] = cat;
              ctables_section_recurse_add_empty_categories (s, cats, c,
                                                            a, a_idx + 1, true);
            }
        }
    }
}

static void
ctables_section_add_empty_categories (struct ctables_section *s)
{
  for (size_t a = 0; a < PIVOT_N_AXES; a++)
    if (s->nests[a])
      for (size_t k = 0; k < s->nests[a]->n; k++)
        if (k != s->nests[a]->scale_idx)
          {
            const struct variable *var = s->nests[a]->vars[k];
            size_t idx = var_get_dict_index (var);
            const struct ctables_categories *cats = s->table->categories[idx];
            if (s->table->show_empty[idx])
              ctables_add_category_occurrences (var, &s->occurrences[a][k], cats);
          }

  const struct ctables_category *layer_cats[s->nests[PIVOT_AXIS_LAYER]->n];
  const struct ctables_category *row_cats[s->nests[PIVOT_AXIS_ROW]->n];
  const struct ctables_category *column_cats[s->nests[PIVOT_AXIS_COLUMN]->n];
  const struct ctables_category **cats[PIVOT_N_AXES] =
    {
      [PIVOT_AXIS_LAYER] = layer_cats,
      [PIVOT_AXIS_ROW] = row_cats,
      [PIVOT_AXIS_COLUMN] = column_cats,
    };
  struct ccase *c = case_create (dict_get_proto (s->table->ctables->dict));
  ctables_section_recurse_add_empty_categories (s, cats, c, 0, 0, false);
  case_unref (c);
}

static void
ctables_section_clear (struct ctables_section *s)
{
  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      const struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n; i++)
        if (i != nest->scale_idx)
          {
            const struct variable *var = nest->vars[i];
            int width = var_get_width (var);
            struct ctables_occurrence *o, *next;
            struct hmap *map = &s->occurrences[a][i];
            HMAP_FOR_EACH_SAFE (o, next, struct ctables_occurrence, node, map)
              {
                value_destroy (&o->value, width);
                hmap_delete (map, &o->node);
                free (o);
              }
            hmap_shrink (map);
          }
    }

  struct ctables_cell *cell, *next_cell;
  HMAP_FOR_EACH_SAFE (cell, next_cell, struct ctables_cell, node, &s->cells)
    {
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          const struct ctables_nest *nest = s->nests[a];
          for (size_t i = 0; i < nest->n; i++)
            if (i != nest->scale_idx)
              value_destroy (&cell->axes[a].cvs[i].value,
                             var_get_width (nest->vars[i]));
          free (cell->axes[a].cvs);
        }

      const struct ctables_nest *ss = s->nests[s->table->summary_axis];
      const struct ctables_summary_spec_set *specs = &ss->specs[cell->sv];
      for (size_t i = 0; i < specs->n; i++)
        ctables_summary_uninit (&cell->summaries[i], &specs->specs[i]);
      free (cell->summaries);

      hmap_delete (&s->cells, &cell->node);
      free (cell);
    }
  hmap_shrink (&s->cells);

  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    {
      struct ctables_area *area, *next_area;
      HMAP_FOR_EACH_SAFE (area, next_area, struct ctables_area, node,
                          &s->areas[at])
        {
          free (area->sums);
          hmap_delete (&s->areas[at], &area->node);
          free (area);
        }
      hmap_shrink (&s->areas[at]);
    }
}

static void
ctables_section_uninit (struct ctables_section *s)
{
  ctables_section_clear (s);

  for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
    {
      struct ctables_nest *nest = s->nests[a];
      for (size_t i = 0; i < nest->n; i++)
        hmap_destroy (&s->occurrences[a][i]);
      free (s->occurrences[a]);
    }

  hmap_destroy (&s->cells);
  for (enum ctables_area_type at = 0; at < N_CTATS; at++)
    hmap_destroy (&s->areas[at]);
}

static void
ctables_table_clear (struct ctables_table *t)
{
  for (size_t i = 0; i < t->n_sections; i++)
    ctables_section_clear (&t->sections[i]);

  if (t->clabels_example)
    {
      int width = var_get_width (t->clabels_example);
      struct ctables_value *value, *next_value;
      HMAP_FOR_EACH_SAFE (value, next_value, struct ctables_value, node,
                          &t->clabels_values_map)
        {
          value_destroy (&value->value, width);
          hmap_delete (&t->clabels_values_map, &value->node);
          free (value);
        }
      hmap_shrink (&t->clabels_values_map);

      free (t->clabels_values);
      t->clabels_values = NULL;
      t->n_clabels_values = 0;
    }
}

static bool
ctables_execute (struct dataset *ds, struct casereader *input,
                 struct ctables *ct)
{
  for (size_t i = 0; i < ct->n_tables; i++)
    {
      struct ctables_table *t = ct->tables[i];
      t->sections = xnmalloc (MAX (1, t->stacks[PIVOT_AXIS_ROW].n) *
                              MAX (1, t->stacks[PIVOT_AXIS_COLUMN].n) *
                              MAX (1, t->stacks[PIVOT_AXIS_LAYER].n),
                              sizeof *t->sections);
      size_t ix[PIVOT_N_AXES];
      ctables_table_add_section (t, 0, ix);
    }

  struct dictionary *dict = dataset_dict (ds);

  bool splitting = dict_get_split_type (dict) == SPLIT_SEPARATE;
  struct casegrouper *grouper
    = (splitting
       ? casegrouper_create_splits (input, dict)
       : casegrouper_create_vars (input, NULL, 0));
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      if (splitting)
        output_split_file_values_peek (ds, group);

      bool warn_on_invalid = true;
      for (struct ccase *c = casereader_read (group); c;
           case_unref (c), c = casereader_read (group))
        {
          double d_weight = dict_get_rounded_case_weight (dict, c, &warn_on_invalid);
          double e_weight = (ct->e_weight
                             ? var_force_valid_weight (ct->e_weight,
                                                       case_num (c, ct->e_weight),
                                                       &warn_on_invalid)
                             : d_weight);
          double weight[] = {
            [CTW_DICTIONARY] = d_weight,
            [CTW_EFFECTIVE] = e_weight,
            [CTW_UNWEIGHTED] = 1.0,
          };

          for (size_t i = 0; i < ct->n_tables; i++)
            {
              struct ctables_table *t = ct->tables[i];

              for (size_t j = 0; j < t->n_sections; j++)
                ctables_cell_insert (&t->sections[j], c, weight);

              for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
                if (t->label_axis[a] != a)
                  ctables_insert_clabels_values (t, c, a);
            }
        }
      casereader_destroy (group);

      for (size_t i = 0; i < ct->n_tables; i++)
        {
          struct ctables_table *t = ct->tables[i];

          if (t->clabels_example)
            ctables_sort_clabels_values (t);

          for (size_t j = 0; j < t->n_sections; j++)
            ctables_section_add_empty_categories (&t->sections[j]);

          ctables_table_output (ct, t);
          ctables_table_clear (t);
        }
    }
  return casegrouper_destroy (grouper);
}

static struct ctables_postcompute *
ctables_find_postcompute (struct ctables *ct, const char *name)
{
  struct ctables_postcompute *pc;
  HMAP_FOR_EACH_WITH_HASH (pc, struct ctables_postcompute, hmap_node,
                           utf8_hash_case_string (name, 0), &ct->postcomputes)
    if (!utf8_strcasecmp (pc->name, name))
      return pc;
  return NULL;
}

static bool
ctables_parse_pcompute (struct lexer *lexer, struct dictionary *dict,
                        struct ctables *ct)
{
  int pcompute_start = lex_ofs (lexer) - 1;

  if (!lex_match (lexer, T_AND))
    {
      lex_error_expecting (lexer, "&");
      return false;
    }
  if (!lex_force_id (lexer))
    return false;

  char *name = ss_xstrdup (lex_tokss (lexer));

  lex_get (lexer);
  if (!lex_force_match_phrase (lexer, "=EXPR("))
    {
      free (name);
      return false;
    }

  int expr_start = lex_ofs (lexer);
  struct ctables_pcexpr *expr = ctables_pcexpr_parse_add (lexer, dict);
  int expr_end = lex_ofs (lexer) - 1;
  if (!expr || !lex_force_match (lexer, T_RPAREN))
    {
      ctables_pcexpr_destroy (expr);
      free (name);
      return false;
    }
  int pcompute_end = lex_ofs (lexer) - 1;

  struct msg_location *location = lex_ofs_location (lexer, pcompute_start,
                                                    pcompute_end);

  struct ctables_postcompute *pc = ctables_find_postcompute (ct, name);
  if (pc)
    {
      msg_at (SW, location, _("New definition of &%s will override the "
                              "previous definition."),
              pc->name);
      msg_at (SN, pc->location, _("This is the previous definition."));

      ctables_pcexpr_destroy (pc->expr);
      msg_location_destroy (pc->location);
      free (name);
    }
  else
    {
      pc = xmalloc (sizeof *pc);
      *pc = (struct ctables_postcompute) { .name = name };
      hmap_insert (&ct->postcomputes, &pc->hmap_node,
                   utf8_hash_case_string (pc->name, 0));
    }
  pc->expr = expr;
  pc->location = location;
  if (!pc->label)
    pc->label = lex_ofs_representation (lexer, expr_start, expr_end);
  return true;
}

static bool
ctables_parse_pproperties_format (struct lexer *lexer,
                                  struct ctables_summary_spec_set *sss)
{
  *sss = (struct ctables_summary_spec_set) { .n = 0 };

  while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH
         && !(lex_token (lexer) == T_ID
              && (lex_id_match (ss_cstr ("LABEL"), lex_tokss (lexer))
                  || lex_id_match (ss_cstr ("HIDESOURCECATS"),
                                   lex_tokss (lexer)))))
    {
      /* Parse function. */
      enum ctables_summary_function function;
      enum ctables_weighting weighting;
      enum ctables_area_type area;
      if (!parse_ctables_summary_function (lexer, &function, &weighting, &area))
        goto error;

      /* Parse percentile. */
      double percentile = 0;
      if (function == CTSF_PTILE)
        {
          if (!lex_force_num_range_closed (lexer, "PTILE", 0, 100))
            goto error;
          percentile = lex_number (lexer);
          lex_get (lexer);
        }

      /* Parse format. */
      struct fmt_spec format;
      bool is_ctables_format;
      if (!parse_ctables_format_specifier (lexer, &format, &is_ctables_format))
        goto error;

      if (sss->n >= sss->allocated)
        sss->specs = x2nrealloc (sss->specs, &sss->allocated,
                                 sizeof *sss->specs);
      sss->specs[sss->n++] = (struct ctables_summary_spec) {
        .function = function,
        .weighting = weighting,
        .calc_area = area,
        .user_area = area,
        .percentile = percentile,
        .format = format,
        .is_ctables_format = is_ctables_format,
      };
    }
  return true;

error:
  ctables_summary_spec_set_uninit (sss);
  return false;
}

static bool
ctables_parse_pproperties (struct lexer *lexer, struct ctables *ct)
{
  struct ctables_postcompute **pcs = NULL;
  size_t n_pcs = 0;
  size_t allocated_pcs = 0;

  while (lex_match (lexer, T_AND))
    {
      if (!lex_force_id (lexer))
        goto error;
      struct ctables_postcompute *pc
        = ctables_find_postcompute (ct, lex_tokcstr (lexer));
      if (!pc)
        {
          lex_error (lexer, _("Unknown computed category &%s."),
                     lex_tokcstr (lexer));
          goto error;
        }
      lex_get (lexer);

      if (n_pcs >= allocated_pcs)
        pcs = x2nrealloc (pcs, &allocated_pcs, sizeof *pcs);
      pcs[n_pcs++] = pc;
    }

  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "LABEL"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto error;

          for (size_t i = 0; i < n_pcs; i++)
            {
              free (pcs[i]->label);
              pcs[i]->label = ss_xstrdup (lex_tokss (lexer));
            }

          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);

          struct ctables_summary_spec_set sss;
          if (!ctables_parse_pproperties_format (lexer, &sss))
            goto error;

          for (size_t i = 0; i < n_pcs; i++)
            {
              if (pcs[i]->specs)
                ctables_summary_spec_set_uninit (pcs[i]->specs);
              else
                pcs[i]->specs = xmalloc (sizeof *pcs[i]->specs);
              ctables_summary_spec_set_clone (pcs[i]->specs, &sss);
            }
          ctables_summary_spec_set_uninit (&sss);
        }
      else if (lex_match_id (lexer, "HIDESOURCECATS"))
        {
          lex_match (lexer, T_EQUALS);
          bool hide_source_cats;
          if (!parse_bool (lexer, &hide_source_cats))
            goto error;
          for (size_t i = 0; i < n_pcs; i++)
            pcs[i]->hide_source_cats = hide_source_cats;
        }
      else
        {
          lex_error_expecting (lexer, "LABEL", "FORMAT", "HIDESOURCECATS");
          goto error;
        }
    }
  free (pcs);
  return true;

error:
  free (pcs);
  return false;
}

static void
put_strftime (struct string *out, time_t now, const char *format)
{
  const struct tm *tm = localtime (&now);
  char value[128];
  strftime (value, sizeof value, format, tm);
  ds_put_cstr (out, value);
}

static bool
skip_prefix (struct substring *s, struct substring prefix)
{
  if (ss_starts_with (*s, prefix))
    {
      ss_advance (s, prefix.length);
      return true;
    }
  else
    return false;
}

static void
put_table_expression (struct string *out, struct lexer *lexer,
                      struct dictionary *dict, int expr_start, int expr_end)
{
  size_t nest = 0;
  for (int ofs = expr_start; ofs < expr_end; ofs++)
    {
      const struct token *t = lex_ofs_token (lexer, ofs);
      if (t->type == T_LBRACK)
        nest++;
      else if (t->type == T_RBRACK && nest > 0)
        nest--;
      else if (nest > 0)
        {
          /* Nothing. */
        }
      else if (t->type == T_ID)
        {
          const struct variable *var
            = dict_lookup_var (dict, t->string.string);
          const char *label = var ? var_get_label (var) : NULL;
          ds_put_cstr (out, label ? label : t->string.string);
        }
      else
        {
          if (ofs != expr_start && t->type != T_RPAREN && ds_last (out) != ' ')
            ds_put_byte (out, ' ');

          char *repr = lex_ofs_representation (lexer, ofs, ofs);
          ds_put_cstr (out, repr);
          free (repr);

          if (ofs + 1 != expr_end && t->type != T_LPAREN)
            ds_put_byte (out, ' ');
        }
    }
}

static void
put_title_text (struct string *out, struct substring in, time_t now,
                struct lexer *lexer, struct dictionary *dict,
                int expr_start, int expr_end)
{
  for (;;)
    {
      size_t chunk = ss_find_byte (in, ')');
      ds_put_substring (out, ss_head (in, chunk));
      ss_advance (&in, chunk);
      if (ss_is_empty (in))
        return;

      if (skip_prefix (&in, ss_cstr (")DATE")))
        put_strftime (out, now, "%x");
      else if (skip_prefix (&in, ss_cstr (")TIME")))
        put_strftime (out, now, "%X");
      else if (skip_prefix (&in, ss_cstr (")TABLE")))
        put_table_expression (out, lexer, dict, expr_start, expr_end);
      else
        {
          ds_put_byte (out, ')');
          ss_advance (&in, 1);
        }
    }
}

int
cmd_ctables (struct lexer *lexer, struct dataset *ds)
{
  struct casereader *input = NULL;

  struct measure_guesser *mg = measure_guesser_create (ds);
  if (mg)
    {
      input = proc_open (ds);
      measure_guesser_run (mg, input);
      measure_guesser_destroy (mg);
    }

  size_t n_vars = dict_get_n_vars (dataset_dict (ds));
  enum ctables_vlabel *vlabels = xnmalloc (n_vars, sizeof *vlabels);
  enum settings_value_show tvars = settings_get_show_variables ();
  for (size_t i = 0; i < n_vars; i++)
    vlabels[i] = (enum ctables_vlabel) tvars;

  struct pivot_table_look *look = pivot_table_look_unshare (
    pivot_table_look_ref (pivot_table_look_get_default ()));

  struct ctables *ct = xmalloc (sizeof *ct);
  *ct = (struct ctables) {
    .dict = dataset_dict (ds),
    .look = look,
    .ctables_formats = FMT_SETTINGS_INIT,
    .vlabels = vlabels,
    .postcomputes = HMAP_INITIALIZER (ct->postcomputes),
  };

  time_t now = time (NULL);

  struct ctf
    {
      enum fmt_type type;
      const char *dot_string;
      const char *comma_string;
    };
  static const struct ctf ctfs[4] = {
    { CTEF_NEGPAREN, "(,,,)",   "(...)" },
    { CTEF_NEQUAL,   "-,N=,,",  "-.N=.." },
    { CTEF_PAREN,    "-,(,),",  "-.(.)." },
    { CTEF_PCTPAREN, "-,(,%),", "-.(.%)." },
  };
  bool is_dot = settings_get_fmt_settings ()->decimal == '.';
  for (size_t i = 0; i < 4; i++)
    {
      const char *s = is_dot ? ctfs[i].dot_string : ctfs[i].comma_string;
      fmt_settings_set_cc (&ct->ctables_formats, ctfs[i].type,
                           fmt_number_style_from_string (s));
    }

  if (!lex_force_match (lexer, T_SLASH))
    goto error;

  while (!lex_match_id (lexer, "TABLE"))
    {
      if (lex_match_id (lexer, "FORMAT"))
        {
          double widths[2] = { SYSMIS, SYSMIS };
          double units_per_inch = 72.0;

          int start_ofs = lex_ofs (lexer);
          while (lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "MINCOLWIDTH"))
                {
                  if (!parse_col_width (lexer, "MINCOLWIDTH", &widths[0]))
                    goto error;
                }
              else if (lex_match_id (lexer, "MAXCOLWIDTH"))
                {
                  if (!parse_col_width (lexer, "MAXCOLWIDTH", &widths[1]))
                    goto error;
                }
              else if (lex_match_id (lexer, "UNITS"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "POINTS"))
                    units_per_inch = 72.0;
                  else if (lex_match_id (lexer, "INCHES"))
                    units_per_inch = 1.0;
                  else if (lex_match_id (lexer, "CM"))
                    units_per_inch = 2.54;
                  else
                    {
                      lex_error_expecting (lexer, "POINTS", "INCHES", "CM");
                      goto error;
                    }
                }
              else if (lex_match_id (lexer, "EMPTY"))
                {
                  free (ct->zero);
                  ct->zero = NULL;

                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "ZERO"))
                    {
                      /* Nothing to do. */
                    }
                  else if (lex_match_id (lexer, "BLANK"))
                    ct->zero = xstrdup ("");
                  else if (lex_force_string (lexer))
                    {
                      ct->zero = ss_xstrdup (lex_tokss (lexer));
                      lex_get (lexer);
                    }
                  else
                    goto error;
                }
              else if (lex_match_id (lexer, "MISSING"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (!lex_force_string (lexer))
                    goto error;

                  free (ct->missing);
                  ct->missing = (strcmp (lex_tokcstr (lexer), ".")
                                 ? ss_xstrdup (lex_tokss (lexer))
                                 : NULL);
                  lex_get (lexer);
                }
              else
                {
                  lex_error_expecting (lexer, "MINCOLWIDTH", "MAXCOLWIDTH",
                                       "UNITS", "EMPTY", "MISSING");
                  goto error;
                }
            }

          if (widths[0] != SYSMIS && widths[1] != SYSMIS
              && widths[0] > widths[1])
            {
              lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                             _("MINCOLWIDTH must not be greater than "
                               "MAXCOLWIDTH."));
              goto error;
            }

          for (size_t i = 0; i < 2; i++)
            if (widths[i] != SYSMIS)
              {
                int *wr = ct->look->col_heading_width_range;
                wr[i] = widths[i] / units_per_inch * 96.0;
                if (wr[0] > wr[1])
                  wr[!i] = wr[i];
              }
        }
      else if (lex_match_id (lexer, "VLABELS"))
        {
          if (!lex_force_match_id (lexer, "VARIABLES"))
            goto error;
          lex_match (lexer, T_EQUALS);

          struct variable **vars;
          size_t n_vars;
          if (!parse_variables (lexer, dataset_dict (ds), &vars, &n_vars,
                                PV_NO_SCRATCH))
            goto error;

          if (!lex_force_match_id (lexer, "DISPLAY"))
            {
              free (vars);
              goto error;
            }
          lex_match (lexer, T_EQUALS);

          enum ctables_vlabel vlabel;
          if (lex_match_id (lexer, "DEFAULT"))
            vlabel = (enum ctables_vlabel) settings_get_show_variables ();
          else if (lex_match_id (lexer, "NAME"))
            vlabel = CTVL_NAME;
          else if (lex_match_id (lexer, "LABEL"))
            vlabel = CTVL_LABEL;
          else if (lex_match_id (lexer, "BOTH"))
            vlabel = CTVL_BOTH;
          else if (lex_match_id (lexer, "NONE"))
            vlabel = CTVL_NONE;
          else
            {
              lex_error_expecting (lexer, "DEFAULT", "NAME", "LABEL",
                                   "BOTH", "NONE");
              free (vars);
              goto error;
            }

          for (size_t i = 0; i < n_vars; i++)
            ct->vlabels[var_get_dict_index (vars[i])] = vlabel;
          free (vars);
        }
      else if (lex_match_id (lexer, "MRSETS"))
        {
          if (!lex_force_match_id (lexer, "COUNTDUPLICATES"))
            goto error;
          lex_match (lexer, T_EQUALS);
          if (!parse_bool (lexer, &ct->mrsets_count_duplicates))
            goto error;
        }
      else if (lex_match_id (lexer, "SMISSING"))
        {
          if (lex_match_id (lexer, "VARIABLE"))
            ct->smissing_listwise = false;
          else if (lex_match_id (lexer, "LISTWISE"))
            ct->smissing_listwise = true;
          else
            {
              lex_error_expecting (lexer, "VARIABLE", "LISTWISE");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "PCOMPUTE"))
        {
          if (!ctables_parse_pcompute (lexer, dataset_dict (ds), ct))
            goto error;
        }
      else if (lex_match_id (lexer, "PPROPERTIES"))
        {
          if (!ctables_parse_pproperties (lexer, ct))
            goto error;
        }
      else if (lex_match_id (lexer, "WEIGHT"))
        {
          if (!lex_force_match_id (lexer, "VARIABLE"))
            goto error;
          lex_match (lexer, T_EQUALS);
          ct->e_weight = parse_variable (lexer, dataset_dict (ds));
          if (!ct->e_weight)
            goto error;
        }
      else if (lex_match_id (lexer, "HIDESMALLCOUNTS"))
        {
          if (lex_match_id (lexer, "COUNT"))
            {
              lex_match (lexer, T_EQUALS);
              if (!lex_force_int_range (lexer, "HIDESMALLCOUNTS COUNT",
                                        2, INT_MAX))
                goto error;
              ct->hide_threshold = lex_integer (lexer);
              lex_get (lexer);
            }
          else if (ct->hide_threshold == 0)
            ct->hide_threshold = 5;
        }
      else
        {
          lex_error_expecting (lexer, "FORMAT", "VLABELS", "MRSETS",
                               "SMISSING", "PCOMPUTE", "PPROPERTIES",
                               "WEIGHT", "HIDESMALLCOUNTS", "TABLE");
          if (lex_match_id (lexer, "SLABELS")
              || lex_match_id (lexer, "CLABELS")
              || lex_match_id (lexer, "CRITERIA")
              || lex_match_id (lexer, "CATEGORIES")
              || lex_match_id (lexer, "TITLES")
              || lex_match_id (lexer, "SIGTEST")
              || lex_match_id (lexer, "COMPARETEST"))
            lex_next_msg (lexer, SN, -1, -1,
                          _("TABLE must appear before this subcommand."));
          goto error;
        }

      if (!lex_force_match (lexer, T_SLASH))
        goto error;
    }

  size_t allocated_tables = 0;
  do
    {
      if (ct->n_tables >= allocated_tables)
        ct->tables = x2nrealloc (ct->tables, &allocated_tables,
                                 sizeof *ct->tables);

      struct ctables_category *cat = xmalloc (sizeof *cat);
      *cat = (struct ctables_category) {
        .type = CCT_VALUE,
        .include_missing = false,
        .sort_ascending = true,
      };

      struct ctables_categories *c = xmalloc (sizeof *c);
      size_t n_vars = dict_get_n_vars (dataset_dict (ds));
      *c = (struct ctables_categories) {
        .n_refs = n_vars,
        .cats = cat,
        .n_cats = 1,
      };

      struct ctables_categories **categories = xnmalloc (n_vars,
                                                         sizeof *categories);
      for (size_t i = 0; i < n_vars; i++)
        categories[i] = c;

      bool *show_empty = xmalloc (n_vars);
      memset (show_empty, true, n_vars);

      struct ctables_table *t = xmalloc (sizeof *t);
      *t = (struct ctables_table) {
        .ctables = ct,
        .slabels_axis = PIVOT_AXIS_COLUMN,
        .slabels_visible = true,
        .clabels_values_map = HMAP_INITIALIZER (t->clabels_values_map),
        .label_axis = {
          [PIVOT_AXIS_ROW] = PIVOT_AXIS_ROW,
          [PIVOT_AXIS_COLUMN] = PIVOT_AXIS_COLUMN,
          [PIVOT_AXIS_LAYER] = PIVOT_AXIS_LAYER,
        },
        .clabels_from_axis = PIVOT_AXIS_LAYER,
        .clabels_to_axis = PIVOT_AXIS_LAYER,
        .categories = categories,
        .n_categories = n_vars,
        .show_empty = show_empty,
        .cilevel = 95,
      };
      ct->tables[ct->n_tables++] = t;

      lex_match (lexer, T_EQUALS);
      int expr_start = lex_ofs (lexer);
      if (!ctables_axis_parse (lexer, dataset_dict (ds),
                               &t->axes[PIVOT_AXIS_ROW]))
        goto error;
      if (lex_match (lexer, T_BY))
        {
          if (!ctables_axis_parse (lexer, dataset_dict (ds),
                                   &t->axes[PIVOT_AXIS_COLUMN]))
            goto error;

          if (lex_match (lexer, T_BY))
            {
              if (!ctables_axis_parse (lexer, dataset_dict (ds),
                                       &t->axes[PIVOT_AXIS_LAYER]))
                goto error;
            }
        }
      int expr_end = lex_ofs (lexer);

      if (!t->axes[PIVOT_AXIS_ROW] && !t->axes[PIVOT_AXIS_COLUMN]
          && !t->axes[PIVOT_AXIS_LAYER])
        {
          lex_error (lexer, _("At least one variable must be specified."));
          goto error;
        }

      const struct ctables_axis *scales[PIVOT_N_AXES];
      size_t n_scales = 0;
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          scales[a] = find_scale (t->axes[a]);
          if (scales[a])
            n_scales++;
        }
      if (n_scales > 1)
        {
          msg (SE, _("Scale variables may appear only on one axis."));
          if (scales[PIVOT_AXIS_ROW])
            msg_at (SN, scales[PIVOT_AXIS_ROW]->loc,
                    _("This scale variable appears on the rows axis."));
          if (scales[PIVOT_AXIS_COLUMN])
            msg_at (SN, scales[PIVOT_AXIS_COLUMN]->loc,
                    _("This scale variable appears on the columns axis."));
          if (scales[PIVOT_AXIS_LAYER])
            msg_at (SN, scales[PIVOT_AXIS_LAYER]->loc,
                    _("This scale variable appears on the layer axis."));
          goto error;
        }

      const struct ctables_axis *summaries[PIVOT_N_AXES];
      size_t n_summaries = 0;
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        {
          summaries[a] = (scales[a]
                          ? scales[a]
                          : find_categorical_summary_spec (t->axes[a]));
          if (summaries[a])
            n_summaries++;
        }
      if (n_summaries > 1)
        {
          msg (SE, _("Summaries may appear only on one axis."));
          for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
            if (summaries[a])
              {
                msg_at (SN, summaries[a]->loc,
                        a == PIVOT_AXIS_ROW
                        ? _("This variable on the rows axis has a summary.")
                        : a == PIVOT_AXIS_COLUMN
                        ? _("This variable on the columns axis has a summary.")
                        : _("This variable on the layers axis has a summary."));
                if (scales[a])
                  msg_at (SN, summaries[a]->loc,
                          _("This is a scale variable, so it always has a "
                            "summary even if the syntax does not explicitly "
                            "specify one."));
              }
          goto error;
        }
      for (enum pivot_axis_type a = 0; a < PIVOT_N_AXES; a++)
        if (n_summaries ? summaries[a] : t->axes[a])
          {
            t->summary_axis = a;
            break;
          }

      if (lex_token (lexer) == T_ENDCMD)
        {
          if (!ctables_prepare_table (t, lexer))
            goto error;
          break;
        }
      if (!lex_force_match (lexer, T_SLASH))
        goto error;

      while (!lex_match_id (lexer, "TABLE") && lex_token (lexer) != T_ENDCMD)
        {
          if (lex_match_id (lexer, "SLABELS"))
            {
              while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
                {
                  if (lex_match_id (lexer, "POSITION"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "COLUMN"))
                        t->slabels_axis = PIVOT_AXIS_COLUMN;
                      else if (lex_match_id (lexer, "ROW"))
                        t->slabels_axis = PIVOT_AXIS_ROW;
                      else if (lex_match_id (lexer, "LAYER"))
                        t->slabels_axis = PIVOT_AXIS_LAYER;
                      else
                        {
                          lex_error_expecting (lexer, "COLUMN", "ROW", "LAYER");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "VISIBLE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!parse_bool (lexer, &t->slabels_visible))
                        goto error;
                    }
                  else
                    {
                      lex_error_expecting (lexer, "POSITION", "VISIBLE");
                      goto error;
                    }
                }
            }
          else if (lex_match_id (lexer, "CLABELS"))
            {
              int start_ofs = lex_ofs (lexer) - 1;
              if (lex_match_id (lexer, "AUTO"))
                {
                  t->label_axis[PIVOT_AXIS_ROW] = PIVOT_AXIS_ROW;
                  t->label_axis[PIVOT_AXIS_COLUMN] = PIVOT_AXIS_COLUMN;
                }
              else if (lex_match_id (lexer, "ROWLABELS"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "OPPOSITE"))
                    t->label_axis[PIVOT_AXIS_ROW] = PIVOT_AXIS_COLUMN;
                  else if (lex_match_id (lexer, "LAYER"))
                    t->label_axis[PIVOT_AXIS_ROW] = PIVOT_AXIS_LAYER;
                  else
                    {
                      lex_error_expecting (lexer, "OPPOSITE", "LAYER");
                      goto error;
                    }
                }
              else if (lex_match_id (lexer, "COLLABELS"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "OPPOSITE"))
                    t->label_axis[PIVOT_AXIS_COLUMN] = PIVOT_AXIS_ROW;
                  else if (lex_match_id (lexer, "LAYER"))
                    t->label_axis[PIVOT_AXIS_COLUMN] = PIVOT_AXIS_LAYER;
                  else
                    {
                      lex_error_expecting (lexer, "OPPOSITE", "LAYER");
                      goto error;
                    }
                }
              else
                {
                  lex_error_expecting (lexer, "AUTO", "ROWLABELS",
                                       "COLLABELS");
                  goto error;
                }
              int end_ofs = lex_ofs (lexer) - 1;

              if (t->label_axis[PIVOT_AXIS_ROW] != PIVOT_AXIS_ROW
                  && t->label_axis[PIVOT_AXIS_COLUMN] != PIVOT_AXIS_COLUMN)
                {
                  msg (SE, _("ROWLABELS and COLLABELS may not both be "
                             "specified."));

                  lex_ofs_msg (lexer, SN, t->clabels_start_ofs,
                               t->clabels_end_ofs,
                               _("This is the first specification."));
                  lex_ofs_msg (lexer, SN, start_ofs, end_ofs,
                               _("This is the second specification."));
                  goto error;
                }

              t->clabels_start_ofs = start_ofs;
              t->clabels_end_ofs = end_ofs;
            }
          else if (lex_match_id (lexer, "CRITERIA"))
            {
              if (!lex_force_match_id (lexer, "CILEVEL"))
                goto error;
              lex_match (lexer, T_EQUALS);

              if (!lex_force_num_range_co (lexer, "CILEVEL", 0, 100))
                goto error;
              t->cilevel = lex_number (lexer);
              lex_get (lexer);
            }
          else if (lex_match_id (lexer, "CATEGORIES"))
            {
              if (!ctables_table_parse_categories (lexer, dataset_dict (ds),
                                                   ct, t))
                goto error;
            }
          else if (lex_match_id (lexer, "TITLES"))
            {
              do
                {
                  char **textp;
                  if (lex_match_id (lexer, "CAPTIONS"))
                    textp = &t->caption;
                  else if (lex_match_id (lexer, "CORNERS"))
                    textp = &t->corner;
                  else if (lex_match_id (lexer, "TITLES"))
                    textp = &t->title;
                  else
                    {
                      lex_error_expecting (lexer, "CAPTION", "CORNER", "TITLE");
                      goto error;
                    }
                  lex_match (lexer, T_EQUALS);

                  struct string s = DS_EMPTY_INITIALIZER;
                  while (lex_is_string (lexer))
                    {
                      if (!ds_is_empty (&s))
                        ds_put_byte (&s, '\n');
                      put_title_text (&s, lex_tokss (lexer), now,
                                      lexer, dataset_dict (ds),
                                      expr_start, expr_end);
                      lex_get (lexer);
                    }
                  free (*textp);
                  *textp = ds_steal_cstr (&s);
                }
              while (lex_token (lexer) != T_SLASH
                     && lex_token (lexer) != T_ENDCMD);
            }
          else if (lex_match_id (lexer, "SIGTEST"))
            {
              int start_ofs = lex_ofs (lexer) - 1;
              if (!t->chisq)
                {
                  t->chisq = xmalloc (sizeof *t->chisq);
                  *t->chisq = (struct ctables_chisq) {
                    .alpha = .05,
                    .include_mrsets = true,
                    .all_visible = true,
                  };
                }

              do
                {
                  if (lex_match_id (lexer, "TYPE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!lex_force_match_id (lexer, "CHISQUARE"))
                        goto error;
                    }
                  else if (lex_match_id (lexer, "ALPHA"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!lex_force_num_range_co (lexer, "ALPHA", 0, 1))
                        goto error;
                      t->chisq->alpha = lex_number (lexer);
                      lex_get (lexer);
                    }
                  else if (lex_match_id (lexer, "INCLUDEMRSETS"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!parse_bool (lexer, &t->chisq->include_mrsets))
                        goto error;
                    }
                  else if (lex_match_id (lexer, "CATEGORIES"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "ALLVISIBLE"))
                        t->chisq->all_visible = true;
                      else if (lex_match_id (lexer, "SUBTOTALS"))
                        t->chisq->all_visible = false;
                      else
                        {
                          lex_error_expecting (lexer,
                                               "ALLVISIBLE", "SUBTOTALS");
                          goto error;
                        }
                    }
                  else
                    {
                      lex_error_expecting (lexer, "TYPE", "ALPHA",
                                           "INCLUDEMRSETS", "CATEGORIES");
                      goto error;
                    }
                }
              while (lex_token (lexer) != T_SLASH
                     && lex_token (lexer) != T_ENDCMD);

              lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                             _("Support for SIGTEST not yet implemented."));
              goto error;
            }
          else if (lex_match_id (lexer, "COMPARETEST"))
            {
              int start_ofs = lex_ofs (lexer) - 1;
              if (!t->pairwise)
                {
                  t->pairwise = xmalloc (sizeof *t->pairwise);
                  *t->pairwise = (struct ctables_pairwise) {
                    .type = PROP,
                    .alpha = { .05, .05 },
                    .adjust = BONFERRONI,
                    .include_mrsets = true,
                    .meansvariance_allcats = true,
                    .all_visible = true,
                    .merge = false,
                    .apa_style = true,
                    .show_sig = false,
                  };
                }

              do
                {
                  if (lex_match_id (lexer, "TYPE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "PROP"))
                        t->pairwise->type = PROP;
                      else if (lex_match_id (lexer, "MEAN"))
                        t->pairwise->type = MEAN;
                      else
                        {
                          lex_error_expecting (lexer, "PROP", "MEAN");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "ALPHA"))
                    {
                      lex_match (lexer, T_EQUALS);

                      if (!lex_force_num_range_open (lexer, "ALPHA", 0, 1))
                        goto error;
                      double a0 = lex_number (lexer);
                      lex_get (lexer);

                      lex_match (lexer, T_COMMA);
                      if (lex_is_number (lexer))
                        {
                          if (!lex_force_num_range_open (lexer, "ALPHA", 0, 1))
                            goto error;
                          double a1 = lex_number (lexer);
                          lex_get (lexer);

                          t->pairwise->alpha[0] = MIN (a0, a1);
                          t->pairwise->alpha[1] = MAX (a0, a1);
                        }
                      else
                        t->pairwise->alpha[0] = t->pairwise->alpha[1] = a0;
                    }
                  else if (lex_match_id (lexer, "ADJUST"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "BONFERRONI"))
                        t->pairwise->adjust = BONFERRONI;
                      else if (lex_match_id (lexer, "BH"))
                        t->pairwise->adjust = BH;
                      else if (lex_match_id (lexer, "NONE"))
                        t->pairwise->adjust = 0;
                      else
                        {
                          lex_error_expecting (lexer, "BONFERRONI", "BH",
                                               "NONE");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "INCLUDEMRSETS"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!parse_bool (lexer, &t->pairwise->include_mrsets))
                        goto error;
                    }
                  else if (lex_match_id (lexer, "MEANSVARIANCE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "ALLCATS"))
                        t->pairwise->meansvariance_allcats = true;
                      else if (lex_match_id (lexer, "TESTEDCATS"))
                        t->pairwise->meansvariance_allcats = false;
                      else
                        {
                          lex_error_expecting (lexer, "ALLCATS", "TESTEDCATS");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "CATEGORIES"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "ALLVISIBLE"))
                        t->pairwise->all_visible = true;
                      else if (lex_match_id (lexer, "SUBTOTALS"))
                        t->pairwise->all_visible = false;
                      else
                        {
                          lex_error_expecting (lexer, "ALLVISIBLE",
                                               "SUBTOTALS");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "MERGE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!parse_bool (lexer, &t->pairwise->merge))
                        goto error;
                    }
                  else if (lex_match_id (lexer, "STYLE"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (lex_match_id (lexer, "APA"))
                        t->pairwise->apa_style = true;
                      else if (lex_match_id (lexer, "SIMPLE"))
                        t->pairwise->apa_style = false;
                      else
                        {
                          lex_error_expecting (lexer, "APA", "SIMPLE");
                          goto error;
                        }
                    }
                  else if (lex_match_id (lexer, "SHOWSIG"))
                    {
                      lex_match (lexer, T_EQUALS);
                      if (!parse_bool (lexer, &t->pairwise->show_sig))
                        goto error;
                    }
                  else
                    {
                      lex_error_expecting (lexer, "TYPE", "ALPHA", "ADJUST",
                                           "INCLUDEMRSETS", "MEANSVARIANCE",
                                           "CATEGORIES", "MERGE", "STYLE",
                                           "SHOWSIG");
                      goto error;
                    }
                }
              while (lex_token (lexer) != T_SLASH
                     && lex_token (lexer) != T_ENDCMD);

              lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                             _("Support for COMPARETEST not yet implemented."));
              goto error;
            }
          else
            {
              lex_error_expecting (lexer, "TABLE", "SLABELS", "CLABELS",
                                   "CRITERIA", "CATEGORIES", "TITLES",
                                   "SIGTEST", "COMPARETEST");
              if (lex_match_id (lexer, "FORMAT")
                  || lex_match_id (lexer, "VLABELS")
                  || lex_match_id (lexer, "MRSETS")
                  || lex_match_id (lexer, "SMISSING")
                  || lex_match_id (lexer, "PCOMPUTE")
                  || lex_match_id (lexer, "PPROPERTIES")
                  || lex_match_id (lexer, "WEIGHT")
                  || lex_match_id (lexer, "HIDESMALLCOUNTS"))
                lex_next_msg (lexer, SN, -1, -1,
                              _("This subcommand must appear before TABLE."));
              goto error;
            }

          if (!lex_match (lexer, T_SLASH))
            break;
        }

      if (t->label_axis[PIVOT_AXIS_ROW] != PIVOT_AXIS_ROW)
        t->clabels_from_axis = PIVOT_AXIS_ROW;
      else if (t->label_axis[PIVOT_AXIS_COLUMN] != PIVOT_AXIS_COLUMN)
        t->clabels_from_axis = PIVOT_AXIS_COLUMN;
      t->clabels_to_axis = t->label_axis[t->clabels_from_axis];

      if (!ctables_prepare_table (t, lexer))
        goto error;
    }
  while (lex_token (lexer) != T_ENDCMD);

  if (!input)
    input = proc_open (ds);
  bool ok = ctables_execute (ds, input, ct);
  ok = proc_commit (ds) && ok;

  ctables_destroy (ct);
  return ok ? CMD_SUCCESS : CMD_FAILURE;

error:
  if (input)
    proc_commit (ds);
  ctables_destroy (ct);
  return CMD_FAILURE;
}

