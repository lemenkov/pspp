/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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

/* This is a test program for the bt_* routines defined in bt.c.
   This test program aims to be as comprehensive as possible.
   "gcov -b" should report 100% coverage of lines and branches in
   bt.c.  "valgrind --leak-check=yes --show-reachable=yes" should
   give a clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/bt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/compiler.h>

/* Exit with a failure code.
   (Place a breakpoint on this function while debugging.) */
static void
check_die (void)
{
  exit (EXIT_FAILURE);
}

/* If OK is not true, prints a message about failure on the
   current source file and the given LINE and terminates. */
static void
check_func (bool ok, int line)
{
  if (!ok)
    {
      fprintf (stderr, "%s:%d: check failed\n", __FILE__, line);
      check_die ();
    }
}

/* Verifies that EXPR evaluates to true.
   If not, prints a message citing the calling line number and
   terminates. */
#define check(EXPR) check_func ((EXPR), __LINE__)

/* Prints a message about memory exhaustion and exits with a
   failure code. */
static void
xalloc_die (void)
{
  printf ("virtual memory exhausted\n");
  exit (EXIT_FAILURE);
}

/* Allocates and returns N bytes of memory. */
static void *
xmalloc (size_t n)
{
  if (n != 0)
    {
      void *p = malloc (n);
      if (p == NULL)
        xalloc_die ();

      return p;
    }
  else
    return NULL;
}

static void *
xmemdup (const void *p, size_t n)
{
  void *q = xmalloc (n);
  memcpy (q, p, n);
  return q;
}

/* Allocates and returns N * M bytes of memory. */
static void *
xnmalloc (size_t n, size_t m)
{
  if ((size_t) -1 / m <= n)
    xalloc_die ();
  return xmalloc (n * m);
}

/* Node type and support routines. */

/* Test data element. */
struct element
  {
    struct bt_node node;        /* Embedded binary tree element. */
    int data;                   /* Primary value. */
  };

static int aux_data;

/* Returns the `struct element' that NODE is embedded within. */
static struct element *
bt_node_to_element (const struct bt_node *node)
{
  return BT_DATA (node, struct element, node);
}

/* Compares the `x' values in A and B and returns a strcmp-type
   return value.  Verifies that AUX points to aux_data. */
static int
compare_elements (const struct bt_node *a_, const struct bt_node *b_,
                  const void *aux)
{
  const struct element *a = bt_node_to_element (a_);
  const struct element *b = bt_node_to_element (b_);

  check (aux == &aux_data);
  return a->data < b->data ? -1 : a->data > b->data;
}

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_ints_noaux (const void *a_, const void *b_)
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Swaps *A and *B. */
static void
swap (int *a, int *b)
{
  int t = *a;
  *a = *b;
  *b = t;
}

/* Reverses the order of the N integers starting at VALUES. */
static void
reverse (int *values, size_t n)
{
  size_t i = 0;
  size_t j = n;

  while (j > i)
    swap (&values[i++], &values[--j]);
}

/* Arranges the N elements in VALUES into the lexicographically
   next greater permutation.  Returns true if successful.
   If VALUES is already the lexicographically greatest
   permutation of its elements (i.e. ordered from greatest to
   smallest), arranges them into the lexicographically least
   permutation (i.e. ordered from smallest to largest) and
   returns false. */
static bool
next_permutation (int *values, size_t n)
{
  if (n > 0)
    {
      size_t i = n - 1;
      while (i != 0)
        {
          i--;
          if (values[i] < values[i + 1])
            {
              size_t j;
              for (j = n - 1; values[i] >= values[j]; j--)
                continue;
              swap (values + i, values + j);
              reverse (values + (i + 1), n - (i + 1));
              return true;
            }
        }

      reverse (values, n);
    }

  return false;
}

/* Returns N!. */
static unsigned int
factorial (unsigned int n)
{
  unsigned int value = 1;
  while (n > 1)
    value *= n--;
  return value;
}

/* Randomly shuffles the N elements in ARRAY, each of which is
   SIZE bytes in size. */
static void
random_shuffle (void *array_, size_t n, size_t size)
{
  char *array = array_;
  char *tmp = xmalloc (size);
  size_t i;

  for (i = 0; i < n; i++)
    {
      size_t j = rand () % (n - i) + i;
      if (i != j)
        {
          memcpy (tmp, array + j * size, size);
          memcpy (array + j * size, array + i * size, size);
          memcpy (array + i * size, tmp, size);
        }
    }

  free (tmp);
}

