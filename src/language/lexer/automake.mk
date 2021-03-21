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


language_lexer_sources = \
	src/language/lexer/command-name.c \
	src/language/lexer/command-name.h \
	src/language/lexer/include-path.c \
	src/language/lexer/include-path.h \
	src/language/lexer/lexer.c \
	src/language/lexer/lexer.h \
	src/language/lexer/subcommand-list.c  \
	src/language/lexer/subcommand-list.h \
	src/language/lexer/format-parser.c \
	src/language/lexer/format-parser.h \
	src/language/lexer/scan.c \
	src/language/lexer/scan.h \
	src/language/lexer/segment.c \
	src/language/lexer/segment.h \
	src/language/lexer/token.c \
	src/language/lexer/token.h \
	src/language/lexer/value-parser.c \
	src/language/lexer/value-parser.h \
	src/language/lexer/variable-parser.c \
	src/language/lexer/variable-parser.h
