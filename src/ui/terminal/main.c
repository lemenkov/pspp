/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2006-2007, 2009-2014 Free Software Foundation, Inc.

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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#if HAVE_FPU_CONTROL_H
#include <fpu_control.h>
#endif
#if HAVE_FENV_H
#include <fenv.h>
#endif
#if HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#include <unistd.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/session.h"
#include "data/settings.h"
#include "data/variable.h"
#include "gsl/gsl_errno.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/include-path.h"
#include "libpspp/argv-parser.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "math/random.h"
#include "output/driver.h"
#include "output/output-item.h"
#include "ui/source-init-opts.h"
#include "ui/terminal/terminal-opts.h"
#include "ui/terminal/terminal-reader.h"

#include "gl/fatal-signal.h"
#include "gl/progname.h"
#include "gl/relocatable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static struct session *the_session;

static void add_syntax_reader (struct lexer *, const char *file_name,
                               const char *encoding, enum segmenter_mode);
static void bug_handler(int sig);
static void fpu_init (void);
static void output_msg (const struct msg *, struct lexer *);

/* Program entry point. */
int
main (int argc, char **argv)
{
  struct terminal_opts *terminal_opts;
  struct argv_parser *parser;
  enum segmenter_mode syntax_mode;
  char *syntax_encoding;
  bool process_statrc;
  struct lexer *lexer;

  set_program_name (argv[0]);

  prepare_fatal_error_message ();
  prepare_diagnostic_information ();

  signal (SIGABRT, bug_handler);
  signal (SIGSEGV, bug_handler);
  signal (SIGFPE, bug_handler);

  i18n_init ();
  fpu_init ();
  gsl_set_error_handler_off ();

  output_engine_push ();
  fh_init ();
  settings_init ();
  random_init ();

  lexer = lex_create ();
  the_session = session_create (NULL);
  dataset_create (the_session, "");

  parser = argv_parser_create ();
  terminal_opts = terminal_opts_init (parser, &syntax_mode, &process_statrc,
                                      &syntax_encoding);
  source_init_register_argv_parser (parser);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  terminal_opts_done (terminal_opts, argc, argv);
  argv_parser_destroy (parser);

  lex_set_message_handler (lexer, output_msg);
  session_set_default_syntax_encoding (the_session, syntax_encoding);

  /* Add syntax files to source stream. */
  if (process_statrc)
    {
      char *rc = include_path_search ("rc");
      if (rc != NULL)
        {
          add_syntax_reader (lexer, rc, "Auto", SEG_MODE_AUTO);
          free (rc);
        }
    }
  if (optind < argc)
    {
      int i;

      for (i = optind; i < argc; i++)
        add_syntax_reader (lexer, argv[i], syntax_encoding, syntax_mode);
    }
  else
    add_syntax_reader (lexer, "-", syntax_encoding, syntax_mode);

  /* Parse and execute syntax. */
  lex_get (lexer);
  for (;;)
    {
      int result = cmd_parse (lexer, session_active_dataset (the_session));

      if (result == CMD_EOF || result == CMD_FINISH)
        break;
      else if (cmd_result_is_failure (result) && lex_token (lexer) != T_STOP)
        {
          switch (lex_get_error_mode (lexer))
            {
            case LEX_ERROR_STOP:
              msg (MW, _("Error encountered while ERROR=STOP is effective."));
              lex_discard_noninteractive (lexer);
              break;

            case LEX_ERROR_CONTINUE:
              if (result == CMD_CASCADING_FAILURE)
                {
                  msg (SE, _("Stopping syntax file processing here to avoid "
                             "a cascade of dependent command failures."));
                  lex_discard_noninteractive (lexer);
                }
              break;

            case LEX_ERROR_TERMINAL:
            case LEX_ERROR_IGNORE:
              break;
            }
        }

      if (msg_ui_too_many_errors ())
        lex_discard_noninteractive (lexer);
    }


  output_engine_pop ();
  session_destroy (the_session);

  random_done ();
  settings_done ();
  fh_done ();
  lex_destroy (lexer);
  i18n_done ();

  return msg_ui_any_errors ();
}

static void
fpu_init (void)
{
#if HAVE_FEHOLDEXCEPT
  fenv_t foo;
  feholdexcept (&foo);
#elif HAVE___SETFPUCW && defined(_FPU_IEEE)
  __setfpucw (_FPU_IEEE);
#elif HAVE_FPSETMASK
  fpsetmask (0);
#endif
}

/* If a segfault happens, issue a message to that effect and halt */
static void
bug_handler(int sig)
{
  /* Reset SIG to its default handling so that if it happens again we won't
     recurse. */
  signal (sig, SIG_DFL);

  switch (sig)
    {
    case SIGABRT:
      request_bug_report("Assertion Failure/Abort");
      break;
    case SIGFPE:
      request_bug_report("Floating Point Exception");
      break;
    case SIGSEGV:
      request_bug_report("Segmentation Violation");
      break;
    default:
      request_bug_report("Unknown");
      break;
    }

  /* Re-raise the signal so that we terminate with the correct status. */
  raise (sig);
}

static void
output_msg (const struct msg *m_, struct lexer *lexer)
{
  struct msg_location *location = m_->location;
  if (!location && lexer)
    {
      location = lex_get_location (lexer, 0, 0);
      msg_location_remove_columns (location);
    }

  struct msg m = {
    .category = m_->category,
    .severity = m_->severity,
    .stack = m_->stack,
    .n_stack = m_->n_stack,
    .location = location,
    .command_name = output_get_uppercase_command_name (),
    .text = m_->text,
  };

  output_item_submit (message_item_create (&m));

  free (m.command_name);
  if (m.location != m_->location)
    msg_location_destroy (m.location);
}

static void
add_syntax_reader (struct lexer *lexer, const char *file_name,
                   const char *encoding, enum segmenter_mode syntax_mode)
{
  struct lex_reader *reader;

  bool interactive;
  if (!strcmp (file_name, "-"))
    {
      /* This allows the testsuite to simulate interactive behavior by setting
         PSPP_INTERACTIVE=1 in the environment. */
      const char *env = getenv ("PSPP_INTERACTIVE");
      interactive = env ? strcmp (env, "0") : isatty (STDIN_FILENO);
    }
  else
    interactive = false;

  reader = (interactive
            ? terminal_reader_create ()
            : lex_reader_for_file (file_name, encoding, syntax_mode,
                                   LEX_ERROR_CONTINUE));

  if (reader)
    lex_append (lexer, reader);
}
