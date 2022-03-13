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

noinst_LTLIBRARIES += src/ui/terminal/libui.la

src_ui_terminal_libui_la_SOURCES = \
	src/ui/terminal/main.c \
	src/ui/terminal/terminal-opts.c \
	src/ui/terminal/terminal-opts.h	\
	src/ui/terminal/terminal-reader.c \
	src/ui/terminal/terminal-reader.h

src_ui_terminal_libui_la_CFLAGS = $(NCURSES_CFLAGS)

bin_PROGRAMS += src/ui/terminal/pspp

src_ui_terminal_pspp_SOURCES =

src_ui_terminal_pspp_LDADD = \
	src/ui/terminal/libui.la \
	src/ui/libuicommon.la \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS) \
	$(NCURSES_LIBS) \
	$(LTLIBREADLINE) \
	$(GSL_LIBS) \
	$(LIB_GETRANDOM)


src_ui_terminal_pspp_LDFLAGS = $(PSPP_LDFLAGS) $(PG_LDFLAGS)

if RELOCATABLE_VIA_LD
src_ui_terminal_pspp_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif
