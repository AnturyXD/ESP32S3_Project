#include "service_cloud.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "service_audio.h"
#include "service_network.h"
#include "service_power.h"
#include "service_storage.h"
#include "service_time.h"

static const char *TAG = "CLOUD";

namespace {

constexpr uint32_t kTaskStackBytes = 8192;
constexpr UBaseType_t kTaskPriority = 4;
constexpr TickType_t kNetworkWaitDelay = pdMS_TO_TICKS(2000);
constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(15000);
constexpr TickType_t kHeartbeatDelay = pdMS_TO_TICKS(30000);
constexpr TickType_t kHttpMutexTimeout = pdMS_TO_TICKS(12000);
constexpr int kHttpTimeoutMs = 6000;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_http_mutex = nullptr;
static TaskHandle_t s_task_handle = nullptr;
static cloud_state_t s_state = CLOUD_STATE_UNINIT;
static bool s_registered = false;
static int s_last_http_status = 0;
static uint32_t s_revision = 0;
static char s_last_event[96] = "uninit";
static char s_last_error[128] = "None";
static char s_last_heartbeat[32] = "--";

static const char *cloud_state_to_string(cloud_state_t state)
{
    switch (state) {
    case CLOUD_STATE_UNINIT:
        return "Uninit";
    case CLOUD_STATE_IDLE:
        return "Idle";
    case CLOUD_STATE_REGISTERING:
        return "Registering";
    case CLOUD_STATE_REGISTERED:
        return "Registered";
    case CLOUD_STATE_HEARTBEAT_SENDING:
        return "Heartbeat";
    case CLOUD_STATE_ONLINE:
        return "Online";
    case CLOUD_STATE_ERROR:
        return "Error";
    case CLOUD_STATE_CONFIG_MISSING:
        return "Config Missing";
    default:
        return "Unknown";
    }
}

static const char *power_state_to_string(power_state_t state)
{
    switch (state) {
    case POWER_STATE_INIT:
        return "Init";
    case POWER_STATE_HOLD_ON:
        return "Hold On";
    case POWER_STATE_RUNNING:
        return "Running";
    case POWER_STATE_PWR_PRESSED:
        return "PWR Pressed";
    case POWER_STATE_SHUTDOWN_PENDING:
        return "Shutdown Pending";
    case POWER_STATE_POWER_OFF:
        return "Power Off";
    case POWER_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static void cloud_set_state(cloud_state_t state, const char *event)
{
    portENTER_CRITICAL(&s_lock);
    if (s_state != state) {
        s_state = state;
        ++s_revision;
    }
    if (event != nullptr) {
        snprintf(s_last_event, sizeof(s_last_event), "%s", event);
        ++s_revision;
    }
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "state -> %s, event=%s", cloud_state_to_string(state), event ? event : "-");
}

static void cloud_set_error(const char *message, int http_status)
{
    portENTER_CRITICAL(&s_lock);
    s_state = CLOUD_STATE_ERROR;
    s_last_http_status = http_status;
    snprintf(s_last_error, sizeof(s_last_error), "%s", message ? message : "Unknown");
    snprintf(s_last_event, sizeof(s_last_event), "request failed");
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "error: %s, http_status=%d", message ? message : "Unknown", http_status);
}

static void cloud_set_http_status(int http_status)
{
    portENTER_CRITICAL(&s_lock);
    s_last_http_status = http_status;
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
}

static bool cloud_config_ready(void)
{
    return strlen(CLOUD_SERVER_BASE_URL) > 0 && strlen(DEVICE_ID) > 0;
}

static void cloud_join_url(const char *path, char *url, size_t url_size)
{
    const size_t base_len = strlen(CLOUD_SERVER_BASE_URL);
    const bool base_has_slash = base_len > 0 && CLOUD_SERVER_BASE_URL[base_len - 1] == '/';
    const bool path_has_slash = path != nullptr && path[0] == '/';

    if (base_has_slash && path_has_slash) {
        snprintf(url, url_size, "%s%s", CLOUD_SERVER_BASE_URL, path + 1);
    } else if (!base_has_slash && !path_has_slash) {
        snprintf(url, url_size, "%s/%s", CLOUD_SERVER_BASE_URL, path);
    } else {
        snprintf(url, url_size, "%s%s", CLOUD_SERVER_BASE_URL, path);
    }
}

/**
 * @brief 对 JSON 字符串字段做最小转义。
 *
 * 设备 ID、硬件名、状态字符串理论上都由固件控制，但仍然在拼 JSON 前做转义，
 * 避免后续本地配置里出现引号或反斜杠时破坏请求体格式。
 */
static void cloud_json_escape(const char *input, char *output, size_t output_size)
{
    if (output_size == 0) {
        return;
    }
    size_t out = 0;
    const char *src = input ? input : "";
    while (*src != '\0' && out + 1 < output_size) {
        if ((*src == '"' || *src == '\\') && out + 2 < output_size) {
            output[out++] = '\\';
            output[out++] = *src++;
        } else if (*src == '\n' && out + 2 < output_size) {
            output[out++] = '\\';
            output[out++] = 'n';
            ++src;
        } else {
            output[out++] = *src++;
        }
    }
    output[out] = '\0';
}

static esp_err_t cloud_post_json(const char *path, const char *json_body, int *http_status)
{
    if (!cloud_config_ready()) {
        cloud_set_state(CLOUD_STATE_CONFIG_MISSING, "cloud config missing");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_http_mutex == nullptr || xSemaphoreTake(s_http_mutex, kHttpMutexTimeout) != pdTRUE) {
        cloud_set_error("HTTP busy", 0);
        return ESP_ERR_TIMEOUT;
    }

    char url[192] = {0};
    cloud_join_url(path, url, sizeof(url));

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = kHttpTimeoutMs;
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        xSemaphoreGive(s_http_mutex);
        cloud_set_error("esp_http_client_init failed", 0);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (strlen(DEVICE_SHARED_SECRET) > 0) {
        /* 只把 token 放进请求头，不在串口日志中打印原文。 */
        esp_http_client_set_header(client, "X-Device-Token", DEVICE_SHARED_SECRET);
    }
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    ESP_LOGI(TAG, "POST %s token_configured=%s", path, strlen(DEVICE_SHARED_SECRET) > 0 ? "yes" : "no");
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    if (http_status != nullptr) {
        *http_status = status;
    }
    cloud_set_http_status(status);
    esp_http_client_cleanup(client);
    xSemaphoreGive(s_http_mutex);

    if (err != ESP_OK) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "HTTP perform failed: %s", esp_err_to_name(err));
        cloud_set_error(msg, status);
        return err;
    }
    if (status < 200 || status >= 300) {
        char msg[96] = {0};
        snprintf(msg, sizeof(msg), "HTTP status %d", status);
        cloud_set_error(msg, status);
        return ESP_FAIL;
    }

    portENTER_CRITICAL(&s_lock);
    snprintf(s_last_error, sizeof(s_last_error), "None");
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static void cloud_update_last_heartbeat(void)
{
    const char *time_str = service_time_get_time_string();
    portENTER_CRITICAL(&s_lock);
    if (time_str != nullptr && strcmp(time_str, "--:--") != 0) {
        snprintf(s_last_heartbeat, sizeof(s_last_heartbeat), "%s", time_str);
    } else {
        snprintf(s_last_heartbeat, sizeof(s_last_heartbeat), "+%llds", esp_timer_get_time() / 1000000LL);
    }
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
}

