/*
 * This file is part of hildon-control-panel
 *
 * Copyright (C) 2006 Nokia Corporation.
 *
 * Author: Lucas Rocha <lucas.rocha@nokia.com>
 * Contact: Karoliina Salminen <karoliina.t.salminen@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <dlfcn.h>
#include <string.h>

#include <osso-log.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "hcp-program.h"
#include "hcp-app.h"

#define HCP_APP_GET_PRIVATE(object) \
        (G_TYPE_INSTANCE_GET_PRIVATE ((object), HCP_TYPE_APP, HCPAppPrivate))

G_DEFINE_TYPE (HCPApp, hcp_app, G_TYPE_OBJECT);

enum
{
  PROP_NAME = 1,
  PROP_PLUGIN,
  PROP_ICON,
  PROP_CATEGORY,
  PROP_IS_RUNNING,
  PROP_GRID,
  PROP_ITEM_POS
};

struct _HCPAppPrivate 
{
    gchar      *name;
    gchar      *plugin;
    gchar      *icon;
    gchar      *category;
    gboolean    is_running;
    GtkWidget  *grid;
    gint        item_pos;

    hcp_plugin_save_state_f *save_state;
};

typedef struct _HCPPlugin
{
  void               *handle;
  hcp_plugin_exec_f  *exec;
} HCPPlugin;

typedef struct _PluginLaunchData
{
  HCPApp     *app;
  HCPPlugin  *plugin;
  gboolean   user_activated;
} PluginLaunchData;

#define HCP_PLUGIN_EXEC_SYMBOL        "execute"
#define HCP_PLUGIN_SAVE_STATE_SYMBOL  "save_state"

static void
hcp_app_init (HCPApp *app)
{
  app->priv = HCP_APP_GET_PRIVATE (app);

  app->priv->name = NULL;
  app->priv->plugin = NULL;
  app->priv->icon = NULL;
  app->priv->category = NULL;
  app->priv->is_running = FALSE;
  app->priv->grid = NULL;
  app->priv->item_pos = -1;
  app->priv->save_state = NULL;
}

static void
hcp_app_load (HCPApp *app, HCPPlugin *plugin)
{
  gchar *plugin_path = NULL;

  g_return_if_fail (app && app->priv->plugin);

  if (*app->priv->plugin == G_DIR_SEPARATOR)
  {
    /* .desktop provided fullpath, use that */
    plugin_path = g_strdup (app->priv->plugin);
  }
  else
  {
    plugin_path = g_build_filename (HCP_PLUGIN_DIR, app->priv->plugin, NULL);
  }

  plugin->handle = dlopen (plugin_path, RTLD_LAZY);

  g_free (plugin_path);

  if (!plugin->handle)
  {
    ULOG_ERR ("Could not load hildon-control-panel applet %s: %s",
              app->priv->plugin,
              dlerror());
    return;
  }

  plugin->exec = dlsym (plugin->handle, HCP_PLUGIN_EXEC_SYMBOL);

  if (!plugin->exec)
  {
    ULOG_ERR ("Could not find "HCP_PLUGIN_SYMBOL" symbol in "
              "hildon-control-panel applet %s: %s",
              app->priv->plugin,
              dlerror ());

    dlclose (plugin->handle);

    plugin->handle = NULL;
  }

  app->priv->save_state = dlsym (plugin->handle, HCP_PLUGIN_SAVE_STATE_SYMBOL);
}

static void
hcp_app_unload (HCPApp *item, HCPPlugin *plugin)
{
    g_return_if_fail (plugin->handle);

    if (dlclose (plugin->handle))
    {
        ULOG_ERR ("An error occurred when unloading hildon-control-panel "
                  "applet %s: %s",
                  app->priv->plugin,
                  dlerror ());
    }
}

static gboolean
hcp_app_idle_launch (PluginLaunchData *d)
{
  HCPPlugin *p;
  HCPProgram *program = hcp_program_get_instance ();
  
  p = g_new0 (HCPPlugin, 1);
  
  hcp_app_load (d->app, p);
  
  if (!p->handle)
    goto cleanup;
  
  d->app->priv->is_running = TRUE;

  /* Always use hcp->window as parent. If CP is launched without
   * UI (run_applet RPC) the applet's dialog will be system modal */ 
  p->exec (program->osso, program->window, d->user_activated);
  d->app->priv->is_running = FALSE;

  program->execute = 0;

  hcp_app_unload (d->app, p);

  /* HCP was launched window less, so we can exit once we are done
   * with this applet */
  if (!program->window)
     gtk_main_quit ();

cleanup:
  g_free (p);
  g_object_unref (d->app);
  g_free (d);

  return FALSE;
}

static void
hcp_app_finalize (GObject *object)
{
  HCPApp *app;
  HCPAppPrivate *priv;
  
  g_return_if_fail (object != NULL);
  g_return_if_fail (HCP_IS_APP (object));

  app = HCP_APP (object);
  priv = app->priv;

  if (priv->name != NULL) 
  {
    g_free (priv->name);
    priv->name = NULL;
  }

  if (priv->plugin != NULL) 
  {
    g_free (priv->plugin);
    priv->plugin = NULL;
  }

  if (priv->icon != NULL) 
  {
    g_free (priv->icon);
    priv->icon = NULL;
  }

  if (priv->category != NULL) 
  {
    g_free (priv->category);
    priv->category = NULL;
  }

  if (priv->grid != NULL) 
  {
    g_object_unref (priv->grid);
    priv->grid = NULL;
  }

  G_OBJECT_CLASS (hcp_app_parent_class)->finalize (object);
}

static void
hcp_app_get_property (GObject    *gobject,
                      guint      prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{

  HCPApp *app = HCP_APP (gobject);

  switch (prop_id)
  {
    case PROP_NAME:
      g_value_set_string (value, app->priv->name);
      break;

    case PROP_PLUGIN:
      g_value_set_string (value, app->priv->plugin);
      break;

    case PROP_ICON:
      g_value_set_string (value, app->priv->icon);
      break;

    case PROP_CATEGORY:
      g_value_set_string (value, app->priv->category);
      break;

    case PROP_IS_RUNNING:
      g_value_set_boolean (value, app->priv->is_running);
      break;

    case PROP_GRID:
      g_value_set_object (value, app->priv->grid);
      break;

    case PROP_ITEM_POS:
      g_value_set_int (value, app->priv->item_pos);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
hcp_app_set_property (GObject      *gobject,
                      guint        prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  HCPApp *app = HCP_APP (gobject);
  
  switch (prop_id)
  {
    case PROP_NAME:
      g_free (app->priv->name);
      app->priv->name = g_strdup (g_value_get_string (value));
      break;

    case PROP_PLUGIN:
      g_free (app->priv->plugin);
      app->priv->plugin = g_strdup (g_value_get_string (value));
      break;

    case PROP_ICON:
      g_free (app->priv->icon);
      app->priv->icon = g_strdup (g_value_get_string (value));
      break;

    case PROP_CATEGORY:
      g_free (app->priv->category);
      app->priv->category = g_strdup (g_value_get_string (value));
      break;

    case PROP_IS_RUNNING:
      app->priv->is_running = g_value_get_boolean (value);
      break;

    case PROP_GRID:
      if (app->priv->grid)
        g_object_unref (app->priv->grid);
      app->priv->grid = g_object_ref (g_value_get_object (value));
      break;

    case PROP_ITEM_POS:
      app->priv->item_pos = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
hcp_app_class_init (HCPAppClass *class)
{
  GObjectClass *g_object_class = (GObjectClass *) class;
  
  g_object_class->finalize = hcp_app_finalize;

  g_object_class->get_property = hcp_app_get_property;
  g_object_class->set_property = hcp_app_set_property;
 
  g_object_class_install_property (g_object_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "Set app's name",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));
 
  g_object_class_install_property (g_object_class,
                                   PROP_PLUGIN,
                                   g_param_spec_string ("plugin",
                                                        "Plugin",
                                                        "Set app's plugin path",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));
 
  g_object_class_install_property (g_object_class,
                                   PROP_ICON,
                                   g_param_spec_string ("icon",
                                                        "Icon",
                                                        "Set app's icon",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (g_object_class,
                                   PROP_CATEGORY,
                                   g_param_spec_string ("category",
                                                        "Category",
                                                        "Set app's category",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (g_object_class,
                                   PROP_IS_RUNNING,
                                   g_param_spec_boolean ("is-running",
                                                        "Running",
                                                        "Whether the application is running or not",
                                                        FALSE,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (g_object_class,
                                   PROP_GRID,
                                   g_param_spec_object ("grid",
                                                        "Grid",
                                                        "The grid associated with this application",
                                                        GTK_TYPE_WIDGET,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (g_object_class,
                                   PROP_ITEM_POS,
                                   g_param_spec_int ("item-pos",
                                                     "Item position",
                                                     "Application position inside the grid",
                                                     -1,
                                                     100,
                                                     -1,
                                                      (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_type_class_add_private (g_object_class, sizeof (HCPAppPrivate));
}

GObject *
hcp_app_new ()
{
  GObject *app = g_object_new (HCP_TYPE_APP, NULL);

  return app;
}

void
hcp_app_launch (HCPApp *app, gboolean user_activated)
{
  PluginLaunchData *d;
  HCPProgram *program = hcp_program_get_instance ();

  if (!program->execute)
  {
      program->execute = 1;

      d = g_new0 (PluginLaunchData, 1);

      d->user_activated = user_activated;
      d->app = g_object_ref (app);

      /* We launch plugins inside an idle loop so we are still able
       * to receive DBus messages */
      g_idle_add ((GSourceFunc) hcp_app_idle_launch, d);
  }
}

void
hcp_app_focus (HCPApp *app)
{
  if (app->priv->grid) 
  {
    GtkTreePath *path;

    gtk_widget_grab_focus (app->priv->grid);
    path = gtk_tree_path_new_from_indices (app->priv->item_pos, -1);
    gtk_icon_view_select_path (GTK_ICON_VIEW (app->priv->grid), path);
    gtk_tree_path_free (path);
  }
}

void
hcp_app_save_state (HCPApp *app)
{
  HCPProgram *program = hcp_program_get_instance ();

  if (app->priv->save_state)
    app->priv->save_state (program->osso, NULL /* What is expected here? -- Jobi */);
}

gboolean
hcp_app_is_running (HCPApp *app)
{
  return app->priv->is_running;
}

gboolean
hcp_app_can_save_state (HCPApp *app)
{
  return (app->priv->save_state == NULL);
}

gint
hcp_app_sort_func (const HCPApp *a, const HCPApp *b)
{
  g_return_val_if_fail (a && b, 0);

  /* Sort by the translated name */
  return strcmp (_(a->priv->name), _(b->priv->name));
}
