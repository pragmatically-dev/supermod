TEMPLATE = lib
TARGET   = chiplinks-backend
CONFIG  += shared plugin no_plugin_name_prefix
CONFIG  += c++17

QMAKE_LFLAGS += -Wl,--no-undefined

OBJECTS_DIR = build/obj
MOC_DIR     = build/moc
UI_DIR      = build/ui
XOVI_DIR    = build/xovi

xoviextension.target   = $$XOVI_DIR/xovi.c
xoviextension.commands = mkdir -p $$XOVI_DIR && python3 $$(XOVI_REPO)/util/xovigen.py -o $$XOVI_DIR/xovi.c -H $$XOVI_DIR/xovi.h chiplinks-backend.xovi
xoviextension.depends  = chiplinks-backend.xovi \
                         ../qmd/supermod_base/values.qmd \
                         ../qmd/supermod_base/dv_state.qmd \
                         ../qmd/supermod_base/dv_logic.qmd \
                         ../qmd/supermod_pins/dv_pin_gesture_guard.qmd \
                         ../qmd/supermod_pins/dv_pin_placement.qmd \
                         ../qmd/supermod_pins/dv_pins.qmd \
                         ../qmd/supermod_pins/dv_pin_render.qmd \
                         ../qmd/supermod_links/dv_crossdoc.qmd \
                         ../qmd/supermod_links/dv_crossdoc_errors.qmd \
                         ../qmd/supermod_links/dv_crossdoc_overlay.qmd \
                         ../qmd/supermod_links/link_panels.qmd \
                         ../qmd/supermod_links/navigator.qmd \
                         ../qmd/supermod_links/action_header.qmd \
                         ../qmd/supermod_links/pages_item.qmd \
                         ../qmd/supermod_toc/toc_state.qmd \
                         ../qmd/supermod_toc/toc_header.qmd \
                         ../qmd/supermod_toc/toc_numbering.qmd \
                         ../qmd/supermod_toc/toc_scroll.qmd \
                         ../qmd/supermod_toc/toc_delegate.qmd \
                         ../qmd/supermod_toc/dv_toc_view.qmd \
                         ../qmd/supermod_toc/dv_dialogs.qmd \
                         ../qmd/supermod_toc/pages.qmd \
                         ../qmd/supermod_base/dv_document_hooks.qmd \
                         ../qmd/supermod_ui/dv_toast.qmd \
                         ../qmd/supermod_ui/notification.qmd \
                         ../qmd/supermod_graphview/toc_backlinks_view.qmd \
                         ../qmd/supermod_graphview/toc_graphview.qmd \
                         ../qmd/supermod_ui/toolbar.qmd \
                         ../qmd/supermod_ui/dv_toolbar_wiring.qmd \
                         ../qmd/supermod_ui/additional_tools.qmd \
                         ../qmd/supermod_ui/settings_menu.qmd \
                         ../qmd/supermod_bookmarks/dv_bookmarks.qmd \
                         ../qmd/supermod_bookmarks/bookmarks_grid.qmd \
                         ../qmd/supermod_dashboard/sidebar_button.qmd \
                         ../qmd/supermod_dashboard/navigator_wiring.qmd \
                         ../qmd/supermod_dashboard/dashboard_overlay.qmd \
                         ../qmd/supermod_postit/dv_postit_state.qmd \
                         ../qmd/supermod_postit/dv_postit_render.qmd \
                         ../qmd/supermod_postit/dv_postit_editor.qmd \
                         ../qmd/supermod_capture/capture_overlay.qmd \
                         ../qmd/supermod_capture/capture_lasso.qmd \
                         ../qmd/supermod_planner/planner_sidebar.qmd \
                         ../qmd/supermod_planner/planner_wiring.qmd \
                         ../qmd/supermod_planner/planner_overlay.qmd \
                         ../qmd/supermod_planner/planner_docview_refs.qmd \
                         ../qmd/supermod_planner/planner_quicksettings.qmd

QMAKE_EXTRA_TARGETS += xoviextension
PRE_TARGETDEPS      += $$XOVI_DIR/xovi.c

QT += quick qml concurrent

# sqlite3 bundled (amalgamation 3.45.3) — compilado dentro del .so con -fPIC.
# Self-contained: no depende de libsqlite3 del device. FTS5 + THREADSAFE.
SQLITE_AMALGAMATION = $$PWD/../vendor/sqlite

SOURCES += \
    main.cpp \
    entry.c \
    $$XOVI_DIR/xovi.c \
    ChiplinksBackend.cpp \
    db/CaptureService.cpp \
    db/BackendTrace.cpp \
    db/Database.cpp \
    db/RowMarshaller.cpp \
    db/DocsRepo.cpp \
    db/TocItemsRepo.cpp \
    db/LinksRepo.cpp \
    db/PinsRepo.cpp \
    db/BacklinksRepo.cpp \
    db/RecentsRepo.cpp \
    db/MetaRepo.cpp \
    db/PostitsRepo.cpp \
    db/SettingsRepo.cpp \
    db/PlannerRepo.cpp \
    db/NotificationService.cpp \
    db/GraphLayout.cpp \
    db/TocNumbering.cpp \
    db/LinkFlow.cpp \
    $$SQLITE_AMALGAMATION/sqlite3.c

HEADERS += \
    ChiplinksBackend.hpp \
    db/BackendTrace.hpp \
    db/Database.hpp \
    db/RowMarshaller.hpp \
    db/DocsRepo.hpp \
    db/TocItemsRepo.hpp \
    db/LinksRepo.hpp \
    db/PinsRepo.hpp \
    db/BacklinksRepo.hpp \
    db/RecentsRepo.hpp \
    db/MetaRepo.hpp \
    db/PostitsRepo.hpp \
    db/SettingsRepo.hpp \
    db/PlannerRepo.hpp \
    db/NotificationService.hpp \
    db/GraphLayout.hpp \
    db/TocNumbering.hpp \
    db/LinkFlow.hpp

INCLUDEPATH += $$XOVI_DIR $$SQLITE_AMALGAMATION

DEFINES += SQLITE_ENABLE_FTS5 \
           SQLITE_THREADSAFE=1 \
           SQLITE_DEFAULT_MEMSTATUS=0 \
           SQLITE_OMIT_DEPRECATED \
           SQLITE_OMIT_LOAD_EXTENSION

QMAKE_CXXFLAGS += -fPIC -Werror -O3 -mfpu=neon -mfloat-abi=hard
QMAKE_CFLAGS   += -fPIC -O3 -mfpu=neon -mfloat-abi=hard

# sqlite3.c (amalgamation) tira muchos warnings benignos en GCC 13. Quitar
# -Werror solo para C (no afecta a nuestro código C++).
QMAKE_CFLAGS += -Wno-error -Wno-unused-but-set-variable -Wno-unused-parameter \
                -Wno-unused-function -Wno-implicit-fallthrough