static void cloud_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "cloud task started");

    while (true) {
        if (!cloud_config_ready()) {
            cloud_set_state(CLOUD_STATE_CONFIG_MISSING, "cloud config missing");
            vTaskDelay(kRetryDelay);
            continue;
        }

        if (!service_network_is_connected()) {
            cloud_set_state(CLOUD_STATE_IDLE, "wait network");
            vTaskDelay(kNetworkWaitDelay);
            continue;
        }

        if (!s_registered) {
            if (service_cloud_register_device() != ESP_OK) {
                vTaskDelay(kRetryDelay);
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const esp_err_t err = service_cloud_send_heartbeat();
        vTaskDelay(err == ESP_OK ? kHeartbeatDelay : kRetryDelay);
    }
}

} // namespace

esp_err_t service_cloud_init(void)
{
    if (s_task_handle != nullptr) {
        return ESP_OK;
    }

    s_http_mutex = xSemaphoreCreateMutex();
    if (s_http_mutex == nullptr) {
        cloud_set_error("create HTTP mutex failed", 0);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "service_cloud init: base_url_configured=%s device_id=%s token_configured=%s",
             strlen(CLOUD_SERVER_BASE_URL) > 0 ? "yes" : "no", DEVICE_ID,
             strlen(DEVICE_SHARED_SECRET) > 0 ? "yes" : "no");

    if (!cloud_config_ready()) {
        cloud_set_state(CLOUD_STATE_CONFIG_MISSING, "cloud config missing");
        return ESP_OK;
    }

    cloud_set_state(CLOUD_STATE_IDLE, "cloud init done");
    const BaseType_t ok = xTaskCreate(cloud_task, "cloud_task", kTaskStackBytes, nullptr, kTaskPriority, &s_task_handle);
    if (ok != pdPASS) {
        s_task_handle = nullptr;
        cloud_set_error("create cloud task failed", 0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t service_cloud_register_device(void)
{
    if (!service_network_is_connected()) {
        cloud_set_state(CLOUD_STATE_IDLE, "register skipped: no network");
        return ESP_ERR_INVALID_STATE;
    }

    char ip[16] = {0};
    char device_id[64] = {0};
    char firmware[32] = {0};
    char hardware[64] = {0};
    char json[384] = {0};

    service_network_get_ip(ip, sizeof(ip));
    cloud_json_escape(DEVICE_ID, device_id, sizeof(device_id));
    cloud_json_escape(APP_FIRMWARE_VERSION, firmware, sizeof(firmware));
    cloud_json_escape(DEVICE_HARDWARE_NAME, hardware, sizeof(hardware));

    snprintf(json, sizeof(json),
             "{\"device_id\":\"%s\",\"firmware_version\":\"%s\",\"hardware\":\"%s\",\"ip\":\"%s\"}",
             device_id, firmware, hardware, ip);

    cloud_set_state(CLOUD_STATE_REGISTERING, "register device");
    int status = 0;
    const esp_err_t err = cloud_post_json("/api/esp-ai-terminal/devices/register", json, &status);
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_lock);
    s_registered = true;
    s_state = CLOUD_STATE_REGISTERED;
    s_last_http_status = status;
    snprintf(s_last_event, sizeof(s_last_event), "registered");
    snprintf(s_last_error, sizeof(s_last_error), "None");
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "device registered, http_status=%d", status);
    return ESP_OK;
}

esp_err_t service_cloud_send_heartbeat(void)
{
    if (!service_network_is_connected()) {
        cloud_set_state(CLOUD_STATE_IDLE, "heartbeat skipped: no network");
        return ESP_ERR_INVALID_STATE;
    }

    char ip[16] = {0};
    char device_id[64] = {0};
    char firmware[32] = {0};
    char wifi_state[32] = {0};
    char storage_state[32] = {0};
    char audio_state[32] = {0};
    char power_state[32] = {0};
    char json[512] = {0};

    service_network_get_ip(ip, sizeof(ip));
    cloud_json_escape(DEVICE_ID, device_id, sizeof(device_id));
    cloud_json_escape(APP_FIRMWARE_VERSION, firmware, sizeof(firmware));
    cloud_json_escape(service_network_get_state_string(), wifi_state, sizeof(wifi_state));
    cloud_json_escape(service_storage_get_state_string(), storage_state, sizeof(storage_state));
    cloud_json_escape(service_audio_get_state_string(), audio_state, sizeof(audio_state));
    cloud_json_escape(power_state_to_string(service_power_get_state()), power_state, sizeof(power_state));

    snprintf(json, sizeof(json),
             "{\"device_id\":\"%s\",\"firmware_version\":\"%s\",\"wifi_state\":\"%s\",\"ip\":\"%s\","
             "\"storage_state\":\"%s\",\"audio_state\":\"%s\",\"power_state\":\"%s\"}",
             device_id, firmware, wifi_state, ip, storage_state, audio_state, power_state);

    cloud_set_state(CLOUD_STATE_HEARTBEAT_SENDING, "send heartbeat");
    int status = 0;
    const esp_err_t err = cloud_post_json("/api/esp-ai-terminal/devices/heartbeat", json, &status);
    if (err != ESP_OK) {
        return err;
    }

    cloud_update_last_heartbeat();
    portENTER_CRITICAL(&s_lock);
    s_state = CLOUD_STATE_ONLINE;
    s_last_http_status = status;
    snprintf(s_last_event, sizeof(s_last_event), "heartbeat ok");
    snprintf(s_last_error, sizeof(s_last_error), "None");
    ++s_revision;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "heartbeat ok, http_status=%d", status);
    return ESP_OK;
}

