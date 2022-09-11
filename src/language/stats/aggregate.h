/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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


#ifndef AGGREGATE_H
#define AGGREGATE_H

#include <stddef.h>

#include "data/format.h"
#include "data/val-type.h"

enum agr_src_vars
  {
    AGR_SV_NO,
    AGR_SV_YES,
    AGR_SV_OPT
  };

#define AGGREGATE_FUNCTIONS                                             \
  AGRF(AGRF_SUM,     "SUM",     N_("Sum of values"),                         AGR_SV_YES, 0, -1,           8,  2) \
  AGRF(AGRF_MEAN,    "MEAN",    N_("Mean average"),                          AGR_SV_YES, 0, -1,           8,  2) \
  AGRF(AGRF_MEDIAN,  "MEDIAN",  N_("Median"),                                AGR_SV_YES, 0, -1,           8,  2) \
  AGRF(AGRF_SD,      "SD",      N_("Standard deviation"),                    AGR_SV_YES, 0, -1,           8,  2) \
  AGRF(AGRF_MAX,     "MAX",     N_("Maximum value"),                         AGR_SV_YES, 0, VAL_STRING,  -1, -1) \
  AGRF(AGRF_MIN,     "MIN",     N_("Minimum value"),                         AGR_SV_YES, 0, VAL_STRING,  -1, -1) \
  AGRF(AGRF_PGT,     "PGT",     N_("Percentage greater than"),               AGR_SV_YES, 1, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_PLT,     "PLT",     N_("Percentage less than"),                  AGR_SV_YES, 1, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_PIN,     "PIN",     N_("Percentage included in range"),          AGR_SV_YES, 2, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_POUT,    "POUT",    N_("Percentage excluded from range"),        AGR_SV_YES, 2, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_FGT,     "FGT",     N_("Fraction greater than"),                 AGR_SV_YES, 1, VAL_NUMERIC,  5,  3) \
  AGRF(AGRF_FLT,     "FLT",     N_("Fraction less than"),                    AGR_SV_YES, 1, VAL_NUMERIC,  5,  3) \
  AGRF(AGRF_FIN,     "FIN",     N_("Fraction included in range"),            AGR_SV_YES, 2, VAL_NUMERIC,  5,  3) \
  AGRF(AGRF_FOUT,    "FOUT",    N_("Fraction excluded from range"),          AGR_SV_YES, 2, VAL_NUMERIC,  5,  3) \
  AGRF(AGRF_CGT,     "CGT",     N_("Count greater than"),                    AGR_SV_YES, 1, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_CLT,     "CLT",     N_("Count less than"),                       AGR_SV_YES, 1, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_CIN,     "CIN",     N_("Count included in range"),               AGR_SV_YES, 2, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_COUT,    "COUT",    N_("Count excluded from range"),             AGR_SV_YES, 2, VAL_NUMERIC,  5,  1) \
  AGRF(AGRF_N,       "N",       N_("Number of cases"),                       AGR_SV_NO,  0, VAL_NUMERIC,  7,  0) \
  AGRF(AGRF_NU,      "NU",      N_("Number of cases (unweighted)"),          AGR_SV_OPT, 0, VAL_NUMERIC,  7,  0) \
  AGRF(AGRF_NMISS,   "NMISS",   N_("Number of missing values"),              AGR_SV_YES, 0, VAL_NUMERIC,  7,  0) \
  AGRF(AGRF_NUMISS,  "NUMISS",  N_("Number of missing values (unweighted)"), AGR_SV_YES, 0, VAL_NUMERIC,  7,  0) \
  AGRF(AGRF_FIRST,   "FIRST",   N_("First non-missing value"),               AGR_SV_YES, 0, VAL_STRING,  -1, -1) \
  AGRF(AGRF_LAST,    "LAST",    N_("Last non-missing value"),                AGR_SV_YES, 0, VAL_STRING,  -1, -1)

/* Aggregation functions. */
enum agr_function
  {
#define AGRF(ENUM, NAME, DESCRIPTION, SRC_VARS, N_ARGS, ALPHA_TYPE, W, D) \
    ENUM,
AGGREGATE_FUNCTIONS
#undef AGRF
  };

/* Attributes of an aggregation function. */
struct agr_func
  {
    const char *name;           /* Aggregation function name. */
    const char *description;    /* Translatable string describing the function. */
    enum agr_src_vars src_vars; /* Whether source variables are a parameter of the function */
    size_t n_args;              /* Number of arguments (not including src vars). */
    enum val_type alpha_type;   /* When given ALPHA arguments, output type. */
    struct fmt_spec format;     /* Format spec if alpha_type != ALPHA. */
  };

extern const struct agr_func agr_func_tab[];


#endif
