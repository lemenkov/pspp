/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2005, 2009, 2010, 2011 Free Software Foundation, Inc.

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

/* This file implements parts of identifier.h that call the msg() function.
   This allows test programs that do not use those functions to avoid linking
   additional object files. */

#include <config.h>

#include "data/identifier.h"

#include <string.h>
#include <unistr.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"

#include "gl/c-ctype.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool
error_to_bool (char *error)
{
  if (error)
    {
      free (error);
      return false;
    }
  else
    return true;
}

/* Checks whether if UTF-8 string ID is an acceptable identifier in encoding
   DICT_ENCODING (UTF-8 if null) for a variable in one of the classes in
   CLASSES.  Returns NULL if it is acceptable, otherwise an error message that
   the caller must free(). */
char * WARN_UNUSED_RESULT
id_is_valid__ (const char *id, const char *dict_encoding,
               enum dict_class classes)
{
  assert (classes && !(classes & ~DC_ALL));

  char *error = id_is_plausible__ (id);
  if (error)
    return error;

  size_t dict_len;
  if (dict_encoding != NULL)
    {
      struct substring out;
      bool ok = !recode_pedantically (dict_encoding, "UTF-8", ss_cstr (id),
                                      NULL, &out);
      dict_len = ss_length (out);
      ss_dealloc (&out);
      if (!ok)
        return xasprintf (_("Identifier `%s' is not valid in encoding `%s' "
                            "used for this dictionary."), id, dict_encoding);
    }
  else
    dict_len = strlen (id);

  enum dict_class c = dict_class_from_id (id);
  if (!(classes & c))
    {
      switch (c)
        {
        case DC_ORDINARY:
          switch ((int) classes)
            {
            case DC_SYSTEM:
              return xasprintf (_("`%s' is not valid here because this "
                                  "identifier must start with `$'."), id);

            case DC_SCRATCH:
              return xasprintf (_("`%s' is not valid here because this "
                                  "identifier must start with `#'."), id);

            case DC_SYSTEM | DC_SCRATCH:
              return xasprintf (_("`%s' is not valid here because this "
                                  "identifier must start with `$' or `#'."),
                                id);

            case DC_ORDINARY:
            default:
              NOT_REACHED ();
            }
          NOT_REACHED ();

        case DC_SYSTEM:
          return xasprintf (_("`%s' and other identifiers starting with `$' "
                              "are not valid here."), id);

        case DC_SCRATCH:
          return xasprintf (_("`%s' and other identifiers starting with `#' "
                              "are not valid here."), id);
        }
    }

  if (dict_len > ID_MAX_LEN)
    return xasprintf (_("Identifier `%s' exceeds %d-byte limit."),
                      id, ID_MAX_LEN);

  return NULL;
}

/* Returns true if UTF-8 string ID is an acceptable identifier in encoding
   DICT_ENCODING (UTF-8 if null) for variable in one of the classes in CLASSES,
   false otherwise. */
bool
id_is_valid (const char *id, const char *dict_encoding, enum dict_class classes)
{
  return error_to_bool (id_is_valid__ (id, dict_encoding, classes));
}

/* Checks whether UTF-8 string ID is a plausible identifier.  Returns NULL if
   it is, otherwise an error message that the caller must free().  */
char * WARN_UNUSED_RESULT
id_is_plausible__ (const char *id)
{
  /* ID cannot be the empty string. */
  if (id[0] == '\0')
    return xstrdup (_("Identifier cannot be empty string."));

  /* ID cannot be a reserved word. */
  if (lex_id_to_token (ss_cstr (id)) != T_ID)
    return xasprintf (_("`%s' may not be used as an identifier because it "
                        "is a reserved word."), id);

  const uint8_t *bad_unit = u8_check (CHAR_CAST (const uint8_t *, id),
                                      strlen (id));
  if (bad_unit != NULL)
    {
      /* If this message ever appears, it probably indicates a PSPP bug since
         it shouldn't be possible to get invalid UTF-8 this far. */
      return xasprintf (_("`%s' may not be used as an identifier because it "
                          "contains ill-formed UTF-8 at byte offset %tu."),
                        id, CHAR_CAST (const char *, bad_unit) - id);
    }

  /* Check that it is a valid identifier. */
  ucs4_t uc;
  int mblen = u8_strmbtouc (&uc, CHAR_CAST (uint8_t *, id));
  if (!lex_uc_is_id1 (uc))
    {
      char ucname[16];
      return xasprintf (_("Character %s (in `%s') may not appear "
                          "as the first character in an identifier."),
                        uc_name (uc, ucname), id);
    }

  for (const uint8_t *s = CHAR_CAST (uint8_t *, id + mblen);
       (mblen = u8_strmbtouc (&uc, s)) != 0;
        s += mblen)
    if (!lex_uc_is_idn (uc))
      {
        char ucname[16];
        return xasprintf (_("Character %s (in `%s') may not appear in an "
                            "identifier."),
                          uc_name (uc, ucname), id);
      }

  return NULL;
}

/* Returns true if UTF-8 string ID is a plausible identifier, false
   otherwise. */
bool
id_is_plausible (const char *id)
{
  return error_to_bool (id_is_plausible__ (id));
}
