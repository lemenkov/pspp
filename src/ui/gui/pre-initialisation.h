/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020  Free Software Foundation

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

#if !ENABLE_RELOCATABLE || !defined(__APPLE__)

static inline void
pre_initialisation (int *argc, char **argv)
{
}

#else

#include <sys/stat.h>
#include <glib.h>

static inline void
pre_initialisation (int *argc, char **argv)
{
  /* remove MacOS session identifier from the command line args */
  gint newargc = 0;
  for (gint i = 0; i < *argc; i++)
    {
      if (!g_str_has_prefix (argv[i], "-psn_"))
        {
          argv[newargc] = argv[i];
          newargc++;
        }
    }
  if (*argc > newargc)
    {
      argv[newargc] = NULL; /* glib expects NULL terminated array */
      *argc = newargc;
    }

  const char * progname = argv[0];

  /* helper to set environment variables for pspp to be relocatable.
   * Due to the latest changes it is not recommended to set it in the shell
   * wrapper anymore.
   */
  gchar resolved_path[PATH_MAX];
  /* on some OSX installations open file limit is 256 and GIMP needs more */
  struct rlimit limit;
  limit.rlim_cur = 10000;
  limit.rlim_max = 10000;
  setrlimit (RLIMIT_NOFILE, &limit);
  if (realpath (progname, resolved_path))
    {
      gchar  tmp[PATH_MAX];
      gchar *app_dir;
      gchar  res_dir[PATH_MAX];
      struct stat sb;

      app_dir = g_path_get_dirname (resolved_path);
      g_snprintf (tmp, sizeof(tmp), "%s/../../Resources", app_dir);
      if (realpath (tmp, res_dir) && !stat (res_dir,&sb) && S_ISDIR (sb.st_mode))
        g_print ("pspp is started as MacOS application\n");
      else
        return;
      g_free (app_dir);

      g_snprintf (tmp, sizeof(tmp), "%s/lib/gtk-3.0/3.0.0", res_dir);
      g_setenv ("GTK_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/gtk-3.0/gtk.immodules", res_dir);
      g_setenv ("GTK_IM_MODULE_FILE", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gegl-0.4", res_dir);
      g_setenv ("GEGL_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/babl-0.1", res_dir);
      g_setenv ("BABL_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache", res_dir);
      g_setenv ("GDK_PIXBUF_MODULE_FILE", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/fonts", res_dir);
      g_setenv ("FONTCONFIG_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gio/modules", res_dir);
      g_setenv ("GIO_MODULE_DIR", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/xdg", res_dir);
      g_setenv ("XDG_CONFIG_DIRS", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/share", res_dir);
      g_setenv ("XDG_DATA_DIRS", tmp, TRUE);

      if (g_getenv ("HOME")!=NULL)
        {
          g_snprintf (tmp, sizeof(tmp),
                      "%s/Library/Application Support/pspp/1.3/cache",
                      g_getenv("HOME"));
          g_setenv ("XDG_CACHE_HOME", tmp, TRUE);
        }
    }
}
#endif
