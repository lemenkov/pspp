/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2016  Free Software Foundation

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

#include "psppire-dialog-action.h"
#include "psppire-dialog.h"
#include "executor.h"
#include "helper.h"
#include "psppire-data-window.h"

static void psppire_dialog_action_init            (PsppireDialogAction      *act);
static void psppire_dialog_action_class_init      (PsppireDialogActionClass *class);

static GObjectClass * parent_class = NULL;


static const gchar *
__get_name (GAction *act)
{
  return G_OBJECT_TYPE_NAME (act);
}

static const GVariantType *
__get_state_type (GAction *act)
{
  return NULL;
}


static GVariant *
__get_state (GAction *act)
{
  return NULL;
}


static const GVariantType *
__get_parameter_type (GAction *act)
{
  return PSPPIRE_DIALOG_ACTION (act)->parameter_type;
}

static gboolean
__get_enabled (GAction *act)
{
  return TRUE;
}

static void psppire_dialog_action_activate (PsppireDialogAction *act, GVariant *parameter);

void
psppire_dialog_action_activate_null (PsppireDialogAction *act)
{
  psppire_dialog_action_activate (act, NULL);
}


static void
__activate (GAction *action, GVariant *parameter)
{
  psppire_dialog_action_activate (PSPPIRE_DIALOG_ACTION (action), parameter);
}


static void
action_model_init (GActionInterface *iface)
{
  iface->get_name = __get_name;
  iface->get_state_type = __get_state_type;
  iface->get_state = __get_state;
  iface->get_parameter_type = __get_parameter_type;
  iface->get_enabled = __get_enabled;
  iface->activate = __activate;
}


GType
psppire_dialog_action_get_type (void)
{
  static GType de_type = 0;

  if (!de_type)
    {
      static const GTypeInfo de_info =
      {
	sizeof (PsppireDialogActionClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_dialog_action_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireDialogAction),
	0,
	(GInstanceInitFunc) psppire_dialog_action_init,
      };


      static const GInterfaceInfo ga_info = {
	(GInterfaceInitFunc) action_model_init,
	NULL,
	NULL
      };


      de_type = g_type_register_static (G_TYPE_OBJECT, "PsppireDialogAction",
					&de_info, G_TYPE_FLAG_ABSTRACT);

      g_type_add_interface_static (de_type, G_TYPE_ACTION, &ga_info);
    }

  return de_type;
}


/* Properties */
enum
{
  PROP_0,
  PROP_TOPLEVEL,
  PROP_NAME,
  PROP_ENABLED,
  PROP_STATE,
  PROP_STATE_TYPE,
  PROP_PARAMETER_TYPE
};

