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
bin_PROGRAMS += utilities/pspp-dump-sav
dist_man_MANS += utilities/pspp-dump-sav.1
utilities_pspp_dump_sav_SOURCES = \
	src/libpspp/integer-format.c \
	src/libpspp/float-format.c \
	utilities/pspp-dump-sav.c
utilities_pspp_dump_sav_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"

bin_PROGRAMS += utilities/pspp-convert
dist_man_MANS += utilities/pspp-convert.1
utilities_pspp_convert_SOURCES = utilities/pspp-convert.c
utilities_pspp_convert_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"
utilities_pspp_convert_LDADD = src/libpspp.la src/libpspp-core.la $(CAIRO_LIBS)

utilities_pspp_convert_LDFLAGS = $(PSPP_LDFLAGS) $(PG_LDFLAGS)
if RELOCATABLE_VIA_LD
utilities_pspp_convert_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif

bin_PROGRAMS += utilities/pspp-output
dist_man_MANS += utilities/pspp-output.1
utilities_pspp_output_SOURCES = utilities/pspp-output.c
utilities_pspp_output_CPPFLAGS = \
	$(LIBXML2_CFLAGS) $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"
utilities_pspp_output_LDADD = \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS)
utilities_pspp_output_LDFLAGS = $(PSPP_LDFLAGS) $(LIBXML2_LIBS)
if RELOCATABLE_VIA_LD
utilities_pspp_output_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif
