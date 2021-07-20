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

#include <limits.h>

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/macro.h"
#include "language/lexer/scan.h"
#include "language/lexer/token.h"
#include "libpspp/message.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool
force_macro_id (struct lexer *lexer)
{
  return lex_token (lexer) == T_MACRO_ID || lex_force_id (lexer);
}

static bool
match_macro_id (struct lexer *lexer, const char *keyword)
{
  if (keyword[0] != '!')
    return lex_match_id (lexer, keyword);
  else if (lex_token (lexer) == T_MACRO_ID
           && lex_id_match_n (ss_cstr (keyword), lex_tokss (lexer), 4))
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* Obtains a quoted string from LEXER and then tokenizes the quoted string's
   content to obtain a single TOKEN.  Returns true if successful, false
   otherwise.  The caller takes ownership of TOKEN on success, otherwise TOKEN
   is indeterminate. */
static bool
parse_quoted_token (struct lexer *lexer, struct token *token)
{
  if (!lex_force_string (lexer))
    return false;

  struct substring s = lex_tokss (lexer);
  struct string_lexer slex;
  string_lexer_init (&slex, s.string, s.length, SEG_MODE_INTERACTIVE, true);
  struct token another_token = { .type = T_STOP };
  if (string_lexer_next (&slex, token) != SLR_TOKEN
      || string_lexer_next (&slex, &another_token) != SLR_END)
    {
      token_uninit (token);
      token_uninit (&another_token);
      lex_error (lexer, _("String must contain exactly one token."));
      return false;
    }
  lex_get (lexer);
  return true;
}

static bool
dup_arg_type (struct lexer *lexer, bool *saw_arg_type)
{
  if (*saw_arg_type)
    {
      lex_error (lexer, _("Only one of !TOKENS, !CHAREND, !ENCLOSE, or "
                          "!CMDEND is allowed."));
      return false;
    }
  else
    {
      *saw_arg_type = true;
      return true;
    }
}

int
cmd_define (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (!force_macro_id (lexer))
    return CMD_FAILURE;

  /* Parse macro name. */
  struct macro *m = xmalloc (sizeof *m);
  *m = (struct macro) {
    .name = ss_xstrdup (lex_tokss (lexer)),
    .location = xmalloc (sizeof *m->location),
  };
  *m->location = (struct msg_location) {
    .file_name = xstrdup_if_nonnull (lex_get_file_name (lexer)),
    .first_line = lex_get_first_line_number (lexer, 0),
  };
  lex_get (lexer);

  if (!lex_force_match (lexer, T_LPAREN))
    goto error;

  size_t allocated_params = 0;
  while (!lex_match (lexer, T_RPAREN))
    {
      if (m->n_params >= allocated_params)
        m->params = x2nrealloc (m->params, &allocated_params,
                                sizeof *m->params);

      size_t param_index = m->n_params++;
      struct macro_param *p = &m->params[param_index];
      *p = (struct macro_param) { .expand_arg = true };

      /* Parse parameter name. */
      if (match_macro_id (lexer, "!POSITIONAL"))
        {
          if (param_index > 0 && !m->params[param_index - 1].positional)
            {
              lex_error (lexer, _("Positional parameters must precede "
                                  "keyword parameters."));
              goto error;
            }

          p->positional = true;
          p->name = xasprintf ("!%zu", param_index + 1);
        }
      else
        {
          if (lex_token (lexer) == T_MACRO_ID)
            {
              lex_error (lexer, _("Keyword macro parameter must be named in "
                                  "definition without \"!\" prefix."));
              goto error;
            }
          if (!lex_force_id (lexer))
            goto error;

          if (is_macro_keyword (lex_tokss (lexer)))
            {
              lex_error (lexer, _("Cannot use macro keyword \"%s\" "
                                  "as an argument name."),
                         lex_tokcstr (lexer));
              goto error;
            }

          p->positional = false;
          p->name = xasprintf ("!%s", lex_tokcstr (lexer));
          lex_get (lexer);

          if (!lex_force_match (lexer, T_EQUALS))
            goto error;
        }

      bool saw_default = false;
      bool saw_arg_type = false;
      for (;;)
        {
          if (match_macro_id (lexer, "!DEFAULT"))
            {
              if (saw_default)
                {
                  lex_error (lexer,
                             _("!DEFAULT is allowed only once per argument."));
                  goto error;
                }
              saw_default = true;

              if (!lex_force_match (lexer, T_LPAREN))
                goto error;

              /* XXX Should this handle balanced inner parentheses? */
              while (!lex_match (lexer, T_RPAREN))
                {
                  if (lex_token (lexer) == T_ENDCMD)
                    {
                      lex_error_expecting (lexer, ")");
                      goto error;
                    }
                  char *syntax = lex_next_representation (lexer, 0, 0);
                  const struct macro_token mt = {
                    .token = *lex_next (lexer, 0),
                    .syntax = ss_cstr (syntax),
                  };
                  macro_tokens_add (&p->def, &mt);
                  free (syntax);

                  lex_get (lexer);
                }
            }
          else if (match_macro_id (lexer, "!NOEXPAND"))
            p->expand_arg = false;
          else if (match_macro_id (lexer, "!TOKENS"))
            {
              if (!dup_arg_type (lexer, &saw_arg_type)
                  || !lex_force_match (lexer, T_LPAREN)
                  || !lex_force_int_range (lexer, "!TOKENS", 1, INT_MAX))
                goto error;
              p->arg_type = ARG_N_TOKENS;
              p->n_tokens = lex_integer (lexer);
              lex_get (lexer);
              if (!lex_force_match (lexer, T_RPAREN))
                goto error;
            }
          else if (match_macro_id (lexer, "!CHAREND"))
            {
              if (!dup_arg_type (lexer, &saw_arg_type))
                goto error;

              p->arg_type = ARG_CHAREND;
              p->charend = (struct token) { .type = T_STOP };

              if (!lex_force_match (lexer, T_LPAREN)
                  || !parse_quoted_token (lexer, &p->charend)
                  || !lex_force_match (lexer, T_RPAREN))
                goto error;
            }
          else if (match_macro_id (lexer, "!ENCLOSE"))
            {
              if (!dup_arg_type (lexer, &saw_arg_type))
                goto error;

              p->arg_type = ARG_ENCLOSE;
              p->enclose[0] = p->enclose[1] = (struct token) { .type = T_STOP };

              if (!lex_force_match (lexer, T_LPAREN)
                  || !parse_quoted_token (lexer, &p->enclose[0])
                  || !lex_force_match (lexer, T_COMMA)
                  || !parse_quoted_token (lexer, &p->enclose[1])
                  || !lex_force_match (lexer, T_RPAREN))
                goto error;
            }
          else if (match_macro_id (lexer, "!CMDEND"))
            {
              if (!dup_arg_type (lexer, &saw_arg_type))
                goto error;

              p->arg_type = ARG_CMDEND;
            }
          else
            break;
        }
      if (!saw_arg_type)
        {
          lex_error_expecting (lexer, "!TOKENS", "!CHAREND", "!ENCLOSE",
                               "!CMDEND");
          goto error;
        }

      if (lex_token (lexer) != T_RPAREN && !lex_force_match (lexer, T_SLASH))
        goto error;
    }

  struct string body = DS_EMPTY_INITIALIZER;
  while (!match_macro_id (lexer, "!ENDDEFINE"))
    {
      if (lex_token (lexer) != T_STRING)
        {
          lex_error (lexer, _("Expecting macro body or !ENDDEFINE"));
          ds_destroy (&body);
          goto error;
        }

      ds_put_substring (&body, lex_tokss (lexer));
      ds_put_byte (&body, '\n');
      lex_get (lexer);
    }
  m->location->last_line = lex_get_last_line_number (lexer, 0);

  macro_tokens_from_string (&m->body, body.ss, lex_get_syntax_mode (lexer));
  ds_destroy (&body);

  lex_define_macro (lexer, m);

  return CMD_SUCCESS;

error:
  macro_destroy (m);
  return CMD_FAILURE;
}

int
cmd_debug_expand (struct lexer *lexer, struct dataset *ds UNUSED)
{
  settings_set_mprint (true);

  while (lex_token (lexer) != T_STOP)
    {
      if (!lex_next_is_from_macro (lexer, 0) && lex_token (lexer) != T_ENDCMD)
        {
          char *rep = lex_next_representation (lexer, 0, 0);
          msg (MN, "unexpanded token \"%s\"", rep);
          free (rep);
        }
      lex_get (lexer);
    }
  return CMD_SUCCESS;
}
