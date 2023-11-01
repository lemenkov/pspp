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

check-programs: $(check_PROGRAMS)

check_PROGRAMS += tests/data/datasheet-test
tests_data_datasheet_test_SOURCES = \
	tests/data/datasheet-test.c
tests_data_datasheet_test_LDADD = src/libpspp-core.la
tests_data_datasheet_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/data/sack
tests_data_sack_SOURCES = \
	tests/data/sack.c
tests_data_sack_LDADD = src/libpspp-core.la
tests_data_sack_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/data/spreadsheet-test
tests_data_spreadsheet_test_SOURCES = \
	tests/data/spreadsheet-test.c
tests_data_spreadsheet_test_LDADD = src/libpspp-core.la
tests_data_spreadsheet_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/libpspp/line-reader-test
tests_libpspp_line_reader_test_SOURCES = tests/libpspp/line-reader-test.c
tests_libpspp_line_reader_test_LDADD = src/libpspp-core.la

check_PROGRAMS += tests/libpspp/ll-test
tests_libpspp_ll_test_SOURCES = \
	src/libpspp/ll.c \
	tests/libpspp/ll-test.c
tests_libpspp_ll_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/libpspp/llx-test
tests_libpspp_llx_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/llx.c \
	tests/libpspp/llx-test.c
tests_libpspp_llx_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/libpspp/encoding-guesser-test
tests_libpspp_encoding_guesser_test_SOURCES = \
	tests/libpspp/encoding-guesser-test.c
tests_libpspp_encoding_guesser_test_LDADD = src/libpspp-core.la

check_PROGRAMS += tests/libpspp/heap-test
tests_libpspp_heap_test_SOURCES = \
	tests/libpspp/heap-test.c
tests_libpspp_heap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_heap_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/hmap-test
tests_libpspp_hmap_test_SOURCES = \
	src/libpspp/hmap.c \
	tests/libpspp/hmap-test.c
tests_libpspp_hmap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/hmapx-test
tests_libpspp_hmapx_test_SOURCES = \
	src/libpspp/hmap.c \
	src/libpspp/hmapx.c \
	tests/libpspp/hmapx-test.c
tests_libpspp_hmapx_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/i18n-test
tests_libpspp_i18n_test_SOURCES = tests/libpspp/i18n-test.c
tests_libpspp_i18n_test_LDADD = src/libpspp-core.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/abt-test
tests_libpspp_abt_test_SOURCES = \
	src/libpspp/abt.c \
	tests/libpspp/abt-test.c
tests_libpspp_abt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/bt-test
tests_libpspp_bt_test_SOURCES = \
	src/libpspp/bt.c \
	tests/libpspp/bt-test.c
tests_libpspp_bt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/cmac-aes256-test
tests_libpspp_cmac_aes256_test_SOURCES = \
	src/libpspp/cmac-aes256.c \
	tests/libpspp/cmac-aes256-test.c
tests_libpspp_cmac_aes256_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/range-map-test
tests_libpspp_range_map_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/range-map.c \
	tests/libpspp/range-map-test.c
tests_libpspp_range_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/range-set-test
tests_libpspp_range_set_test_SOURCES = \
	tests/libpspp/range-set-test.c
tests_libpspp_range_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_range_set_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/range-tower-test
tests_libpspp_range_tower_test_SOURCES = \
	tests/libpspp/range-tower-test.c
tests_libpspp_range_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_range_tower_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/str-test
tests_libpspp_str_test_SOURCES = \
	tests/libpspp/str-test.c
tests_libpspp_str_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/string-map-test
tests_libpspp_string_map_test_SOURCES = \
	tests/libpspp/string-map-test.c
tests_libpspp_string_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_string_map_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/stringi-map-test
tests_libpspp_stringi_map_test_SOURCES = \
	tests/libpspp/stringi-map-test.c
tests_libpspp_stringi_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_stringi_map_test_LDADD = src/libpspp-core.la

check_PROGRAMS += tests/libpspp/string-set-test
tests_libpspp_string_set_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/string-set.c \
	tests/libpspp/string-set-test.c
