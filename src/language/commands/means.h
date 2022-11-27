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

#include "libpspp/hmap.h"
#include "libpspp/bt.h"
#include "libpspp/compiler.h"

struct casereader;
struct dataset;
struct lexer;

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
};


struct statistic;

typedef struct statistic *stat_create (struct pool *pool);
typedef void stat_update  (struct statistic *stat, double w, double x);
typedef double stat_get   (const struct statistic *);
typedef void stat_destroy (struct statistic *);


struct cell_spec
{
  /* Printable title for output */
  const char *title;

  /* Keyword for syntax */
  const char *keyword;

  /* The result class for the datum.  */
  const char *rc;

  stat_create *sc;
  stat_update *su;
  stat_get *sd;
  stat_destroy *sf;
};

struct summary
{
  double n_total;
  double n_missing;
};

/* Intermediate data per table.  */
struct workspace
{
  /* An array of n_layers integers which are used
     to permute access into the factor_vars of each layer.  */
  int *control_idx;

  /* An array of n_layers cell_containers which hold the union
     of instances used respectively by each layer.  */
  struct cell_container *instances;

  struct cell *root_cell;
};

/* The thing parsed after TABLES= */
struct mtable
{
  size_t n_dep_vars;
  const struct variable **dep_vars;

  struct layer **layers;
  int n_layers;

  int n_combinations;

  /* An array of n_combinations workspaces.  */
  struct workspace *ws;

  /* An array of n_combinations * n_dep_vars summaries.
     These are displayed in the Case Processing
     Summary box.  */
  struct summary *summ;
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
  enum mv_class ctrl_exclude;

  /* Missing value class for dependent variables */
  enum mv_class dep_exclude;

  /* The statistics to be calculated for each cell.  */
  int *statistics;
  int n_statistics;
  size_t allocated_statistics;

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

void run_means (struct means *, struct casereader *, const struct dataset *);
bool means_parse (struct lexer *, struct means *);
void means_set_default_statistics (struct means *);

#endif
