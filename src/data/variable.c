/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013,
   2014, 2016, 2020 Free Software Foundation, Inc.

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

#include "data/variable.h"

#include <stdlib.h>

#include "data/attributes.h"
#include "data/data-out.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/identifier.h"
#include "data/missing-values.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/vardict.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* This should follow the definition in Gtk */
typedef struct
{
  int value;
  const char *name;
  const char *label;
} GEnumValue;

const GEnumValue align[] =
  {
    {ALIGN_LEFT,   "left", N_("Left")},
    {ALIGN_RIGHT,  "right", N_("Right")},
    {ALIGN_CENTRE, "center", N_("Center")},
    {0,0,0}
  };

const GEnumValue measure[] =
  {
    {MEASURE_UNKNOWN, "unknown", N_("Unknown")},
    {MEASURE_NOMINAL, "nominal", N_("Nominal")},
    {MEASURE_ORDINAL, "ordinal", N_("Ordinal")},
    {MEASURE_SCALE,   "scale", N_("Scale")},
    {0,0,0}
  };

const GEnumValue role[] =
  {
    {ROLE_INPUT,  "input",    N_("Input")},
    {ROLE_TARGET, "output",   N_("Output")},
    {ROLE_BOTH,   "both",     N_("Both")},
    {ROLE_NONE,   "none",     N_("None")},
    {ROLE_PARTITION, "partition", N_("Partition")},
    {ROLE_SPLIT,  "split",    N_("Split")},
    {0,0,0}
  };

/* A variable. */
struct variable
  {
    int ref_cnt;
    /* Dictionary information. */
    char *name;                 /* Variable name.  Mixed case. */
    int width;                        /* 0 for numeric, otherwise string width. */
    struct missing_values miss; /* Missing values. */
    struct fmt_spec print;        /* Default format for PRINT. */
    struct fmt_spec write;        /* Default format for WRITE. */
    struct val_labs *val_labs;  /* Value labels. */
    char *label;                /* Variable label. */
    struct string name_and_label; /* The name and label in the same string */

    /* GUI information. */
    enum measure measure;       /* Nominal, ordinal, or continuous. */
    enum var_role role;         /* Intended use. */
    int display_width;          /* Width of data editor column. */
    enum alignment alignment;   /* Alignment of data in GUI. */

    /* Case information. */
    bool leave;                 /* Leave value from case to case? */

    /* Data for use by containing dictionary. */
    struct vardict_info *vardict;

    /* Used only for system and portable file input and output.
       See short-names.h. */
    char **short_names;
    size_t n_short_names;

    /* Custom attributes. */
    struct attrset attributes;
  };


static void var_set_print_format_quiet (struct variable *v, struct fmt_spec);
static void var_set_write_format_quiet (struct variable *v, struct fmt_spec);
static void var_set_label_quiet (struct variable *v, const char *label);
static void var_set_name_quiet (struct variable *v, const char *name);

/* Creates and returns a new variable with the given NAME and
   WIDTH and other fields initialized to default values.  The
   variable is not added to a dictionary; for that, use
   dict_create_var instead. */
struct variable *
var_create (const char *name, int width)
{
  enum val_type type;

  assert (width >= 0 && width <= MAX_STRING);

  struct variable *v = XZALLOC (struct variable);
  var_set_name_quiet (v, name);
  v->width = width;
  mv_init (&v->miss, width);
  v->leave = var_must_leave (v);
  type = val_type_from_width (width);
  v->alignment = var_default_alignment (type);
  v->measure = var_default_measure_for_type (type);
  v->role = ROLE_INPUT;
  v->display_width = var_default_display_width (width);
  v->print = v->write = var_default_formats (width);
  attrset_init (&v->attributes);
  ds_init_empty (&v->name_and_label);

  v->ref_cnt = 1;

  return v;
}

/* Destroys variable V.
   V must not belong to a dictionary.  If it does, use
   dict_delete_var instead. */
static void
var_destroy__ (struct variable *v)
{
  assert (!var_has_vardict (v));
  mv_destroy (&v->miss);
  var_clear_short_names (v);
  val_labs_destroy (v->val_labs);
  var_set_label_quiet (v, NULL);
  attrset_destroy (var_get_attributes (v));
  free (v->name);
  ds_destroy (&v->name_and_label);
  free (v);
}

struct variable *
var_ref (struct variable *v)
{
  v->ref_cnt++;
  return v;
}

void
var_unref (struct variable *v)
{
  if (--v->ref_cnt == 0)
    var_destroy__ (v);
}