tests_libpspp_string_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

check_PROGRAMS += tests/libpspp/stringi-set-test
tests_libpspp_stringi_set_test_SOURCES = \
	tests/libpspp/stringi-set-test.c
tests_libpspp_stringi_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_stringi_set_test_LDADD = src/libpspp-core.la

check_PROGRAMS += tests/libpspp/tower-test
tests_libpspp_tower_test_SOURCES = \
	tests/libpspp/tower-test.c
tests_libpspp_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_tower_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/u8-istream-test
tests_libpspp_u8_istream_test_SOURCES = tests/libpspp/u8-istream-test.c
tests_libpspp_u8_istream_test_LDADD = src/libpspp-core.la

check_PROGRAMS += tests/libpspp/sparse-array-test
tests_libpspp_sparse_array_test_SOURCES = \
	tests/libpspp/sparse-array-test.c
tests_libpspp_sparse_array_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_sparse_array_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la $(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/sparse-xarray-test
tests_libpspp_sparse_xarray_test_SOURCES = \
	tests/libpspp/sparse-xarray-test.c
tests_libpspp_sparse_xarray_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_sparse_xarray_test_LDADD = \
	src/libpspp-core.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/data/inexactify
tests_data_inexactify_SOURCES = tests/data/inexactify.c

check_PROGRAMS += tests/language/lexer/command-name-test
tests_language_lexer_command_name_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	tests/language/lexer/command-name-test.c
tests_language_lexer_command_name_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)
tests_language_lexer_command_name_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/language/lexer/scan-test
tests_language_lexer_scan_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	src/language/lexer/scan.c \
	src/language/lexer/segment.c \
	src/language/lexer/token.c \
	tests/language/lexer/scan-test.c
tests_language_lexer_scan_test_CFLAGS = $(AM_CFLAGS)
tests_language_lexer_scan_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/language/lexer/segment-test
tests_language_lexer_segment_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	src/language/lexer/command-segmenter.c \
	src/language/lexer/segment.c \
	tests/language/lexer/segment-test.c
tests_language_lexer_segment_test_CFLAGS = $(AM_CFLAGS)
tests_language_lexer_segment_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/libpspp/zip-test
tests_libpspp_zip_test_SOURCES = \
	tests/libpspp/zip-test.c
tests_libpspp_zip_test_CFLAGS = $(AM_CFLAGS)
tests_libpspp_zip_test_LDADD = \
	src/libpspp-core.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/math/chart-get-scale-test
tests_math_chart_get_scale_test_SOURCES = tests/math/chart-get-scale-test.c
tests_math_chart_get_scale_test_LDADD = \
	src/math/libpspp-math.la \
	src/libpspp/liblibpspp.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/math/chart-get-ticks-format-test
tests_math_chart_get_ticks_format_test_SOURCES = tests/math/chart-get-ticks-format-test.c
tests_math_chart_get_ticks_format_test_LDADD = \
	src/math/libpspp-math.la \
	src/libpspp/liblibpspp.la \
	gl/libgl.la \
	$(LIB_GETRANDOM)

check_PROGRAMS += tests/output/pivot-table-test
tests_output_pivot_table_test_SOURCES = tests/output/pivot-table-test.c
tests_output_pivot_table_test_LDADD = \
	src/libpspp.la \
	src/libpspp-core.la \
	gl/libgl.la \
	$(LIB_GETRANDOM) \
	$(CAIRO_LIBS)
EXTRA_DIST += tests/output/look.stt

check_PROGRAMS += tests/output/ascii-test
tests_output_ascii_test_SOURCES = tests/output/ascii-test.c
tests_output_ascii_test_LDADD = \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS)

check_PROGRAMS += tests/ui/syntax-gen-test
tests_ui_syntax_gen_test_SOURCES = tests/ui/syntax-gen-test.c
tests_ui_syntax_gen_test_LDADD = \
	src/ui/libuicommon.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS)

check_PROGRAMS += tests/output/tex-glyphs
tests_output_tex_glyphs_SOURCES = \
	tests/output/tex-glyphs.c
