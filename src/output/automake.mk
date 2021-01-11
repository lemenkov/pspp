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

noinst_LTLIBRARIES += src/output/liboutput.la

src_output_liboutput_la_CPPFLAGS = $(LIBXML2_CFLAGS) $(AM_CPPFLAGS)

src_output_liboutput_la_SOURCES = \
	src/output/ascii.c \
	src/output/ascii.h \
	src/output/cairo-chart.c \
	src/output/cairo-chart.h \
	src/output/cairo-fsm.c \
	src/output/cairo-fsm.h \
	src/output/cairo-pager.c \
	src/output/cairo-pager.h \
	src/output/cairo.c \
	src/output/chart-provider.h \
	src/output/chart.c \
	src/output/chart.h \
	src/output/charts/barchart-cairo.c \
	src/output/charts/barchart.c \
	src/output/charts/barchart.h \
	src/output/charts/boxplot-cairo.c \
	src/output/charts/boxplot.c \
	src/output/charts/boxplot.h \
	src/output/charts/np-plot-cairo.c \
	src/output/charts/np-plot.c \
	src/output/charts/np-plot.h \
	src/output/charts/piechart-cairo.c \
	src/output/charts/piechart.c \
	src/output/charts/piechart.h \
	src/output/charts/plot-hist-cairo.c \
	src/output/charts/plot-hist.c \
	src/output/charts/plot-hist.h \
	src/output/charts/roc-chart-cairo.c \
	src/output/charts/roc-chart.c \
	src/output/charts/roc-chart.h \
	src/output/charts/scatterplot-cairo.c \
	src/output/charts/scatterplot.c \
	src/output/charts/scatterplot.h \
	src/output/charts/scree-cairo.c \
	src/output/charts/scree.c \
	src/output/charts/scree.h \
	src/output/charts/spreadlevel-cairo.c \
	src/output/charts/spreadlevel-plot.c \
	src/output/charts/spreadlevel-plot.h \
	src/output/csv.c \
	src/output/driver-provider.h \
	src/output/driver.c \
	src/output/driver.h \
	src/output/html.c \
	src/output/journal.c \
	src/output/journal.h \
	src/output/measure.c \
	src/output/measure.h \
	src/output/msglog.c \
	src/output/msglog.h \
	src/output/odt.c \
	src/output/options.c \
	src/output/options.h \
	src/output/output-item.c \
	src/output/output-item.h \
	src/output/page-setup.c \
	src/output/page-setup.h \
	src/output/pivot-output.c \
	src/output/pivot-output.h \
	src/output/pivot-table.c \
	src/output/pivot-table.h \
	src/output/render.c \
	src/output/render.h \
	src/output/spv-driver.c \
	src/output/table-provider.h \
	src/output/table.c \
	src/output/table.h \
	src/output/tex-glyphs.c \
	src/output/tex-glyphs.h \
	src/output/tex-parsing.c \
	src/output/tex-parsing.h \
	src/output/tex-rendering.c \
	src/output/tex-rendering.h \
	src/output/tex.c
nodist_src_output_liboutput_la_SOURCES =

EXTRA_DIST += \
	src/output/README \
	src/output/mk-class-boilerplate

include src/output/spv/automake.mk
