/* PSPP - a program for statistical analysis.
   Copyright (C) 2013, 2014, 2015, 2016 Free Software Foundation, Inc.

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

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "data/any-reader.h"
#include "data/case-map.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/csv-file-writer.h"
#include "data/dictionary.h"
#include "data/encrypted-file.h"
#include "data/file-name.h"
#include "data/por-file-writer.h"
#include "data/settings.h"
#include "data/sys-file-writer.h"
#include "data/file-handle-def.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"

#include "gl/error.h"
#include "gl/getpass.h"
#include "gl/localcharset.h"
#include "gl/progname.h"
#include "gl/version-etc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static void usage (void);

static bool decrypt_file (struct encrypted_file *enc,
                          const struct file_handle *input_filename,
                          const struct file_handle *output_filename,
                          const char *password,
                          const char *alphabet, int max_length,
                          const char *password_list);

static void
parse_character_option (const char *arg, const char *option_name, char *out)
{
  if (strlen (arg) != 1)
    {
      /* XXX support multibyte characters */
      error (1, 0, _("%s argument must be a single character"), option_name);
    }
  *out = arg[0];
}

static bool
parse_variables_option (const char *arg, struct dictionary *dict,
                        struct variable ***vars, size_t *n_vars)
{
  struct lexer *lexer = lex_create ();
  lex_append (lexer, lex_reader_for_string (arg, locale_charset ()));
  lex_get (lexer);

  bool ok = parse_variables (lexer, dict, vars, n_vars, 0);
  if (ok && (lex_token (lexer) != T_STOP && lex_token (lexer) != T_ENDCMD))
    {
      lex_error (lexer, _("Syntax error expecting variable name."));
      ok = false;
    }

  lex_destroy (lexer);
  if (!ok)
    {
      free (*vars);
      *vars = NULL;
      *n_vars = 0;
    }
  return ok;
}

