/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "language/command.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/session.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/lexer/command-name.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/driver.h"
#include "output/output-item.h"

#include "xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Returns true if RESULT is a valid "enum cmd_result",
   false otherwise. */
static inline bool
cmd_result_is_valid (enum cmd_result result)
{
  switch (result)
    {
    case CMD_SUCCESS:
    case CMD_EOF:
    case CMD_FINISH:
    case CMD_FAILURE:
    case CMD_NOT_IMPLEMENTED:
    case CMD_CASCADING_FAILURE:
      return true;

    default:
      return false;
    }
}

/* Returns true if RESULT indicates success,
   false otherwise. */
bool
cmd_result_is_success (enum cmd_result result)
{
  assert (cmd_result_is_valid (result));
  return result > 0;
}

/* Returns true if RESULT indicates failure,
   false otherwise. */
bool
cmd_result_is_failure (enum cmd_result result)
{
  assert (cmd_result_is_valid (result));
  return result < 0;
}

/* Command processing states. */
enum states
  {
    S_INITIAL = 1 << CMD_STATE_INITIAL,
    S_DATA = 1 << CMD_STATE_DATA,
    S_INPUT_PROGRAM = 1 << CMD_STATE_INPUT_PROGRAM,
    S_FILE_TYPE = 1 << CMD_STATE_FILE_TYPE,
    S_NESTED_DATA = 1 << CMD_STATE_NESTED_DATA,
    S_NESTED_INPUT_PROGRAM = 1 << CMD_STATE_NESTED_INPUT_PROGRAM,

    S_NESTED_ANY = S_NESTED_DATA | S_NESTED_INPUT_PROGRAM,
    S_ANY = S_INITIAL | S_DATA | S_INPUT_PROGRAM | S_FILE_TYPE | S_NESTED_ANY,
  };

/* Other command requirements. */
enum flags
  {
    F_ENHANCED = 1 << 0,        /* Allowed only in enhanced syntax mode. */
    F_TESTING = 1 << 1,         /* Allowed only in testing mode. */
    F_ABBREV = 1 << 2           /* Not a candidate for name completion. */
  };

/* A single command. */
struct command
  {
    enum states states;         /* States in which command is allowed. */
    enum flags flags;           /* Other command requirements. */
    const char *name;                /* Command name. */
    const char *label;                /* Translated command label. */
    int (*function) (struct lexer *, struct dataset *);        /* Function to call. */
  };

/* Define the command array. */
#define DEF_CMD(STATES, FLAGS, NAME, LABEL, FUNCTION) {STATES, FLAGS, NAME, LABEL, FUNCTION},
#define UNIMPL_CMD(NAME, DESCRIPTION) {S_ANY, 0, NAME, NAME, NULL},
static const struct command commands[] =
  {
#include "command.def"
  };
#undef DEF_CMD
#undef UNIMPL_CMD

static const size_t n_commands = sizeof commands / sizeof *commands;

static bool in_correct_state (const struct command *, enum cmd_state);
static char *report_state_mismatch (const struct command *, enum cmd_state);
static void set_completion_state (enum cmd_state);

/* Command parser. */

static const struct command *parse_command_name (struct lexer *,
                                                 int *n_tokens);
static enum cmd_result do_parse_command (struct lexer *, struct dataset *, enum cmd_state);

/* Parses an entire command, from command name to terminating
   dot.  On failure, skips to the terminating dot.
   Returns the command's success or failure result. */
enum cmd_result
cmd_parse_in_state (struct lexer *lexer, struct dataset *ds,
                    enum cmd_state state)
{
  struct session *session = dataset_session (ds);
  int result;

  result = do_parse_command (lexer, ds, state);

  ds = session_active_dataset (session);
  assert (!proc_is_open (ds));
  unset_cmd_algorithm ();
  if (!dataset_end_of_command (ds))
    result = CMD_CASCADING_FAILURE;

  return result;
}

enum cmd_result
cmd_parse (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  return cmd_parse_in_state (lexer, ds,
                             dataset_has_source (ds) &&
                             dict_get_n_vars (dict) > 0 ?
                             CMD_STATE_DATA : CMD_STATE_INITIAL);
}


/* Parses an entire command, from command name to terminating
   dot. */
