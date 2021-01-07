/* PSPP - a program for statistical analysis.
   Copyright (C) 2017-2018 Free Software Foundation, Inc.

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

#ifndef OUTPUT_PIVOT_TABLE_H
#define OUTPUT_PIVOT_TABLE_H 1

#include <stdint.h>
#include <time.h>
#include "data/format.h"
#include "data/settings.h"
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"
#include "output/table.h"

struct pivot_value;
struct variable;
union value;

/* Pivot tables.

   Pivot tables are PSPP's primary form of output.  They are analogous to the
   pivot tables you might be familiar with from spreadsheets and databases.
   See https://en.wikipedia.org/wiki/Pivot_table for a brief introduction to
   the overall concept of a pivot table.

   In PSPP, the most important internal pieces of a pivot table are:

   - Title.  Every pivot table has a title that is displayed above it.  It also
     has an optional caption (displayed below it) and corner text (displayed in
     the upper left corner).

   - Dimensions.  A dimension consists of zero or more categories.  A category
     has a label, such as "df" or "Asymp. Sig." or 123 or a variable name.  The
     categories are the leaves of a tree whose non-leaf nodes form groups of
     categories.  The tree always has a root group whose label is the name of
     the dimension.

   - Axes.  A table has three axes: column, row, and layer.  Each dimension is
     assigned to an axis, and each axis has zero or more dimensions.  When an
     axis has more than one dimension, they are ordered from innermost to
     outermost.

   - Data.  A table's data consists of zero or more cells.  Each cell maps from
     a category for each dimension to a value, which is commonly a number but
     could also be a variable name or an arbitrary text string.

   Creating a pivot table usually consists of the following steps:

   1. Create the table with pivot_table_create(), passing in the title.

   2. Optionally, set the format to use for "count" values with
      pivot_table_set_weight_var() or pivot_table_set_weight_format().

   3. Create each dimension with pivot_dimension_create() and populate it with
      categories and, possibly, with groups that contain the categories.  This
      call also assigns the dimension to an axis.

      In simple cases, only a call to pivot_dimension_create() is needed.
      Other functions such as pivot_category_create_group() can be used for
      hierarchies of categories.

      Sometimes it's easier to create categories in tandem with inserting data,
      for example by adding a category for a variable just before inserting the
      first cell for that variable.  In that case, creating categories and
      inserting data can be interleaved.

   4. Insert data.  For each cell, supply the category indexes, which are
      assigned starting from 0 in the order in which the categories were
      created in step 2, and the value to go in the cell.  If the table has a
      small, fixed number of dimensions, functions like, e.g.
      pivot_table_put3() for 3 dimensions, can be used.  The general function
      pivot_table_put() works for other cases.

   5. Output the table for user consumption.  Use pivot_table_submit(). */

/* Pivot table display styling. */

/* Areas of a pivot table for styling purposes. */
enum pivot_area
  {
    PIVOT_AREA_TITLE,
    PIVOT_AREA_CAPTION,
    PIVOT_AREA_FOOTER,          /* Footnotes. */
    PIVOT_AREA_CORNER,          /* Top-left corner. */
    PIVOT_AREA_COLUMN_LABELS,
    PIVOT_AREA_ROW_LABELS,
    PIVOT_AREA_DATA,
    PIVOT_AREA_LAYERS,          /* Layer indication. */
    PIVOT_N_AREAS
  };

const char *pivot_area_to_string (enum pivot_area);