int
main (int argc, char *argv[])
{
  const char *input_filename;
  const char *output_filename;

  long long int max_cases = LLONG_MAX;
  const char *keep = NULL;
  const char *drop = NULL;
  struct dictionary *dict = NULL;
  struct casereader *reader = NULL;
  struct file_handle *input_fh = NULL;
  const char *encoding = NULL;
  struct encrypted_file *enc;

  const char *output_format = NULL;
  struct file_handle *output_fh = NULL;
  struct casewriter *writer;
  const char *password = NULL;
  struct string alphabet = DS_EMPTY_INITIALIZER;
  const char *password_list = NULL;
  int length = 0;

  struct csv_writer_options csv_opts = {
    .include_var_names = true,
    .decimal = '.',
    .delimiter = 0,             /* The default will be set later. */
    .qualifier = '"',
  };

  long long int i;

  set_program_name (argv[0]);
  i18n_init ();
  fh_init ();
  settings_init ();

  for (;;)
    {
      enum
        {
          OPT_PASSWORD_LIST = UCHAR_MAX + 1,
          OPT_RECODE,
          OPT_NO_VAR_NAMES,
          OPT_LABELS,
          OPT_PRINT_FORMATS,
          OPT_DECIMAL,
          OPT_DELIMITER,
          OPT_QUALIFIER,
        };
      static const struct option long_options[] =
        {
          { "cases", required_argument, NULL, 'c' },
          { "keep", required_argument, NULL, 'k' },
          { "drop", required_argument, NULL, 'd' },
          { "encoding", required_argument, NULL, 'e' },

          { "recode", no_argument, NULL, OPT_RECODE },
          { "no-var-names", no_argument, NULL, OPT_NO_VAR_NAMES },
          { "labels", no_argument, NULL, OPT_LABELS },
          { "print-formats", no_argument, NULL, OPT_PRINT_FORMATS },
          { "decimal", required_argument, NULL, OPT_DECIMAL },
          { "delimiter", required_argument, NULL, OPT_DELIMITER },
          { "qualifier", required_argument, NULL, OPT_QUALIFIER },

          { "password", required_argument, NULL, 'p' },
          { "password-alphabet", required_argument, NULL, 'a' },
          { "password-length", required_argument, NULL, 'l' },
          { "password-list", required_argument, NULL, OPT_PASSWORD_LIST },

          { "output-format", required_argument, NULL, 'O' },

          { "help",    no_argument,       NULL, 'h' },
          { "version", no_argument,       NULL, 'v' },
          { NULL,      0,                 NULL, 0 },
        };

      int c;

      c = getopt_long (argc, argv, "c:k:d:e:p:a:l:O:hv", long_options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'c':
          max_cases = strtoull (optarg, NULL, 0);
          break;

        case 'k':
          keep = optarg;
          break;

        case 'd':
          drop = optarg;
          break;

        case 'e':
          encoding = optarg;
          break;

        case 'p':
          password = optarg;
          break;

        case 'l':
          length = atoi (optarg);
          break;

        case OPT_PASSWORD_LIST:
          password_list = optarg;
          break;

        case OPT_RECODE:
          csv_opts.recode_user_missing = true;
          break;

        case OPT_NO_VAR_NAMES:
          csv_opts.include_var_names = false;
          break;

        case OPT_LABELS:
          csv_opts.use_value_labels = true;
          break;

        case OPT_DECIMAL:
          parse_character_option (optarg, "--decimal", &csv_opts.decimal);
          break;

        case OPT_DELIMITER:
          parse_character_option (optarg, "--delimiter", &csv_opts.delimiter);
          break;

        case OPT_QUALIFIER:
          parse_character_option (optarg, "--qualifier", &csv_opts.qualifier);
          break;

        case 'a':
          for (const char *p = optarg; *p;)
            if (p[1] == '-' && p[2] > p[0])
              {
                for (int ch = p[0]; ch <= p[2]; ch++)
                  ds_put_byte (&alphabet, ch);
                p += 3;
              }
            else
              ds_put_byte (&alphabet, *p++);
          break;

        case 'O':
          output_format = optarg;
          break;

        case 'v':
          version_etc (stdout, "pspp-convert", PACKAGE_NAME, PACKAGE_VERSION,
                       "Ben Pfaff", "John Darrington", NULL_SENTINEL);
          exit (EXIT_SUCCESS);

        case 'h':
          usage ();
          exit (EXIT_SUCCESS);

        default:
          goto error;
        }
    }

  if (optind + 2 != argc)
    error (1, 0, _("exactly two non-option arguments are required; "
                   "use --help for help"));

  input_filename = argv[optind];
  output_filename = argv[optind + 1];
  input_fh = fh_create_file (NULL, input_filename, NULL, fh_default_properties ());

  if (output_format == NULL)
    {
      const char *dot = strrchr (output_filename, '.');
      if (dot == NULL)
        error (1, 0, _("%s: cannot guess output format (use -O option)"),
               output_filename);

      output_format = dot + 1;
    }

  output_fh = fh_create_file (NULL, output_filename, NULL, fh_default_properties ());
  if (encrypted_file_open (&enc, input_fh) > 0)
    {
      if (decrypt_file (enc, input_fh, output_fh, password,
                         ds_cstr (&alphabet), length, password_list))
        goto exit;
      else
        goto error;
    }


  reader = any_reader_open_and_decode (input_fh, encoding, &dict, NULL);
  if (reader == NULL)
    goto error;

  struct case_map_stage *stage = case_map_stage_create (dict);
  if (keep)
    {
      struct variable **keep_vars;
      size_t n_keep_vars;
      if (!parse_variables_option (keep, dict, &keep_vars, &n_keep_vars))
        goto error;
      dict_reorder_vars (dict, keep_vars, n_keep_vars);
      dict_delete_consecutive_vars (dict, n_keep_vars,
                                    dict_get_n_vars (dict) - n_keep_vars);
      free (keep_vars);
    }

  if (drop)
    {
      struct variable **drop_vars;
      size_t n_drop_vars;
      if (!parse_variables_option (drop, dict, &drop_vars, &n_drop_vars))
        goto error;
      dict_delete_vars (dict, drop_vars, n_drop_vars);
      free (drop_vars);
    }

  reader = case_map_create_input_translator (
    case_map_stage_to_case_map (stage), reader);

  if (!strcmp (output_format, "csv") || !strcmp (output_format, "txt"))
    {
      if (!csv_opts.delimiter)
        csv_opts.delimiter = csv_opts.decimal == '.' ? ',' : ';';
      writer = csv_writer_open (output_fh, dict, &csv_opts);
    }
  else if (!strcmp (output_format, "sav") || !strcmp (output_format, "sys"))
    {
      struct sfm_write_options options;

      options = sfm_writer_default_options ();
      writer = sfm_open_writer (output_fh, dict, options);
    }
  else if (!strcmp (output_format, "por"))
    {
      struct pfm_write_options options;

      options = pfm_writer_default_options ();
      writer = pfm_open_writer (output_fh, dict, options);
    }
  else
    {
      error (1, 0, _("%s: unknown output format (use -O option)"),
             output_filename);
      NOT_REACHED ();
    }
  if (!writer)
    error (1, 0, _("%s: error opening output file"), output_filename);

  for (i = 0; i < max_cases; i++)
    {
      struct ccase *c;

      c = casereader_read (reader);
      if (c == NULL)
        break;

      casewriter_write (writer, c);
    }

  if (!casereader_destroy (reader))
    error (1, 0, _("%s: error reading input file"), input_filename);
  if (!casewriter_destroy (writer))
    error (1, 0, _("%s: error writing output file"), output_filename);

exit:
  ds_destroy (&alphabet);
  dict_unref (dict);
  fh_unref (output_fh);
  fh_unref (input_fh);
  fh_done ();
  i18n_done ();

  return 0;

error:
  casereader_destroy (reader);
  ds_destroy (&alphabet);
  dict_unref (dict);
  fh_unref (output_fh);
  fh_unref (input_fh);
  fh_done ();
  i18n_done ();

  return 1;
}