/* Calculates floor(log(n)/log(sqrt(2))). */
static int
calculate_h_alpha (size_t n)
{
  size_t thresholds[] =
    {
      0, 2, 2, 3, 4, 6, 8, 12, 16, 23, 32, 46, 64, 91, 128, 182, 256, 363,
      512, 725, 1024, 1449, 2048, 2897, 4096, 5793, 8192, 11586, 16384,
      23171, 32768, 46341, 65536, 92682, 131072, 185364, 262144, 370728,
      524288, 741456, 1048576, 1482911, 2097152, 2965821, 4194304, 5931642,
      8388608, 11863284, 16777216, 23726567, 33554432, 47453133, 67108864,
      94906266, 134217728, 189812532, 268435456, 379625063, 536870912,
      759250125, 1073741824, 1518500250, 2147483648, 3037000500,
    };
  size_t n_thresholds = sizeof thresholds / sizeof *thresholds;
  size_t i;

  for (i = 0; i < n_thresholds; i++)
    if (thresholds[i] > n)
      break;
  return i - 1;
}

/* Returns the height of the tree rooted at NODE. */
static int
get_height (struct bt_node *node)
{
  if (node == NULL)
    return 0;
  else
    {
      int left = get_height (node->down[0]);
      int right = get_height (node->down[1]);
      return 1 + (left > right ? left : right);
    }
}

/* Checks that BT is loosely alpha-height balanced, that is, that
   its height is no more than h_alpha(count) + 1, where
   h_alpha(n) = floor(log(n)/log(1/alpha)). */
static void
check_balance (struct bt *bt)
{
  /* In the notation of the Galperin and Rivest paper (and of
     CLR), the height of a tree is the number of edges in the
     longest path from the root to a leaf, so we have to subtract
     1 from our measured height. */
  int height = get_height (bt->root) - 1;
  int max_height = calculate_h_alpha (bt_count (bt)) + 1;
  check (height <= max_height);
}

/* Checks that BT contains the N ints in DATA, that its
   structure is correct, and that certain operations on BT
   produce the expected results. */
static void
check_bt (struct bt *bt, const int data[], size_t n)
{
  struct element e;
  size_t i;
  int *order;

  order = xmemdup (data, n * sizeof *data);
  qsort (order, n, sizeof *order, compare_ints_noaux);

  for (i = 0; i < n; i++)
    {
      struct bt_node *p;

      e.data = data[i];
      if (rand () % 2)
        p = bt_find (bt, &e.node);
      else
        p = bt_insert (bt, &e.node);
      check (p != NULL);
      check (p != &e.node);
      check (bt_node_to_element (p)->data == data[i]);
    }

  e.data = -1;
  check (bt_find (bt, &e.node) == NULL);

  check_balance (bt);

  if (n == 0)
    {
      check (bt_first (bt) == NULL);
      check (bt_last (bt) == NULL);
      check (bt_next (bt, NULL) == NULL);
      check (bt_prev (bt, NULL) == NULL);
    }
  else
    {
      struct bt_node *p;

      for (p = bt_first (bt), i = 0; i < n; p = bt_next (bt, p), i++)
        check (bt_node_to_element (p)->data == order[i]);
      check (p == NULL);

      for (p = bt_last (bt), i = 0; i < n; p = bt_prev (bt, p), i++)
        check (bt_node_to_element (p)->data == order[n - i - 1]);
      check (p == NULL);
    }

  free (order);
}

/* Inserts the N values from 0 to N - 1 (inclusive) into an
   BT in the order specified by INSERTIONS, then deletes them in
   the order specified by DELETIONS, checking the BT's contents
   for correctness after each operation. */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t n)
{
  struct element *elements;
  struct bt bt;
  size_t i;

  elements = xnmalloc (n, sizeof *elements);
  for (i = 0; i < n; i++)
    elements[i].data = i;

  bt_init (&bt, compare_elements, &aux_data);
  check_bt (&bt, NULL, 0);
  for (i = 0; i < n; i++)
    {
      check (bt_insert (&bt, &elements[insertions[i]].node) == NULL);
      check_bt (&bt, insertions, i + 1);
    }
  for (i = 0; i < n; i++)
    {
      bt_delete (&bt, &elements[deletions[i]].node);
      check_bt (&bt, deletions + i + 1, n - i - 1);
    }

  free (elements);
}

/* Inserts values into an BT in each possible order, then
   removes them in each possible order, up to a specified maximum
   size. */
static void
test_insert_any_remove_any (void)
{
  const int max_elems = 5;
  int n;

  for (n = 0; n <= max_elems; n++)
    {
      int *insertions, *deletions;
      unsigned int ins_n_perms;
      int i;

      insertions = xnmalloc (n, sizeof *insertions);
      deletions = xnmalloc (n, sizeof *deletions);
      for (i = 0; i < n; i++)
        insertions[i] = i;

      for (ins_n_perms = 0;
           ins_n_perms == 0 || next_permutation (insertions, n);
           ins_n_perms++)
        {
          unsigned int del_n_perms;
          int i;

          for (i = 0; i < n; i++)
            deletions[i] = i;

          for (del_n_perms = 0;
               del_n_perms == 0 || next_permutation (deletions, n);
               del_n_perms++)
            test_insert_delete (insertions, deletions, n);

          check (del_n_perms == factorial (n));
        }
      check (ins_n_perms == factorial (n));

      free (insertions);
      free (deletions);
    }
}

