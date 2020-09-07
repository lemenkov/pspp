/* pspp - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "data/settings.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/temp-file.h"
#include "output/text-item.h"

#include "gl/error.h"
#include "gl/intprops.h"
#include "gl/localcharset.h"
#include "gl/read-file.h"
#include "gl/timespec.h"
#include "gl/xalloc.h"
#include "gl/xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#if !HAVE_FORK
static bool
run_commands (const struct string_array *commands, double time_limit)
{
  if (time_limit != DBL_MAX)
    {
      msg (SE, _("Time limit not supported on this platform."));
      return false;
    }

  for (size_t i = 0; i < commands->n; i++)
    {
      /* XXX No way to capture command output */
      char *s = recode_string (locale_charset (), "UTF-8",
                               commands->strings[i], -1);
      int retval = system (s);
      free (s);

      if (retval)
        {
          msg (SE, _("%s: Command exited with status %d."),
               commands->strings[i], retval);
          return false;
        }
    }
  return true;
}
#else
static bool
run_command (const char *command, struct timespec timeout)
{
  /* Same exit codes used by 'sh'. */
  enum {
    EXIT_CANNOT_INVOKE = 126,
    EXIT_ENOENT = 127,
  };

  /* Create a temporary file to capture command output. */
  FILE *output_file = create_temp_file ();
  if (!output_file)
    {
      msg (SE, _("Failed to create temporary file (%s)."), strerror (errno));
      return false;
    }

  int dev_null_fd = open ("/dev/null", O_RDONLY);
  if (dev_null_fd < 0)
    {
      msg (SE, _("/dev/null: Failed to open (%s)."), strerror (errno));
      fclose (output_file);
      return false;
    }

  char *locale_command = recode_string (locale_charset (), "UTF-8",
                                        command, -1);

  pid_t pid = fork ();
  if (pid < 0)
    {
      close (dev_null_fd);
      fclose (output_file);
      free (locale_command);

      msg (SE, _("Couldn't fork: %s."), strerror (errno));
      return false;
    }
  else if (!pid)
    {
      /* Running in the child. */

#if __GNU__
      /* Hurd doesn't support inheriting process timers in a way that works. */
      if (setpgid (0, 0) < 0)
        error (1, errno, _("Failed to set process group."));
#else
      /* Set up timeout. */
      if (timeout.tv_sec < TYPE_MAXIMUM (time_t))
        {
          signal (SIGALRM, SIG_DFL);

          struct timespec left = timespec_sub (timeout, current_timespec ());
          if (timespec_sign (left) <= 0)
            raise (SIGALRM);

          struct itimerval it = {
            .it_value = {
              .tv_sec = left.tv_sec,
              .tv_usec = left.tv_nsec / 1000
            }
          };
          if (setitimer (ITIMER_REAL, &it, NULL) < 0)
            error (1, errno, _("Failed to set timeout."));
        }
#endif

      /* Set up file descriptors:
         - /dev/null for stdin
         - Temporary file to capture stdout and stderr.
         - Close everything else.
      */
      dup2 (dev_null_fd, 0);
      dup2 (fileno (output_file), 1);
      dup2 (fileno (output_file), 2);
      close (dev_null_fd);
      for (int fd = 3; fd < 256; fd++)
        close (fd);

      /* Choose the shell. */
      const char *shell = getenv ("SHELL");
      if (shell == NULL)
        shell = "/bin/sh";

      /* Run subprocess. */
      execl (shell, shell, "-c", locale_command, NULL);

      /* Failed to start the shell. */
      _exit (errno == ENOENT ? EXIT_ENOENT : EXIT_CANNOT_INVOKE);
    }

  /* Running in the parent. */
  close (dev_null_fd);
  free (locale_command);

  /* Wait for child to exit. */
  int status = 0;
  int error = 0;
  for (;;)
    {
#if __GNU__
      if (timespec_cmp (current_timespec (), timeout) >= 0)
        kill (-pid, SIGALRM);

      int flags = WNOHANG;
#else
      int flags = 0;
#endif
      pid_t retval = waitpid (pid, &status, flags);
      if (retval == pid)
        break;
      else if (retval < 0)
        {
          if (errno != EINTR)
            {
              error = errno;
              break;
            }
        }
#if __GNU__
      else if (retval == 0)
        sleep (1);
#endif
      else
        NOT_REACHED ();
    }

  bool ok = true;
  if (error)
    {
      msg (SW, _("While running \"%s\", waiting for child process "
                 "failed (%s)."),
           command, strerror (errno));
      ok = false;
    }

  if (WIFSIGNALED (status))
    {
      int signum = WTERMSIG (status);
      if (signum == SIGALRM)
        msg (SW, _("Command \"%s\" timed out."), command);
      else
        msg (SW, _("Command \"%s\" terminated by signal %d."), command, signum);
      ok = false;
    }
  else if (WIFEXITED (status) && WEXITSTATUS (status))
    {
      int exit_code = WEXITSTATUS (status);
      const char *detail = (exit_code == EXIT_ENOENT
                            ? _("Command or shell not found")
                            : exit_code == EXIT_CANNOT_INVOKE
                            ? _("Could not invoke command or shell")
                            : NULL);
      if (detail)
        msg (SW, _("Command \"%s\" exited with status %d (%s)."),
             command, exit_code, detail);
      else
        msg (SW, _("Command \"%s\" exited with status %d."),
             command, exit_code);
      ok = false;
    }

  rewind (output_file);
  size_t length;
  char *locale_output = fread_file (output_file, 0, &length);
  if (!locale_output)
    {
      msg (SW, _("Command \"%s\" output could not be read (%s)."),
           command, strerror (errno));
      ok = false;
    }
  else if (length > 0)
    {
      char *output = recode_string ("UTF-8", locale_charset (),
                                    locale_output, -1);

      /* Drop final new-line, if any. */
      char *end = strchr (output, '\0');
      if (end > output && end[-1] == '\n')
        end[-1] = '\0';

      text_item_submit (text_item_create_nocopy (TEXT_ITEM_LOG, output));
    }
  free (locale_output);

  return ok;
}

