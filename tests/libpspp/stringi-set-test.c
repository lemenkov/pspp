/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2008, 2009, 2010, 2012, 2014 Free Software Foundation, Inc.

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

/* This is a test program for the stringi_set_* routines defined in
   stringi-set.c.  This test program aims to be as comprehensive as possible.
   "gcov -a -b" should report almost complete coverage of lines, blocks and
   branches in stringi-set.c, except that one branch caused by hash collision
   is not exercised because our hash function has so few collisions.  "valgrind
   --leak-check=yes --show-reachable=yes" should give a clean report. */

#include <config.h>

#include "libpspp/stringi-set.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

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

/* Support routines. */

enum { MAX_VALUE = 1024 };

static char *string_table[MAX_VALUE];

static const char *
make_string (int value)
{
  char **s;

  assert (value >= 0 && value < MAX_VALUE);
  s = &string_table[value];
  if (*s == NULL)
    {
      size_t size = F26ADIC_STRLEN_MAX + 1;
      *s = xmalloc (size);
      str_format_26adic (value + 1, true, *s, size);
    }
  return *s;
}

static void
free_strings (void)
{
  int i;

  for (i = 0; i < MAX_VALUE; i++)
    free (string_table[i]);
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

/* Checks that SET contains STRING. */
static void
check_set_contains (struct stringi_set *set, const char *string)
{
  struct stringi_set_node *node;

  check (stringi_set_contains (set, string));
  check (!stringi_set_insert (set, string));
  check (!stringi_set_insert_nocopy (set, xstrdup (string)));

  node = stringi_set_find_node (set, string);
  check (node != NULL);
  check (!utf8_strcasecmp (string, stringi_set_node_get_string (node)));
}

/* Checks that SET contains the N strings in DATA, that its structure is
   correct, and that certain operations on SET produce the expected results. */
static void
check_stringi_set (struct stringi_set *set, const int data[], size_t n)
{
  size_t i;

  check (stringi_set_is_empty (set) == (n == 0));
  check (stringi_set_count (set) == n);

  for (i = 0; i < n; i++)
    {
      const char *s;
      char copy[16];
      char *p;

      s = make_string (data[i]);
      check_set_contains (set, s);

      strcpy (copy, s);
      for (p = copy; *p != '\0'; p++)
        {
          assert (isupper (*p));
          *p = tolower (*p);
          check_set_contains (set, copy);
        }
    }

  check (!stringi_set_contains (set, "xxx"));
  check (stringi_set_find_node (set, "") == NULL);

  if (n == 0)
    {
      check (stringi_set_first (set) == NULL);
      free (stringi_set_get_array (set));
    }
  else
    {
      const struct stringi_set_node *node;
      char **array;
      int *data_copy;
      int left;

      array = stringi_set_get_array (set);
      data_copy = xmemdup (data, n * sizeof *data);
      left = n;
      for (node = stringi_set_first (set), i = 0; i < n;
           node = stringi_set_next (set, node), i++)
        {
          const char *s = stringi_set_node_get_string (node);
          size_t j;

          check (s == array[i]);

          for (j = 0; j < left; j++)
            if (!utf8_strcasecmp (s, make_string (data_copy[j])))
              {
                data_copy[j] = data_copy[--left];
                goto next;
              }
          check_die ();

        next: ;
        }
      check (node == NULL);
      free (data_copy);
      free (array);

      array = stringi_set_get_sorted_array (set);
      for (i = 0; i < n; i++)
        {
          if (i > 0)
            check (utf8_strcasecmp (array[i - 1], array[i]) < 0);
          check (stringi_set_contains (set, array[i]));
        }
      free (array);
    }
}

/* Inserts the N strings from 0 to N - 1 (inclusive) into a set in the
   order specified by INSERTIONS, then deletes them in the order specified by
   DELETIONS, checking the set's contents for correctness after each
   operation.  */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t n)
{
  struct stringi_set set;
  size_t i;

  stringi_set_init (&set);
  check_stringi_set (&set, NULL, 0);
  for (i = 0; i < n; i++)
    {
      check (stringi_set_insert (&set, make_string (insertions[i])));
      check_stringi_set (&set, insertions, i + 1);
    }
  for (i = 0; i < n; i++)
    {
      check (stringi_set_delete (&set, make_string (deletions[i])));
      check_stringi_set (&set, deletions + i + 1, n - i - 1);
    }
  stringi_set_destroy (&set);
}

/* Inserts strings into a set in each possible order, then removes them in each
   possible order, up to a specified maximum size. */
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

/* Inserts strings into a set in each possible order, then removes them in the
   same order, up to a specified maximum size. */
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

/* Inserts strings into a set in each possible order, then
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

/* Inserts and removes strings in a set, in random order. */
static void
test_random_sequence (void)
{
  const int max_elems = 64;
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

/* Inserts strings into a set in ascending order, then delete in ascending
   order. */
static void
test_insert_ordered (void)
{
  const int max_elems = 64;
  int *values;
  struct stringi_set set;
  int i;

  stringi_set_init (&set);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = i;
      stringi_set_insert_nocopy (&set, xstrdup (make_string (i)));
      check_stringi_set (&set, values, i + 1);
    }
  for (i = 0; i < max_elems; i++)
    {
      stringi_set_delete (&set, make_string (i));
      check_stringi_set (&set, values + i + 1, max_elems - i - 1);
    }
  stringi_set_destroy (&set);
  free (values);
}

