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

UI_FILES = \
	src/ui/gui/aggregate.ui \
	src/ui/gui/autorecode.ui \
	src/ui/gui/binomial.ui \
	src/ui/gui/compute.ui \
	src/ui/gui/barchart.ui \
	src/ui/gui/correlation.ui \
	src/ui/gui/count.ui \
	src/ui/gui/comments.ui \
	src/ui/gui/crosstabs.ui \
	src/ui/gui/chi-square.ui \
	src/ui/gui/descriptives.ui \
	src/ui/gui/entry-dialog.ui \
	src/ui/gui/examine.ui \
	src/ui/gui/goto-case.ui \
	src/ui/gui/factor.ui \
	src/ui/gui/find.ui \
	src/ui/gui/frequencies.ui \
	src/ui/gui/histogram.ui \
	src/ui/gui/indep-samples.ui \
	src/ui/gui/k-independent.ui \
	src/ui/gui/k-means.ui \
	src/ui/gui/k-related.ui \
	src/ui/gui/ks-one-sample.ui \
	src/ui/gui/logistic.ui \
	src/ui/gui/means.ui \
	src/ui/gui/missing-val-dialog.ui \
	src/ui/gui/oneway.ui \
	src/ui/gui/options.ui \
	src/ui/gui/paired-samples.ui \
	src/ui/gui/rank.ui \
	src/ui/gui/runs.ui \
	src/ui/gui/sort.ui \
	src/ui/gui/split-file.ui \
	src/ui/gui/recode.ui \
	src/ui/gui/regression.ui \
	src/ui/gui/reliability.ui \
	src/ui/gui/roc.ui \
	src/ui/gui/scatterplot.ui \
	src/ui/gui/select-cases.ui \
	src/ui/gui/spreadsheet-import.ui \
	src/ui/gui/t-test.ui \
	src/ui/gui/text-data-import.ui \
	src/ui/gui/transpose.ui \
	src/ui/gui/univariate.ui \
	src/ui/gui/val-labs-dialog.ui \
	src/ui/gui/variable-info.ui \
	src/ui/gui/data-editor.ui \
	src/ui/gui/output-window.ui \
	src/ui/gui/syntax-editor.ui \
	src/ui/gui/var-type-dialog.ui \
	src/ui/gui/weight.ui

if building_gui

EXTRA_DIST += \
	src/ui/gui/marshaller-list \
	src/ui/gui/pspplogo.svg \
	src/ui/gui/pspp.rc.in

src_ui_gui_psppire_CPPFLAGS=

bin_PROGRAMS += src/ui/gui/psppire
noinst_PROGRAMS += src/ui/gui/spreadsheet-test

src_ui_gui_psppire_CFLAGS = $(GTK_CFLAGS) $(GTKSOURCEVIEW_CFLAGS) \
        $(SPREAD_SHEET_WIDGET_CFLAGS) \
        $(LIBRSVG_CFLAGS) \
        $(AM_CFLAGS) -DGDK_MULTIHEAD_SAFE=1
src_ui_gui_spreadsheet_test_CFLAGS = $(GTK_CFLAGS) $(AM_CFLAGS) -DGDK_MULTIHEAD_SAFE=1

if cc_is_gcc
src_ui_gui_psppire_CFLAGS+=-Wno-unused-parameter
src_ui_gui_spreadsheet_test_CFLAGS+=-Wno-unused-parameter
endif


src_ui_gui_psppire_LDFLAGS = \
	$(PSPPIRE_LDFLAGS) \
	$(PG_LDFLAGS)




if RELOCATABLE_VIA_LD
src_ui_gui_psppire_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
else
src_ui_gui_psppire_LDFLAGS += -rpath $(pkglibdir)
endif


src_ui_gui_psppire_LDADD = \
	src/ui/gui/libwidgets-essential.la \
	src/ui/libuicommon.la \
	src/libpspp.la \
	src/libpspp-core.la \
	$(GTK_LIBS) \
	$(GTHREAD_LIBS) \
	$(GTKSOURCEVIEW_LIBS) \
	$(SPREAD_SHEET_WIDGET_LIBS) \
	$(LIBRSVG_LIBS) \
	$(CAIRO_LIBS) \
	$(LIBINTL) \
	$(GSL_LIBS) \
	$(LIB_GETRANDOM)

if host_is_w32
src_ui_gui_psppire_LDADD += src/ui/gui/pspp.res
src_ui_gui_psppire_CFLAGS += -mwindows -mwin32
endif


