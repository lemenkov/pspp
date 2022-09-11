/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2015 Free Software Foundation, Inc.

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

#include "libpspp/message.h"

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/dataset.h"
#include "data/missing-values.h"

#include "language/lexer/lexer.h"
#include "language/command.h"
#include "language/lexer/variable-parser.h"
#include "language/lexer/value-parser.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include "t-test.h"

int
cmd_t_test (struct lexer *lexer, struct dataset *ds)
{
  bool ok = false;
  const struct dictionary *dict = dataset_dict (ds);
  struct tt tt;
  int mode_count = 0;

  /* Variables pertaining to the paired mode */
  const struct variable **v1 = NULL;
  size_t n_v1 = 0;
  const struct variable **v2 = NULL;
  size_t n_v2 = 0;

  size_t n_pairs = 0;
  vp *pairs = NULL;


  /* One sample mode */
  double testval = SYSMIS;

  /* Independent samples mode */
  const struct variable *gvar;
  union value gval0;
  union value gval1;
  int gval_width = -1;
  bool cut = false;

  tt.wv = dict_get_weight (dict);
  tt.dict = dict;
  tt.confidence = 0.95;
  tt.exclude = MV_ANY;
  tt.missing_type = MISS_ANALYSIS;
  tt.n_vars = 0;
  tt.vars = NULL;
  tt.mode = MODE_undef;

  lex_match (lexer, T_EQUALS);

  for (; lex_token (lexer) != T_ENDCMD;)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "TESTVAL"))
        {
          mode_count++;
          tt.mode = MODE_SINGLE;
          lex_match (lexer, T_EQUALS);
          if (!lex_force_num (lexer))
            goto exit;
          testval = lex_number (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "GROUPS"))
        {
          mode_count++;
          cut = false;
          tt.mode = MODE_INDEP;
          lex_match (lexer, T_EQUALS);

          int groups_start = lex_ofs (lexer);
          if (NULL == (gvar = parse_variable (lexer, dict)))
            goto exit;

          gval_width = var_get_width (gvar);
          value_init (&gval0, gval_width);
          value_init (&gval1, gval_width);

          int n;
          if (lex_match (lexer, T_LPAREN))
            {
              parse_value (lexer, &gval0, gvar);
              if (lex_token (lexer) != T_RPAREN)
                {
                  lex_match (lexer, T_COMMA);
                  parse_value (lexer, &gval1, gvar);
                  cut = false;
                  n = 2;
                }
              else
                {
                  cut = true;
                  n = 1;
                }

              if (! lex_force_match (lexer, T_RPAREN))
                goto exit;
            }
          else
            {
              gval0.f = 1.0;
              gval1.f = 2.0;
              cut = false;
              n = 0;
            }
          int groups_end = lex_ofs (lexer) - 1;

          if (n != 2 && var_is_alpha (gvar))
            {
              lex_ofs_error (lexer, groups_start, groups_end,
                             _("When applying %s to a string variable, two "
                               "values must be specified."), "GROUPS");
              goto exit;
            }
        }
      else if (lex_match_id (lexer, "PAIRS"))
        {
          bool with = false;
          bool paired = false;

          if (tt.n_vars > 0)
            {
              lex_next_error (lexer, -1, -1,
                              _("%s subcommand may not be used with %s."),
                              "VARIABLES", "PAIRS");
              goto exit;
            }

          mode_count++;
          tt.mode = MODE_PAIRED;
          lex_match (lexer, T_EQUALS);

          if (!parse_variables_const (lexer, dict,
                                      &v1, &n_v1,
                                      PV_NO_DUPLICATE | PV_NUMERIC))
            goto exit;

          if (lex_match (lexer, T_WITH))
            {
              with = true;
              if (!parse_variables_const (lexer, dict,
                                          &v2, &n_v2,
                                          PV_NO_DUPLICATE | PV_NUMERIC))
                goto exit;

              if (lex_match (lexer, T_LPAREN)
                  && lex_match_id (lexer, "PAIRED")
                  && lex_match (lexer, T_RPAREN))
                {
                  paired = true;
                  if (n_v1 != n_v2)
                    {
                      msg (SE, _("PAIRED was specified but the number of variables "
                                 "preceding WITH (%zu) did not match the number "
                                 "following (%zu)."),
                           n_v1, n_v2);
                      goto exit;
                    }
                }
            }
          {
            int i;

            if (!with)
              n_pairs = (n_v1 * (n_v1 - 1)) / 2.0;
            else if (paired)
              n_pairs = n_v1;
            else
              n_pairs = n_v1 * n_v2;

            pairs = xcalloc (n_pairs, sizeof *pairs);

            if (with)
              {
                int x = 0;
                if (paired)
                  {
                    for (i = 0 ; i < n_v1; ++i)
                      {
                        vp *pair = &pairs[i];
                        (*pair)[0] = v1[i];
                        (*pair)[1] = v2[i];
                      }
                  }
                else
                  {
                    for (i = 0 ; i < n_v1; ++i)
                      {
                        int j;
                        for (j = 0 ; j < n_v2; ++j)
                          {
                            vp *pair = &pairs[x++];
                            (*pair)[0] = v1[i];
                            (*pair)[1] = v2[j];
                          }
                      }
                  }
              }
            else
              {
                int x = 0;
                for (i = 0 ; i < n_v1; ++i)
                  {
                    int j;

                    for (j = i + 1 ; j < n_v1; ++j)
                      {
                        vp *pair = &pairs[x++];
                        (*pair)[0] = v1[i];
                        (*pair)[1] = v1[j];
                      }
                  }
              }

          }
        }
      else if (lex_match_id (lexer, "VARIABLES"))
        {
          if (tt.mode == MODE_PAIRED)
            {
              lex_next_error (lexer, -1, -1,
                              _("%s subcommand may not be used with %s."),
                              "VARIABLES", "PAIRS");
              goto exit;
            }

          lex_match (lexer, T_EQUALS);

          if (!parse_variables_const (lexer, dict,
                                      &tt.vars,
                                      &tt.n_vars,
                                      PV_NO_DUPLICATE | PV_NUMERIC))
            goto exit;
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "INCLUDE"))
                {
                  tt.exclude = MV_SYSTEM;
                }
              else if (lex_match_id (lexer, "EXCLUDE"))
                {
                  tt.exclude = MV_ANY;
                }
              else if (lex_match_id (lexer, "LISTWISE"))
                {
                  tt.missing_type = MISS_LISTWISE;
                }
              else if (lex_match_id (lexer, "ANALYSIS"))
                {
                  tt.missing_type = MISS_ANALYSIS;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto exit;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "CRITERIA"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "CIN") || lex_force_match_id (lexer, "CI"))
            if (lex_force_match (lexer, T_LPAREN))
              {
                if (!lex_force_num (lexer))
                  goto exit;
                tt.confidence = lex_number (lexer);
                lex_get (lexer);
                if (! lex_force_match (lexer, T_RPAREN))
                  goto exit;
              }
        }
      else
        {
          lex_error (lexer, NULL);
          goto exit;
        }
    }

  if (mode_count != 1)
    {
      msg (SE, _("Exactly one of TESTVAL, GROUPS and PAIRS subcommands "
                 "must be specified."));
      goto exit;
    }

  if (tt.n_vars == 0 && tt.mode != MODE_PAIRED)
    {
      lex_sbc_missing (lexer, "VARIABLES");
      goto exit;
    }



  /* Deal with splits etc */
  {
    struct casereader *group;
    struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);

    while (casegrouper_get_next_group (grouper, &group))
      {
        if (tt.mode == MODE_SINGLE)
          {
            if (tt.missing_type == MISS_LISTWISE)
              group  = casereader_create_filter_missing (group,
                                                         tt.vars, tt.n_vars,
                                                         tt.exclude,
                                                         NULL,  NULL);
            one_sample_run (&tt, testval, group);
          }
        else if (tt.mode == MODE_PAIRED)
          {
            if (tt.missing_type == MISS_LISTWISE)
              {
                group  = casereader_create_filter_missing (group,
                                                           v1, n_v1,
                                                           tt.exclude,
                                                           NULL,  NULL);
                group  = casereader_create_filter_missing (group,
                                                           v2, n_v2,
                                                           tt.exclude,
                                                           NULL,  NULL);
              }

            paired_run (&tt, n_pairs, pairs, group);
          }
        else /* tt.mode == MODE_INDEP */
          {
            if (tt.missing_type == MISS_LISTWISE)
              {
                group  = casereader_create_filter_missing (group,
                                                           tt.vars, tt.n_vars,
                                                           tt.exclude,
                                                           NULL,  NULL);

                group  = casereader_create_filter_missing (group,
                                                           &gvar, 1,
                                                           tt.exclude,
                                                           NULL,  NULL);

              }

            indep_run (&tt, gvar, cut, &gval0, &gval1, group);
          }
      }

    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

exit:
  if (gval_width != -1)
    {
      value_destroy (&gval0, gval_width);
      value_destroy (&gval1, gval_width);
    }
  free (pairs);
  free (v1);
  free (v2);
  free (tt.vars);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

