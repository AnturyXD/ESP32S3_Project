#include "app_shell.h"

#include "esp_log.h"

static const char *TAG = "APP_SHELL";

namespace {

constexpr size_t kMaxApps = 16;

static app_shell_app_t s_apps[kMaxApps];
static size_t s_app_count = 0;
static bool s_initialized = false;
static app_page_id_t s_current_page = APP_PAGE_HOME;
static app_shell_route_handler_t s_route_handler = nullptr;
static void *s_route_user_ctx = nullptr;

static bool app_shell_is_valid_page_id(app_page_id_t page_id)
{
    return page_id >= APP_PAGE_HOME && page_id < APP_PAGE_MAX;
}

} // namespace

const char *app_shell_page_name(app_page_id_t page_id)
{
    switch (page_id) {
    case APP_PAGE_HOME:
        return "Home";
    case APP_PAGE_AI_VOICE:
        return "AI Voice";
    case APP_PAGE_SETTINGS:
        return "Settings";
    case APP_PAGE_DEBUG:
        return "Debug";
    case APP_PAGE_ABOUT:
        return "About";
    case APP_PAGE_PLACEHOLDER:
        return "Placeholder";
    default:
        return "Unknown";
    }
}

esp_err_t app_shell_set_route_handler(app_shell_route_handler_t handler, void *user_ctx)
{
    s_route_handler = handler;
    s_route_user_ctx = user_ctx;
    ESP_LOGI(TAG, "route handler registered");
    return ESP_OK;
}

esp_err_t app_shell_register_app(const app_shell_app_t *app)
{
    if (app == nullptr || app->name == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!app_shell_is_valid_page_id(app->page_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_app_count >= kMaxApps) {
        return ESP_ERR_NO_MEM;
    }
    if (app_shell_get_app_by_id(app->page_id) != nullptr) {
        ESP_LOGW(TAG, "app already registered: %s", app->name);
        return ESP_ERR_INVALID_STATE;
    }

    s_apps[s_app_count] = *app;
    s_app_count++;
    ESP_LOGI(TAG, "app registered: %s", app->name);
    return ESP_OK;
}

size_t app_shell_get_app_count(void)
{
    return s_app_count;
}

const app_shell_app_t *app_shell_get_app_by_id(app_page_id_t page_id)
{
    for (size_t i = 0; i < s_app_count; ++i) {
        if (s_apps[i].page_id == page_id) {
            return &s_apps[i];
        }
    }
    return nullptr;
}

app_page_id_t app_shell_get_current_page(void)
{
    return s_current_page;
}

esp_err_t app_shell_navigate(app_page_id_t page_id)
{
    if (!app_shell_is_valid_page_id(page_id)) {
        ESP_LOGE(TAG, "invalid page id: %d", static_cast<int>(page_id));
        return ESP_ERR_INVALID_ARG;
    }
    if (app_shell_get_app_by_id(page_id) == nullptr) {
        ESP_LOGE(TAG, "page not registered: %d", static_cast<int>(page_id));
        return ESP_ERR_NOT_FOUND;
    }

    const app_page_id_t from = s_current_page;
    s_current_page = page_id;
    ESP_LOGI(TAG,
             "route: current_page=%d(%s) target_page=%d(%s)",
             static_cast<int>(from),
             app_shell_page_name(from),
             static_cast<int>(page_id),
             app_shell_page_name(page_id));

    if (s_route_handler == nullptr) {
        ESP_LOGW(TAG, "route handler not ready");
        return ESP_OK;
    }
    return s_route_handler(page_id, s_route_user_ctx);
}

esp_err_t app_shell_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "app_shell already initialized");
        return ESP_OK;
    }

    static const app_shell_app_t kDefaultApps[] = {
        {APP_PAGE_HOME, "Home"},
        {APP_PAGE_AI_VOICE, "AI Voice"},
        {APP_PAGE_SETTINGS, "Settings"},
        {APP_PAGE_DEBUG, "Debug"},
        {APP_PAGE_ABOUT, "About"},
        {APP_PAGE_PLACEHOLDER, "Placeholder"},
    };

    for (const app_shell_app_t &app : kDefaultApps) {
        esp_err_t err = app_shell_register_app(&app);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "failed to register app: %s", app.name);
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "app_shell initialized, apps=%u", static_cast<unsigned>(s_app_count));
    return app_shell_navigate(APP_PAGE_HOME);
}
