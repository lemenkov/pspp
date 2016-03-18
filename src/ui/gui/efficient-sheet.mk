src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/jmd-axis-model.c \
	src/ui/gui/efficient-sheet/jmd-constraint.c \
	src/ui/gui/efficient-sheet/jmd-sheet.c \
	src/ui/gui/efficient-sheet/jmd-sheet-axis.c \
	src/ui/gui/efficient-sheet/jmd-sheet-body.c \
	src/ui/gui/efficient-sheet/jmd-sheet-single.c \
	src/ui/gui/efficient-sheet/jmd-datum.c \
	src/ui/gui/efficient-sheet/jmd-cell.c


nodist_src_ui_gui_psppire_SOURCES += \
	src/ui/gui/efficient-sheet/jmd-marshaller.c \
	src/ui/gui/efficient-sheet/jmd-marshaller.h

src_ui_gui_psppire_CPPFLAGS+=-Isrc/ui/gui/efficient-sheet



BUILT_SOURCES += \
	src/ui/gui/efficient-sheet/jmd-marshaller.c \
	src/ui/gui/efficient-sheet/jmd-marshaller.h

src/ui/gui/efficient-sheet/jmd-marshaller.c: src/ui/gui/efficient-sheet/marshall-list
	glib-genmarshal --body --prefix=jmd_cclosure_marshal $< > $@

src/ui/gui/efficient-sheet/jmd-marshaller.h: src/ui/gui/efficient-sheet/marshall-list
	glib-genmarshal --header --prefix=jmd_cclosure_marshal $< > $@