src_ui_gui_spreadsheet_test_LDADD = \
	src/libpspp-core.la \
	$(GTK_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIB_GETRANDOM)


src_ui_gui_spreadsheet_test_SOURCES = src/ui/gui/spreadsheet-test.c src/ui/gui/psppire-spreadsheet-model.c

src_ui_gui_psppiredir = $(pkgdatadir)


install-lang:
	$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/pspp.lang $(DESTDIR)$(pkgdatadir)

INSTALL_DATA_HOOKS += install-lang

dist_src_ui_gui_psppire_DATA = \
	$(UI_FILES) \
	$(top_srcdir)/src/ui/gui/pspp.lang

src_ui_gui_psppire_SOURCES = \
	src/ui/gui/builder-wrapper.c \
	src/ui/gui/builder-wrapper.h \
	src/ui/gui/entry-dialog.c \
	src/ui/gui/entry-dialog.h \
	src/ui/gui/executor.c \
	src/ui/gui/executor.h \
	src/ui/gui/find-dialog.c \
	src/ui/gui/find-dialog.h \
	src/ui/gui/goto-case-dialog.c \
	src/ui/gui/goto-case-dialog.h \
	src/ui/gui/helper.c \
	src/ui/gui/helper.h \
	src/ui/gui/help-menu.c \
	src/ui/gui/help-menu.h \
	src/ui/gui/main.c \
	src/ui/gui/missing-val-dialog.c \
	src/ui/gui/missing-val-dialog.h \
	src/ui/gui/options-dialog.c \
	src/ui/gui/options-dialog.h \
	src/ui/gui/pre-initialisation.h \
	src/ui/gui/psppire.c \
	src/ui/gui/psppire-data-editor.c \
	src/ui/gui/psppire-data-editor.h \
	src/ui/gui/psppire-data-store.c \
	src/ui/gui/psppire-data-store.h \
	src/ui/gui/psppire-data-window.c \
	src/ui/gui/psppire-data-window.h \
	src/ui/gui/psppire-delimited-text.c \
	src/ui/gui/psppire-delimited-text.h \
	src/ui/gui/psppire-encoding-selector.c \
	src/ui/gui/psppire-encoding-selector.h \
	src/ui/gui/psppire.h \
	src/ui/gui/psppire-import-assistant.c \
	src/ui/gui/psppire-import-assistant.h \
	src/ui/gui/psppire-import-spreadsheet.c \
	src/ui/gui/psppire-import-spreadsheet.h \
	src/ui/gui/psppire-import-textfile.c \
	src/ui/gui/psppire-import-textfile.h \
	src/ui/gui/psppire-lex-reader.c \
	src/ui/gui/psppire-lex-reader.h \
	src/ui/gui/psppire-output-view.c \
	src/ui/gui/psppire-output-view.h \
	src/ui/gui/psppire-output-window.c \
	src/ui/gui/psppire-output-window.h \
	src/ui/gui/psppire-scanf.c \
	src/ui/gui/psppire-scanf.h \
	src/ui/gui/psppire-spreadsheet-data-model.c \
	src/ui/gui/psppire-spreadsheet-data-model.h \
	src/ui/gui/psppire-spreadsheet-model.c \
	src/ui/gui/psppire-spreadsheet-model.h \
	src/ui/gui/psppire-syntax-window.c \
	src/ui/gui/psppire-syntax-window.h \
	src/ui/gui/psppire-value-entry.c \
	src/ui/gui/psppire-value-entry.h \
	src/ui/gui/psppire-window.c \
	src/ui/gui/psppire-window.h \
	src/ui/gui/psppire-window-register.c \
	src/ui/gui/psppire-window-register.h \
	src/ui/gui/t-test-options.c \
	src/ui/gui/t-test-options.h \
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/value-variant.c \
	src/ui/gui/value-variant.h \
	src/ui/gui/var-display.c \
	src/ui/gui/var-display.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h \
	src/ui/gui/widget-io.c \
	src/ui/gui/widget-io.h \
	src/ui/gui/windows-menu.c \
	src/ui/gui/windows-menu.h

src/ui/gui/pspp.rc: src/ui/gui/pspp.rc.in
	@$(MKDIR_P) src/ui/gui
	sed -e 's/%version%/'$(PACKAGE_VERSION)'/' $< > $@

noinst_LTLIBRARIES += src/ui/gui/libwidgets-essential.la