static void
psppire_dialog_action_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireDialogAction *act = PSPPIRE_DIALOG_ACTION (object);

  switch (prop_id)
    {
    case PROP_TOPLEVEL:
      {
	GObject *p = g_value_get_object (value);
	act->toplevel = GTK_WIDGET (p);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_dialog_action_get_property (GObject    *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireDialogAction *dialog_action = PSPPIRE_DIALOG_ACTION (object);

  switch (prop_id)
    {
    case PROP_TOPLEVEL:
      g_value_take_object (value, dialog_action->toplevel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}



static void
on_destroy_dataset (GObject *w)
{
  GHashTable *t = g_object_get_data (w, "thing-table");
  GSList *dl = g_object_get_data (w, "widget-list");
  
  g_slist_free_full (dl, (GDestroyNotify) gtk_widget_destroy);
  g_hash_table_unref (t);
}

/* Each toplevel widget - that is the data window, which generally has a 1-1 association
   with a dataset - has an associated GHashTable.
   
   This GHashTable is keyed by the address of a PsppireDialogAction, and its values
   are user determined pointers (typically a GtkBuilder*).

   This is useful for storing the state of dialogs so they can persist between invocations.
*/
GHashTable *
psppire_dialog_action_get_hash_table (PsppireDialogAction *act)
{
  GHashTable *t = g_object_get_data (G_OBJECT (act->toplevel), "thing-table");
  if (t == NULL)
    {
      t = g_hash_table_new_full (g_direct_hash, g_direct_equal, 0, g_object_unref);
      g_object_set_data (G_OBJECT (act->toplevel), "thing-table", t);
      g_object_set_data (G_OBJECT (act->toplevel), "widget-list", NULL);
      g_signal_connect (act->toplevel, "destroy", G_CALLBACK (on_destroy_dataset), NULL);
    }

  return t;
}


static void
psppire_dialog_action_activate (PsppireDialogAction *act, GVariant *parameter)
{
  gint response;

  PsppireDialogActionClass *class = PSPPIRE_DIALOG_ACTION_GET_CLASS (act);

  act->dict = PSPPIRE_DATA_WINDOW(act->toplevel)->dict;

  GSList *wl = g_object_get_data (G_OBJECT (act->toplevel), "widget-list");
  wl = g_slist_prepend (wl, act->dialog);
  g_object_set_data (G_OBJECT (act->toplevel), "widget-list", wl);

  if (class->activate)
    class->activate (act, parameter);

  gtk_window_set_transient_for (GTK_WINDOW (act->dialog),
				GTK_WINDOW (act->toplevel));

  if (act->source)
    {
      g_object_set (act->source, "model", act->dict, NULL);
      gtk_widget_grab_focus (act->source);
    }

  if (!act->activated)
    psppire_dialog_reload (PSPPIRE_DIALOG (act->dialog));

  act->activated = TRUE;

  response = psppire_dialog_run (PSPPIRE_DIALOG (act->dialog));

  if ( class->generate_syntax )
    {
      switch (response)
	{
	case GTK_RESPONSE_OK:
	  g_free (execute_syntax_string (PSPPIRE_DATA_WINDOW (act->toplevel),
					 class->generate_syntax (act)));
	  break;
	case PSPPIRE_RESPONSE_PASTE:
	  g_free (paste_syntax_to_window (class->generate_syntax (act)));
	  break;
	default:
	  break;
	}
    }
}

static void
psppire_dialog_action_class_init (PsppireDialogActionClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  GParamSpec *toplevel_spec =
    g_param_spec_object ("top-level",
			 "Top Level",
			 "The top level widget to which this dialog action belongs",
			 GTK_TYPE_WINDOW,
			 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  object_class->set_property = psppire_dialog_action_set_property;
  object_class->get_property = psppire_dialog_action_get_property;

  class->generate_syntax = NULL;

  class->activate = psppire_dialog_action_activate;

  g_object_class_install_property (object_class,
                                   PROP_TOPLEVEL,
                                   toplevel_spec);

  g_object_class_override_property (object_class, PROP_NAME, "name");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_STATE, "state");
  g_object_class_override_property (object_class, PROP_STATE_TYPE, "state-type");
  g_object_class_override_property (object_class, PROP_PARAMETER_TYPE, "parameter-type");
}


static void
psppire_dialog_action_init (PsppireDialogAction *act)
{
  act->toplevel = NULL;
  act->dict = NULL;
  act->activated = FALSE;
  act->parameter_type = NULL;
}

void
psppire_dialog_action_set_valid_predicate (PsppireDialogAction *act, 
					   ContentsAreValid dialog_state_valid)
{
  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (act->dialog),
                                      dialog_state_valid, act);
}

void
psppire_dialog_action_set_refresh (PsppireDialogAction *pda, 
				   PsppireDialogActionRefresh refresh)
{
  g_signal_connect_swapped (pda->dialog, "refresh", G_CALLBACK (refresh),  pda);
}


void 
psppire_dialog_action_set_activation (gpointer class, activation activate)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->activate = (void (*)(PsppireDialogAction *, GVariant *)) activate;
}