static bool
run_commands (const struct string_array *commands, double time_limit)
{
  struct timespec timeout = timespec_add (dtotimespec (time_limit),
                                          current_timespec ());

  for (size_t i = 0; i < commands->n; i++)
    {
      if (!run_command (commands->strings[i], timeout))
        return false;
    }

  return true;
}
#endif

int
cmd_host (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (settings_get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the %s option is set."), "SAFER");
      return CMD_FAILURE;
    }

  if (!lex_force_match_id (lexer, "COMMAND")
      || !lex_force_match (lexer, T_EQUALS)
      || !lex_force_match (lexer, T_LBRACK)
      || !lex_force_string (lexer))
    return CMD_FAILURE;

  struct string_array commands = STRING_ARRAY_INITIALIZER;
  while (lex_token (lexer) == T_STRING)
    {
      string_array_append (&commands, lex_tokcstr (lexer));
      lex_get (lexer);
    }
  if (!lex_force_match (lexer, T_RBRACK))
    {
      string_array_destroy (&commands);
      return CMD_FAILURE;
    }

  double time_limit = DBL_MAX;
  if (lex_match_id (lexer, "TIMELIMIT"))
    {
      if (!lex_force_match (lexer, T_EQUALS)
          || !lex_force_num (lexer))
        {
          string_array_destroy (&commands);
          return CMD_FAILURE;
        }

      double num = lex_number (lexer);
      lex_get (lexer);
      time_limit = num < 0.0 ? 0.0 : num;
    }

  enum cmd_result result = lex_end_of_command (lexer);
  if (result == CMD_SUCCESS && !run_commands (&commands, time_limit))
    result = CMD_FAILURE;
  string_array_destroy (&commands);
  return result;
}