cloud_state_t service_cloud_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    const cloud_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_cloud_get_state_string(void)
{
    return cloud_state_to_string(service_cloud_get_state());
}

const char *service_cloud_get_last_event(void)
{
    static char event[96];
    portENTER_CRITICAL(&s_lock);
    snprintf(event, sizeof(event), "%s", s_last_event);
    portEXIT_CRITICAL(&s_lock);
    return event;
}

const char *service_cloud_get_last_error(void)
{
    static char error[128];
    portENTER_CRITICAL(&s_lock);
    snprintf(error, sizeof(error), "%s", s_last_error);
    portEXIT_CRITICAL(&s_lock);
    return error;
}

int service_cloud_get_last_http_status(void)
{
    portENTER_CRITICAL(&s_lock);
    const int status = s_last_http_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}

bool service_cloud_is_registered(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool registered = s_registered;
    portEXIT_CRITICAL(&s_lock);
    return registered;
}

const char *service_cloud_get_last_heartbeat(void)
{
    static char heartbeat[32];
    portENTER_CRITICAL(&s_lock);
    snprintf(heartbeat, sizeof(heartbeat), "%s", s_last_heartbeat);
    portEXIT_CRITICAL(&s_lock);
    return heartbeat;
}

uint32_t service_cloud_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    const uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}