static bool
decrypt_file (struct encrypted_file *enc,
              const struct file_handle *ifh,
              const struct file_handle *ofh,
              const char *password,
              const char *alphabet,
              int max_length,
              const char *password_list)
{
  FILE *out;
  int err;
  const char *input_filename = fh_get_file_name (ifh);
  const char *output_filename = fh_get_file_name (ofh);

  if (password_list)
    {
      FILE *password_file;
      if (!strcmp (password_list, "-"))
        password_file = stdin;
      else
        {
          password_file = fopen (password_list, "r");
          if (!password_file)
            error (1, errno, _("%s: error opening password file"),
                   password_list);
        }

      struct string pw = DS_EMPTY_INITIALIZER;
      unsigned int target = 100000;
      for (unsigned int i = 0; ; i++)
        {
          ds_clear (&pw);
          if (!ds_read_line (&pw, password_file, SIZE_MAX))
            {
              if (isatty (STDOUT_FILENO))
                {
                  putchar ('\r');
                  fflush (stdout);
                }
              error (1, 0, _("\n%s: password not in file"), password_list);
            }
          ds_chomp_byte (&pw, '\n');

          if (i >= target)
            {
              target += 100000;
              if (isatty (STDOUT_FILENO))
                {
                  printf ("\r%u", i);
                  fflush (stdout);
                }
            }

          if (encrypted_file_unlock__ (enc, ds_cstr (&pw)))
            {
              printf ("\npassword is: \"%s\"\n", ds_cstr (&pw));
              password = ds_cstr (&pw);
              break;
            }
        }
    }
  else if (alphabet[0] && max_length)
    {
      size_t alphabet_size = strlen (alphabet);
      char *pw = xmalloc (max_length + 1);
      int *indexes = xzalloc (max_length * sizeof *indexes);

      for (int len = password ? strlen (password) : 0;
           len <= max_length; len++)
        {
          if (password && len == strlen (password))
            {
              for (int i = 0; i < len; i++)
                {
                  const char *p = strchr (alphabet, password[i]);
                  if (!p)
                    error (1, 0, _("%s: '%c' is not in alphabet"),
                           password, password[i]);
                  indexes[i] = p - alphabet;
                  pw[i] = *p;
                }
            }
          else
            {
              memset (indexes, 0, len * sizeof *indexes);
              for (int i = 0; i < len; i++)
                pw[i] = alphabet[0];
            }
          pw[len] = '\0';

          unsigned int target = 0;
          for (unsigned int j = 0; ; j++)
            {
              if (j >= target)
                {
                  target += 100000;
                  if (isatty (STDOUT_FILENO))
                    {
                      printf ("\rlength %d: %s", len, pw);
                      fflush (stdout);
                    }
                }
              if (encrypted_file_unlock__ (enc, pw))
                {
                  printf ("\npassword is: \"%s\"\n", pw);
                  password = pw;
                  goto success;
                }

              int i;
              for (i = 0; i < len; i++)
                if (++indexes[i] < alphabet_size)
                  {
                    pw[i] = alphabet[indexes[i]];
                    break;
                  }
                else
                  {
                    indexes[i] = 0;
                    pw[i] = alphabet[indexes[i]];
                  }
              if (i == len)
                break;
            }
        }
      free (indexes);
      free (pw);

    success:;
    }
  else
    {
      if (password == NULL)
        {
          password = getpass ("password: ");
          if (password == NULL)
            return false;
        }

      if (!encrypted_file_unlock (enc, password))
        error (1, 0, _("sorry, wrong password"));
    }

  out = fn_open (ofh, "wb");
  if (out == NULL)
    error (1, errno, ("%s: error opening output file"), output_filename);

  for (;;)
    {
      uint8_t buffer[1024];
      size_t n;

      n = encrypted_file_read (enc, buffer, sizeof buffer);
      if (n == 0)
        break;

      if (fwrite (buffer, 1, n, out) != n)
        error (1, errno, ("%s: write error"), output_filename);
    }

  err = encrypted_file_close (enc);
  if (err)
    error (1, err, ("%s: read error"), input_filename);

  if (fflush (out) == EOF)
    error (1, errno, ("%s: write error"), output_filename);
  fn_close (ofh, out);

  return true;
}

