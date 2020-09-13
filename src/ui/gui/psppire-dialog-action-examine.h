/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2020  Free Software Foundation

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


#ifndef __PSPPIRE_DIALOG_ACTION_EXAMINE_H__
#define __PSPPIRE_DIALOG_ACTION_EXAMINE_H__

#include <glib-object.h>
#include <glib.h>

#include "psppire-dialog-action.h"

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE (psppire_dialog_action_examine_get_type ())

#define PSPPIRE_DIALOG_ACTION_EXAMINE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, PsppireDialogActionExamine))

#define PSPPIRE_DIALOG_ACTION_EXAMINE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, \
                                 PsppireDialogActionExamineClass))


#define PSPPIRE_IS_DIALOG_ACTION_EXAMINE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE))

#define PSPPIRE_IS_DIALOG_ACTION_EXAMINE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE))


#define PSPPIRE_DIALOG_ACTION_EXAMINE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_EXAMINE, \
				   PsppireDialogActionExamineClass))

typedef struct _PsppireDialogActionExamine       PsppireDialogActionExamine;
typedef struct _PsppireDialogActionExamineClass  PsppireDialogActionExamineClass;


enum PsppireDialogActionExamineOpts
  {
    OPT_LISTWISE,
    OPT_PAIRWISE,
    OPT_REPORT
  };

enum PsppireDialogActionExaminePlotsBoxplots
  {
   BOXPLOT_FACTORS,
   BOXPLOT_DEPENDENTS,
   BOXPLOT_NONE
  };

enum PsppireDialogActionExaminePlotsSpreadlevel
  {
   SPREAD_NONE,
   SPREAD_POWER,
   SPREAD_TRANS,
   SPREAD_UNTRANS
  };

enum PsppireDialogActionExaminePlotsSpreadpower
  {
   SPREADPOWER_NATLOG,
   SPREADPOWER_CUBE,
   SPREADPOWER_SQUARE,
   SPREADPOWER_SQUAREROOT,
   SPREADPOWER_RECROOT,
   SPREADPOWER_RECIPROCAL
  };

struct _PsppireDialogActionExamine
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *variables;
  GtkWidget *factors;
  GtkWidget *id_var;

  GtkWidget *display_both_button;
  GtkWidget *display_stats_button;
  GtkWidget *display_plots_button;

  /* The stats dialog */
  GtkWidget *stats_dialog;
  GtkWidget *descriptives_button;
  GtkWidget *extremes_button;
  GtkWidget *percentiles_button;
  guint stats;

  /* The options dialog */
  GtkWidget *opts_dialog;
  GtkWidget *listwise;
  GtkWidget *pairwise;
  GtkWidget *report;
  enum PsppireDialogActionExamineOpts opts;

  /* The plots dialog */
  GtkWidget *plots_dialog;
  GtkWidget *boxplot_factors_button;
  GtkWidget *boxplot_dependents_button;
  GtkWidget *boxplot_none_button;
  enum PsppireDialogActionExaminePlotsBoxplots boxplots;
  GtkWidget *histogram_button;
  bool histogram;
  GtkWidget *npplots_button;
  bool npplots;
  GtkWidget *spread_none_button;
  GtkWidget *spread_power_button;
  GtkWidget *spread_trans_button;
  GtkWidget *spread_untrans_button;
  enum PsppireDialogActionExaminePlotsSpreadlevel spreadlevel;
  GtkWidget *spread_power_combo;
  enum PsppireDialogActionExaminePlotsSpreadpower spreadpower;

};


struct _PsppireDialogActionExamineClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_examine_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_EXAMINE_H__ */
