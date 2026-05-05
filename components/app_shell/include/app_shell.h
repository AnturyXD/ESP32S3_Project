#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_AI_VOICE,
    APP_PAGE_SETTINGS,
    APP_PAGE_DEBUG,
    APP_PAGE_ABOUT,
    APP_PAGE_PLACEHOLDER,
    APP_PAGE_MAX,
} app_page_id_t;

typedef struct {
    app_page_id_t page_id;
    const char *name;
} app_shell_app_t;

typedef esp_err_t (*app_shell_route_handler_t)(app_page_id_t page_id, void *user_ctx);

esp_err_t app_shell_init(void);
esp_err_t app_shell_set_route_handler(app_shell_route_handler_t handler, void *user_ctx);
esp_err_t app_shell_register_app(const app_shell_app_t *app);
size_t app_shell_get_app_count(void);
const app_shell_app_t *app_shell_get_app_by_id(app_page_id_t page_id);
app_page_id_t app_shell_get_current_page(void);
esp_err_t app_shell_navigate(app_page_id_t page_id);
const char *app_shell_page_name(app_page_id_t page_id);

#ifdef __cplusplus
}
#endif