static enum cmd_result
do_parse_command (struct lexer *lexer,
                  struct dataset *ds, enum cmd_state state)
{
  const struct command *command = NULL;
  size_t nesting_level = SIZE_MAX;
  enum cmd_result result;
  int n_tokens;

  /* Read the command's first token. */
  set_completion_state (state);
  if (lex_token (lexer) == T_STOP)
    {
      result = CMD_EOF;
      goto finish;
    }
  else if (lex_token (lexer) == T_ENDCMD)
    {
      /* Null commands can result from extra empty lines. */
      result = CMD_SUCCESS;
      goto finish;
    }

  /* Parse the command name. */
  command = parse_command_name (lexer, &n_tokens);
  if (command == NULL)
    {
      result = CMD_FAILURE;
      goto finish;
    }

  nesting_level = output_open_group (group_item_create (
                                       command->name,
                                       gettext (command->label)));

  int end = n_tokens - 1;
  if (command->function == NULL)
    {
      lex_ofs_error (lexer, 0, end, _("%s is not yet implemented."),
                     command->name);
      result = CMD_NOT_IMPLEMENTED;
    }
  else if ((command->flags & F_TESTING) && !settings_get_testing_mode ())
    {
      lex_ofs_error (lexer, 0, end, _("%s may be used only in testing mode."),
                     command->name);
      result = CMD_FAILURE;
    }
  else if ((command->flags & F_ENHANCED) && settings_get_syntax () != ENHANCED)
    {
      lex_ofs_error (lexer, 0, end,
                     _("%s may be used only in enhanced syntax mode."),
                     command->name);
      result = CMD_FAILURE;
    }
  else if (!in_correct_state (command, state))
    {
      char *message = report_state_mismatch (command, state);
      lex_ofs_error (lexer, 0, end, "%s", message);
      free (message);

      result = CMD_FAILURE;
    }
  else
    {
      /* Execute command. */
      int i;

      for (i = 0; i < n_tokens; i++)
        lex_get (lexer);
      result = command->function (lexer, ds);
    }

  assert (cmd_result_is_valid (result));

finish:
  if (cmd_result_is_failure (result))
    lex_interactive_reset (lexer);
  else if (result == CMD_SUCCESS)
    result = lex_end_of_command (lexer);

  lex_discard_rest_of_command (lexer);
  if (nesting_level != SIZE_MAX)
    output_close_groups (nesting_level);

  if (result != CMD_EOF && result != CMD_FINISH)
    while (lex_token (lexer) == T_ENDCMD)
      lex_get (lexer);

  return result;
}

static int
find_best_match (struct substring s, const struct command **matchp)
{
  const struct command *cmd;
  struct command_matcher cm;
  int missing_words;

  command_matcher_init (&cm, s);
  for (cmd = commands; cmd < &commands[n_commands]; cmd++)
    command_matcher_add (&cm, ss_cstr (cmd->name), CONST_CAST (void *, cmd));

  *matchp = command_matcher_get_match (&cm);
  missing_words = command_matcher_get_missing_words (&cm);

  command_matcher_destroy (&cm);

  return missing_words;
}

static bool
parse_command_word (struct lexer *lexer, struct string *s, int n)
{
  bool need_space = ds_last (s) != EOF && ds_last (s) != '-';

  switch (lex_next_token (lexer, n))
    {
    case T_DASH:
      ds_put_byte (s, '-');
      return true;

    case T_ID:
      if (need_space)
        ds_put_byte (s, ' ');
      ds_put_cstr (s, lex_next_tokcstr (lexer, n));
      return true;

    case T_POS_NUM:
      if (lex_next_is_integer (lexer, n))
        {
          int integer = lex_next_integer (lexer, n);
          if (integer >= 0)
            {
              if (need_space)
                ds_put_byte (s, ' ');
              ds_put_format (s, "%ld", lex_next_integer (lexer, n));
              return true;
            }
        }
      return false;

    default:
      return false;
    }
}

/* Parses the command name.  On success returns a pointer to the corresponding
   struct command and stores the number of tokens in the command name into
   *N_TOKENS.  On failure, returns a null pointer and stores the number of
   tokens required to determine that no command name was present into
   *N_TOKENS. */
