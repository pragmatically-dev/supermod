#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "xovi.h"

struct FramebufferConfig {
    void *framebufferAddress;
    int width, height, type, bpl;
    bool requiresReload;
};

void ci_getFramebufferInfo(void **addr, int *width, int *height,
                           int *type, int *bpl) {
    if (!framebuffer_spy$getFramebufferConfig) {
        fprintf(stderr, "[supermod-capture] framebuffer-spy not available\n");
        *addr = NULL;
        return;
    }

    struct FramebufferConfig cfg =
        ((struct FramebufferConfig (*)()) framebuffer_spy$getFramebufferConfig)();

    if (cfg.requiresReload && framebuffer_spy$refreshFramebuffer) {
        ((void (*)()) framebuffer_spy$refreshFramebuffer)();
    }

    *addr   = cfg.framebufferAddress;
    *width  = cfg.width;
    *height = cfg.height;
    *type   = cfg.type;
    *bpl    = cfg.bpl;
}

void registerQmldiff();
extern char *program_invocation_short_name;

void _xovi_construct() {

    if (program_invocation_short_name == NULL ||
        strcmp(program_invocation_short_name, "xochitl") != 0) {
        fprintf(stderr, "[chiplinks-backend] skip load in subprocess: %s\n",
                program_invocation_short_name ? program_invocation_short_name : "(null)");
        return;
    }
    fprintf(stderr, "[chiplinks-backend] loaded in xochitl, registering singleton\n");
    Environment->requireExtension("qt-resource-rebuilder", 0, 2, 0);
    registerQmldiff();
#ifdef CHIPLINKS_DIAG_NO_DIFFS
    fprintf(stderr, "[chiplinks-perf] DIAG build: skipping ALL qmldiff registration "
                    "(singleton + DB + qt-rr loaded, 0 diffs applied)\n");
#else
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_base_values,
                                                    "supermod-base values");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_base_dv_state,
                                                    "supermod-base dv_state");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_base_dv_logic,
                                                    "supermod-base dv_logic");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_pins_dv_pin_gesture_guard,
                                                    "supermod-pins gesture guard");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_pins_dv_pin_placement,
                                                    "supermod-pins placement overlay");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_pins_dv_pins,
                                                    "supermod-pins operations");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_pins_dv_pin_render,
                                                    "supermod-pins render");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_dv_crossdoc,
                                                    "supermod-links crossdoc");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_dv_crossdoc_errors,
                                                    "supermod-links crossdoc errors");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_dv_crossdoc_overlay,
                                                    "supermod-links crossdoc overlay");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_link_panels,
                                                    "supermod-links panels");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_navigator,
                                                    "supermod-links navigator");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_action_header,
                                                    "supermod-links action header");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_links_pages_item,
                                                    "supermod-links pages item");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_toc_state,
                                                    "supermod-toc state");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_toc_header,
                                                    "supermod-toc header");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_toc_numbering,
                                                    "supermod-toc numbering");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_toc_scroll,
                                                    "supermod-toc scroll");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_toc_delegate,
                                                    "supermod-toc delegate");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_dv_toc_view,
                                                    "supermod-toc dv_toc_view");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_dv_dialogs,
                                                    "supermod-toc dv_dialogs");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_toc_pages,
                                                    "supermod-toc pages");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_base_dv_document_hooks,
                                                    "supermod-base document hooks");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_dv_toast,
                                                    "supermod-ui toast");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_notification,
                                                    "supermod-ui notification");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_graphview_toc_backlinks_view,
                                                    "supermod-graphview backlinks view");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_graphview_toc_graphview,
                                                    "supermod-graphview graph view");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_toolbar,
                                                    "supermod-ui toolbar");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_dv_toolbar_wiring,
                                                    "supermod-ui toolbar wiring");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_additional_tools,
                                                    "supermod-ui additional tools");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_ui_settings_menu,
                                                    "supermod-ui settings menu");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_bookmarks_dv_bookmarks,
                                                    "supermod-bookmarks dv_bookmarks");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_bookmarks_bookmarks_grid,
                                                    "supermod-bookmarks grid");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_dashboard_sidebar_button,
                                                    "supermod-dashboard sidebar button");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_dashboard_navigator_wiring,
                                                    "supermod-dashboard navigator wiring");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_dashboard_overlay,
                                                    "supermod-dashboard overlay");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_postit_state,
                                                    "supermod-postit state");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_postit_render,
                                                    "supermod-postit render");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_postit_editor,
                                                    "supermod-postit editor");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_capture_overlay,
                                                    "supermod-capture overlay");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_capture_lasso,
                                                    "supermod-capture lasso");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_planner_sidebar,
                                                    "supermod-planner sidebar button");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_planner_wiring,
                                                    "supermod-planner navigator wiring");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_planner_overlay,
                                                    "supermod-planner overlay");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_planner_docview,
                                                    "supermod-planner docview refs");
    qt_resource_rebuilder$qmldiff_add_external_diff(r$supermod_planner_quicksettings,
                                                    "supermod-planner quicksettings shortcut");
#endif
}