src_ui_gui_libwidgets_essential_la_SOURCES = \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/psppire-acr.h \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-buttonbox.h \
	src/ui/gui/psppire-checkbox-treeview.c \
	src/ui/gui/psppire-checkbox-treeview.h \
	src/ui/gui/psppire-conf.c \
	src/ui/gui/psppire-conf.h \
	src/ui/gui/psppire-data-sheet.c \
	src/ui/gui/psppire-data-sheet.h \
	src/ui/gui/psppire-dialog-action-1sks.c \
	src/ui/gui/psppire-dialog-action-1sks.h \
	src/ui/gui/psppire-dialog-action-aggregate.c \
	src/ui/gui/psppire-dialog-action-aggregate.h \
	src/ui/gui/psppire-dialog-action-autorecode.c \
	src/ui/gui/psppire-dialog-action-autorecode.h \
	src/ui/gui/psppire-dialog-action-barchart.c \
	src/ui/gui/psppire-dialog-action-barchart.h \
	src/ui/gui/psppire-dialog-action-binomial.c \
	src/ui/gui/psppire-dialog-action-binomial.h \
	src/ui/gui/psppire-dialog-action.c \
	src/ui/gui/psppire-dialog-action-chisquare.c \
	src/ui/gui/psppire-dialog-action-chisquare.h \
	src/ui/gui/psppire-dialog-action-comments.c \
	src/ui/gui/psppire-dialog-action-comments.h \
	src/ui/gui/psppire-dialog-action-compute.c \
	src/ui/gui/psppire-dialog-action-compute.h \
	src/ui/gui/psppire-dialog-action-correlation.c \
	src/ui/gui/psppire-dialog-action-correlation.h \
	src/ui/gui/psppire-dialog-action-count.c \
	src/ui/gui/psppire-dialog-action-count.h \
	src/ui/gui/psppire-dialog-action-crosstabs.c \
	src/ui/gui/psppire-dialog-action-crosstabs.h \
	src/ui/gui/psppire-dialog-action-descriptives.c \
	src/ui/gui/psppire-dialog-action-descriptives.h \
	src/ui/gui/psppire-dialog-action-examine.c \
	src/ui/gui/psppire-dialog-action-examine.h \
	src/ui/gui/psppire-dialog-action-factor.c \
	src/ui/gui/psppire-dialog-action-factor.h \
	src/ui/gui/psppire-dialog-action-flip.c \
	src/ui/gui/psppire-dialog-action-flip.h \
	src/ui/gui/psppire-dialog-action-frequencies.c \
	src/ui/gui/psppire-dialog-action-frequencies.h \
	src/ui/gui/psppire-dialog-action.h \
	src/ui/gui/psppire-dialog-action-histogram.c \
	src/ui/gui/psppire-dialog-action-histogram.h \
	src/ui/gui/psppire-dialog-action-indep-samps.c \
	src/ui/gui/psppire-dialog-action-indep-samps.h \
	src/ui/gui/psppire-dialog-action-k-independent.c \
	src/ui/gui/psppire-dialog-action-k-independent.h \
	src/ui/gui/psppire-dialog-action-kmeans.c \
	src/ui/gui/psppire-dialog-action-kmeans.h \
	src/ui/gui/psppire-dialog-action-k-related.c \
	src/ui/gui/psppire-dialog-action-k-related.h \
	src/ui/gui/psppire-dialog-action-logistic.c \
	src/ui/gui/psppire-dialog-action-logistic.h \
	src/ui/gui/psppire-dialog-action-means.c \
	src/ui/gui/psppire-dialog-action-means.h \
	src/ui/gui/psppire-dialog-action-oneway.c \
	src/ui/gui/psppire-dialog-action-oneway.h \
	src/ui/gui/psppire-dialog-action-paired.c \
	src/ui/gui/psppire-dialog-action-paired.h \
	src/ui/gui/psppire-dialog-action-rank.c \
	src/ui/gui/psppire-dialog-action-rank.h \
	src/ui/gui/psppire-dialog-action-recode.c \
	src/ui/gui/psppire-dialog-action-recode-different.c \
	src/ui/gui/psppire-dialog-action-recode-different.h \
	src/ui/gui/psppire-dialog-action-recode.h \
	src/ui/gui/psppire-dialog-action-recode-same.c \
	src/ui/gui/psppire-dialog-action-recode-same.h \
	src/ui/gui/psppire-dialog-action-regression.c \
	src/ui/gui/psppire-dialog-action-regression.h \
	src/ui/gui/psppire-dialog-action-reliability.c \
	src/ui/gui/psppire-dialog-action-reliability.h \
	src/ui/gui/psppire-dialog-action-roc.c \
	src/ui/gui/psppire-dialog-action-roc.h \
	src/ui/gui/psppire-dialog-action-runs.c \
	src/ui/gui/psppire-dialog-action-runs.h \
	src/ui/gui/psppire-dialog-action-scatterplot.c \
	src/ui/gui/psppire-dialog-action-scatterplot.h \
	src/ui/gui/psppire-dialog-action-select.c \
	src/ui/gui/psppire-dialog-action-select.h \
	src/ui/gui/psppire-dialog-action-sort.c \
	src/ui/gui/psppire-dialog-action-sort.h \
	src/ui/gui/psppire-dialog-action-split.c \
	src/ui/gui/psppire-dialog-action-split.h \
	src/ui/gui/psppire-dialog-action-tt1s.c \
	src/ui/gui/psppire-dialog-action-tt1s.h \
	src/ui/gui/psppire-dialog-action-two-sample.c \
	src/ui/gui/psppire-dialog-action-two-sample.h \
	src/ui/gui/psppire-dialog-action-univariate.c \
	src/ui/gui/psppire-dialog-action-univariate.h \
	src/ui/gui/psppire-dialog-action-var-info.c \
	src/ui/gui/psppire-dialog-action-var-info.h \
	src/ui/gui/psppire-dialog-action-weight.c \
	src/ui/gui/psppire-dialog-action-weight.h \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-dialog.h \
	src/ui/gui/psppire-dict.c \
	src/ui/gui/psppire-dict.h \
	src/ui/gui/psppire-dictview.c \
	src/ui/gui/psppire-dictview.h \
	src/ui/gui/psppire-format.c \
	src/ui/gui/psppire-format.h \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-keypad.h \
	src/ui/gui/psppire-means-layer.c \
	src/ui/gui/psppire-means-layer.h \
	src/ui/gui/psppire-select-dest.c \
	src/ui/gui/psppire-select-dest.h \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-selector.h \
	src/ui/gui/psppire-text-file.c \
	src/ui/gui/psppire-text-file.h \
	src/ui/gui/psppire-val-chooser.c \
	src/ui/gui/psppire-val-chooser.h \
	src/ui/gui/psppire-var-info.c \
	src/ui/gui/psppire-var-info.h \
	src/ui/gui/psppire-var-ptr.c \
	src/ui/gui/psppire-var-ptr.h \
	src/ui/gui/psppire-var-view.c \
	src/ui/gui/psppire-var-view.h \
	src/ui/gui/psppire-var-sheet-header.c \
	src/ui/gui/psppire-var-sheet-header.h \
	src/ui/gui/psppire-variable-sheet.c \
	src/ui/gui/psppire-variable-sheet.h \
	src/ui/gui/psppire-window-base.c \
	src/ui/gui/psppire-window-base.h \
	src/ui/gui/dialog-common.c \
	src/ui/gui/dialog-common.h \
	src/ui/gui/widgets.c \
	src/ui/gui/widgets.h \
	src/ui/gui/dict-display.c \
	src/ui/gui/dict-display.h