static const struct command *
parse_command_name (struct lexer *lexer, int *n_tokens)
{
  const struct command *command;
  int missing_words;
  struct string s;
  int word;

  command = NULL;
  missing_words = 0;
  ds_init_empty (&s);
  word = 0;
  while (parse_command_word (lexer, &s, word))
    {
      missing_words = find_best_match (ds_ss (&s), &command);
      if (missing_words <= 0)
        break;
      word++;
    }

  if (command == NULL && missing_words > 0)
    {
      ds_put_cstr (&s, " .");
      missing_words = find_best_match (ds_ss (&s), &command);
      ds_truncate (&s, ds_length (&s) - 2);
    }

  *n_tokens = (word + 1) + missing_words;
  if (command == NULL)
    {
      if (ds_is_empty (&s))
        lex_error (lexer, _("Syntax error expecting command name."));
      else
        lex_ofs_error (lexer, 0, *n_tokens - 1,
                       _("Unknown command `%s'."), ds_cstr (&s));
    }

  ds_destroy (&s);

  return command;
}

/* Returns true if COMMAND is allowed in STATE,
   false otherwise. */
static bool
in_correct_state (const struct command *command, enum cmd_state state)
{
  return command->states & (1 << state);
}

/* Returns an appropriate error message for trying to invoke
   COMMAND in STATE. */
static char *
report_state_mismatch (const struct command *command, enum cmd_state state)
{
  assert (!in_correct_state (command, state));

  switch (state)
    {
    case CMD_STATE_INITIAL:
    case CMD_STATE_DATA:
      switch ((int) command->states
              & (S_INITIAL | S_DATA | S_INPUT_PROGRAM | S_FILE_TYPE))
        {
          /* One allowed state. */
        case S_INITIAL:
          return xasprintf (_("%s is allowed only before the active dataset has "
                              "been defined."), command->name);
        case S_DATA:
          return xasprintf (_("%s is allowed only after the active dataset has "
                              "been defined."), command->name);
        case S_INPUT_PROGRAM:
          return xasprintf (_("%s is allowed only inside %s."),
                            command->name, "INPUT PROGRAM");
        case S_FILE_TYPE:
          return xasprintf (_("%s is allowed only inside %s."), command->name, "FILE TYPE");

          /* Two allowed states. */
        case S_INITIAL | S_DATA:
          NOT_REACHED ();
        case S_INITIAL | S_INPUT_PROGRAM:
          return xasprintf (_("%s is allowed only before the active dataset "
                              "has been defined or inside %s."),
                            command->name, "INPUT PROGRAM");
        case S_INITIAL | S_FILE_TYPE:
          return xasprintf (_("%s is allowed only before the active dataset "
                              "has been defined or inside %s."),
                            command->name, "FILE TYPE");
        case S_DATA | S_INPUT_PROGRAM:
          return xasprintf (_("%s is allowed only after the active dataset "
                              "has been defined or inside %s."),
                            command->name, "INPUT PROGRAM");
        case S_DATA | S_FILE_TYPE:
          return xasprintf (_("%s is allowed only after the active dataset "
                              "has been defined or inside %s."),
                            command->name, "FILE TYPE");
        case S_INPUT_PROGRAM | S_FILE_TYPE:
          return xasprintf (_("%s is allowed only inside %s or inside %s."),
                            command->name, "INPUT PROGRAM", "FILE TYPE");

          /* Three allowed states. */
        case S_DATA | S_INPUT_PROGRAM | S_FILE_TYPE:
          return xasprintf (_("%s is allowed only after the active dataset has "
                              "been defined, inside INPUT PROGRAM, or inside "
                              "FILE TYPE."), command->name);
        case S_INITIAL | S_INPUT_PROGRAM | S_FILE_TYPE:
          return xasprintf (_("%s is allowed only before the active dataset "
                              "has been defined, inside INPUT PROGRAM, or "
                              "inside FILE TYPE."), command->name);
        case S_INITIAL | S_DATA | S_FILE_TYPE:
          NOT_REACHED ();
        case S_INITIAL | S_DATA | S_INPUT_PROGRAM:
          NOT_REACHED ();

          /* Four allowed states. */
        case S_INITIAL | S_DATA | S_INPUT_PROGRAM | S_FILE_TYPE:
          NOT_REACHED ();

        default:
          NOT_REACHED ();
        }
      break;

    case CMD_STATE_INPUT_PROGRAM:
      return xasprintf (_("%s is not allowed inside %s."),
                        command->name, "INPUT PROGRAM");

    case CMD_STATE_FILE_TYPE:
      return xasprintf (_("%s is not allowed inside %s."),
                        command->name, "FILE TYPE");

    case CMD_STATE_NESTED_DATA:
    case CMD_STATE_NESTED_INPUT_PROGRAM:
      switch ((int) command->states & S_NESTED_ANY)
        {
        case 0:
          return xasprintf (_("%s is not allowed inside DO IF or LOOP."),
                            command->name);

        case S_NESTED_DATA:
          return xasprintf (_("In INPUT PROGRAM, "
                              "%s is not allowed inside DO IF or LOOP."),
                            command->name);

        case S_NESTED_INPUT_PROGRAM:
          return xasprintf (_("%s is not allowed inside DO IF or LOOP "
                              "(except inside INPUT PROGRAM)."),
                            command->name);

        default:
          NOT_REACHED ();
        }
    }

  NOT_REACHED ();
}