/* Inserts values into an BT in each possible order, then
   removes them in the same order, up to a specified maximum
   size. */
static void
test_insert_any_remove_same (void)
{
  const int max_elems = 7;
  int n;

  for (n = 0; n <= max_elems; n++)
    {
      int *values;
      unsigned int n_permutations;
      int i;

      values = xnmalloc (n, sizeof *values);
      for (i = 0; i < n; i++)
        values[i] = i;

      for (n_permutations = 0;
           n_permutations == 0 || next_permutation (values, n);
           n_permutations++)
        test_insert_delete (values, values, n);
      check (n_permutations == factorial (n));

      free (values);
    }
}

/* Inserts values into an BT in each possible order, then
   removes them in reverse order, up to a specified maximum
   size. */
static void
test_insert_any_remove_reverse (void)
{
  const int max_elems = 7;
  int n;

  for (n = 0; n <= max_elems; n++)
    {
      int *insertions, *deletions;
      unsigned int n_permutations;
      int i;

      insertions = xnmalloc (n, sizeof *insertions);
      deletions = xnmalloc (n, sizeof *deletions);
      for (i = 0; i < n; i++)
        insertions[i] = i;

      for (n_permutations = 0;
           n_permutations == 0 || next_permutation (insertions, n);
           n_permutations++)
        {
          memcpy (deletions, insertions, sizeof *insertions * n);
          reverse (deletions, n);

          test_insert_delete (insertions, deletions, n);
        }
      check (n_permutations == factorial (n));

      free (insertions);
      free (deletions);
    }
}

/* Inserts and removes values in an BT in random orders. */
static void
test_random_sequence (void)
{
  const int max_elems = 128;
  const int max_trials = 8;
  int n;

  for (n = 0; n <= max_elems; n += 2)
    {
      int *insertions, *deletions;
      int trial;
      int i;

      insertions = xnmalloc (n, sizeof *insertions);
      deletions = xnmalloc (n, sizeof *deletions);
      for (i = 0; i < n; i++)
        insertions[i] = i;
      for (i = 0; i < n; i++)
        deletions[i] = i;

      for (trial = 0; trial < max_trials; trial++)
        {
          random_shuffle (insertions, n, sizeof *insertions);
          random_shuffle (deletions, n, sizeof *deletions);

          test_insert_delete (insertions, deletions, n);
        }

      free (insertions);
      free (deletions);
    }
}

/* Inserts elements into an BT in ascending order. */
static void
test_insert_ordered (void)
{
  const int max_elems = 1024;
  struct element *elements;
  int *values;
  struct bt bt;
  int i;

  bt_init (&bt, compare_elements, &aux_data);
  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = elements[i].data = i;
      check (bt_insert (&bt, &elements[i].node) == NULL);
      check_bt (&bt, values, i + 1);
    }
  free (elements);
  free (values);
}

/* Tests bt_find_ge and bt_find_le. */
static void
test_find_ge_le (void)
{
  const int max_elems = 10;
  struct element *elements;
  int *values;
  unsigned int inc_pat;

  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (inc_pat = 0; inc_pat < (1u << max_elems); inc_pat++)
    {
      struct bt bt;
      int n_elems = 0;
      int i;

      /* Insert the values in the pattern into BT. */
      bt_init (&bt, compare_elements, &aux_data);
      for (i = 0; i < max_elems; i++)
        if (inc_pat & (1u << i))
          {
            values[n_elems] = elements[n_elems].data = i;
            check (bt_insert (&bt, &elements[n_elems].node) == NULL);
            n_elems++;
          }
      check_bt (&bt, values, n_elems);

      /* Try find_ge and find_le for each possible element value. */
      for (i = -1; i <= max_elems; i++)
        {
          struct element tmp;
          struct bt_node *ge, *le;
          int j;

          ge = le = NULL;
          for (j = 0; j < n_elems; j++)
            {
              if (ge == NULL && values[j] >= i)
                ge = &elements[j].node;
              if (values[j] <= i)
                le = &elements[j].node;
            }

          tmp.data = i;
          check (bt_find_ge (&bt, &tmp.node) == ge);
          check (bt_find_le (&bt, &tmp.node) == le);
        }
    }
  free (elements);
  free (values);
}

/* Inserts elements into an BT, then moves the nodes around in
   memory. */
