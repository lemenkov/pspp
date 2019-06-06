/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013, 2019 Free Software Foundation, Inc.

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

#ifndef MEANS_H
#define MEANS_H

#include "libpspp/compiler.h"

struct cell_container
{
  /* A hash table containing the cells.  The table is indexed by a hash
     based on the cell's categorical value.  */
  struct hmap map;

  /* A binary tree containing the cells.  This  is
   used to sort the elements in order of their categorical
   values.  */
  struct bt bt;
};



struct layer
{
  size_t n_factor_vars;
  const struct variable **factor_vars;

  /* A container holding the union of all instances of values used
     as categories forming this layer.  */
  struct cell_container instances;
};


struct per_var_data;

typedef struct per_var_data *stat_create (struct pool *pool);
typedef void stat_update  (struct per_var_data *stat, double w, double x);
typedef double stat_get   (const struct per_var_data *);


struct cell_spec
{
  /* Printable title for output */
  const char *title;

  /* Keyword for syntax */
  const char *keyword;

  stat_create *sc;
  stat_update *su;
  stat_get *sd;
};


/* The thing parsed after TABLES= */
struct mtable
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  const struct variable **control_vars;

  struct layer **layers;
  int n_layers;

  struct cell *root_cell;
};

/* A structure created by the parser.  Contains the definition of the
   what the procedure should calculate.  */
struct means
{
  const struct dictionary *dict;

  /* The "tables" (ie, a definition of how the data should
     be broken down).  */
  struct mtable *table;
  size_t n_tables;

  /* Missing value class for categorical variables.  */
  enum mv_class exclude;

  /* Missing value class for dependent variables */
  enum mv_class dep_exclude;

  bool listwise_exclude;


  /* The statistics to be calculated for each cell.  */
  int *statistics;
  int n_statistics;

  /* Pool on which cell functions may allocate data.  */
  struct pool *pool;
};



#define n_MEANS_STATISTICS 17
extern const struct cell_spec cell_spec[n_MEANS_STATISTICS];

/* This enum must be consistent with the array cell_spec (in means-calc.c).
   A bitfield instead of enums would in my opinion be
   more elegent.  However we want the order of the specified
   statistics to be retained in the output.  */
enum
  {
    MEANS_MEAN = 0,
    MEANS_N,
    MEANS_STDDEV
  };



struct dataset;
struct casereader;
void run_means (struct means *cmd, struct casereader *input, const struct dataset *ds UNUSED);

void means_shipout (const struct mtable *mt, const struct means *means);

#endif
