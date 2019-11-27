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

language_data_io_sources = \
	src/language/data-io/combine-files.c \
	src/language/data-io/data-list.c \
	src/language/data-io/data-parser.c \
	src/language/data-io/data-parser.h \
	src/language/data-io/data-reader.c \
	src/language/data-io/data-reader.h \
	src/language/data-io/data-writer.c \
	src/language/data-io/data-writer.h \
	src/language/data-io/dataset.c \
	src/language/data-io/file-handle.c \
	src/language/data-io/file-handle.h \
	src/language/data-io/get-data.c \
	src/language/data-io/get.c \
	src/language/data-io/inpt-pgm.c \
	src/language/data-io/inpt-pgm.h \
	src/language/data-io/list.c \
	src/language/data-io/placement-parser.c \
	src/language/data-io/placement-parser.h \
	src/language/data-io/print-space.c \
	src/language/data-io/print.c \
	src/language/data-io/matrix-data.c \
	src/language/data-io/matrix-reader.c \
	src/language/data-io/matrix-reader.h \
	src/language/data-io/save-translate.c \
	src/language/data-io/save.c \
	src/language/data-io/trim.c \
	src/language/data-io/trim.h
