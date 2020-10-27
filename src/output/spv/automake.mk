# PSPP - a program for statistical analysis.
# Copyright (C) 2019, 2020 Free Software Foundation, Inc.
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

src_output_liboutput_la_SOURCES += \
	src/output/spv/spv-css-parser.c \
	src/output/spv/spv-css-parser.h \
	src/output/spv/spv-dump.c \
	src/output/spv/spv-legacy-data.c \
	src/output/spv/spv-legacy-data.h \
	src/output/spv/spv-legacy-decoder.c \
	src/output/spv/spv-legacy-decoder.h \
	src/output/spv/spv-light-decoder.c \
	src/output/spv/spv-light-decoder.h \
	src/output/spv/spv-output.c \
	src/output/spv/spv-output.h \
	src/output/spv/spv-select.c \
	src/output/spv/spv-select.h \
	src/output/spv/spv-table-look.c \
	src/output/spv/spv-table-look.h \
	src/output/spv/spv-writer.c \
	src/output/spv/spv-writer.h \
	src/output/spv/spv.c \
	src/output/spv/spv.h \
	src/output/spv/spvbin-helpers.c \
	src/output/spv/spvbin-helpers.h \
	src/output/spv/spvxml-helpers.c \
	src/output/spv/spvxml-helpers.h

AM_CPPFLAGS += -Isrc

light_binary_in = \
	src/output/spv/binary-parser-generator \
	src/output/spv/light-binary.grammar
light_binary_out = \
	src/output/spv/light-binary-parser.c \
	src/output/spv/light-binary-parser.h
src/output/spv/light-binary-parser.c: $(light_binary_in)
	$(AM_V_GEN)$(PYTHON) $^ code spvlb '"output/spv/light-binary-parser.h"' > $@.tmp
	$(AM_V_at)mv $@.tmp $@
src/output/spv/light-binary-parser.h: $(light_binary_in)
	$(AM_V_GEN)$(PYTHON) $^ header spvlb > $@.tmp && mv $@.tmp $@
nodist_src_output_liboutput_la_SOURCES += $(light_binary_out)
BUILT_SOURCES += $(light_binary_out)
CLEANFILES += $(light_binary_out)
EXTRA_DIST += $(light_binary_in)

old_binary_in = \
	src/output/spv/binary-parser-generator \
	src/output/spv/old-binary.grammar
old_binary_out = \
	src/output/spv/old-binary-parser.c \
	src/output/spv/old-binary-parser.h
src/output/spv/old-binary-parser.c: $(old_binary_in)
	$(AM_V_GEN)$(PYTHON) $^ code spvob '"output/spv/old-binary-parser.h"' > $@.tmp
	$(AM_V_at)mv $@.tmp $@
src/output/spv/old-binary-parser.h: $(old_binary_in)
	$(AM_V_GEN)$(PYTHON) $^ header spvob > $@.tmp && mv $@.tmp $@
nodist_src_output_liboutput_la_SOURCES += $(old_binary_out)
BUILT_SOURCES += $(old_binary_out)
CLEANFILES += $(old_binary_out)
EXTRA_DIST += $(old_binary_in)

detail_xml_in = \
	src/output/spv/xml-parser-generator \
	src/output/spv/detail-xml.grammar
detail_xml_out = \
	src/output/spv/detail-xml-parser.c \
	src/output/spv/detail-xml-parser.h
src/output/spv/detail-xml-parser.c: $(detail_xml_in)
	$(AM_V_GEN)$(PYTHON) $^ code spvdx '"output/spv/detail-xml-parser.h"' > $@.tmp
	$(AM_V_at)mv $@.tmp $@
src/output/spv/detail-xml-parser.h: $(detail_xml_in)
	$(AM_V_GEN)$(PYTHON) $^ header spvdx > $@.tmp && mv $@.tmp $@
nodist_src_output_liboutput_la_SOURCES += $(detail_xml_out)
BUILT_SOURCES += $(detail_xml_out)
CLEANFILES += $(detail_xml_out)
EXTRA_DIST += $(detail_xml_in)

structure_xml_in = \
	src/output/spv/xml-parser-generator \
	src/output/spv/structure-xml.grammar
structure_xml_out = \
	src/output/spv/structure-xml-parser.c \
	src/output/spv/structure-xml-parser.h
src/output/spv/structure-xml-parser.c: $(structure_xml_in)
	$(AM_V_GEN)$(PYTHON) $^ code spvsx '"output/spv/structure-xml-parser.h"' > $@.tmp
	$(AM_V_at)mv $@.tmp $@
src/output/spv/structure-xml-parser.h: $(structure_xml_in)
	$(AM_V_GEN)$(PYTHON) $^ header spvsx > $@.tmp && mv $@.tmp $@
nodist_src_output_liboutput_la_SOURCES += $(structure_xml_out)
BUILT_SOURCES += $(structure_xml_out)
CLEANFILES += $(structure_xml_out)
EXTRA_DIST += $(structure_xml_in)
