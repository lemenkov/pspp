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

include $(top_srcdir)/src/language/commands/automake.mk
include $(top_srcdir)/src/language/expressions/automake.mk
include $(top_srcdir)/src/language/lexer/automake.mk
include $(top_srcdir)/src/language/tests/automake.mk

noinst_LTLIBRARIES +=  src/language/liblanguage.la


src_language_liblanguage_la_SOURCES = \
	src/language/command.c \
	src/language/command.h \
	src/language/command.def \
	$(language_lexer_sources) \
	$(language_commands_sources) \
	$(language_tests_sources) \
	$(language_expressions_sources)

nodist_src_language_liblanguage_la_SOURCES = \
	$(expressions_built_sources)
