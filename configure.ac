dnl PSPP - a program for statistical analysis.
dnl Copyright (C) 2017, 2021 Free Software Foundation, Inc.
dnl
dnl This program is free software: you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation, either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.
dnl
dnl Process this file with autoconf to produce a configure script.

dnl Initialize.
AC_PREREQ(2.64)
AC_INIT([GNU PSPP], [2.0.1], [bug-gnu-pspp@gnu.org], [pspp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_TESTDIR([tests])
AM_INIT_AUTOMAKE([tar-ustar info-in-builddir])

dnl Checks for programs.
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
gl_EARLY
if test x"$cross_compiling" = x"yes"; then
   if test x"$srcdir" = x"."; then
      AC_MSG_ERROR([Cross compiling must be done out of source.])
   fi
fi

AM_PROG_CC_C_O
LT_INIT([dlopen win32-dll])
AC_HEADER_TIOCGWINSZ

AC_CHECK_SIZEOF([ptrdiff_t])
binary_width=`expr 8 '*' $ac_cv_sizeof_ptrdiff_t`
AC_SUBST([binary_width])


PKG_PROG_PKG_CONFIG
m4_pattern_forbid([PKG_CHECK_MODULES])
PSPP_CHECK_CLICKSEQUENCE
PSPP_CHECK_MAKEINFO_DOCBOOK_XML
PSPP_CHECK_DOT

PSPP_ENABLE_WERROR
PSPP_ENABLE_OPTION([-Wall])
PSPP_ENABLE_OPTION([-Wextra])
PSPP_ENABLE_OPTION([-Wwrite-strings])
PSPP_ENABLE_OPTION([-Wstrict-prototypes])
PSPP_ENABLE_OPTION([-Wpointer-arith])
PSPP_ENABLE_OPTION([-Wno-sign-compare])
PSPP_ENABLE_OPTION([-Wmissing-prototypes])

AM_CONDITIONAL(cc_is_gcc, test x"$GCC" = x"yes" )
AM_CONDITIONAL(host_is_w32, [case $host_os in mingw*) true;; *) false;; esac] )

PSPP_PERL
PSPP_PYTHON3
AC_CHECK_PROGS([TEX], [tex], [no])
AC_SUBST([TEX])

# This suppresses an error in gl/careadlinkat.c that otherwise can't be
# avoided in GCC 10.x:
#   ../../gl/careadlinkat.c: In function ‘careadlinkat’:
#   cc1: error: function may return address of local variable
CFLAGS="$CFLAGS -DGCC_LINT"

dnl Disable automatic po/ support, because PSPP provides its own po/ support.
AC_PROVIDE([AM_PO_SUBDIRS])
AM_GNU_GETTEXT([external], [need-ngettext])

LIBS="$LIBINTL $LIBS"
AC_CHECK_PROGS([MSGFMT], [msgfmt])
if test "x$MSGFMT" = x; then
  PSPP_REQUIRED_PREREQ([msgfmt (is usually provided with gettext)])
fi

AC_CHECK_PROGS([XGETTEXT], [xgettext])
if test "x$XGETTEXT" = x; then
  PSPP_REQUIRED_PREREQ([xgettext (is usually provided with gettext)])
fi

PSPP_CHECK_XGETTEXT

if test "$pspp_cv_progrecent_xgettext"  != "yes"; then
   PSPP_REQUIRED_PREREQ([xgettext version 0.20 or greater])
fi

dnl Checks for libraries.
AC_SYS_LARGEFILE
AC_SEARCH_LIBS([sin], [m])
PSPP_LC_PAPER


AC_ARG_VAR([PSPP_LDFLAGS], [linker flags to be used for linking the pspp binary only])
AC_ARG_VAR([PSPPIRE_LDFLAGS], [linker flags to be used for linking the psppire binary only])

# Support for Cairo and Pango.
PKG_CHECK_MODULES([CAIRO], [cairo >= 1.5 pango >= 1.22 pangocairo],
  [CPPFLAGS="$CPPFLAGS $CAIRO_CFLAGS"],
  [PSPP_REQUIRED_PREREQ([cairo 1.5 or later and pango 1.22 or later])])

dnl One of the tests uses xmllint.
AC_CACHE_CHECK([for an xmllint program which fits our needs],[ac_cv_path_XMLLINT],
[AC_PATH_PROGS_FEATURE_CHECK([XMLLINT], [xmllint],
  [[$ac_path_XMLLINT --version 2>&1 | $GREP XPath > /dev/null &&
    test `$ac_path_XMLLINT --version 2>&1 | $SED 's/^.* \([0-9]*\).*/\1/'` -gt 20900 &&
    ac_cv_path_XMLLINT=$ac_path_XMLLINT]],
   [AC_MSG_WARN([no appropriate xmllint program found. Using a dummy (echo)]); ac_cv_path_XMLLINT=$ECHO])])