static void
usage (void)
{
  printf ("\
%s, a utility for converting SPSS data files to other formats.\n\
Usage: %s [OPTION]... INPUT OUTPUT\n\
where INPUT is an SPSS data file or encrypted syntax file\n\
  and OUTPUT is the name of the desired output file.\n\
\n\
The desired format of OUTPUT is by default inferred from its extension:\n\
  csv txt             comma-separated value\n\
  sav sys             SPSS system file\n\
  por                 SPSS portable file\n\
  sps                 SPSS syntax file (encrypted syntax input files only)\n\
\n\
General options:\n\
  -O, --output-format=FORMAT  set specific output format, where FORMAT\n\
                      is one of the extensions listed above\n\
  -e, --encoding=CHARSET  override encoding of input data file\n\
  -c MAXCASES         limit number of cases to copy (default is all cases)\n\
  -k, --keep=VAR...   include only the given variables in output\n\
  -d, --drop=VAR...   drop the given variables from output\n\
CSV output options:\n\
  --recode            convert user-missing values to system-missing\n\
  --no-var-names      do not include variable names as first row\n\
  --labels            write value labels to output\n\
  --print-formats     honor variables' print formats\n\
  --decimal=CHAR      use CHAR as the decimal point (default: .)\n\
  --delimiter=CHAR    use CHAR to separate fields (default: ,)\n\
  --qualifier=CHAR    use CHAR to quote the delimiter (default: \")\n\
Password options (for used with encrypted files):\n\
  -p PASSWORD         individual password\n\
  -a ALPHABET         with -l, alphabet of passwords to try\n\
  -l MAX-LENGTH       with -a, maximum number of characters to try\n\
  --password-list=FILE  try all of the passwords in FILE (one per line)\n\
Other options:\n\
  --help              display this help and exit\n\
  --version           output version information and exit\n",
          program_name, program_name);
}
