# PSPP - a program for statistical analysis.
# Copyright (C) 2017, 2020 Free Software Foundation, Inc.
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

include $(top_srcdir)/src/ui/terminal/automake.mk
if HAVE_GUI
include $(top_srcdir)/src/ui/gui/automake.mk
endif

noinst_LTLIBRARIES += src/ui/libuicommon.la

src_ui_libuicommon_la_SOURCES = \
	src/ui/source-init-opts.c src/ui/source-init-opts.h \
	src/ui/syntax-gen.c src/ui/syntax-gen.h
