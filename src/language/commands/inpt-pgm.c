/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/caseinit.h"
#include "data/casereader-provider.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/session.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/data-reader.h"
#include "language/commands/file-handle.h"
#include "language/commands/inpt-pgm.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Indicates how a `union value' should be initialized. */
struct input_program_pgm
  {
    struct session *session;
    struct dataset *ds;

    struct trns_chain xforms;
    size_t idx;
    bool eof;

    casenumber case_nr;             /* Incremented by END CASE transformation. */

    struct caseinit *init;
    struct caseproto *proto;
  };

static void destroy_input_program (struct input_program_pgm *);
static const struct trns_class end_case_trns_class;
static const struct trns_class reread_trns_class;
static const struct trns_class end_file_trns_class;

static const struct casereader_class input_program_casereader_class;

static bool inside_input_program;
static bool saw_END_CASE;
static bool saw_END_FILE;
static bool saw_DATA_LIST;

/* Returns true if we're parsing the inside of a INPUT
   PROGRAM...END INPUT PROGRAM construct, false otherwise. */
bool
in_input_program (void)
{
  return inside_input_program;
}

void
data_list_seen (void)
{
  saw_DATA_LIST = true;
}

/* Emits an END CASE transformation for INP. */
static void
emit_END_CASE (struct dataset *ds)
{
  add_transformation (ds, &end_case_trns_class, xzalloc (sizeof (bool)));
}

int
cmd_input_program (struct lexer *lexer, struct dataset *ds)
{
  struct msg_location *location = lex_ofs_location (lexer, 0, 1);
  if (!lex_match (lexer, T_ENDCMD))
    {
      msg_location_destroy (location);
      return lex_end_of_command (lexer);
    }

  struct session *session = session_create (dataset_session (ds));
  struct dataset *inp_ds = dataset_create (session, "INPUT PROGRAM");

  struct input_program_pgm *inp = xmalloc (sizeof *inp);
  *inp = (struct input_program_pgm) { .session = session, .ds = inp_ds };

  proc_push_transformations (inp->ds);
  inside_input_program = true;
  saw_END_CASE = saw_END_FILE = saw_DATA_LIST = false;
  while (!lex_match_phrase (lexer, "END INPUT PROGRAM"))
    {
      enum cmd_result result;

      result = cmd_parse_in_state (lexer, inp->ds, CMD_STATE_INPUT_PROGRAM);
      if (result == CMD_EOF
          || result == CMD_FINISH
          || result == CMD_CASCADING_FAILURE)
        {
          proc_pop_transformations (inp->ds, &inp->xforms);

          if (result == CMD_EOF)
            msg (SE, _("Unexpected end-of-file within %s."), "INPUT PROGRAM");
          inside_input_program = false;
          destroy_input_program (inp);
          msg_location_destroy (location);
          return result;
        }
    }
  if (!saw_END_CASE)
    emit_END_CASE (inp->ds);
  inside_input_program = false;
  proc_pop_transformations (inp->ds, &inp->xforms);

  struct msg_location *end = lex_ofs_location (lexer, 0, 2);
  msg_location_merge (&location, end);
  location->omit_underlines = true;
  msg_location_destroy (end);

  if (!saw_DATA_LIST && !saw_END_FILE)
    {
      msg_at (SE, location, _("Input program does not contain %s or %s."),
              "DATA LIST", "END FILE");
      destroy_input_program (inp);
      msg_location_destroy (location);
      return CMD_FAILURE;
    }
  if (dict_get_n_vars (dataset_dict (inp->ds)) == 0)
    {
      msg_at (SE, location, _("Input program did not create any variables."));
      destroy_input_program (inp);
      msg_location_destroy (location);
      return CMD_FAILURE;
    }
  msg_location_destroy (location);

  /* Figure out how to initialize each input case. */
  inp->init = caseinit_create ();
  caseinit_mark_for_init (inp->init, dataset_dict (inp->ds));
  inp->proto = caseproto_ref (dict_get_proto (dataset_dict (inp->ds)));

  dataset_set_dict (ds, dict_clone (dataset_dict (inp->ds)));
  dataset_set_source (
    ds, casereader_create_sequential (NULL, inp->proto, CASENUMBER_MAX,
                                      &input_program_casereader_class, inp));

  return CMD_SUCCESS;
}

/* Reads and returns one case.
   Returns the case if successful, null at end of file or if an
   I/O error occurred. */
static struct ccase *
input_program_casereader_read (struct casereader *reader UNUSED, void *inp_)
{
  struct input_program_pgm *inp = inp_;

  if (inp->eof || !inp->xforms.n)
    return NULL;

  struct ccase *c = case_create (inp->proto);
  caseinit_init_vars (inp->init, c);
  caseinit_restore_left_vars (inp->init, c);

  for (size_t i = inp->idx < inp->xforms.n ? inp->idx : 0; ; i++)
    {
      if (i >= inp->xforms.n)
        {
          i = 0;
          c = case_unshare (c);
          caseinit_save_left_vars (inp->init, c);
          caseinit_init_vars (inp->init, c);
        }

      const struct transformation *trns = &inp->xforms.xforms[i];
      switch (trns->class->execute (trns->aux, &c, inp->case_nr))
        {
        case TRNS_END_CASE:
          inp->case_nr++;
          inp->idx = i;
          return c;

        case TRNS_ERROR:
          casereader_force_error (reader);
          /* Fall through. */
        case TRNS_END_FILE:
          inp->eof = true;
          case_unref (c);
          return NULL;

        case TRNS_CONTINUE:
          break;

        default:
          NOT_REACHED ();
        }
    }
}

static void
destroy_input_program (struct input_program_pgm *pgm)
{
  if (pgm != NULL)
    {
      session_destroy (pgm->session);
      trns_chain_uninit (&pgm->xforms);
      caseinit_destroy (pgm->init);
      caseproto_unref (pgm->proto);
      free (pgm);
    }
}

/* Destroys the casereader. */
static void
input_program_casereader_destroy (struct casereader *reader UNUSED, void *inp_)
{
  struct input_program_pgm *inp = inp_;
  destroy_input_program (inp);
}

static const struct casereader_class input_program_casereader_class =
  {
    input_program_casereader_read,
    input_program_casereader_destroy,
    NULL,
    NULL,
  };

int
cmd_end_case (struct lexer *lexer UNUSED, struct dataset *ds)
{
  assert (in_input_program ());
  emit_END_CASE (ds);
  saw_END_CASE = true;
  return CMD_SUCCESS;
}

/* Outputs the current case */
static enum trns_result
end_case_trns_proc (void *resume_, struct ccase **c UNUSED,
                    casenumber case_nr UNUSED)
{
  bool *resume = resume_;
  enum trns_result retval = *resume ? TRNS_CONTINUE : TRNS_END_CASE;
  *resume = !*resume;
  return retval;
}

static bool
end_case_trns_free (void *resume)
{
  free (resume);
  return true;
}

static const struct trns_class end_case_trns_class = {
  .name = "END CASE",
  .execute = end_case_trns_proc,
  .destroy = end_case_trns_free,
};

/* REREAD transformation. */
struct reread_trns
  {
    struct dfm_reader *reader;        /* File to move file pointer back on. */
    struct expression *column;        /* Column to reset file pointer to. */
  };

/* Parses REREAD command. */
int
cmd_reread (struct lexer *lexer, struct dataset *ds)
{
  char *encoding = NULL;
  struct file_handle *fh = fh_get_default_handle ();
  struct expression *e = NULL;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "COLUMN"))
        {
          lex_match (lexer, T_EQUALS);

          if (e)
            {
              lex_sbc_only_once (lexer, "COLUMN");
              goto error;
            }

          e = expr_parse (lexer, ds, VAL_NUMERIC);
          if (!e)
            goto error;
        }
      else if (lex_match_id (lexer, "FILE"))
        {
          lex_match (lexer, T_EQUALS);
          fh_unref (fh);
          fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
          if (fh == NULL)
            goto error;
        }
      else if (lex_match_id (lexer, "ENCODING"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

          lex_get (lexer);
        }
      else
        {
          lex_error_expecting (lexer, "COLUMN", "FILE", "ENCODING");
          goto error;
        }
    }

  struct reread_trns *t = xmalloc (sizeof *t);
  *t = (struct reread_trns) {
    .reader = dfm_open_reader (fh, lexer, encoding),
    .column = e,
  };
  add_transformation (ds, &reread_trns_class, t);

  fh_unref (fh);
  free (encoding);
  return CMD_SUCCESS;

error:
  expr_free (e);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}

/* Executes a REREAD transformation. */
static enum trns_result
reread_trns_proc (void *t_, struct ccase **c, casenumber case_num)
{
  struct reread_trns *t = t_;

  if (t->column == NULL)
    dfm_reread_record (t->reader, 1);
  else
    {
      double column = expr_evaluate_num (t->column, *c, case_num);
      if (!isfinite (column) || column < 1)
        {
          msg (SE, _("REREAD: Column numbers must be positive finite "
               "numbers.  Column set to 1."));
          dfm_reread_record (t->reader, 1);
        }
      else
        dfm_reread_record (t->reader, column);
    }
  return TRNS_CONTINUE;
}

/* Frees a REREAD transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
reread_trns_free (void *t_)
{
  struct reread_trns *t = t_;
  expr_free (t->column);
  dfm_close_reader (t->reader);
  return true;
}

static const struct trns_class reread_trns_class = {
  .name = "REREAD",
  .execute = reread_trns_proc,
  .destroy = reread_trns_free,
};

/* Parses END FILE command. */
int
cmd_end_file (struct lexer *lexer UNUSED, struct dataset *ds)
{
  assert (in_input_program ());

  add_transformation (ds, &end_file_trns_class, NULL);
  saw_END_FILE = true;

  return CMD_SUCCESS;
}

/* Executes an END FILE transformation. */
static enum trns_result
end_file_trns_proc (void *trns_ UNUSED, struct ccase **c UNUSED,
                    casenumber case_num UNUSED)
{
  return TRNS_END_FILE;
}

static const struct trns_class end_file_trns_class = {
  .name = "END FILE",
  .execute = end_file_trns_proc,
};