/* Table borders for styling purposes. */
enum pivot_border
  {
    PIVOT_BORDER_TITLE,

    /* Outer frame. */
    PIVOT_BORDER_OUTER_LEFT,
    PIVOT_BORDER_OUTER_TOP,
    PIVOT_BORDER_OUTER_RIGHT,
    PIVOT_BORDER_OUTER_BOTTOM,

    /* Inner frame. */
    PIVOT_BORDER_INNER_LEFT,
    PIVOT_BORDER_INNER_TOP,
    PIVOT_BORDER_INNER_RIGHT,
    PIVOT_BORDER_INNER_BOTTOM,

    /* Data area. */
    PIVOT_BORDER_DATA_LEFT,
    PIVOT_BORDER_DATA_TOP,

    /* Dimensions. */
    PIVOT_BORDER_DIM_ROW_HORZ,
    PIVOT_BORDER_DIM_ROW_VERT,
    PIVOT_BORDER_DIM_COL_HORZ,
    PIVOT_BORDER_DIM_COL_VERT,

    /* Categories. */
    PIVOT_BORDER_CAT_ROW_HORZ,
    PIVOT_BORDER_CAT_ROW_VERT,
    PIVOT_BORDER_CAT_COL_HORZ,
    PIVOT_BORDER_CAT_COL_VERT,

    PIVOT_N_BORDERS
  };

const char *pivot_border_to_string (enum pivot_border);

/* Sizing for rows or columns of a rendered table.  The comments below talk
   about columns and their widths but they apply equally to rows and their
   heights. */
struct pivot_table_sizing
  {
    /* Specific column widths, in 1/96" units. */
    int *widths;
    size_t n_widths;

    /* Specific page breaks: 0-based columns after which a page break must
       occur, e.g. a value of 1 requests a break after the second column. */
    size_t *breaks;
    size_t n_breaks;

    /* Keeps: columns to keep together on a page if possible. */
    struct pivot_keep *keeps;
    size_t n_keeps;
  };

void pivot_table_sizing_uninit (struct pivot_table_sizing *);

/* A set of columns to keep together on a page if possible, e.g. ofs=1, n=10
   requests keeping together the 2nd through 11th columns. */
struct pivot_keep
  {
    size_t ofs;                 /* 0-based first column. */
    size_t n;                   /* Number of columns. */
  };

/* Axes. */

enum pivot_axis_type
  {
    PIVOT_AXIS_LAYER,
    PIVOT_AXIS_ROW,
    PIVOT_AXIS_COLUMN,

    PIVOT_N_AXES
  };

const char *pivot_axis_type_to_string (enum pivot_axis_type);

/* An axis within a pivot table. */
struct pivot_axis
  {
    /* dimensions[0] is the innermost dimension,
       dimensions[1] is the next outer dimension,
       ...
       dimensions[n_dimensions - 1] is the outermost dimension. */
    struct pivot_dimension **dimensions;
    size_t n_dimensions;

    /* The number of rows or columns along the axis,
       that is, the product of dimension[*]->n_leaves.
       It is 0 if any dimension has 0 leaves. */
    size_t extent;

    /* Sum of dimensions[*]->label_depth. */
    size_t label_depth;
  };

/* Successively assigns to INDEXES (which should be a "size_t *") each of the
   combinations of the categories in AXIS's dimensions, in lexicographic order
   with the innermost dimension iterating most quickly.

   The value assigned to INDEXES is dynamically allocated.  If the client
   breaks out of the loop prematurely, it needs to free it with free(). */
#define PIVOT_AXIS_FOR_EACH(INDEXES, AXIS)                              \
  for ((INDEXES) = NULL;                                                \
       ((INDEXES) = pivot_axis_iterator_next (INDEXES, AXIS)) != NULL;)
size_t *pivot_axis_iterator_next (size_t *indexes, const struct pivot_axis *);