/* Variable names. */

/* Return variable V's name, as a UTF-8 encoded string. */
const char *
var_get_name (const struct variable *v)
{
  return v->name;
}



/* Sets V's name to NAME, a UTF-8 encoded string.
   Do not use this function for a variable in a dictionary.  Use
   dict_rename_var instead. */
static void
var_set_name_quiet (struct variable *v, const char *name)
{
  assert (!var_has_vardict (v));

  free (v->name);
  v->name = xstrdup (name);
  ds_destroy (&v->name_and_label);
  ds_init_empty (&v->name_and_label);
}

/* Sets V's name to NAME, a UTF-8 encoded string.
   Do not use this function for a variable in a dictionary.  Use
   dict_rename_var instead. */
void
var_set_name (struct variable *v, const char *name)
{
  struct variable *ov = var_clone (v);
  var_set_name_quiet (v, name);
  dict_var_changed (v, VAR_TRAIT_NAME, ov);
}

/* Returns VAR's dictionary class. */
enum dict_class
var_get_dict_class (const struct variable *var)
{
  return dict_class_from_id (var->name);
}

/* A hsh_compare_func that orders variables A and B by their
   names. */
int
compare_vars_by_name (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return utf8_strcasecmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_var_by_name (const void *v_, const void *aux UNUSED)
{
  const struct variable *v = v_;

  return utf8_hash_case_string (v->name, 0);
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their names. */
int
compare_var_ptrs_by_name (const void *a_, const void *b_,
                          const void *aux UNUSED)
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return utf8_strcasecmp (var_get_name (*a), var_get_name (*b));
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their dictionary indexes. */
int
compare_var_ptrs_by_dict_index (const void *a_, const void *b_,
                                const void *aux UNUSED)
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;
  size_t a_index = var_get_dict_index (*a);
  size_t b_index = var_get_dict_index (*b);

  return a_index < b_index ? -1 : a_index > b_index;
}

/* A hsh_hash_func that hashes pointer to variable V based on its
   name. */
unsigned
hash_var_ptr_by_name (const void *v_, const void *aux UNUSED)
{
  struct variable *const *v = v_;

  return utf8_hash_case_string (var_get_name (*v), 0);
}

/* Returns the type of variable V. */
enum val_type
var_get_type (const struct variable *v)
{
  return val_type_from_width (v->width);
}

/* Returns the width of variable V. */
int
var_get_width (const struct variable *v)
{
  return v->width;
}

void
var_set_width_and_formats (struct variable *v, int new_width,
                           const struct fmt_spec *print, const struct fmt_spec *write)
{
  struct variable *ov;
  unsigned int traits = 0;

  ov = var_clone (v);

  if (mv_is_resizable (&v->miss, new_width))
    mv_resize (&v->miss, new_width);
  else
    {
      mv_destroy (&v->miss);
      mv_init (&v->miss, new_width);
    }
  if (new_width != var_get_width (v))
    traits |= VAR_TRAIT_MISSING_VALUES;

  if (v->val_labs != NULL)
    {
      if (val_labs_can_set_width (v->val_labs, new_width))
        val_labs_set_width (v->val_labs, new_width);
      else
        {
          val_labs_destroy (v->val_labs);
          v->val_labs = NULL;
        }
      traits |= VAR_TRAIT_VALUE_LABELS;
    }

  if (fmt_resize (&v->print, new_width))
    traits |= VAR_TRAIT_PRINT_FORMAT;

  if (fmt_resize (&v->write, new_width))
    traits |= VAR_TRAIT_WRITE_FORMAT;

  if (v->width != new_width)
    {
      v->width = new_width;
      traits |= VAR_TRAIT_WIDTH;
    }

  if (print)
    {
      var_set_print_format_quiet (v, *print);
      traits |= VAR_TRAIT_PRINT_FORMAT;
    }

  if (write)
    {
      var_set_write_format_quiet (v, *write);
      traits |= VAR_TRAIT_WRITE_FORMAT;
    }

  if (traits != 0)
    dict_var_changed (v, traits, ov);
}

/* Changes the width of V to NEW_WIDTH.
   This function should be used cautiously. */
void
var_set_width (struct variable *v, int new_width)
{
  const int old_width = v->width;

  if (old_width == new_width)
    return;

  var_set_width_and_formats (v, new_width, NULL, NULL);
}




/* Returns true if variable V is numeric, false otherwise. */
bool
var_is_numeric (const struct variable *v)
{
  return var_get_type (v) == VAL_NUMERIC;
}

/* Returns true if variable V is a string variable, false
   otherwise. */
bool
var_is_alpha (const struct variable *v)
{
  return var_get_type (v) == VAL_STRING;
}

/* Returns variable V's missing values. */
const struct missing_values *
var_get_missing_values (const struct variable *v)
{
  return &v->miss;
}

/* Sets variable V's missing values to MISS, which must be of V's
   width or at least resizable to V's width.
   If MISS is null, then V's missing values, if any, are
   cleared. */
static void
var_set_missing_values_quiet (struct variable *v, const struct missing_values *miss)
{
  if (miss != NULL)
    {
      assert (mv_is_resizable (miss, v->width));
      mv_destroy (&v->miss);
      mv_copy (&v->miss, miss);
      mv_resize (&v->miss, v->width);
    }
  else
    mv_clear (&v->miss);
}

/* Sets variable V's missing values to MISS, which must be of V's
   width or at least resizable to V's width.
   If MISS is null, then V's missing values, if any, are
   cleared. */
void
var_set_missing_values (struct variable *v, const struct missing_values *miss)
{
  struct variable *ov = var_clone (v);
  var_set_missing_values_quiet (v, miss);
  dict_var_changed (v, VAR_TRAIT_MISSING_VALUES, ov);
}

/* Sets variable V to have no user-missing values. */
void
var_clear_missing_values (struct variable *v)
{
  var_set_missing_values (v, NULL);
}

/* Returns true if V has any user-missing values,
   false otherwise. */
bool
var_has_missing_values (const struct variable *v)
{
  return !mv_is_empty (&v->miss);
}

/* Returns MV_SYSTEM if VALUE is system-missing, MV_USER if VALUE is
   user-missing for V, and otherwise 0. */
enum mv_class
var_is_value_missing (const struct variable *v, const union value *value)
{
  return mv_is_value_missing (&v->miss, value);
}

/* Returns MV_SYSTEM if VALUE is system-missing, MV_USER if VALUE is
   user-missing for V, and otherwise 0.  V must be a numeric variable. */
enum mv_class
var_is_num_missing (const struct variable *v, double d)
{
  return mv_is_num_missing (&v->miss, d);
}

/* Returns MV_USER if VALUE is user-missing for V and otherwise 0.  V must be
   a string variable. */
enum mv_class
var_is_str_missing (const struct variable *v, const uint8_t s[])
{
  return mv_is_str_missing (&v->miss, s);
}

/* Returns variable V's value labels,
   possibly a null pointer if it has none. */
const struct val_labs *
var_get_value_labels (const struct variable *v)
{
  return v->val_labs;
}

/* Returns true if variable V has at least one value label. */
bool
var_has_value_labels (const struct variable *v)
{
  return val_labs_count (v->val_labs) > 0;
}

/* Sets variable V's value labels to a copy of VLS,
   which must have a width equal to V's width or one that can be
   changed to V's width.
   If VLS is null, then V's value labels, if any, are removed. */
static void
var_set_value_labels_quiet (struct variable *v, const struct val_labs *vls)
{
  val_labs_destroy (v->val_labs);
  v->val_labs = NULL;

  if (vls != NULL)
    {
      assert (val_labs_can_set_width (vls, v->width));
      v->val_labs = val_labs_clone (vls);
      val_labs_set_width (v->val_labs, v->width);
    }
}


/* Sets variable V's value labels to a copy of VLS,
   which must have a width equal to V's width or one that can be
   changed to V's width.
   If VLS is null, then V's value labels, if any, are removed. */
void
var_set_value_labels (struct variable *v, const struct val_labs *vls)
{
  struct variable *ov = var_clone (v);
  var_set_value_labels_quiet (v, vls);
  dict_var_changed (v, VAR_TRAIT_LABEL, ov);
}


/* Makes sure that V has a set of value labels,
   by assigning one to it if necessary. */
static void
alloc_value_labels (struct variable *v)
{
  if (v->val_labs == NULL)
    v->val_labs = val_labs_create (v->width);
}

/* Attempts to add a value label with the given VALUE and UTF-8 encoded LABEL
   to V.  Returns true if successful, false otherwise (probably due to an
   existing label).

   In LABEL, the two-byte sequence "\\n" is interpreted as a new-line. */
bool
var_add_value_label (struct variable *v,
                     const union value *value, const char *label)
{
  alloc_value_labels (v);
  return val_labs_add (v->val_labs, value, label);
}

/* Adds or replaces a value label with the given VALUE and UTF-8 encoded LABEL
   to V.

   In LABEL, the two-byte sequence "\\n" is interpreted as a new-line. */
void
var_replace_value_label (struct variable *v,
                         const union value *value, const char *label)
{
  alloc_value_labels (v);
  val_labs_replace (v->val_labs, value, label);
}

/* Removes V's value labels, if any. */
void
var_clear_value_labels (struct variable *v)
{
  var_set_value_labels (v, NULL);
}

/* Returns the label associated with VALUE for variable V, as a UTF-8 string in
   a format suitable for output, or a null pointer if none. */
const char *
var_lookup_value_label (const struct variable *v, const union value *value)
{
  return val_labs_find (v->val_labs, value);
}

/*
   Append to STR the string representation of VALUE for variable V.
   STR must be a pointer to an initialised struct string.
*/
static void
append_value (const struct variable *v, const union value *value,
              struct string *str)
{
  char *s = data_out (value, var_get_encoding (v), v->print,
                      settings_get_fmt_settings ());
  struct substring ss = ss_cstr (s);
  ss_rtrim (&ss, ss_cstr (" "));
  ds_put_substring (str, ss);
  free (s);
}

void
var_append_value_name__ (const struct variable *v, const union value *value,
                         enum settings_value_show show, struct string *str)
{
  const char *label = var_lookup_value_label (v, value);

  switch (show)
    {
    case SETTINGS_VALUE_SHOW_VALUE:
      append_value (v, value, str);
      break;

    default:
    case SETTINGS_VALUE_SHOW_LABEL:
      if (label)
        ds_put_cstr (str, label);
      else
        append_value (v, value, str);
      break;

    case SETTINGS_VALUE_SHOW_BOTH:
      append_value (v, value, str);
      if (label != NULL)
        ds_put_format (str, " %s", label);
      break;
    }
}

/* Append STR with a string representing VALUE for variable V.
   That is, if VALUE has a label, append that label,
   otherwise format VALUE and append the formatted string.
   STR must be a pointer to an initialised struct string.
*/
void
var_append_value_name (const struct variable *v, const union value *value,
                       struct string *str)
{
  var_append_value_name__ (v, value, settings_get_show_values (), str);
}

/* Print and write formats. */

/* Returns V's print format specification. */
struct fmt_spec
var_get_print_format (const struct variable *v)
{
  return v->print;
}

/* Sets V's print format specification to PRINT, which must be a
   valid format specification for a variable of V's width
   (ordinarily an output format, but input formats are not
   rejected). */
static void
var_set_print_format_quiet (struct variable *v, struct fmt_spec print)
{
  if (!fmt_equal (v->print, print))
    {
      assert (fmt_check_width_compat (print, v->width));
      v->print = print;
    }
}

/* Sets V's print format specification to PRINT, which must be a
   valid format specification for a variable of V's width
   (ordinarily an output format, but input formats are not
   rejected). */
void
var_set_print_format (struct variable *v, struct fmt_spec print)
{
  struct variable *ov = var_clone (v);
  var_set_print_format_quiet (v, print);
  dict_var_changed (v, VAR_TRAIT_PRINT_FORMAT, ov);
}

/* Returns V's write format specification. */
struct fmt_spec
var_get_write_format (const struct variable *v)
{
  return v->write;
}

/* Sets V's write format specification to WRITE, which must be a
   valid format specification for a variable of V's width
   (ordinarily an output format, but input formats are not
   rejected). */
static void
var_set_write_format_quiet (struct variable *v, struct fmt_spec write)
{
  if (!fmt_equal (v->write, write))
    {
      assert (fmt_check_width_compat (write, v->width));
      v->write = write;
    }
}

/* Sets V's write format specification to WRITE, which must be a
   valid format specification for a variable of V's width
   (ordinarily an output format, but input formats are not
   rejected). */
void
var_set_write_format (struct variable *v, struct fmt_spec write)
{
  struct variable *ov = var_clone (v);
  var_set_write_format_quiet (v, write);
  dict_var_changed (v, VAR_TRAIT_WRITE_FORMAT, ov);
}


/* Sets V's print and write format specifications to FORMAT,
   which must be a valid format specification for a variable of
   V's width (ordinarily an output format, but input formats are
   not rejected). */
void
var_set_both_formats (struct variable *v, struct fmt_spec format)
{
  struct variable *ov = var_clone (v);
  var_set_print_format_quiet (v, format);
  var_set_write_format_quiet (v, format);
  dict_var_changed (v, VAR_TRAIT_PRINT_FORMAT | VAR_TRAIT_WRITE_FORMAT, ov);
}

/* Returns the default print and write format for a variable of
   the given TYPE, as set by var_create.  The return value can be
   used to reset a variable's print and write formats to the
   default. */
struct fmt_spec
var_default_formats (int width)
{
  return (width == 0
          ? fmt_for_output (FMT_F, 8, 2)
          : fmt_for_output (FMT_A, width, 0));
}




/* Update the combined name and label string if necessary */
static void
update_vl_string (const struct variable *v)
{
  /* Cast away const! */
  struct string *str = (struct string *) &v->name_and_label;

  if (ds_is_empty (str))
    {
      if (v->label)
        ds_put_format (str, _("%s (%s)"), v->label, v->name);
      else
        ds_put_cstr (str, v->name);
    }
}


/* Return a string representing this variable, in the form most
   appropriate from a human factors perspective, that is, its
   variable label if it has one, otherwise its name. */
const char *
var_to_string (const struct variable *v)
{
  switch (settings_get_show_variables ())
    {
    case SETTINGS_VALUE_SHOW_VALUE:
      return v->name;

    case SETTINGS_VALUE_SHOW_LABEL:
    default:
      return v->label != NULL ? v->label : v->name;

    case SETTINGS_VALUE_SHOW_BOTH:
      update_vl_string (v);
      return ds_cstr (&v->name_and_label);
    }
}

/* Returns V's variable label, or a null pointer if it has none. */
const char *
var_get_label (const struct variable *v)
{
  return v->label;
}

/* Sets V's variable label to UTF-8 encoded string LABEL, stripping off leading
   and trailing white space.  If LABEL is a null pointer or if LABEL is an
   empty string, then V's variable label (if any) is removed. */
static void
var_set_label_quiet (struct variable *v, const char *label)
{
  free (v->label);
  v->label = NULL;

  if (label != NULL && label[0])
    v->label = xstrdup (label);

  ds_destroy (&v->name_and_label);
  ds_init_empty (&v->name_and_label);
}

/* Sets V's variable label to UTF-8 encoded string LABEL, stripping off leading
   and trailing white space.  If LABEL is a null pointer or if LABEL is an
   empty string (after stripping white space), then V's variable label (if any)
   is removed. */
void
var_set_label (struct variable *v, const char *label)
{
  struct variable *ov = var_clone (v);
  var_set_label_quiet (v, label);
  dict_var_changed (v, VAR_TRAIT_LABEL, ov);
}

/* Removes any variable label from V. */
void
var_clear_label (struct variable *v)
{
  var_set_label (v, NULL);
}

/* Returns true if V has a variable V,
   false otherwise. */
bool
var_has_label (const struct variable *v)
{
  return v->label != NULL;
}

/* Returns true if M is a valid variable measurement level,
   false otherwise. */
bool
measure_is_valid (enum measure m)
{
  return (m == MEASURE_UNKNOWN || m == MEASURE_NOMINAL
          || m == MEASURE_ORDINAL || m == MEASURE_SCALE);
}

/* Returns a string version of measurement level M, for display to a user.
   The caller may translate the string by passing it to gettext(). */
const char *
measure_to_string (enum measure m)
{
  assert (m == measure[m].value);
  return measure[m].label;
}

/* Returns a string version of measurement level M, for use in PSPP command
   syntax. */
const char *
measure_to_syntax (enum measure m)
{
  switch (m)
    {
    case MEASURE_NOMINAL:
      return "NOMINAL";

    case MEASURE_ORDINAL:
      return "ORDINAL";

    case MEASURE_SCALE:
      return "SCALE";

    default:
      return "Invalid";
    }
}

/* Returns V's measurement level. */
enum measure
var_get_measure (const struct variable *v)
{
  return v->measure;
}

/* Sets V's measurement level to MEASURE. */
static void
var_set_measure_quiet (struct variable *v, enum measure measure)
{
  assert (measure_is_valid (measure));
  v->measure = measure;
}


/* Sets V's measurement level to MEASURE. */
void
var_set_measure (struct variable *v, enum measure measure)
{
  struct variable *ov = var_clone (v);
  var_set_measure_quiet (v, measure);
  dict_var_changed (v, VAR_TRAIT_MEASURE, ov);
}


/* Returns the default measurement level for a variable of the
   given TYPE, as set by var_create.  The return value can be
   used to reset a variable's measurement level to the
   default. */
enum measure
var_default_measure_for_type (enum val_type type)
{
  return type == VAL_NUMERIC ? MEASURE_UNKNOWN : MEASURE_NOMINAL;
}

/* Returns the default measurement level for a variable with the given
   FORMAT, or MEASURE_UNKNOWN if there is no good default. */
enum measure
var_default_measure_for_format (enum fmt_type format)
{
  if (format == FMT_DOLLAR)
    return MEASURE_SCALE;

  switch (fmt_get_category (format))
    {
    case FMT_CAT_BASIC:
    case FMT_CAT_LEGACY:
    case FMT_CAT_BINARY:
    case FMT_CAT_HEXADECIMAL:
      return MEASURE_UNKNOWN;

    case FMT_CAT_CUSTOM:
    case FMT_CAT_DATE:
    case FMT_CAT_TIME:
      return MEASURE_SCALE;

    case FMT_CAT_DATE_COMPONENT:
    case FMT_CAT_STRING:
      return MEASURE_NOMINAL;
    }

  NOT_REACHED ();
}

/* Returns true if M is a valid variable role,
   false otherwise. */
bool
var_role_is_valid (enum var_role role)
{
  switch (role)
    {
    case ROLE_NONE:
    case ROLE_INPUT:
    case ROLE_TARGET:
    case ROLE_BOTH:
    case ROLE_PARTITION:
    case ROLE_SPLIT:
      return true;

    default:
      return false;
    }
}

/* Returns a string version of ROLE, for display to a user.
   The caller may translate the string by passing it to gettext(). */
const char *
var_role_to_string (enum var_role r)
{
  assert (r == role[r].value);
  return role[r].label;
}

/* Returns a string version of ROLE, for use in PSPP comamnd syntax. */
const char *
var_role_to_syntax (enum var_role role)
{
  switch (role)
    {
    case ROLE_INPUT:
      return "INPUT";

    case ROLE_TARGET:
      return "TARGET";

    case ROLE_BOTH:
      return "BOTH";

    case ROLE_NONE:
      return "NONE";

    case ROLE_PARTITION:
      return "PARTITION";

    case ROLE_SPLIT:
      return "SPLIT";

    default:
      return "<invalid>";
    }
}

/* Returns V's role. */
enum var_role
var_get_role (const struct variable *v)
{
  return v->role;
}

/* Sets V's role to ROLE. */
static void
var_set_role_quiet (struct variable *v, enum var_role role)
{
  assert (var_role_is_valid (role));
  v->role = role;
}


/* Sets V's role to ROLE. */
void
var_set_role (struct variable *v, enum var_role role)
{
  struct variable *ov = var_clone (v);
  var_set_role_quiet (v, role);
  dict_var_changed (v, VAR_TRAIT_ROLE, ov);
}

/* Returns V's display width, which applies only to GUIs. */
int
var_get_display_width (const struct variable *v)
{
  return v->display_width;
}

/* Sets V's display width to DISPLAY_WIDTH. */
static void
var_set_display_width_quiet (struct variable *v, int new_width)
{
  if (v->display_width != new_width)
    {
      v->display_width = new_width;
    }
}

void
var_set_display_width (struct variable *v, int new_width)
{
  if (v->display_width != new_width)
    {
      struct variable *ov = var_clone (v);
      var_set_display_width_quiet (v, new_width);
      dict_var_changed (v, VAR_TRAIT_DISPLAY_WIDTH, ov);
    }
}

/* Returns the default display width for a variable of the given
   WIDTH, as set by var_create.  The return value can be used to
   reset a variable's display width to the default. */
int
var_default_display_width (int width)
{
  return width == 0 ? 8 : MIN (width, 32);
}

/* Returns true if A is a valid alignment,
   false otherwise. */
bool
alignment_is_valid (enum alignment a)
{
  return a == ALIGN_LEFT || a == ALIGN_RIGHT || a == ALIGN_CENTRE;
}

/* Returns a string version of alignment A, for display to a user.
   The caller may translate the string by passing it to gettext(). */
const char *
alignment_to_string (enum alignment a)
{
  assert (a == align[a].value);
  return align[a].label;
}

/* Returns a string version of alignment A, for use in PSPP command syntax. */
const char *
alignment_to_syntax (enum alignment a)
{
  switch (a)
    {
    case ALIGN_LEFT:
      return "LEFT";

    case ALIGN_RIGHT:
      return "RIGHT";

    case ALIGN_CENTRE:
      return "CENTER";

    default:
      return "Invalid";
    }
}

/* Returns V's display alignment, which applies only to GUIs. */
enum alignment
var_get_alignment (const struct variable *v)
{
  return v->alignment;
}

/* Sets V's display alignment to ALIGNMENT. */
static void
var_set_alignment_quiet (struct variable *v, enum alignment alignment)
{
  assert (alignment_is_valid (alignment));
  v->alignment = alignment;
}

/* Sets V's display alignment to ALIGNMENT. */
void
var_set_alignment (struct variable *v, enum alignment alignment)
{
  struct variable *ov = var_clone (v);
  var_set_alignment_quiet (v, alignment);
  dict_var_changed (v, VAR_TRAIT_ALIGNMENT, ov);
}


/* Returns the default display alignment for a variable of the
   given TYPE, as set by var_create.  The return value can be
   used to reset a variable's display alignment to the default. */
enum alignment
var_default_alignment (enum val_type type)
{
  return type == VAL_NUMERIC ? ALIGN_RIGHT : ALIGN_LEFT;
}

/* Whether variables' values should be preserved from case to
   case. */

/* Returns true if variable V's value should be left from case to
   case, instead of being reset to system-missing or blanks. */
bool
var_get_leave (const struct variable *v)
{
  return v->leave;
}

/* Sets V's leave setting to LEAVE. */
static void
var_set_leave_quiet (struct variable *v, bool leave)
{
  assert (leave || !var_must_leave (v));
  v->leave = leave;
}


/* Sets V's leave setting to LEAVE. */
void
var_set_leave (struct variable *v, bool leave)
{
  struct variable *ov = var_clone (v);
  var_set_leave_quiet (v, leave);
  dict_var_changed (v, VAR_TRAIT_LEAVE, ov);
}


/* Returns true if V must be left from case to case,
   false if it can be set either way. */
bool
var_must_leave (const struct variable *v)
{
  return var_get_dict_class (v) == DC_SCRATCH;
}

/* Returns the number of short names stored in VAR.

   Short names are used only for system and portable file input
   and output.  They are upper-case only, not necessarily unique,
   and limited to SHORT_NAME_LEN characters (plus a null
   terminator).  Ordinarily a variable has at most one short
   name, but very long string variables (longer than 255 bytes)
   may have more.  A variable might not have any short name at
   all if it hasn't been saved to or read from a system or
   portable file. */
size_t
var_get_n_short_names (const struct variable *var)
{
  return var->n_short_names;
}

/* Returns VAR's short name with the given IDX, if it has one
   with that index, or a null pointer otherwise.  Short names may
   be sparse: even if IDX is less than the number of short names
   in VAR, this function may return a null pointer. */
const char *
var_get_short_name (const struct variable *var, size_t idx)
{
  return idx < var->n_short_names ? var->short_names[idx] : NULL;
}

/* Sets VAR's short name with the given IDX to the UTF-8 string SHORT_NAME.
   The caller must already have checked that, in the dictionary encoding,
   SHORT_NAME is no more than SHORT_NAME_LEN bytes long.  The new short name
   will be converted to uppercase.

   Specifying a null pointer for SHORT_NAME clears the specified short name. */
void
var_set_short_name (struct variable *var, size_t idx, const char *short_name)
{
  struct variable *ov = var_clone (var);

  /* Clear old short name numbered IDX, if any. */
  if (idx < var->n_short_names)
    {
      free (var->short_names[idx]);
      var->short_names[idx] = NULL;
    }

  /* Install new short name for IDX. */
  if (short_name != NULL)
    {
      if (idx >= var->n_short_names)
        {
          size_t n_old = var->n_short_names;
          size_t i;
          var->n_short_names = MAX (idx * 2, 1);
          var->short_names = xnrealloc (var->short_names, var->n_short_names,
                                        sizeof *var->short_names);
          for (i = n_old; i < var->n_short_names; i++)
            var->short_names[i] = NULL;
        }
      var->short_names[idx] = utf8_to_upper (short_name);
    }

  dict_var_changed (var, VAR_TRAIT_NAME, ov);
}

/* Clears V's short names. */
void
var_clear_short_names (struct variable *v)
{
  size_t i;

  for (i = 0; i < v->n_short_names; i++)
    free (v->short_names[i]);
  free (v->short_names);
  v->short_names = NULL;
  v->n_short_names = 0;
}

/* Relationship with dictionary. */

/* Returns V's index within its dictionary, the value
   for which "dict_get_var (dict, index)" will return V.
   V must be in a dictionary. */
size_t
var_get_dict_index (const struct variable *v)
{
  assert (var_has_vardict (v));
  return vardict_get_dict_index (v->vardict);
}

/* Returns variable V's attribute set.  The caller may examine or
   modify the attribute set, but must not destroy it.  Destroying
   V, or calling var_set_attributes() on V, will also destroy its
   attribute set. */
struct attrset *
var_get_attributes (const struct variable *v)
{
  return CONST_CAST (struct attrset *, &v->attributes);
}

/* Replaces variable V's attributes set by a copy of ATTRS. */
static void
var_set_attributes_quiet (struct variable *v, const struct attrset *attrs)
{
  attrset_destroy (&v->attributes);
  attrset_clone (&v->attributes, attrs);
}

/* Replaces variable V's attributes set by a copy of ATTRS. */
void
var_set_attributes (struct variable *v, const struct attrset *attrs)
{
  struct variable *ov = var_clone (v);
  var_set_attributes_quiet (v, attrs);
  dict_var_changed (v, VAR_TRAIT_ATTRIBUTES, ov);
}


/* Returns true if V has any custom attributes, false if it has none. */
bool
var_has_attributes (const struct variable *v)
{
  return attrset_count (&v->attributes) > 0;
}


/* Creates and returns a clone of OLD_VAR.  Most properties of
   the new variable are copied from OLD_VAR, except:

    - The variable's short name is not copied, because there is
      no reason to give a new variable with potentially a new
      name the same short name.

    - The new variable is not added to OLD_VAR's dictionary by
      default.  Use dict_clone_var, instead, to do that.
*/
struct variable *
var_clone (const struct variable *old_var)
{
  struct variable *new_var = var_create (var_get_name (old_var),
                                         var_get_width (old_var));

  var_set_missing_values_quiet (new_var, var_get_missing_values (old_var));
  var_set_print_format_quiet (new_var, var_get_print_format (old_var));
  var_set_write_format_quiet (new_var, var_get_write_format (old_var));
  var_set_value_labels_quiet (new_var, var_get_value_labels (old_var));
  var_set_label_quiet (new_var, var_get_label (old_var));
  var_set_measure_quiet (new_var, var_get_measure (old_var));
  var_set_role_quiet (new_var, var_get_role (old_var));
  var_set_display_width_quiet (new_var, var_get_display_width (old_var));
  var_set_alignment_quiet (new_var, var_get_alignment (old_var));
  var_set_leave_quiet (new_var, var_get_leave (old_var));
  var_set_attributes_quiet (new_var, var_get_attributes (old_var));

  return new_var;
}



/* Returns the encoding of values of variable VAR.  (This is actually a
   property of the dictionary.)  Returns null if no specific encoding has been
   set.  */
const char *
var_get_encoding (const struct variable *var)
{
  return (var_has_vardict (var)
          ? dict_get_encoding (vardict_get_dictionary (var->vardict))
          : NULL);
}

/* Returns V's vardict structure. */
struct vardict_info *
var_get_vardict (const struct variable *v)
{
  return CONST_CAST (struct vardict_info *, v->vardict);
}

/* Sets V's vardict data to VARDICT. */
void
var_set_vardict (struct variable *v, struct vardict_info *vardict)
{
  v->vardict = vardict;
}

/* Returns true if V has vardict data. */
bool
var_has_vardict (const struct variable *v)
{
  return v->vardict != NULL;
}

/* Clears V's vardict data. */
void
var_clear_vardict (struct variable *v)
{
  v->vardict = NULL;
}


/*
  Returns zero, if W is a missing value for WV or if it is less than zero.
  Typically used to force a numerical value into a valid weight.

  As a side effect, this function will emit a warning if the value
  WARN_ON_INVALID points to a bool which is TRUE.  That bool will be then
  set to FALSE.
 */
double
var_force_valid_weight (const struct variable *wv, double w, bool *warn_on_invalid)
{
  if (w <= 0.0 || (wv ? var_is_num_missing (wv, w) : w == SYSMIS))
    {
      w = 0.0;
      if (warn_on_invalid != NULL && *warn_on_invalid)
        {
          *warn_on_invalid = false;
          msg (SW, _("At least one case in the data file had a weight value "
                     "that was user-missing, system-missing, zero, or "
                     "negative.  These case(s) were ignored."));
        }
    }

  return w;
}
