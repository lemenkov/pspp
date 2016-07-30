## Process this file with automake to produce Makefile.in  -*- makefile -*-

module_LTLIBRARIES = libglade-psppire.la

moduledir = `pkg-config --variable=moduledir gladeui-2.0`
catalogdir = `pkg-config --variable=catalogdir gladeui-2.0`
pixmapdir = `pkg-config --variable=pixmapdir gladeui-2.0`

small_pixmapdir = $(pixmapdir)/hicolor/16x16/actions
large_pixmapdir = $(pixmapdir)/hicolor/22x22/actions

# format.c for psppire-value-entry.c

libglade_psppire_la_SOURCES = \
	src/ui/gui/helper.c \
	src/ui/gui/dialog-common.c \
	src/ui/gui/psppire-var-ptr.c \
	src/data/format.c \
	src/ui/gui/psppire-format.c \
	src/data/value-labels.c \
	src/ui/gui/psppire-conf.c \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-dictview.c \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-select-dest.c \
	src/ui/gui/psppire-var-view.c \
	src/ui/gui/psppire-checkbox-treeview.c \
	src/ui/gui/psppire-val-chooser.c \
	src/ui/gui/psppire-value-entry.c \
	src/ui/gui/psppire-window-base.c

dist_catalog_DATA = \
	glade/psppire.xml

dist_small_pixmap_DATA = \
	glade/icons/16x16/widget-psppire-psppire-acr.png \
	glade/icons/16x16/widget-psppire-psppire-dialog.png \
	glade/icons/16x16/widget-psppire-psppire-keypad.png \
	glade/icons/16x16/widget-psppire-psppire-selector.png

dist_large_pixmap_DATA = \
	glade/icons/22x22/widget-psppire-psppire-acr.png \
	glade/icons/22x22/widget-psppire-psppire-dialog.png \
	glade/icons/22x22/widget-psppire-psppire-keypad.png \
	glade/icons/22x22/widget-psppire-psppire-selector.png


libglade_psppire_la_CFLAGS = $(GLADE_UI_CFLAGS) $(GLADE_CFLAGS) \
	$(GTKSOURCEVIEW_CFLAGS) -I $(top_srcdir)/src/ui/gui -DDEBUGGING

libglade_psppire_la_LIBADD = gl/libgl.la
