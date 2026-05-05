#pragma once

#include "ui_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *header;
    lv_obj_t *content;
} ui_page_layout_t;

ui_page_layout_t ui_page_create_layout(lv_obj_t *screen,
                                       const char *title,
                                       app_page_id_t page_id,
                                       ui_page_nav_cb_t nav_cb,
                                       lv_color_t bg_color);

#ifdef __cplusplus
}
#endif