tests_output_tex_glyphs_LDADD = src/output/liboutput.la src/libpspp-core.la
tests_output_tex_glyphs_CFLAGS = $(AM_CFLAGS) -I $(top_srcdir)/src/output

check_PROGRAMS += tests/output/tex-strings
tests_output_tex_strings_SOURCES = \
	tests/output/tex-strings.c
tests_output_tex_strings_LDADD = src/output/liboutput.la src/libpspp-core.la
tests_output_tex_strings_CFLAGS = $(AM_CFLAGS) -I $(top_srcdir)/src/output


EXTRA_DIST += \
	tests/coverage.sh \
	tests/data/repeating-2.ods \
	tests/data/simple.ods \
	tests/data/simple.gnumeric \
	tests/data/sparse.ods \
	tests/data/sparse.gnumeric \
	tests/data/holey.ods \
	tests/data/holey.gnumeric \
	tests/data/multisheet.ods \
	tests/data/multisheet.gnumeric \
	tests/data/repeating.ods \
	tests/data/repeating.gnumeric \
	tests/data/one-thousand-by-fifty-three.ods \
	tests/data/one-thousand-by-fifty-three.gnumeric \
	tests/data/CVE-2017-10791.sav \
	tests/data/CVE-2017-10792.sav \
	tests/data/bcd-in.expected.cmp.gz \
	tests/data/binhex-in.expected.cmp.gz \
	tests/data/binhex-out.expected.gz \
	tests/data/hotel-encrypted.sav \
	tests/data/legacy-in.expected.cmp.gz \
	tests/data/num-in.expected.gz \
	tests/data/num-out.expected.cmp.gz \
	tests/data/test-date-input.py \
	tests/data/test-time-input.py \
	tests/data/v13.sav \
	tests/data/v14.sav \
	tests/data/test-encrypted.sps \
	tests/data/test-decrypted.spv \
	tests/data/test-encrypted.spv \
	tests/language/mann-whitney.txt \
	tests/language/commands/Book1.gnm.unzipped \
	tests/language/commands/test.ods \
	tests/language/commands/newone.ods \
	tests/language/commands/readnames.ods \
	tests/language/commands/nhtsa.sav \
	tests/language/commands/llz.zsav \
	tests/utilities/regress.spv

CLEANFILES += *.save pspp.* foo*

# Autotest testsuite

EXTRA_DIST += \
	tests/testsuite.in \
	$(TESTSUITE_AT) \
	$(TESTSUITE) \
	tests/atlocal.in \
	$(srcdir)/package.m4 \
	$(TESTSUITE)

