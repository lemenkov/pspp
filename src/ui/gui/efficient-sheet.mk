src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/src/ssw-axis-model.c \
	src/ui/gui/efficient-sheet/src/ssw-constraint.c \
	src/ui/gui/efficient-sheet/src/ssw-sheet.c \
	src/ui/gui/efficient-sheet/src/ssw-sheet-axis.c \
	src/ui/gui/efficient-sheet/src/ssw-sheet-body.c \
	src/ui/gui/efficient-sheet/src/ssw-sheet-single.c \
	src/ui/gui/efficient-sheet/src/ssw-datum.c \
	src/ui/gui/efficient-sheet/src/ssw-cell.c

nodist_src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/src/ssw-marshaller.c \
	src/ui/gui/efficient-sheet/src/ssw-marshaller.h

src_ui_gui_psppire_CPPFLAGS+=-Isrc/ui/gui/efficient-sheet/src

BUILT_SOURCES += \
	src/ui/gui/efficient-sheet/src/ssw-marshaller.c \
	src/ui/gui/efficient-sheet/src/ssw-marshaller.h

src/ui/gui/efficient-sheet/src/ssw-marshaller.c: src/ui/gui/efficient-sheet/src/marshall-list
	glib-genmarshal --body --prefix=ssw_cclosure_marshal $< > $@

src/ui/gui/efficient-sheet/src/ssw-marshaller.h: src/ui/gui/efficient-sheet/src/marshall-list
	glib-genmarshal --header --prefix=ssw_cclosure_marshal $< > $@