src_ui_gui_libwidgets_essential_la_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GTKSOURCEVIEW_CFLAGS) \
        $(SPREAD_SHEET_WIDGET_CFLAGS) \
	$(AM_CFLAGS)

# The unused-parameter warning is not by default disabled
# in AM_CFLAGS because the core pspp code has this enabled.
# This is only disabled in the gui code where we have many
# callbacks from gtk3 which have fixed parameters
if cc_is_gcc
src_ui_gui_libwidgets_essential_la_CFLAGS += -Wno-unused-parameter
endif

nodist_src_ui_gui_psppire_SOURCES = \
	src/ui/gui/psppire-marshal.c \
	src/ui/gui/psppire-marshal.h \
	src/ui/gui/resources.c

AM_CPPFLAGS += -Isrc

src/ui/gui/resources.c: src/ui/gui/resources.xml
	$(AM_V_at)$(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir)/src/ui/gui --generate-source $< --target=$@,out
	$(AM_V_GEN)echo '#include <config.h>' > $@,tmp
	cat $@,out >> $@,tmp
	$(RM) $@,out
	mv $@,tmp $@

src/ui/gui/psppire-marshal.c: src/ui/gui/marshaller-list
	$(AM_V_GEN)echo '#include <config.h>' > $@
	$(AM_V_at)$(GLIB_GENMARSHAL) --body --include-header=ui/gui/psppire-marshal.h --prefix=psppire_marshal $? >> $@