/* Dimensions.

   A pivot_dimension identifies the categories associated with a single
   dimension within a multidimensional pivot table.

   A dimension contains a collection of categories, which are the leaves in a
   tree of groups.

   (A dimension or a group can contain zero categories, but this is unusual.
   If a dimension contains no categories, then its table cannot contain any
   data.)
*/
struct pivot_dimension
  {
    /* table->axes[axis_type]->dimensions[level] == dimension. */
    struct pivot_table *table;
    enum pivot_axis_type axis_type;
    size_t level;               /* 0 for innermost dimension within axis. */

    /* table->dimensions[top_index] == dimension. */
    size_t top_index;

    /* Hierarchy of categories within the dimension.  The groups and categories
       are sorted in the order that should be used for display.  This might be
       different from the original order produced for output if the user
       adjusted it.

       The root must always be a group, although it is allowed to have no
       subcategories. */
    struct pivot_category *root;

    /* All of the leaves reachable via the root.

       The indexing for presentation_leaves is presentation order, thus
       presentation_leaves[i]->presentation_index == i.  This order is the same
       as would be produced by an in-order traversal of the groups.  It is the
       order into which the user reordered or sorted the categories.

       The indexing for data_leaves is that used for idx[] in struct
       pivot_cell, thus data_leaves[i]->data_index == i.  This might differ
       from what an in-order traversal of 'root' would yield, if the user
       reordered categories. */
    struct pivot_category **data_leaves;
    struct pivot_category **presentation_leaves;
    size_t n_leaves, allocated_leaves;

    /* Display. */
    bool hide_all_labels;

    /* Number of rows or columns needed to express the labels. */
    int label_depth;
  };

struct pivot_dimension *pivot_dimension_create (
  struct pivot_table *, enum pivot_axis_type, const char *name, ...)
  SENTINEL (0);
#define pivot_dimension_create(...) \
  pivot_dimension_create(__VA_ARGS__, NULL_SENTINEL)
struct pivot_dimension *pivot_dimension_create__ (struct pivot_table *,
                                                  enum pivot_axis_type,
                                                  struct pivot_value *name);

void pivot_dimension_destroy (struct pivot_dimension *);

void pivot_dimension_dump (const struct pivot_dimension *,
                           const struct pivot_table *, int indentation);

/* A pivot_category is a leaf (a category) or a group:

   - For a leaf, neither index is SIZE_MAX.

   - For a group, both indexes are SIZE_MAX.

   Do not use 'subs' or 'n_subs' to determine whether a category is a group,
   because a group may (pathologically) have no leaves. */
struct pivot_category
  {
    struct pivot_value *name;
    struct pivot_category *parent;
    struct pivot_dimension *dimension;
    size_t label_depth, extra_depth;

    /* Groups only.

       If show_label is true, then the group itself has a row (or a column)
       giving the group's name.  Otherwise, the group's own name is not
       displayed. */
    struct pivot_category **subs; /* Child categories or groups. */
    size_t n_subs, allocated_subs;
    bool show_label;            /* Display a label for the group itself? */
    bool show_label_in_corner;

    /* Leaf only. */
    size_t group_index;        /* In ->parent->subs[]. */
    size_t data_index;         /* In ->dimension->data_leaves[]. */
    size_t presentation_index; /* In ->dimension->presentation_leaves[]. */
    struct fmt_spec format;    /* Default format for values in this category. */
    bool honor_small;          /* Honor pivot_table 'small' setting? */
  };

static inline bool
pivot_category_is_group (const struct pivot_category *category)
{
  return category->data_index == SIZE_MAX;
}

static inline bool
pivot_category_is_leaf (const struct pivot_category *category)
{
  return !pivot_category_is_group (category);
}

/* Creating leaf categories. */
int pivot_category_create_leaves (struct pivot_category *parent, ...)
  SENTINEL (0);
#define pivot_category_create_leaves(...) \
  pivot_category_create_leaves(__VA_ARGS__, NULL_SENTINEL)

int pivot_category_create_leaf (
  struct pivot_category *parent, struct pivot_value *name);
int pivot_category_create_leaf_rc (
  struct pivot_category *parent, struct pivot_value *name, const char *rc);

/* Creating category groups. */
struct pivot_category *pivot_category_create_group (
  struct pivot_category *parent, const char *name, ...) SENTINEL (0);
#define pivot_category_create_group(...) \
  pivot_category_create_group(__VA_ARGS__, NULL_SENTINEL)
struct pivot_category *pivot_category_create_group__ (
  struct pivot_category *parent, struct pivot_value *name);

