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

language_expressions_sources = \
	src/language/expressions/evaluate.c \
	src/language/expressions/helpers.c \
	src/language/expressions/helpers.h \
	src/language/expressions/optimize.c \
	src/language/expressions/parse.c \
	src/language/expressions/private.h \
	src/language/expressions/public.h

expressions_built_sources = \
	src/language/expressions/evaluate.h \
	src/language/expressions/evaluate.inc \
	src/language/expressions/operations.h \
	src/language/expressions/optimize.inc \
	src/language/expressions/parse.inc

BUILT_SOURCES += $(expressions_built_sources)
CLEANFILES += $(expressions_built_sources)

helpers = src/language/expressions/generate.pl \
	src/language/expressions/operations.def
EXTRA_DIST += $(helpers)

$(expressions_built_sources): $(helpers)
	$(AV_V_GEN)$(MKDIR_P) `dirname $@` && \
	$(PERL) $< -o $@ -i $(top_srcdir)/src/language/expressions/operations.def

AM_CPPFLAGS += -I$(abs_top_builddir)/src/language/expressions \
	-I$(top_srcdir)/src/language/expressions

EXTRA_DIST += src/language/expressions/TODO

# This seems to be necessary in order to prevent issues building the native
# build when cross compiling
src/language/expressions/evaluate.lo: $(expressions_built_sources)
