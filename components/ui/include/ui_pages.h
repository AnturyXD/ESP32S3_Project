#pragma once

#include "app_shell.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_page_nav_cb_t)(app_page_id_t page_id);

void ui_page_home_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);
void ui_page_ai_voice_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);
void ui_page_settings_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);
void ui_page_debug_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);
void ui_page_about_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);
void ui_page_placeholder_create(lv_obj_t *parent, ui_page_nav_cb_t nav_cb);

#ifdef __cplusplus
}
#endif