static void
test_boolean_ops (void (*function)(struct stringi_set *a, struct stringi_set *b,
                                   unsigned int *a_pat, unsigned int *b_pat))
{
  enum { MAX_STRINGS = 7 };
  unsigned int a_pat, b_pat;

  for (a_pat = 0; a_pat < (1u << MAX_STRINGS); a_pat++)
    for (b_pat = 0; b_pat < (1u << MAX_STRINGS); b_pat++)
      {
        unsigned int new_a_pat = a_pat;
        unsigned int new_b_pat = b_pat;
        struct stringi_set a, b;
        int a_strings[MAX_STRINGS], b_strings[MAX_STRINGS];
        size_t i, n_a, n_b;

        stringi_set_init (&a);
        stringi_set_init (&b);
        for (i = 0; i < MAX_STRINGS; i++)
          {
            if (a_pat & (1u << i))
              stringi_set_insert (&a, make_string (i));
            if (b_pat & (1u << i))
              stringi_set_insert (&b, make_string (i));
          }

        function (&a, &b, &new_a_pat, &new_b_pat);

        n_a = n_b = 0;
        for (i = 0; i < MAX_STRINGS; i++)
          {
            if (new_a_pat & (1u << i))
              a_strings[n_a++] = i;
            if (new_b_pat & (1u << i))
              b_strings[n_b++] = i;
          }
        check_stringi_set (&a, a_strings, n_a);
        check_stringi_set (&b, b_strings, n_b);
        stringi_set_destroy (&a);
        stringi_set_destroy (&b);
      }
}

static void
union_cb (struct stringi_set *a, struct stringi_set *b,
          unsigned int *a_pat, unsigned int *b_pat)
{
  stringi_set_union (a, b);
  *a_pat |= *b_pat;
}

static void
test_union (void)
{
  test_boolean_ops (union_cb);
}

static void
union_and_intersection_cb (struct stringi_set *a, struct stringi_set *b,
                           unsigned int *a_pat, unsigned int *b_pat)
{
  unsigned int orig_a_pat = *a_pat;
  unsigned int orig_b_pat = *b_pat;

  stringi_set_union_and_intersection (a, b);
  *a_pat = orig_a_pat | orig_b_pat;
  *b_pat = orig_a_pat & orig_b_pat;
}

static void
test_union_and_intersection (void)
{
  test_boolean_ops (union_and_intersection_cb);
}

static void
intersect_cb (struct stringi_set *a, struct stringi_set *b,
              unsigned int *a_pat, unsigned int *b_pat)
{
  stringi_set_intersect (a, b);
  *a_pat &= *b_pat;
}

static void
test_intersect (void)
{
  test_boolean_ops (intersect_cb);
}

static void
subtract_cb (struct stringi_set *a, struct stringi_set *b,
              unsigned int *a_pat, unsigned int *b_pat)
{
  stringi_set_subtract (a, b);
  *a_pat &= ~*b_pat;
}

static void
test_subtract (void)
{
  test_boolean_ops (subtract_cb);
}

static void
swap_cb (struct stringi_set *a, struct stringi_set *b,
         unsigned int *a_pat, unsigned int *b_pat)
{
  unsigned int tmp;
  stringi_set_swap (a, b);
  tmp = *a_pat;
  *a_pat = *b_pat;
  *b_pat = tmp;
}

static void
test_swap (void)
{
  test_boolean_ops (swap_cb);
}

static void
clear_cb (struct stringi_set *a, struct stringi_set *b UNUSED,
         unsigned int *a_pat, unsigned int *b_pat UNUSED)
{
  stringi_set_clear (a);
  *a_pat = 0;
}

static void
test_clear (void)
{
  test_boolean_ops (clear_cb);
}

static void
clone_cb (struct stringi_set *a, struct stringi_set *b,
         unsigned int *a_pat, unsigned int *b_pat)
{
  stringi_set_destroy (a);
  stringi_set_clone (a, b);
  *a_pat = *b_pat;
}

static void
test_clone (void)
{
  test_boolean_ops (clone_cb);
}

static void
test_destroy_null (void)
{
  stringi_set_destroy (NULL);
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
      "union",
      "union",
      test_union
    },
    {
      "union-and-intersection",
      "union and intersection",
      test_union_and_intersection
    },
    {
      "intersect",
      "intersect",
      test_intersect
    },
    {
      "subtract",
      "subtract",
      test_subtract
    },
    {
      "swap",
      "swap",
      test_swap
    },
    {
      "clear",
      "clear",
      test_clear
    },
    {
      "clone",
      "clone",
      test_clone
    },
    {
      "destroy-null",
      "destroying null table",
      test_destroy_null
    },
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
      printf ("%s: test case-insensitive string set library\n"
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
            free_strings ();
            return 0;
          }

      fprintf (stderr, "unknown test %s; use --help for help\n", argv[1]);
      return EXIT_FAILURE;
    }
}