static void
test_moved (void)
{
  const int max_elems = 128;
  struct element *e[2];
  int cur;
  int *values;
  struct bt bt;
  int i, j;

  bt_init (&bt, compare_elements, &aux_data);
  e[0] = xnmalloc (max_elems, sizeof *e[0]);
  e[1] = xnmalloc (max_elems, sizeof *e[1]);
  values = xnmalloc (max_elems, sizeof *values);
  cur = 0;
  for (i = 0; i < max_elems; i++)
    {
      values[i] = e[cur][i].data = i;
      check (bt_insert (&bt, &e[cur][i].node) == NULL);
      check_bt (&bt, values, i + 1);

      for (j = 0; j <= i; j++)
        {
          e[!cur][j] = e[cur][j];
          bt_moved (&bt, &e[!cur][j].node);
          check_bt (&bt, values, i + 1);
        }
      cur = !cur;
    }
  free (e[0]);
  free (e[1]);
  free (values);
}

/* Inserts values into an BT, then changes their values. */
static void
test_changed (void)
{
  const int max_elems = 6;
  int n;

  for (n = 0; n <= max_elems; n++)
    {
      int *values, *changed_values;
      struct element *elements;
      unsigned int n_permutations;
      int i;

      values = xnmalloc (n, sizeof *values);
      changed_values = xnmalloc (n, sizeof *changed_values);
      elements = xnmalloc (n, sizeof *elements);
      for (i = 0; i < n; i++)
        values[i] = i;

      for (n_permutations = 0;
           n_permutations == 0 || next_permutation (values, n);
           n_permutations++)
        {
          for (i = 0; i < n; i++)
            {
              int j, k;
              for (j = 0; j <= n; j++)
                {
                  struct bt bt;
                  struct bt_node *changed_retval;

                  bt_init (&bt, compare_elements, &aux_data);

                  /* Add to BT in order. */
                  for (k = 0; k < n; k++)
                    {
                      int n = values[k];
                      elements[n].data = n;
                      check (bt_insert (&bt, &elements[n].node) == NULL);
                    }
                  check_bt (&bt, values, n);

                  /* Change value i to j. */
                  elements[i].data = j;
                  for (k = 0; k < n; k++)
                    changed_values[k] = k;
                  changed_retval = bt_changed (&bt, &elements[i].node);
                  if (i != j && j < n)
                    {
                      /* Will cause duplicate. */
                      check (changed_retval == &elements[j].node);
                      changed_values[i] = changed_values[n - 1];
                      check_bt (&bt, changed_values, n - 1);
                    }
                  else
                    {
                      /* Succeeds. */
                      check (changed_retval == NULL);
                      changed_values[i] = j;
                      check_bt (&bt, changed_values, n);
                    }
                }
            }
        }
      check (n_permutations == factorial (n));

      free (values);
      free (changed_values);
      free (elements);
    }
}

/* Main program. */

struct test
  {
    const char *name;
    const char *description;
    void (*function) (void);
  };

static const struct test tests[] =
  {
    {
      "insert-any-remove-any",
      "insert any order, delete any order",
      test_insert_any_remove_any
    },
    {
      "insert-any-remove-same",
      "insert any order, delete same order",
      test_insert_any_remove_same
    },
    {
      "insert-any-remove-reverse",
      "insert any order, delete reverse order",
      test_insert_any_remove_reverse
    },
    {
      "random-sequence",
      "insert and delete in random sequence",
      test_random_sequence
    },
    {
      "insert-ordered",
      "insert in ascending order",
      test_insert_ordered
    },
    {
      "find-ge-le",
      "find_ge and find_le",
      test_find_ge_le
    },
      {
      "moved",
      "move elements around in memory",
      test_moved
    },
    {
      "changed",
      "change key data in nodes",
      test_changed
    }
  };

enum { N_TESTS = sizeof tests / sizeof *tests };

int
main (int argc, char *argv[])
{
  int i;

  if (argc != 2)
    {
      fprintf (stderr, "exactly one argument required; use --help for help\n");
      return EXIT_FAILURE;
    }
  else if (!strcmp (argv[1], "--help"))
    {
      printf ("%s: test balanced tree\n"
              "usage: %s TEST-NAME\n"
              "where TEST-NAME is one of the following:\n",
              argv[0], argv[0]);
      for (i = 0; i < N_TESTS; i++)
        printf ("  %s\n    %s\n", tests[i].name, tests[i].description);
      return 0;
    }
  else
    {
      for (i = 0; i < N_TESTS; i++)
        if (!strcmp (argv[1], tests[i].name))
          {
            tests[i].function ();
            return 0;
          }

      fprintf (stderr, "unknown test %s; use --help for help\n", argv[1]);
      return EXIT_FAILURE;
    }
}