TESTSUITE_AT = \
	tests/data/calendar.at \
	tests/data/data-in.at \
	tests/data/data-out.at \
	tests/data/datasheet-test.at \
	tests/data/spreadsheet-test.at \
	tests/data/dictionary.at \
	tests/data/file.at \
	tests/data/format-guesser.at \
	tests/data/mdd-file.at \
	tests/data/pc+-file-reader.at \
	tests/data/por-file.at \
	tests/data/sys-file-reader.at \
	tests/data/sys-file.at \
	tests/data/encrypted-file.at \
	tests/language/command.at \
	tests/language/lexer/command-name.at \
	tests/language/lexer/lexer.at \
	tests/language/lexer/scan.at \
	tests/language/lexer/segment.at \
	tests/language/lexer/variable-parser.at \
	tests/language/expressions/evaluate.at \
	tests/language/expressions/parse.at \
	tests/language/commands/add-files.at \
	tests/language/commands/aggregate.at \
	tests/language/commands/apply.at \
	tests/language/commands/attributes.at \
	tests/language/commands/autorecode.at \
	tests/language/commands/cache.at \
	tests/language/commands/cd.at \
	tests/language/commands/compute.at \
	tests/language/commands/correlations.at \
	tests/language/commands/count.at \
	tests/language/commands/crosstabs.at \
	tests/language/commands/ctables.at \
	tests/language/commands/data-list.at \
	tests/language/commands/data-reader.at \
	tests/language/commands/dataset.at \
	tests/language/commands/date.at \
	tests/language/commands/define.at \
	tests/language/commands/delete-variables.at \
	tests/language/commands/descriptives.at \
	tests/language/commands/do-if.at \
	tests/language/commands/do-repeat.at \
	tests/language/commands/examine.at \
	tests/language/commands/factor.at \
	tests/language/commands/file-handle.at \
	tests/language/commands/flip.at \
	tests/language/commands/formats.at \
	tests/language/commands/frequencies.at \
	tests/language/commands/get-data-psql.at \
	tests/language/commands/get-data-spreadsheet.at \
	tests/language/commands/get-data-txt.at \
	tests/language/commands/get-data.at \
	tests/language/commands/get.at \
	tests/language/commands/glm.at \
	tests/language/commands/graph.at \
	tests/language/commands/host.at \
	tests/language/commands/inpt-pgm.at \
	tests/language/commands/insert.at \
	tests/language/commands/leave.at \
	tests/language/commands/list.at \
	tests/language/commands/logistic.at \
	tests/language/commands/loop.at \
	tests/language/commands/match-files.at \
	tests/language/commands/matrix-data.at \
	tests/language/commands/matrix-reader.at \
	tests/language/commands/matrix.at \
	tests/language/commands/mconvert.at \
	tests/language/commands/means.at \
	tests/language/commands/missing-values.at \
	tests/language/commands/mrsets.at \
	tests/language/commands/npar.at \
	tests/language/commands/numeric.at \
	tests/language/commands/oneway.at \
	tests/language/commands/output.at \
	tests/language/commands/permissions.at \
	tests/language/commands/print-space.at \
	tests/language/commands/print.at \
	tests/language/commands/quick-cluster.at \
	tests/language/commands/rank.at \
	tests/language/commands/recode.at \
	tests/language/commands/regression.at \
	tests/language/commands/reliability.at \
	tests/language/commands/rename-variables.at \
	tests/language/commands/roc.at \
	tests/language/commands/sample.at \
	tests/language/commands/save-translate.at \
	tests/language/commands/save.at \
	tests/language/commands/select-if.at \
	tests/language/commands/set.at \
	tests/language/commands/show.at \
	tests/language/commands/sort-cases.at \
	tests/language/commands/sort-variables.at \
	tests/language/commands/split-file.at \
	tests/language/commands/string.at \
	tests/language/commands/sys-file-info.at \
	tests/language/commands/t-test.at \
	tests/language/commands/temporary.at \
	tests/language/commands/title.at \
	tests/language/commands/update.at \
	tests/language/commands/value-labels.at \
	tests/language/commands/variable-labels.at \
	tests/language/commands/variable-display.at \
	tests/language/commands/vector.at \
	tests/language/commands/weight.at \
	tests/libpspp/abt.at \
	tests/libpspp/bt.at \
	tests/libpspp/encoding-guesser.at \
	tests/libpspp/float-format.at \
	tests/libpspp/heap.at \
	tests/libpspp/hmap.at \
	tests/libpspp/hmapx.at \
	tests/libpspp/i18n.at \
	tests/libpspp/line-reader.at \
	tests/libpspp/ll.at \
	tests/libpspp/llx.at \
	tests/libpspp/range-map.at \
	tests/libpspp/range-set.at \
	tests/libpspp/range-tower.at \
	tests/libpspp/sparse-array.at \
	tests/libpspp/sparse-xarray-test.at \
	tests/libpspp/str.at \
	tests/libpspp/string-map.at \
	tests/libpspp/stringi-map.at \
	tests/libpspp/string-set.at \
	tests/libpspp/stringi-set.at \
	tests/libpspp/tower.at \
	tests/libpspp/u8-istream.at \
	tests/libpspp/zip.at \
	tests/math/chart-geometry.at \
	tests/math/moments.at \
	tests/math/randist.at \
	tests/output/ascii.at \
	tests/output/charts.at \
	tests/output/html.at \
	tests/output/journal.at \
	tests/output/output.at \
	tests/output/paper-size.at \
	tests/output/pivot-table.at \
	tests/output/render.at \
	tests/output/tables.at \
	tests/output/tex.at \
	tests/ui/terminal/main.at \
	tests/ui/syntax-gen.at \
	tests/utilities/pspp-convert.at \
	tests/utilities/pspp-output.at \
	tests/perl-module.at