void pivot_category_destroy (struct pivot_category *);

/* Pivot result classes.

   These are used to mark leaf categories as having particular types of data,
   to set their numeric formats.  The formats that actually get used for these
   classes are in the result_classes[] global array in pivot-table.c, except
   that PIVOT_RC_OTHER comes from settings_get_format() and PIVOT_RC_COUNT
   should come from the weight variable in the dataset's dictionary. */
#define PIVOT_RC_OTHER ("RC_OTHER")
#define PIVOT_RC_INTEGER ("RC_INTEGER")
#define PIVOT_RC_CORRELATION ("RC_CORRELATIONS")
#define PIVOT_RC_SIGNIFICANCE ("RC_SIGNIFICANCE")
#define PIVOT_RC_PERCENT ("RC_PERCENT")
#define PIVOT_RC_RESIDUAL ("RC_RESIDUAL")
#define PIVOT_RC_COUNT ("RC_COUNT")

bool pivot_result_class_change (const char *, const struct fmt_spec *);
bool is_pivot_result_class (const char *);

/* Styling for a pivot table.

   The division between this and the style information in struct pivot_table
   seems fairly arbitrary.  The ultimate reason for the division is simply
   because that's how SPSS documentation and file formats do it. */
struct pivot_table_look
  {
    /* Reference count.  A pivot_table_look may be shared between multiple
       owners, indicated by a reference count greater than 1.  When this is the
       case, the pivot_table must not be modified. */
    int ref_cnt;

    char *name;                 /* May be null. */

    /* General properties. */
    bool omit_empty;
    bool row_labels_in_corner;
    int width_ranges[TABLE_N_AXES][2];      /* In 1/96" units. */

    /* Footnote display settings. */
    bool show_numeric_markers;
    bool footnote_marker_superscripts;

    /* Styles. */
    struct table_area_style areas[PIVOT_N_AREAS];
    struct table_border_style borders[PIVOT_N_BORDERS];

    /* Print settings. */
    bool print_all_layers;
    bool paginate_layers;
    bool shrink_to_fit[TABLE_N_AXES];
    bool top_continuation, bottom_continuation;
    char *continuation;
    size_t n_orphan_lines;
  };

const struct pivot_table_look *pivot_table_look_get_default (void);
void pivot_table_look_set_default (const struct pivot_table_look *);

char *pivot_table_look_read (const char *, struct pivot_table_look **)
  WARN_UNUSED_RESULT;

const struct pivot_table_look *pivot_table_look_builtin_default (void);
struct pivot_table_look *pivot_table_look_new_builtin_default (void);
struct pivot_table_look *pivot_table_look_ref (
  const struct pivot_table_look *);
void pivot_table_look_unref (struct pivot_table_look *);
struct pivot_table_look *pivot_table_look_unshare (struct pivot_table_look *);

/* A pivot table.  See the top of this file for more information. */
struct pivot_table
  {
    /* Reference count.  A pivot_table may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the pivot_table must not be modified. */
    int ref_cnt;

    /* Styling. */
    struct pivot_table_look *look;

    /* Display settings. */
    bool rotate_inner_column_labels;
    bool rotate_outer_row_labels;
    bool show_grid_lines;
    bool show_title;
    bool show_caption;
    size_t *current_layer; /* axes[PIVOT_AXIS_LAYER].n_dimensions elements. */
    enum settings_value_show show_values;
    enum settings_value_show show_variables;
    struct fmt_spec weight_format;

    /* Column and row sizing and page breaks.
       sizing[TABLE_HORZ] is for columns, sizing[TABLE_VERT] is for rows. */
    struct pivot_table_sizing sizing[TABLE_N_AXES];

    /* Format settings. */
    struct fmt_settings settings;
    char grouping;              /* Usually '.' or ','. */
    double small;

    /* Command information. */
    char *command_local;        /* May be NULL. */
    char *command_c;            /* May be NULL. */
    char *language;             /* May be NULL. */
    char *locale;               /* May be NULL. */

    /* Source information. */
    char *dataset;              /* May be NULL. */
    char *datafile;             /* May be NULL. */
    time_t date;                /* May be 0 if unknown. */

    /* Footnotes. */
    struct pivot_footnote **footnotes;
    size_t n_footnotes, allocated_footnotes;

    /* Titles. */
    struct pivot_value *title;
    struct pivot_value *subtype;  /* Same as spv_item's subtype. */
    struct pivot_value *corner_text;
    struct pivot_value *caption;
    char *notes;                /* Shown as tooltip. */

    /* Dimensions. */
    struct pivot_dimension **dimensions;
    size_t n_dimensions;

    /* Allocation of dimensions to rows, columns, and layers. */
    struct pivot_axis axes[PIVOT_N_AXES];

    struct hmap cells;          /* Contains "struct pivot_cell"s. */
  };

/* Creating and destroy pivot tables. */
struct pivot_table *pivot_table_create (const char *title);
struct pivot_table *pivot_table_create__ (struct pivot_value *title,
                                          const char *subtype);
struct pivot_table *pivot_table_create_for_text (struct pivot_value *title,
                                                 struct pivot_value *content);

struct pivot_table *pivot_table_ref (const struct pivot_table *);
struct pivot_table *pivot_table_unshare (struct pivot_table *);
void pivot_table_unref (struct pivot_table *);
bool pivot_table_is_shared (const struct pivot_table *);

/* Axes. */
void pivot_table_swap_axes (struct pivot_table *,
                            enum pivot_axis_type, enum pivot_axis_type);
void pivot_table_transpose (struct pivot_table *);
void pivot_table_move_dimension (struct pivot_table *,
                                 struct pivot_dimension *,
                                 enum pivot_axis_type, size_t ofs);

/* Styling. */
const struct pivot_table_look *pivot_table_get_look (
  const struct pivot_table *);
void pivot_table_set_look (struct pivot_table *,
                           const struct pivot_table_look *);

/* Format of PIVOT_RC_COUNT cells. */
void pivot_table_set_weight_var (struct pivot_table *,
                                 const struct variable *);
void pivot_table_set_weight_format (struct pivot_table *,
                                    const struct fmt_spec *);

/* Query. */
bool pivot_table_is_empty (const struct pivot_table *);

/* Output. */
void pivot_table_submit (struct pivot_table *);

/* Data cells. */
void pivot_table_put (struct pivot_table *, const size_t *dindexes, size_t n,
                      struct pivot_value *);
void pivot_table_put1 (struct pivot_table *, size_t idx1,
                       struct pivot_value *);
void pivot_table_put2 (struct pivot_table *, size_t idx1, size_t idx2,
                       struct pivot_value *);
void pivot_table_put3 (struct pivot_table *, size_t idx1, size_t idx2,
                       size_t idx3, struct pivot_value *);
void pivot_table_put4 (struct pivot_table *, size_t idx1, size_t idx2,
                       size_t idx3, size_t idx4, struct pivot_value *);

const struct pivot_value *pivot_table_get (const struct pivot_table *,
                                           const size_t *dindexes);

struct pivot_value *pivot_table_get_rw (struct pivot_table *,
                                        const size_t *dindexes);

bool pivot_table_delete (struct pivot_table *, const size_t *dindexes);

/* Footnotes.

   Use pivot_table_create_footnote() to create a footnote.
   Use pivot_value_add_footnote() to add a reference to a footnote. */
struct pivot_footnote
  {
    size_t idx;
    struct pivot_value *content;
    struct pivot_value *marker;
    bool show;
  };

struct pivot_footnote *pivot_table_create_footnote (
  struct pivot_table *, struct pivot_value *content);
struct pivot_footnote *pivot_table_create_footnote__ (
  struct pivot_table *, size_t idx,
  struct pivot_value *marker, struct pivot_value *content);

void pivot_footnote_destroy (struct pivot_footnote *);

