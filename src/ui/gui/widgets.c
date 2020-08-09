/*
PSPP - a program for statistical analysis.
Copyright (C) 2017 Free Software Foundation, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <gtksourceview/gtksource.h>

#include "widgets.h"

#include "gettext.h"

#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "psppire-buttonbox.h"
#include "psppire-keypad.h"
#include "psppire-acr.h"
#include "psppire-dictview.h"
#include "psppire-var-view.h"
#include "psppire-val-chooser.h"
#include "psppire-checkbox-treeview.h"

#include "psppire-dialog-action-aggregate.h"
#include "psppire-dialog-action-autorecode.h"
#include "psppire-dialog-action-barchart.h"
#include "psppire-dialog-action-binomial.h"
#include "psppire-dialog-action-chisquare.h"
#include "psppire-dialog-action-compute.h"
#include "psppire-dialog-action-comments.h"
#include "psppire-dialog-action-correlation.h"
#include "psppire-dialog-action-count.h"
#include "psppire-dialog-action-crosstabs.h"
#include "psppire-dialog-action-descriptives.h"
#include "psppire-dialog-action-examine.h"
#include "psppire-dialog-action-flip.h"
#include "psppire-dialog-action-factor.h"
#include "psppire-dialog-action-frequencies.h"
#include "psppire-dialog-action-histogram.h"
#include "psppire-dialog-action-indep-samps.h"
#include "psppire-dialog-action-k-related.h"
#include "psppire-dialog-action-k-independent.h"
#include "psppire-dialog-action-1sks.h"
#include "psppire-dialog-action-kmeans.h"
#include "psppire-dialog-action-logistic.h"
#include "psppire-dialog-action-means.h"
#include "psppire-dialog-action-paired.h"
#include "psppire-dialog-action-oneway.h"
#include "psppire-means-layer.h"
#include "psppire-dialog-action-rank.h"
#include "psppire-dialog-action-recode-same.h"
#include "psppire-dialog-action-recode-different.h"
#include "psppire-dialog-action-regression.h"
#include "psppire-dialog-action-reliability.h"
#include "psppire-dialog-action-roc.h"
#include "psppire-dialog-action-runs.h"
#include "psppire-dialog-action-scatterplot.h"
#include "psppire-dialog-action-select.h"
#include "psppire-dialog-action-sort.h"
#include "psppire-dialog-action-split.h"
#include "psppire-dialog-action-tt1s.h"
#include "psppire-dialog-action-two-sample.h"
#include "psppire-dialog-action-univariate.h"
#include "psppire-dialog-action-var-info.h"
#include "psppire-dialog-action-weight.h"
#include "psppire-value-entry.h"

static  volatile GType kludge;


typedef GType (*get_type_func)(void);

static const get_type_func dialog_action_types[]=
{
  psppire_dialog_action_1sks_get_type,
  psppire_dialog_action_autorecode_get_type,
  psppire_dialog_action_aggregate_get_type,
  psppire_dialog_action_binomial_get_type,
  psppire_dialog_action_barchart_get_type,
  psppire_dialog_action_chisquare_get_type,
  psppire_dialog_action_compute_get_type,
  psppire_dialog_action_comments_get_type,
  psppire_dialog_action_correlation_get_type,
  psppire_dialog_action_count_get_type,
  psppire_dialog_action_crosstabs_get_type,
  psppire_dialog_action_descriptives_get_type,
  psppire_dialog_action_examine_get_type,
  psppire_dialog_action_factor_get_type,
  psppire_dialog_action_flip_get_type,
  psppire_dialog_action_frequencies_get_type,
  psppire_dialog_action_histogram_get_type,
  psppire_dialog_action_logistic_get_type,
  psppire_dialog_action_kmeans_get_type,
  psppire_dialog_action_k_independent_get_type,
  psppire_dialog_action_k_related_get_type,
  psppire_dialog_action_means_get_type,
  psppire_dialog_action_oneway_get_type,
  psppire_dialog_action_paired_get_type,
  psppire_dialog_action_indep_samps_get_type,
  psppire_dialog_action_var_info_get_type,
  psppire_dialog_action_rank_get_type,
  psppire_dialog_action_recode_same_get_type,
  psppire_dialog_action_recode_different_get_type,
  psppire_dialog_action_reliability_get_type,
  psppire_dialog_action_regression_get_type,
  psppire_dialog_action_roc_get_type,
  psppire_dialog_action_runs_get_type,
  psppire_dialog_action_scatterplot_get_type,
  psppire_dialog_action_select_get_type,
  psppire_dialog_action_sort_get_type,
  psppire_dialog_action_split_get_type,
  psppire_dialog_action_tt1s_get_type,
  psppire_dialog_action_two_sample_get_type,
  psppire_dialog_action_weight_get_type,
  psppire_dialog_action_univariate_get_type
};


static void
preregister_actions (void)
{
  int i;
  for (i = 0; i < sizeof (dialog_action_types) / sizeof (dialog_action_types[0]); ++i)
    {
      get_type_func x = dialog_action_types[i];
      x ();
    }
}


static void
tx_string_to_double (const GValue *src, GValue *dest)
{
  const gchar *str = g_value_get_string (src);
  gdouble dble = g_strtod (str, NULL);
  g_value_set_double (dest, dble);
}


static void
tx_string_to_int (const GValue *src, GValue *dest)
{
  const gchar *str = g_value_get_string (src);
  gint x = atoi (str);
  g_value_set_int (dest, x);
}

static void
enum_to_string (const GValue *src, GValue *dest)
{
  gint n = g_value_get_enum (src);
  GType t = G_VALUE_TYPE (src);
  GEnumClass *ec = g_type_class_ref (t);
  GEnumValue *ev = g_enum_get_value (ec, n);

  g_value_set_string (dest, gettext (ev->value_nick));
}



extern GType align_enum_type;
extern GType measure_enum_type;
extern GType role_enum_type;


extern const GEnumValue align[];
extern const GEnumValue measure[];
extern const GEnumValue role[];



static void
preregister_misc (void)
{
  align_enum_type = g_enum_register_static ("PsppAlignment", align);
  measure_enum_type = g_enum_register_static ("PsppMeasure", measure);
  role_enum_type = g_enum_register_static ("PsppRole", role);

  g_value_register_transform_func (G_TYPE_STRING, G_TYPE_DOUBLE, tx_string_to_double);
  g_value_register_transform_func (G_TYPE_STRING, G_TYPE_INT, tx_string_to_int);

  g_value_register_transform_func (measure_enum_type, G_TYPE_STRING, enum_to_string);
  g_value_register_transform_func (align_enum_type, G_TYPE_STRING, enum_to_string);
  g_value_register_transform_func (role_enum_type, G_TYPE_STRING, enum_to_string);
}


/* Any custom widgets which are to be used in GtkBuilder ui files
   need to be preregistered, otherwise GtkBuilder refuses to
   acknowledge their existence. */
void
preregister_widgets (void)
{
  psppire_val_chooser_get_type ();
  psppire_dialog_get_type ();
  psppire_selector_get_type ();
  psppire_button_box_get_type ();
  psppire_keypad_get_type ();
  psppire_acr_get_type ();
  psppire_dict_view_get_type ();
  psppire_var_view_get_type ();
  psppire_value_entry_get_type ();
  psppire_checkbox_treeview_get_type ();
  psppire_means_layer_get_type ();

  preregister_actions ();
  preregister_misc ();

  /* This seems to be necessary on Cygwin.
     It ought not to be necessary.  Having it here can't do any harm. */
  kludge = gtk_source_view_get_type ();
}
