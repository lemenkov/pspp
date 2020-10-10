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


examplesdir = $(pkgdatadir)/examples

examples_DATA = \
        examples/t-test.sps \
	examples/descript.sps \
	examples/grid.sps \
	examples/hotel.sav \
	examples/horticulture.sav \
	examples/personnel.sav \
	examples/physiology.sav \
	examples/repairs.sav \
	examples/regress.sps \
	examples/regress_categorical.sps

EXTRA_DIST += $(examples_DATA)
