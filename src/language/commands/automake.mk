## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP - a program for statistical analysis.
# Copyright (C) 2017-2022 Free Software Foundation, Inc.
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

AM_CPPFLAGS += -I"$(top_srcdir)/src/language/commands"

language_commands_sources = \
	src/language/commands/aggregate.c \
	src/language/commands/aggregate.h \
	src/language/commands/apply-dictionary.c \
	src/language/commands/attributes.c \
	src/language/commands/autorecode.c \
	src/language/commands/binomial.c \
	src/language/commands/binomial.h \
	src/language/commands/cache.c \
	src/language/commands/cd.c \
	src/language/commands/chart-category.h  \
	src/language/commands/chisquare.c  \
	src/language/commands/chisquare.h \
	src/language/commands/cochran.c \
	src/language/commands/cochran.h \
	src/language/commands/combine-files.c \
	src/language/commands/compute.c \
	src/language/commands/correlations.c \
	src/language/commands/count.c \
	src/language/commands/crosstabs.c \
	src/language/commands/ctables.c \
	src/language/commands/ctables.inc \
	src/language/commands/data-list.c \
	src/language/commands/data-parser.c \
	src/language/commands/data-parser.h \
	src/language/commands/data-reader.c \
	src/language/commands/data-reader.h \
	src/language/commands/data-writer.c \
	src/language/commands/data-writer.h \
	src/language/commands/dataset.c \
	src/language/commands/date.c \
	src/language/commands/define.c \
	src/language/commands/delete-variables.c \
	src/language/commands/descriptives.c \
	src/language/commands/do-if.c \
	src/language/commands/echo.c \
	src/language/commands/examine.c \
	src/language/commands/factor.c \
	src/language/commands/fail.c \
	src/language/commands/file-handle.c \
	src/language/commands/file-handle.h \
	src/language/commands/flip.c \
	src/language/commands/formats.c \
	src/language/commands/freq.c \
	src/language/commands/freq.h \
	src/language/commands/frequencies.c \
	src/language/commands/friedman.c \
	src/language/commands/friedman.h \
	src/language/commands/get-data.c \
	src/language/commands/get.c \
	src/language/commands/glm.c \
	src/language/commands/graph.c \
	src/language/commands/host.c \
	src/language/commands/include.c \
	src/language/commands/inpt-pgm.c \
	src/language/commands/inpt-pgm.h \
	src/language/commands/jonckheere-terpstra.c \
	src/language/commands/jonckheere-terpstra.h \
	src/language/commands/kruskal-wallis.c \
	src/language/commands/kruskal-wallis.h \
	src/language/commands/ks-one-sample.c \
	src/language/commands/ks-one-sample.h \
	src/language/commands/list.c \
	src/language/commands/logistic.c \
	src/language/commands/loop.c \
	src/language/commands/mann-whitney.c \
	src/language/commands/mann-whitney.h \
	src/language/commands/matrix-data.c \
	src/language/commands/matrix-reader.c \
	src/language/commands/matrix-reader.h \
	src/language/commands/matrix.c \
	src/language/commands/mcnemar.c \
	src/language/commands/mcnemar.h \
	src/language/commands/mconvert.c \
	src/language/commands/means-calc.c \
	src/language/commands/means-parser.c \
	src/language/commands/means.c \
	src/language/commands/means.h \
	src/language/commands/median.c \
	src/language/commands/median.h \
	src/language/commands/missing-values.c \
	src/language/commands/mrsets.c \
	src/language/commands/npar-summary.c \
	src/language/commands/npar-summary.h \
	src/language/commands/npar.c \
	src/language/commands/npar.h \
	src/language/commands/numeric.c \
	src/language/commands/oneway.c \
	src/language/commands/output.c \
	src/language/commands/permissions.c \
	src/language/commands/placement-parser.c \
	src/language/commands/placement-parser.h \
	src/language/commands/print-space.c \
	src/language/commands/print.c \
	src/language/commands/quick-cluster.c \
	src/language/commands/rank.c \
	src/language/commands/recode.c \
	src/language/commands/regression.c \
	src/language/commands/reliability.c \
	src/language/commands/rename-variables.c \
	src/language/commands/repeat.c \
	src/language/commands/roc.c \
	src/language/commands/roc.h \
	src/language/commands/runs.c \
	src/language/commands/runs.h \
	src/language/commands/sample.c \
	src/language/commands/save-translate.c \
	src/language/commands/save.c \
	src/language/commands/select-if.c \
	src/language/commands/set.c \
	src/language/commands/sign.c \
	src/language/commands/sign.h \
	src/language/commands/sort-cases.c \
	src/language/commands/sort-criteria.c \
	src/language/commands/sort-criteria.h \
	src/language/commands/sort-variables.c \
	src/language/commands/split-file.c \
	src/language/commands/split-file.h \
	src/language/commands/sys-file-info.c \
	src/language/commands/t-test-indep.c \
	src/language/commands/t-test-one-sample.c \
	src/language/commands/t-test-paired.c \
	src/language/commands/t-test-parser.c \
	src/language/commands/t-test.h \
	src/language/commands/temporary.c \
	src/language/commands/title.c \
	src/language/commands/trim.c \
	src/language/commands/trim.h \
	src/language/commands/value-labels.c \
	src/language/commands/variable-display.c \
	src/language/commands/variable-label.c \
	src/language/commands/vector.c \
	src/language/commands/weight.c \
	src/language/commands/wilcoxon.c \
	src/language/commands/wilcoxon.h