/* Internals. */
void pivot_table_convert_indexes_ptod (const struct pivot_table *,
                                       const size_t *pindexes[PIVOT_N_AXES],
                                       size_t *dindexes);
size_t *pivot_table_enumerate_axis (const struct pivot_table *,
                                    enum pivot_axis_type,
                                    const size_t *layer_indexes,
                                    bool omit_empty, size_t *n);
#define PIVOT_ENUMERATION_FOR_EACH(INDEXES, ENUMERATION, AXIS)  \
  for ((INDEXES) = (ENUMERATION); *(INDEXES) != SIZE_MAX;       \
       (INDEXES) += MAX (1, (AXIS)->n_dimensions))

void pivot_table_assign_label_depth (struct pivot_table *);

void pivot_table_dump (const struct pivot_table *, int indentation);

/* pivot_value. */

enum pivot_value_type
  {
    PIVOT_VALUE_NUMERIC,          /* A value of a numeric variable. */
    PIVOT_VALUE_STRING,           /* A value of a string variable. */
    PIVOT_VALUE_VARIABLE,         /* Name of a variable. */
    PIVOT_VALUE_TEXT,             /* Text. */
    PIVOT_VALUE_TEMPLATE,         /* Templated text. */
  };

/* A pivot_value is the content of a single pivot table cell.  A pivot_value is
   also a pivot table's title, caption, footnote marker and contents, and so
   on.

   A given pivot_value is one of:

   1. A number resulting from a calculation (PIVOT_VALUE_NUMERIC).  Use
      pivot_value_new_number() to create such a pivot_value.

      A numeric pivot_value has an associated display format (usually an F or
      PCT format).  This format can be set directly on the pivot_value, but
      that is not usually the easiest way.  Instead, it is usually true that
      all of the values in a single category should have the same format
      (e.g. all "Significance" values might use format F40.3), so PSPP makes
      it easy to set the default format for a category while creating the
      category.  See pivot_dimension_create() for more details.

      For numbers that should be displayed as integers,
      pivot_value_new_integer() can occasionally be a useful special case.

   2. A numeric or string value obtained from data (PIVOT_VALUE_NUMERIC or
      PIVOT_VALUE_STRING).  If such a value corresponds to a variable, then the
      variable's name can be attached to the pivot_value.  If the value has a
      value label, then that can also be attached.  When a label is present,
      the user can control whether to show the value or the label or both.

      Use pivot_value_new_var_value() to create pivot_values of these kinds.

   3. A variable name (PIVOT_VALUE_VARIABLE).  The variable label, if any, can
      be attached too, and again the user can control whether to show the value
      or the label or both.

   4. A text string (PIVOT_VALUE_TEXT).  The value stores the string in English
      and translated into the output language (localized).  Use
      pivot_value_new_text() or pivot_value_new_text_format() for those cases.
      In some cases, only an English or a localized version is available for
      one reason or another, although this is regrettable; in those cases, use
      pivot_value_new_user_text() or pivot_value_new_user_text_nocopy().

   (There is also a PIVOT_VALUE_TEMPLATE but PSPP does not yet create these
   itself.)


   Footnotes
   =========

   A pivot_value may reference any number of footnotes.  Use
   pivot_value_add_footnote() to add a footnote reference.  The footnotes being
   referenced must first be created with pivot_table_create_footnote().


   Styling
   =======

   A pivot_value can have specific font and cell styles.  Only the user should
   add these.
*/
struct pivot_value
  {
    struct font_style *font_style;
    struct cell_style *cell_style;

    char **subscripts;
    size_t n_subscripts;

    size_t *footnote_indexes;
    size_t n_footnotes;

    enum pivot_value_type type;
    union
      {
        /* PIVOT_VALUE_NUMERIC. */
        struct
          {
            double x;                 /* The numeric value. */
            struct fmt_spec format;   /* Format to display 'x'. */
            char *var_name;           /* May be NULL. */
            char *value_label;        /* May be NULL. */
            enum settings_value_show show; /* Show value or label or both? */
            bool honor_small;         /* Honor value of pivot table 'small'? */
          }
        numeric;

        /* PIVOT_VALUE_STRING. */
        struct
          {
            char *s;                  /* The string value. */
            bool hex;                 /* Display in hex? */
            char *var_name;           /* May be NULL. */
            char *value_label;        /* May be NULL. */
            enum settings_value_show show; /* Show value or label or both? */
          }
        string;

        /* PIVOT_VALUE_VARIABLE. */
        struct
          {
            char *var_name;
            char *var_label;          /* May be NULL. */
            enum settings_value_show show; /* Show name or label or both? */
          }
        variable;

        /* PIVOT_VALUE_TEXT. */
        struct
          {
            char *local;              /* Localized. */
            char *c;                  /* English. */
            char *id;                 /* Identifier. */
            bool user_provided;
          }
        text;

        /* PIVOT_VALUE_TEMPLATE. */
        struct
          {
            char *local;              /* Localized. */
            char *id;                 /* Identifier. */
            struct pivot_argument *args;
            size_t n_args;
          }
        template;
      };
  };

