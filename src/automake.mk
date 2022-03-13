# PSPP - a program for statistical analysis.
# Copyright (C) 2017 Free Software Foundation, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

include $(top_srcdir)/src/libpspp/automake.mk
include $(top_srcdir)/src/data/automake.mk



AM_CPPFLAGS += -I"$(top_srcdir)/src" -I"$(top_srcdir)/lib"


pkglib_LTLIBRARIES = src/libpspp-core.la src/libpspp.la
src_libpspp_core_la_SOURCES =


src_libpspp_core_la_LDFLAGS = -release $(VERSION)

src_libpspp_core_la_LIBADD = \
	src/data/libdata.la \
	src/libpspp/liblibpspp.la \
	$(LIBXML2_LIBS) $(PG_LIBS) $(LIB_GETRANDOM) \
	gl/libgl.la

src_libpspp_la_SOURCES =

src_libpspp_la_CFLAGS = $(GSL_CFLAGS)
src_libpspp_la_LDFLAGS = -release $(VERSION)

src_libpspp_la_LIBADD = \
	src/language/liblanguage.la \
	src/math/libpspp-math.la \
	src/output/liboutput.la \
        $(GSL_LIBS)

include $(top_srcdir)/src/math/automake.mk
include $(top_srcdir)/src/output/automake.mk
include $(top_srcdir)/src/language/automake.mk
include $(top_srcdir)/src/ui/automake.mk
