/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017  Free Software Foundation

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


#ifndef OPTIONS_DIALOG_H
#define OPTIONS_DIALOG_H

#include "psppire-data-window.h"

typedef enum
{
  PSPP_OPTIONS_VAR_ORDER_UNSORTED,
  PSPP_OPTIONS_VAR_ORDER_NAME,
  PSPP_OPTIONS_VAR_ORDER_LABEL,
} PsppOptionsVarOrder;

GType pspp_options_var_order_get_type (void) G_GNUC_CONST;
#define PSPP_TYPE_OPTIONS_VAR_ORDER (pspp_options_var_order_get_type ())

typedef enum
  {
    PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED,
    PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT,
    PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM,
  } PsppOptionsJournalLocation;

GType pspp_options_journal_location_get_type (void) G_GNUC_CONST;
#define PSPP_TYPE_OPTIONS_JOURNAL_LOCATION (pspp_options_journal_location_get_type ())

/* Pops up the Options dialog box */
void options_dialog (PsppireDataWindow *);

void options_init (void);

#endif