AC_SUBST([XMLLINT],[$ac_cv_path_XMLLINT])

# Support for GUI.
AC_ARG_WITH([gui],
  [AS_HELP_STRING([--without-gui],
                  [Don't build the PSPPIRE GUI (using GTK+)])],
  [], [with_gui=yes])
AM_CONDITIONAL([building_gui], [test "$with_gui" != "no"])
if test "$with_gui" != "no"; then
  PKG_CHECK_MODULES([GTHREAD], [gthread-2.0], [],
    [PSPP_REQUIRED_PREREQ([gthread 2.0 (or use --without-gui)])])

  PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= 3.22.0], [],
    [PSPP_REQUIRED_PREREQ([gtk+ 3.0 version 3.22.0 or later (or use --without-gui)])])

  PKG_CHECK_MODULES([GTKSOURCEVIEW], [gtksourceview-4 >= 4.0], [],
    [PKG_CHECK_MODULES([GTKSOURCEVIEW], [gtksourceview-3.0 >= 3.4.2], [],
      [PSPP_REQUIRED_PREREQ([gtksourceview 4.x or gtksourceview 3.x version 3.4.2 or later (or use --without-gui)])])])

  PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.44], [],
    [PSPP_REQUIRED_PREREQ([glib 2.0 version 2.44 or later (or use --without-gui)])])

  PKG_CHECK_MODULES([SPREAD_SHEET_WIDGET], [spread-sheet-widget >= 0.7], [],
    [PSPP_REQUIRED_PREREQ([spread-sheet-widget 0.7 (or use --without-gui)])])
  PKG_CHECK_VAR([SPREAD_SHEET_WIDGET_LIBDIR], [spread-sheet-widget], [libdir])

  AC_ARG_VAR([GLIB_GENMARSHAL])
  AC_CHECK_PROGS([GLIB_GENMARSHAL], [glib-genmarshal])
  if test "x$GLIB_GENMARSHAL" = x; then
    PSPP_REQUIRED_PREREQ([glib-genmarshal (or use --without-gui)])
  fi

  AC_CHECK_PROGS([GLIB_COMPILE_RESOURCES], [glib-compile-resources])
  if test "x$GLIB_COMPILE_RESOURCES" = x; then
    PSPP_REQUIRED_PREREQ([glib-compile-resources (or use --without-gui)])
  fi

  dnl g_memdup2 has been introduced in glib 2.67.3. Older versions need
  dnl a workaround
  AC_CHECK_LIB(glib-2.0, g_memdup2, [AC_DEFINE([HAVE_G_MEMDUP2], [1], [g_memdup2 check])])

fi

gl_NEXT_HEADERS([glib.h])
gl_NEXT_HEADERS([gtk/gtk.h])

dnl Checks needed for psql reader

dnl The 8.x series postgres tarballs installs with a default prefix of
dnl /usr/local/pgsql whereas, OSes (such as debian)  install into /usr .

