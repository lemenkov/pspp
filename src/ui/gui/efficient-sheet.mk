src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/src/jmd-axis-model.c \
	src/ui/gui/efficient-sheet/src/jmd-constraint.c \
	src/ui/gui/efficient-sheet/src/jmd-sheet.c \
	src/ui/gui/efficient-sheet/src/jmd-sheet-axis.c \
	src/ui/gui/efficient-sheet/src/jmd-sheet-body.c \
	src/ui/gui/efficient-sheet/src/jmd-sheet-single.c \
	src/ui/gui/efficient-sheet/src/jmd-datum.c \
	src/ui/gui/efficient-sheet/src/jmd-cell.c

nodist_src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/jmd-marshaller.c \
	src/ui/gui/efficient-sheet/jmd-marshaller.h

src_ui_gui_psppire_CPPFLAGS+=-Isrc/ui/gui/efficient-sheet/src

BUILT_SOURCES += \
	src/ui/gui/efficient-sheet/src/jmd-marshaller.c \
	src/ui/gui/efficient-sheet/src/jmd-marshaller.h

src/ui/gui/efficient-sheet/src/jmd-marshaller.c: src/ui/gui/efficient-sheet/src/marshall-list
	glib-genmarshal --body --prefix=jmd_cclosure_marshal $< > $@

src/ui/gui/efficient-sheet/src/jmd-marshaller.h: src/ui/gui/efficient-sheet/src/marshall-list
	glib-genmarshal --header --prefix=jmd_cclosure_marshal $< > $@