src/ui/gui/psppire-marshal.h: src/ui/gui/marshaller-list
	$(AM_V_GEN)$(GLIB_GENMARSHAL) --header --prefix=psppire_marshal $? > $@

BUILT_SOURCES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h src/ui/gui/resources.c

CLEANFILES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h \
	src/ui/gui/.deps/psppire-marshal.Plo \
	src/ui/gui/resources.c $(nodist_src_ui_gui_psppire_DATA)


#ensure the installcheck passes even if there is no X server available
installcheck-local:
	DISPLAY=/invalid/port $(MAKE) $(AM_MAKEFLAGS) installcheck-binPROGRAMS

# <gtk/gtk.h> wrapper
src_ui_gui_psppire_CPPFLAGS += $(AM_CPPFLAGS) -Isrc/ui/gui/include
BUILT_SOURCES += src/ui/gui/include/gtk/gtk.h
src/ui/gui/include/gtk/gtk.h: src/ui/gui/include/gtk/gtk.in.h
	@$(MKDIR_P) src/ui/gui/include/gtk
	$(AM_V_GEN)rm -f $@-t $@ && \
	{ echo '/* DO NOT EDIT! GENERATED AUTOMATICALLY! */'; \
	  $(SED) -e 's|%''INCLUDE_NEXT''%|$(INCLUDE_NEXT)|g' \
	      -e 's|%''PRAGMA_SYSTEM_HEADER''%|$(PRAGMA_SYSTEM_HEADER)|g' \
	      -e 's|%''PRAGMA_COLUMNS''%|$(PRAGMA_COLUMNS)|g' \
	      -e 's|%''NEXT_GTK_GTK_H''%|$(NEXT_GTK_GTK_H)|g' \
	      < $(srcdir)/src/ui/gui/include/gtk/gtk.in.h; \
	} > $@-t && \
	mv $@-t $@
CLEANFILES += src/ui/gui/include/gtk/gtk.h
EXTRA_DIST += src/ui/gui/include/gtk/gtk.in.h src/ui/gui/resources.xml

include $(top_srcdir)/src/ui/gui/icons/automake.mk

src/ui/gui/pspp.res: src/ui/gui/pspp.rc $(w32_icons)
	@$(MKDIR_P) src/ui/gui
	$(host_triplet)-windres  $< -O coff -o $@


UNINSTALL_DATA_HOOKS += update-icon-cache
INSTALL_DATA_HOOKS += update-icon-cache

#### Build the tools needed to run glade on our .ui files

EXTRA_pkgdir = $(abs_builddir)/src/ui/gui

EXTRA_pkg_LTLIBRARIES = src/ui/gui/libpsppire-glade.la

src_ui_gui_libpsppire_glade_la_SOURCES = \
	src/ui/gui/dummy.c

src_ui_gui_libpsppire_glade_la_LIBADD = \
	src/ui/gui/libwidgets-essential.la \
	src/ui/gui/psppire-marshal.lo

src_ui_gui_libpsppire_glade_la_CFLAGS = $(GTK_CFLAGS) $(AM_CFLAGS)
if cc_is_gcc
src_ui_gui_libpsppire_glade_la_CFLAGS += -Wno-unused-parameter
endif
src_ui_gui_libpsppire_glade_la_LDFLAGS = -release $(VERSION)  $(SPREAD_SHEET_WIDGET_LIBS)

EXTRA_DIST += src/ui/gui/psppire.xml src/ui/gui/glade-wrapper.in

src/ui/gui/glade-wrapper: src/ui/gui/glade-wrapper.in
	$(SED) -e 's%\@abs_top_srcdir\@%@abs_top_srcdir@%g' -e 's%\@abs_top_builddir\@%@abs_top_builddir@%g'  $< > $@
	chmod a+x $@

PHONY += glade-tools
glade-tools: src/ui/gui/glade-wrapper src/ui/gui/libpsppire-glade.la

# This works around a possible bug in Automake 1.16.1 which installs
# EXTRA_pkgLTLIBRARIES if DESTDIR is set.  It should not do that.
install-EXTRA_pkgLTLIBRARIES:
	true

endif
