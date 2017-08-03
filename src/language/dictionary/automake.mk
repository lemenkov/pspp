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

language_dictionary_sources = \
 src/language/dictionary/attributes.c \
 src/language/dictionary/apply-dictionary.c \
 src/language/dictionary/delete-variables.c \
 src/language/dictionary/formats.c \
 src/language/dictionary/missing-values.c \
 src/language/dictionary/modify-variables.c \
 src/language/dictionary/mrsets.c \
 src/language/dictionary/numeric.c \
 src/language/dictionary/rename-variables.c \
 src/language/dictionary/sort-variables.c \
 src/language/dictionary/split-file.c \
 src/language/dictionary/split-file.h \
 src/language/dictionary/sys-file-info.c \
 src/language/dictionary/value-labels.c \
 src/language/dictionary/variable-label.c \
 src/language/dictionary/vector.c \
 src/language/dictionary/variable-display.c \
 src/language/dictionary/weight.c
