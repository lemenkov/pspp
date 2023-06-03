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

/* This is a test program for the stringi_map_* routines defined in
   stringi-map.c.  This test program aims to be as comprehensive as possible.
   "gcov -a -b" should report almost complete coverage of lines, blocks and
   branches in stringi-map.c, except that one branch caused by hash collision
   is not exercised because our hash function has so few collisions.  "valgrind
   --leak-check=yes --show-reachable=yes" should give a clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/stringi-map.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/hash-functions.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"
#include "libpspp/string-set.h"
#include "libpspp/stringi-set.h"

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

enum {
  IDX_BITS = 10,
  MAX_IDX = 1 << IDX_BITS,
  KEY_MASK = (MAX_IDX - 1),
  KEY_SHIFT = 0,
  VALUE_MASK = (MAX_IDX - 1) << IDX_BITS,
  VALUE_SHIFT = IDX_BITS
};

static char *string_table[MAX_IDX];

static const char *
get_string (int idx)
{
  char **s;

  assert (idx >= 0 && idx < MAX_IDX);
  s = &string_table[idx];
  if (*s == NULL)
    {
      size_t size = F26ADIC_STRLEN_MAX + 1;
      *s = xmalloc (size);
      str_format_26adic (idx + 1, true, *s, size);
    }
  return *s;
}

static void
free_strings (void)
{
  int i;

  for (i = 0; i < MAX_IDX; i++)
    free (string_table[i]);
}

static const char *
make_key (int value)
{
  return get_string ((value & KEY_MASK) >> KEY_SHIFT);
}

static const char *
make_value (int value)
{
  return get_string ((value & VALUE_MASK) >> VALUE_SHIFT);
}

static int
random_value (unsigned int seed, int basis)
{
  return hash_int (seed, basis) & VALUE_MASK;
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

/* Arranges the N elements in VALUES into the lexicographically next greater
   permutation.  Returns true if successful.  If VALUES is already the
   lexicographically greatest permutation of its elements (i.e. ordered from
   greatest to smallest), arranges them into the lexicographically least
   permutation (i.e. ordered from smallest to largest) and returns false.

   Comparisons among elements of VALUES consider only the bits in KEY_MASK. */
static bool
next_permutation (int *values, size_t n)
{
  if (n > 0)
    {
      size_t i = n - 1;
      while (i != 0)
        {
          i--;
          if ((values[i] & KEY_MASK) < (values[i + 1] & KEY_MASK))
            {
              size_t j;
              for (j = n - 1;
                   (values[i] & KEY_MASK) >= (values[j] & KEY_MASK);
                   j--)
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

/* Checks that MAP has (KEY, VALUE) as a pair. */
static void
check_map_contains (struct stringi_map *map,
                    const char *key, const char *value)
{
  struct stringi_map_node *node;
  const char *found_value;

  check (stringi_map_contains (map, key));

  node = stringi_map_find_node (map, key, strlen (key));
  check (node != NULL);
  check (!utf8_strcasecmp (key, stringi_map_node_get_key (node)));
  check (!strcmp (value, stringi_map_node_get_value (node)));

  check (node == stringi_map_insert (map, key, "012"));
  check (!strcmp (value, stringi_map_node_get_value (node)));

  check (node == stringi_map_insert_nocopy (map, xstrdup (key),
                                            xstrdup ("345")));
  check (!strcmp (value, stringi_map_node_get_value (node)));

  found_value = stringi_map_find (map, key);
  check (found_value == stringi_map_node_get_value (node));
  check (found_value != NULL);
  check (!strcmp (found_value, value));
}

/* Checks that MAP contains the N strings in DATA, that its structure is
   correct, and that certain operations on MAP produce the expected results. */
static void
check_stringi_map (struct stringi_map *map, const int data[], size_t n)
{
  size_t i;

  check (stringi_map_is_empty (map) == (n == 0));
  check (stringi_map_count (map) == n);

  for (i = 0; i < n; i++)
    {
      const char *key = make_key (data[i]);
      const char *value = make_value (data[i]);
      char copy[16];
      char *p;

      check_map_contains (map, key, value);

      strcpy (copy, key);
      for (p = copy; *p != '\0'; p++)
        {
          assert (isupper (*p));
          *p = tolower (*p);
          check_map_contains (map, copy, value);
        }
    }

  check (!stringi_map_contains (map, "xxx"));
  check (stringi_map_find (map, "0") == NULL);
  check (stringi_map_find_node (map, "", 0) == NULL);
  check (!stringi_map_delete (map, "xyz"));

  if (n == 0)
    check (stringi_map_first (map) == NULL);
  else
    {
      const struct stringi_map_node *node;
      int *data_copy;
      int left;

      data_copy = xmemdup (data, n * sizeof *data);
      left = n;
      for (node = stringi_map_first (map), i = 0; i < n;
           node = stringi_map_next (map, node), i++)
        {
          const char *key = stringi_map_node_get_key (node);
          const char *value = stringi_map_node_get_value (node);
          size_t j;

          for (j = 0; j < left; j++)
            if (!strcmp (key, make_key (data_copy[j]))
                || !strcmp (value, make_value (data_copy[j])))
              {
                data_copy[j] = data_copy[--left];
                goto next;
              }
          check_die ();

        next: ;
        }
      check (node == NULL);
      free (data_copy);
    }
}

/* Inserts the N strings from 0 to N - 1 (inclusive) into a map in the
   order specified by INSERTIONS, then deletes them in the order specified by
   DELETIONS, checking the map's contents for correctness after each
   operation.  */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t n)
{
  struct stringi_map map;
  size_t i;

  stringi_map_init (&map);
  check_stringi_map (&map, NULL, 0);
  for (i = 0; i < n; i++)
    {
      check (stringi_map_insert (&map, make_key (insertions[i]),
                                make_value (insertions[i])));
      check_stringi_map (&map, insertions, i + 1);
    }
  for (i = 0; i < n; i++)
    {
      check (stringi_map_delete (&map, make_key (deletions[i])));
      check_stringi_map (&map, deletions + i + 1, n - i - 1);
    }
  stringi_map_destroy (&map);
}

/* Inserts strings into a map in each possible order, then removes them in each
   possible order, up to a specified maximum size. */
static void
test_insert_any_remove_any (void)
{
  const int basis = 0;
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
        insertions[i] = i | random_value (i, basis);

      for (ins_n_perms = 0;
           ins_n_perms == 0 || next_permutation (insertions, n);
           ins_n_perms++)
        {
          unsigned int del_n_perms;
          int i;

          for (i = 0; i < n; i++)
            deletions[i] = i | random_value (i, basis);

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

/* Inserts strings into a map in each possible order, then removes them in the
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
        values[i] = i | random_value (i, 1);

      for (n_permutations = 0;
           n_permutations == 0 || next_permutation (values, n);
           n_permutations++)
        test_insert_delete (values, values, n);
      check (n_permutations == factorial (n));

      free (values);
    }
}

/* Inserts strings into a map in each possible order, then
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
        insertions[i] = i | random_value (i, 2);

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

/* Inserts and removes strings in a map, in random order. */
static void
test_random_sequence (void)
{
  const int basis = 3;
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
        insertions[i] = i | random_value (i, basis);
      for (i = 0; i < n; i++)
        deletions[i] = i | random_value (i, basis);

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

/* Inserts strings into a map in ascending order, then delete in ascending
   order. */
static void
test_insert_ordered (void)
{
  const int max_elems = 64;
  int *values;
  struct stringi_map map;
  int i;

  stringi_map_init (&map);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = i | random_value (i, 4);
      stringi_map_insert_nocopy (&map, xstrdup (make_key (values[i])),
                                xstrdup (make_value (values[i])));
      check_stringi_map (&map, values, i + 1);
    }
  for (i = 0; i < max_elems; i++)
    {
      stringi_map_delete (&map, make_key (i));
      check_stringi_map (&map, values + i + 1, max_elems - i - 1);
    }
  stringi_map_destroy (&map);
  free (values);
}

/* Inserts and replaces strings in a map, in random order. */
static void
test_replace (void)
{
  const int basis = 15;
  enum { MAX_ELEMS = 16 };
  const int max_trials = 8;
  int n;

  for (n = 0; n <= MAX_ELEMS; n++)
    {
      int insertions[MAX_ELEMS];
      int trial;
      int i;

      for (i = 0; i < n; i++)
        insertions[i] = (i / 2) | random_value (i, basis);

      for (trial = 0; trial < max_trials; trial++)
        {
          struct stringi_map map;
          int data[MAX_ELEMS];
          int n_data;

          /* Insert with replacement in random order. */
          n_data = 0;
          stringi_map_init (&map);
          random_shuffle (insertions, n, sizeof *insertions);
          for (i = 0; i < n; i++)
            {
              const char *key = make_key (insertions[i]);
              const char *value = make_value (insertions[i]);
              int j;

              for (j = 0; j < n_data; j++)
                if ((data[j] & KEY_MASK) == (insertions[i] & KEY_MASK))
                  {
                    data[j] = insertions[i];
                    goto found;
                  }
              data[n_data++] = insertions[i];
            found:

              if (i % 2)
                stringi_map_replace (&map, key, value);
              else
                stringi_map_replace_nocopy (&map,
                                           xstrdup (key), xstrdup (value));
              check_stringi_map (&map, data, n_data);
            }

          /* Delete in original order. */
          for (i = 0; i < n; i++)
            {
              const char *expected_value;
              char *value;
              int j;

              expected_value = NULL;
              for (j = 0; j < n_data; j++)
                if ((data[j] & KEY_MASK) == (insertions[i] & KEY_MASK))
                  {
                    expected_value = make_value (data[j]);
                    data[j] = data[--n_data];
                    break;
                  }

              value = stringi_map_find_and_delete (&map,
                                                  make_key (insertions[i]));
              check ((value != NULL) == (expected_value != NULL));
              check (value == NULL || !strcmp (value, expected_value));
              free (value);
            }
          assert (stringi_map_is_empty (&map));

          stringi_map_destroy (&map);
        }
    }
}

static void
make_patterned_map (struct stringi_map *map, unsigned int pattern, int basis,
                    int insertions[], int *np)
{
  int n;
  int i;

  stringi_map_init (map);

  n = 0;
  for (i = 0; pattern != 0; i++)
    if (pattern & (1u << i))
      {
        pattern &= pattern - 1;
        insertions[n] = i | random_value (i, basis);
        check (stringi_map_insert (map, make_key (insertions[n]),
                                  make_value (insertions[n])));
        n++;
      }
  check_stringi_map (map, insertions, n);

  *np = n;
}

static void
for_each_map (void (*cb)(struct stringi_map *, int data[], int n),
              int basis)
{
  enum { MAX_ELEMS = 5 };
  unsigned int pattern;

  for (pattern = 0; pattern < (1u << MAX_ELEMS); pattern++)
    {
      int data[MAX_ELEMS];
      struct stringi_map map;
      int n;

      make_patterned_map (&map, pattern, basis, data, &n);
      (*cb) (&map, data, n);
      stringi_map_destroy (&map);
    }
}

static void
for_each_pair_of_maps (
  void (*cb)(struct stringi_map *a, int a_data[], int n_a,
             struct stringi_map *b, int b_data[], int n_b),
  int a_basis, int b_basis)
{
  enum { MAX_ELEMS = 5 };
  unsigned int a_pattern, b_pattern;

  for (a_pattern = 0; a_pattern < (1u << MAX_ELEMS); a_pattern++)
    for (b_pattern = 0; b_pattern < (1u << MAX_ELEMS); b_pattern++)
      {
        int a_data[MAX_ELEMS], b_data[MAX_ELEMS];
        struct stringi_map a_map, b_map;
        int n_a, n_b;

        make_patterned_map (&a_map, a_pattern, a_basis, a_data, &n_a);
        make_patterned_map (&b_map, b_pattern, b_basis, b_data, &n_b);
        (*cb) (&a_map, a_data, n_a, &b_map, b_data, n_b);
        stringi_map_destroy (&a_map);
        stringi_map_destroy (&b_map);
      }
}

static void
clear_cb (struct stringi_map *map, int data[] UNUSED, int n UNUSED)
{
  stringi_map_clear (map);
  check_stringi_map (map, NULL, 0);
}

static void
test_clear (void)
{
  for_each_map (clear_cb, 5);
}

static void
clone_cb (struct stringi_map *map, int data[], int n)
{
  struct stringi_map clone;

  stringi_map_clone (&clone, map);
  check_stringi_map (&clone, data, n);
  stringi_map_destroy (&clone);
}

static void
test_clone (void)
{
  for_each_map (clone_cb, 6);
}

static void
node_swap_value_cb (struct stringi_map *map, int data[], int n)
{
  int i;

  for (i = 0; i < n; i++)
    {
      const char *key = make_key (data[i]);
      const char *value = make_value (data[i]);
      struct stringi_map_node *node;
      char *old_value;

      node = stringi_map_find_node (map, key, strlen (key));
      check (node != NULL);
      check (!strcmp (stringi_map_node_get_value (node), value));
      data[i] = (data[i] & KEY_MASK) | random_value (i, 15);
      old_value = stringi_map_node_swap_value (node, make_value (data[i]));
      check (old_value != NULL);
      check (!strcmp (value, old_value));
      free (old_value);
    }
}

static void
test_node_swap_value (void)
{
  for_each_map (node_swap_value_cb, 14);
}

static void
swap_cb (struct stringi_map *a, int a_data[], int n_a,
         struct stringi_map *b, int b_data[], int n_b)
{
  stringi_map_swap (a, b);
  check_stringi_map (a, b_data, n_b);
  check_stringi_map (b, a_data, n_a);
}

static void
test_swap (void)
{
  for_each_pair_of_maps (swap_cb, 7, 8);
}

static void
insert_map_cb (struct stringi_map *a, int a_data[], int n_a,
               struct stringi_map *b, int b_data[], int n_b)
{
  int i, j;

  stringi_map_insert_map (a, b);

  for (i = 0; i < n_b; i++)
    {
      for (j = 0; j < n_a; j++)
        if ((b_data[i] & KEY_MASK) == (a_data[j] & KEY_MASK))
          goto found;
      a_data[n_a++] = b_data[i];
    found:;
    }
  check_stringi_map (a, a_data, n_a);
  check_stringi_map (b, b_data, n_b);
}

static void
test_insert_map (void)
{
  for_each_pair_of_maps (insert_map_cb, 91, 10);
}

static void
replace_map_cb (struct stringi_map *a, int a_data[], int n_a,
               struct stringi_map *b, int b_data[], int n_b)
{
  int i, j;

  stringi_map_replace_map (a, b);

  for (i = 0; i < n_b; i++)
    {
      for (j = 0; j < n_a; j++)
        if ((b_data[i] & KEY_MASK) == (a_data[j] & KEY_MASK))
          {
            a_data[j] = (a_data[j] & KEY_MASK) | (b_data[i] & VALUE_MASK);
            goto found;
          }
      a_data[n_a++] = b_data[i];
    found:;
    }
  check_stringi_map (a, a_data, n_a);
  check_stringi_map (b, b_data, n_b);
}

static void
test_replace_map (void)
{
  for_each_pair_of_maps (replace_map_cb, 11, 12);
}

static void
check_iset (struct stringi_set *set, const int *data, int n_data,
           int mask, int shift)
{
  int *unique;
  int n_unique;
  int i;

  n_unique = 0;
  unique = xmalloc (n_data * sizeof *unique);
  for (i = 0; i < n_data; i++)
    {
      int idx = (data[i] & mask) >> shift;
      int j;

      for (j = 0; j < n_unique; j++)
        if (unique[j] == idx)
          goto found;
      unique[n_unique++] = idx;
    found:;
    }

  check (stringi_set_count (set) == n_unique);
  for (i = 0; i < n_unique; i++)
    check (stringi_set_contains (set, get_string (unique[i])));
  stringi_set_destroy (set);
  free (unique);
}

static void
check_set (struct string_set *set, const int *data, int n_data,
           int mask, int shift)
{
  int *unique;
  int n_unique;
  int i;

  n_unique = 0;
  unique = xmalloc (n_data * sizeof *unique);
  for (i = 0; i < n_data; i++)
    {
      int idx = (data[i] & mask) >> shift;
      int j;

      for (j = 0; j < n_unique; j++)
        if (unique[j] == idx)
          goto found;
      unique[n_unique++] = idx;
    found:;
    }

  check (string_set_count (set) == n_unique);
  for (i = 0; i < n_unique; i++)
    check (string_set_contains (set, get_string (unique[i])));
  string_set_destroy (set);
  free (unique);
}

static void
get_keys_and_values_cb (struct stringi_map *map, int data[], int n)
{
  struct stringi_set keys;
  struct string_set values;

  stringi_set_init (&keys);
  string_set_init (&values);
  stringi_map_get_keys (map, &keys);
  stringi_map_get_values (map, &values);
  check_iset (&keys, data, n, KEY_MASK, KEY_SHIFT);
  check_set (&values, data, n, VALUE_MASK, VALUE_SHIFT);
}

static void
test_get_keys_and_values (void)
{
  for_each_map (get_keys_and_values_cb, 13);
}

static void
test_destroy_null (void)
{
  stringi_map_destroy (NULL);
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
      "replace",
      "insert and replace in random sequence",
      test_replace
    },
    {
      "insert-ordered",
      "insert in ascending order",
      test_insert_ordered
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
      "swap",
      "swap",
      test_swap
    },
    {
      "node-swap-value",
      "node_swap_value",
      test_node_swap_value
    },
    {
      "insert-map",
      "insert_map",
      test_insert_map
    },
    {
      "replace-map",
      "replace_map",
      test_replace_map
    },
    {
      "get-keys-and-values",
      "get keys and values",
      test_get_keys_and_values
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
      printf ("%s: test case-insensitive string map library\n"
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