/* Life cycle. */
struct pivot_value *pivot_value_clone (const struct pivot_value *);
void pivot_value_destroy (struct pivot_value *);

/* Numbers resulting from calculations. */
struct pivot_value *pivot_value_new_number (double);
struct pivot_value *pivot_value_new_integer (double);

/* Values from data. */
struct pivot_value *pivot_value_new_var_value (
  const struct variable *, const union value *);
struct pivot_value *pivot_value_new_value (const union value *, int width,
                                           const struct fmt_spec *,
                                           const char *encoding);

/* Values from variable names. */
struct pivot_value *pivot_value_new_variable (const struct variable *);

/* Values from text strings. */
struct pivot_value *pivot_value_new_text (const char *);
struct pivot_value *pivot_value_new_text_format (const char *, ...)
#if defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__>= 4) || __GNUC__ > 4)
  __attribute__((format(gnu_printf, 1, 2)));
#else
  __attribute__((format(__printf__, 1, 2)));
#endif

struct pivot_value *pivot_value_new_user_text (const char *, size_t length);
struct pivot_value *pivot_value_new_user_text_nocopy (char *);

/* Footnotes. */
void pivot_value_add_footnote (struct pivot_value *, const struct pivot_footnote *);

/* Numeric formats. */
void pivot_value_set_rc (const struct pivot_table *, struct pivot_value *,
                         const char *rc);

/* Converting a pivot_value to a string for display. */
char *pivot_value_to_string (const struct pivot_value *,
                             const struct pivot_table *);
char *pivot_value_to_string_defaults (const struct pivot_value *);
void pivot_value_format (const struct pivot_value *,
                         const struct pivot_table *, struct string *);
bool pivot_value_format_body (const struct pivot_value *,
                              const struct pivot_table *,
                              struct string *);

/* Styling. */
void pivot_value_get_style (struct pivot_value *,
                            const struct font_style *base_font_style,
                            const struct cell_style *base_cell_style,
                            struct table_area_style *);
void pivot_value_set_style (struct pivot_value *,
                            const struct table_area_style *);
void pivot_value_set_font_style (struct pivot_value *,
                                 const struct font_style *);
void pivot_value_set_cell_style (struct pivot_value *,
                                 const struct cell_style *);

/* Template arguments. */
struct pivot_argument
  {
    size_t n;
    struct pivot_value **values;
  };

void pivot_argument_uninit (struct pivot_argument *);
void pivot_argument_copy (struct pivot_argument *,
                          const struct pivot_argument *);

/* One piece of data within a pivot table. */
struct pivot_cell
  {
    struct hmap_node hmap_node; /* In struct pivot_table's 'cells' hmap. */
    struct pivot_value *value;
    unsigned int idx[];         /* One index per table dimension. */
  };

#endif /* output/pivot-table.h */
