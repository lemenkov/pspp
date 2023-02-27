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
noinst_LTLIBRARIES += src/data/libdata.la

src_data_libdata_la_CPPFLAGS = $(LIBXML2_CFLAGS) $(PG_CFLAGS) $(AM_CPPFLAGS)

src_data_libdata_la_LIBADD =   $(LIBXML2_LIBS) $(PG_LIBS)

src_data_libdata_la_SOURCES = \
	src/data/any-reader.c \
	src/data/any-reader.h \
	src/data/any-writer.c \
	src/data/any-writer.h \
	src/data/attributes.c \
	src/data/attributes.h \
	src/data/calendar.c \
	src/data/calendar.h \
	src/data/case-map.c \
	src/data/case-map.h \
	src/data/case-matcher.c \
	src/data/case-matcher.h \
	src/data/caseproto.c \
	src/data/caseproto.h \
	src/data/case.c \
	src/data/casegrouper.c \
	src/data/casegrouper.h \
	src/data/caseinit.c \
	src/data/caseinit.h \
	src/data/casereader-filter.c \
	src/data/casereader-project.c \
	src/data/casereader-provider.h \
	src/data/casereader-select.c \
	src/data/casereader-shim.c \
	src/data/casereader-shim.h \
	src/data/casereader-translator.c \
	src/data/casereader.c \
	src/data/casereader.h \
	src/data/casewindow.c \
	src/data/casewindow.h \
	src/data/casewriter-provider.h \
	src/data/casewriter-translator.c \
	src/data/casewriter.c \
	src/data/casewriter.h \
	src/data/case.h \
	src/data/case-tmpfile.c \
	src/data/case-tmpfile.h \
	src/data/csv-file-writer.c \
	src/data/csv-file-writer.h \
	src/data/data-in.c \
	src/data/data-in.h \
	src/data/data-out.c \
	src/data/data-out.h \
	src/data/dataset.c \
	src/data/dataset.h \
	src/data/dataset-writer.c \
	src/data/dataset-writer.h \
	src/data/datasheet.c \
	src/data/datasheet.h \
	src/data/dict-class.c \
	src/data/dict-class.h \
	src/data/dictionary.c \
	src/data/dictionary.h \
	src/data/encrypted-file.c \
	src/data/encrypted-file.h \
	src/data/file-handle-def.c \
	src/data/file-handle-def.h \
	src/data/file-name.c \
	src/data/file-name.h \
	src/data/format-guesser.c \
	src/data/format-guesser.h \
	src/data/format.c \
	src/data/format.h \
	src/data/format.def \
	src/data/gnumeric-reader.c \
	src/data/gnumeric-reader.h \
	src/data/identifier.c \
	src/data/identifier2.c \
	src/data/identifier.h \
	src/data/lazy-casereader.c \
	src/data/lazy-casereader.h \
	src/data/mdd-writer.c \
	src/data/mdd-writer.h \
	src/data/missing-values.c \
	src/data/missing-values.h \
	src/data/make-file.c \
	src/data/make-file.h \
	src/data/mrset.c \
	src/data/mrset.h \
	src/data/ods-reader.c \
	src/data/ods-reader.h \
	src/data/pc+-file-reader.c \
	src/data/por-file-reader.c \
	src/data/por-file-writer.c \
	src/data/por-file-writer.h \
	src/data/psql-reader.c \
	src/data/psql-reader.h \
	src/data/session.c \
	src/data/session.h \
	src/data/settings.c \
	src/data/settings.h \
	src/data/short-names.c \
	src/data/short-names.h \
	src/data/spreadsheet-reader.c \
	src/data/spreadsheet-reader.h \
	src/data/subcase.c \
	src/data/subcase.h \
	src/data/sys-file-private.c \
	src/data/sys-file-private.h \
	src/data/sys-file-reader.c \
	src/data/sys-file-writer.c \
	src/data/sys-file-writer.h \
	src/data/transformations.c \
	src/data/transformations.h \
	src/data/val-type.h \
	src/data/value.c \
	src/data/value.h \
	src/data/value-labels.c \
	src/data/value-labels.h \
	src/data/vardict.h \
	src/data/variable.h \
	src/data/variable.c \
	src/data/varset.c \
	src/data/varset.h \
	src/data/vector.c \
	src/data/vector.h

nodist_src_data_libdata_la_SOURCES = src/data/sys-file-encoding.c
src/data/sys-file-encoding.c: \
	src/data/sys-file-encoding.py \
	src/data/convrtrs.txt
	$(AM_V_GEN)$(PYTHON3) $^ > $@.tmp && mv $@.tmp $@
EXTRA_DIST += src/data/sys-file-encoding.py src/data/convrtrs.txt
DISTCLEANFILES += src/data/sys-file-encoding.c
