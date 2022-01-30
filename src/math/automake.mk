# PSPP - a program for statistical analysis.
# Copyright (C) 2017, 2019 Free Software Foundation, Inc.
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


noinst_LTLIBRARIES += src/math/libpspp-math.la

src_math_libpspp_math_la_LIBADD = \
	lib/linreg/liblinreg.la \
	lib/tukey/libtukey.la

src_math_libpspp_math_la_SOURCES = \
	src/math/chart-geometry.c \
	src/math/chart-geometry.h \
	src/math/box-whisker.c src/math/box-whisker.h \
	src/math/categoricals.h \
	src/math/categoricals.c \
	src/math/covariance.c \
	src/math/covariance.h \
	src/math/correlation.c \
	src/math/correlation.h \
	src/math/distributions.c src/math/distributions.h \
	src/math/histogram.c src/math/histogram.h \
	src/math/interaction.c src/math/interaction.h \
	src/math/levene.c src/math/levene.h \
	src/math/linreg.c src/math/linreg.h \
	src/math/merge.c  src/math/merge.h \
	src/math/mode.c src/math/mode.h \
	src/math/moments.c  src/math/moments.h \
	src/math/np.c src/math/np.h \
	src/math/order-stats.c src/math/order-stats.h \
	src/math/percentiles.c src/math/percentiles.h \
	src/math/random.c src/math/random.h \
        src/math/statistic.h \
	src/math/sort.c src/math/sort.h \
	src/math/shapiro-wilk.c	src/math/shapiro-wilk.h \
	src/math/trimmed-mean.c src/math/trimmed-mean.h \
	src/math/tukey-hinges.c src/math/tukey-hinges.h \
	src/math/wilcoxon-sig.c src/math/wilcoxon-sig.h