dnl If the libpq.so file is in a non standard path, (one which the linker
dnl doesn't search) then we must set the PG_LDFLAGS to -R/usr/wherever/
dnl However, if the system happens to have more than one version of other
dnl dependent libraries (eg. libgtk) in different standard paths. (eg:
dnl /usr and /usr/local) then the wrong one may get linked.

dnl To overcome this, we first check for libpq in the standard places, and
dnl only set PG_LDFLAGS if it's not found.


AC_ARG_WITH(
  libpq,
  [AS_HELP_STRING([--without-libpq], [don't compile in Postgres capability])])
AC_ARG_VAR([PG_CONFIG],
           [Full pathname of the pg_config program shipped with Postgresql])
AC_PATH_PROG([PG_CONFIG], [pg_config], [],
             [$PATH:/usr/local/pgsql/bin:/usr/pgsql/bin])

if test x"$with_libpq" != x"no" && test -n "$PG_CONFIG" ; then
  AC_SUBST(PG_CONFIG)
  PG_CFLAGS=-I`$PG_CONFIG --includedir`
  AC_SUBST(PG_CFLAGS)

  AC_CHECK_LIB([pq], [PQexec],
     [:],
     [PG_LDFLAGS="-L`$PG_CONFIG --libdir` -R`$PG_CONFIG --libdir`"])
  AC_SUBST(PG_LDFLAGS)

  PG_LIBS=-lpq
  AC_SUBST(PG_LIBS)

  PKG_CHECK_EXISTS(libssl,
	AC_DEFINE([USE_SSL],1,
		   [Define to 1 if the openssl library is present.])
	   ,)

  # Now verify that we can actually link against libpq.  If we are
  # cross-compiling and picked up a host libpq, or if libpq is just
  # badly installed, this will fail.
  AC_CACHE_CHECK(
    [whether -lpq links successfully],
    [pspp_cv_have_libpq],
    [save_CPPFLAGS=$CPPFLAGS
     save_LIBS=$LIBS
     save_LDFLAGS=$LDFLAGS
     CPPFLAGS="$PG_CFLAGS $CPPFLAGS"
     LIBS="$PG_LIBS $LIBS"
     LDFLAGS="$PG_LDFLAGS $LDFLAGS"
     AC_LINK_IFELSE(
       [AC_LANG_PROGRAM(
	 [#include <libpq-fe.h>
	 ], [PQfinish (0);])],
       [pspp_cv_have_libpq=yes],
       [pspp_cv_have_libpq=no])
     CPPFLAGS=$save_CPPFLAGS
     LIBS=$save_LIBS
     LDFLAGS=$save_LDFLAGS])

  if test $pspp_cv_have_libpq = yes; then
    PSQL_SUPPORT=yes
    AC_DEFINE([PSQL_SUPPORT], [1],
      [Define to 1 if building in support for reading from postgres
       databases.])
  else
    PSQL_SUPPORT=no
    PG_CONFIG=
    PG_CFLAGS=
    PG_LDFLAGS=
    PG_LIBS=
  fi
else
  PSQL_SUPPORT=no
  pspp_cv_have_libpq=no
fi
AC_SUBST([PSQL_SUPPORT])

dnl Check for libxml2
PKG_CHECK_MODULES(
  [LIBXML2], [libxml-2.0], [], [PSPP_REQUIRED_PREREQ([libxml2])])

dnl Check for zlib.
AC_SEARCH_LIBS([gzopen], [z], [], [PSPP_REQUIRED_PREREQ([zlib])])
AC_CHECK_HEADERS([zlib.h], [], [PSPP_REQUIRED_PREREQ([zlib])])

dnl Avoid interference between zlib's crc32() function and gnulib's
dnl crc32() function, which have different signatures.  PSPP calls
dnl only gnulib's crc32() directly, but it calls zlib's crc32()
dnl indirectly through other zlib calls.  If we don't rename gnulib's
dnl crc32(), then zlib will call gnulib's version instead of its own,
dnl causing failures.  (This can happen even if zlib isn't detected
dnl directly, because other libraries that PSPP uses, e.g. libpng,
dnl sometimes use zlib themselves.)
AC_DEFINE(
  [crc32], [gl_crc32],
  [Avoid making zlib call gnulib's crc32() instead of its own.])

PSPP_PERL_MODULE

AC_SEARCH_LIBS([cblas_dsdot], [gslcblas],,[PSPP_REQUIRED_PREREQ([libgslcblas])])
PKG_CHECK_MODULES([GSL], [gsl >= 1.13], [],
			 AC_SEARCH_LIBS([gsl_poly_eval_derivs], [gsl],,[PSPP_REQUIRED_PREREQ([gsl version 1.13 or later])]))


PSPP_GSL_NEEDS_FGNU89_INLINE

PSPP_READLINE

dnl Checks for header files.
AC_CHECK_HEADERS([sys/wait.h fpu_control.h ieeefp.h fenv.h pwd.h])

dnl Some systems don't have SIGWINCH
AC_CHECK_DECLS([SIGWINCH], [], [],
          [#include <signal.h>
          /* NetBSD declares sys_siglist in unistd.h.  */
          #ifdef HAVE_UNISTD_H
          # include <unistd.h>
          #endif
          ])


# For gnulib.
gl_INIT

AC_C_INLINE

AC_C_BIGENDIAN

AC_CHECK_FUNCS([__setfpucw fork execl isinf isnan finite getpid feholdexcept fpsetmask popen round])

AC_PROG_LN_S

# iconv is required
AM_ICONV
if test "$am_cv_func_iconv" != "yes"; then
   PSPP_REQUIRED_PREREQ([iconv (see http://www.gnu.org/software/libiconv/)])
fi
LIBS="$LIBICONV $LIBS"

dnl Required by the gnulib 'relocatable-prog' module.
dnl See doc/relocatable-maint.texi in the gnulib tree for details.
RELOCATABLE_LIBRARY_PATH='$(pkglibdir):$(SPREAD_SHEET_WIDGET_LIBDIR)'
RELOCATABLE_STRIP=:

PSPP_CHECK_PREREQS

dnl The makefile needs to know if we're cross compiling, so that
dnl it can make arrangements to build the documentation examples.
AM_CONDITIONAL(cross_compiling, test x"$cross_compiling" = x"yes" )

AC_CONFIG_FILES(
  [Makefile po/Makefile tests/atlocal perl-module/lib/PSPP.pm])

AC_CONFIG_COMMANDS([doc/pspp-figures/dummy], [:])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AC_OUTPUT
echo "PSPP configured successfully."

dnl configure.ac ends here