/* Command name completion. */

static enum cmd_state completion_state = CMD_STATE_INITIAL;

static void
set_completion_state (enum cmd_state state)
{
  completion_state = state;
}

/* Returns the next possible completion of a command name that
   begins with PREFIX, in the current command state, or a null
   pointer if no completions remain.
   Before calling the first time, set *CMD to a null pointer. */
const char *
cmd_complete (const char *prefix, const struct command **cmd)
{
  if (*cmd == NULL)
    *cmd = commands;

  for (; *cmd < commands + n_commands; (*cmd)++)
    if (!memcasecmp ((*cmd)->name, prefix, strlen (prefix))
        && (!((*cmd)->flags & F_TESTING) || settings_get_testing_mode ())
        && (!((*cmd)->flags & F_ENHANCED) || settings_get_syntax () == ENHANCED)
        && !((*cmd)->flags & F_ABBREV)
        && ((*cmd)->function != NULL)
        && in_correct_state (*cmd, completion_state))
      return (*cmd)++->name;

  return NULL;
}

/* Simple commands. */

/* Parse and execute FINISH command. */
int
cmd_finish (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  return CMD_FINISH;
}

/* Parses the N command. */
int
cmd_n_of_cases (struct lexer *lexer, struct dataset *ds)
{
  if (!lex_force_int_range (lexer, "N OF CASES", 1, LONG_MAX))
    return CMD_FAILURE;
  long n = lex_integer (lexer);
  lex_get (lexer);
  if (!lex_match_id (lexer, "ESTIMATED"))
    dict_set_case_limit (dataset_dict (ds), n);

  return CMD_SUCCESS;
}

/* Parses, performs the EXECUTE procedure. */
int
cmd_execute (struct lexer *lexer UNUSED, struct dataset *ds)
{
  bool ok = casereader_destroy (proc_open (ds));
  if (!proc_commit (ds) || !ok)
    return CMD_CASCADING_FAILURE;
  return CMD_SUCCESS;
}

/* Parses, performs the ERASE command. */
int
cmd_erase (struct lexer *lexer, struct dataset *ds UNUSED)
{
  char *filename;
  int retval;

  if (settings_get_safer_mode ())
    {
      lex_ofs_error (lexer, 0, 0,
                     _("This command not allowed when the %s option is set."),
                     "SAFER");
      return CMD_FAILURE;
    }

  if (!lex_force_match_id (lexer, "FILE"))
    return CMD_FAILURE;
  lex_match (lexer, T_EQUALS);
  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  filename = utf8_to_filename (lex_tokcstr (lexer));
  retval = remove (filename);
  free (filename);

  if (retval == -1)
    {
      msg (SW, _("Error removing `%s': %s."),
           lex_tokcstr (lexer), strerror (errno));
      return CMD_FAILURE;
    }
  lex_get (lexer);

  return CMD_SUCCESS;
}

/* Parses, performs the NEW FILE command. */
int
cmd_new_file (struct lexer *lexer UNUSED, struct dataset *ds)
{
  dataset_clear (ds);
  return CMD_SUCCESS;
}