TESTSUITE = $(srcdir)/tests/testsuite
DISTCLEANFILES += tests/atconfig tests/atlocal
AUTOTEST_PATH = tests/data:tests/language/lexer:tests/libpspp:tests/output:tests/math:src/ui/terminal:utilities

$(srcdir)/tests/testsuite.at: tests/testsuite.in tests/automake.mk
	$(AM_V_GEN)printf '\043 Generated automatically -- do not modify!    -*- buffer-read-only: t -*-\n' > $@,tmp
	$(AM_V_at)cat $< >> $@,tmp
	$(AM_V_at)for t in $(TESTSUITE_AT); do \
	  echo "m4_include([$$t])" >> $@,tmp ;\
	done
	mv $@,tmp $@

EXTRA_DIST += tests/testsuite.at

# Generate a TableLook that prints all layers of pivot tables.
check_DATA = tests/all-layers.stt
tests/all-layers.stt: utilities/pspp-output
	$(AM_V_GEN)$< get-table-look - $@.tmp
	$(AM_V_at)if grep 'printAllLayers="false"' $@.tmp >/dev/null; then :; else \
		echo >&2 "$<: expected printAllLayers=\"false\""; exit 1; fi
	$(AM_v_at)sed 's/printAllLayers="false"/printAllLayers="true"/' < $@.tmp > $@
DISTCLEANFILES += tests/all-layers.stt tests/all-layers.stt.tmp

CHECK_LOCAL += tests_check
tests_check: tests/atconfig tests/atlocal $(TESTSUITE) $(check_PROGRAMS) $(check_DATA)
	XTERM_LOCALE='' $(SHELL) '$(TESTSUITE)' -C tests AUTOTEST_PATH=$(AUTOTEST_PATH) RUNNER='$(RUNNER)' $(TESTSUITEFLAGS)

CLEAN_LOCAL += tests_clean
tests_clean:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' -C tests --clean

AUTOM4TE = $(SHELL) $(srcdir)/build-aux/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): package.m4 $(srcdir)/tests/testsuite.at $(TESTSUITE_AT)
	$(AM_V_GEN)$(AUTOTEST) -I '$(srcdir)' $@.at | $(SED) 's/@<00A0>@/ /g' > $@.tmp
	test -s $@.tmp
	$(AM_V_at)mv $@.tmp $@

# The `:;' works around a Bash 3.2 bug when the output is not writeable.
$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	$(AM_V_GEN):;{ \
	  echo '# Signature of the current package.' && \
	  echo 'm4_define([AT_PACKAGE_NAME],      [$(PACKAGE_NAME)])' && \
	  echo 'm4_define([AT_PACKAGE_TARNAME],   [$(PACKAGE_TARNAME)])' && \
	  echo 'm4_define([AT_PACKAGE_VERSION],   [$(PACKAGE_VERSION)])' && \
	  echo 'm4_define([AT_PACKAGE_STRING],    [$(PACKAGE_STRING)])' && \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [$(PACKAGE_BUGREPORT)])' && \
	  echo 'm4_define([AT_PACKAGE_URL],       [$(PACKAGE_URL)])'; \
	} >'$(srcdir)/package.m4'

check-valgrind:
	$(MAKE) check RUNNER='$(SHELL) $(abs_top_builddir)/libtool --mode=execute valgrind --log-file=valgrind.%p --leak-check=full --num-callers=20 --suppressions=$(abs_top_srcdir)/tests/valgrind.supp --read-inline-info=yes --read-var-info=yes' TESTSUITEFLAGS='$(TESTSUITEFLAGS) -d'
	@echo
	@echo '--------------------------------'
	@echo 'Valgrind output is in:'
	@echo 'tests/testsuite.dir/*/valgrind.*'
	@echo '--------------------------------'
EXTRA_DIST += tests/valgrind.supp tests/lsan.supp
