#include "service_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "sdmmc_cmd.h"

static const char *TAG = "STORAGE";

namespace {

// TF 卡硬件确认：
// - SD interface type: SDMMC 1-bit，Waveshare Wiki 的 04_SD_Card 示例说明为 1-wire SDMMC。
// - 旧工程 sdcard_bsp 使用 CMD=GPIO39、D0=GPIO40、CLK=GPIO41，当前实现沿用这些板级参数。
// - MOSI/MISO/SCK/CS: 不适用。它们属于 SDSPI 接法，本板当前确认不是 SPI 接法。
// - Card Detect: 未在旧工程/board_cfg 中发现独立 CD 引脚，本轮不配置 CD，只通过初始化结果判断缺卡。
// - IO expander: SD 卡没有使用 TCA9554/EXIO 做 CS；service_storage 不直接操作 TCA9554，避免影响 EXIO6/EXIO7。
// 硬件引脚不要凭空修改，后续如更换板卡必须以 Waveshare 官方示例或 board_cfg 为准。
constexpr gpio_num_t kSdmmcCmdPin = GPIO_NUM_39;
constexpr gpio_num_t kSdmmcD0Pin = GPIO_NUM_40;
constexpr gpio_num_t kSdmmcClkPin = GPIO_NUM_41;

constexpr const char *STORAGE_MOUNT_POINT = "/sdcard";
constexpr const char *STORAGE_ROOT_DIR = "/sdcard/EAITERM";
constexpr const char *STORAGE_LOG_DIR = "/sdcard/EAITERM/LOGS";
constexpr const char *STORAGE_AUDIO_DIR = "/sdcard/EAITERM/AUDIO";
constexpr const char *STORAGE_ASR_DIR = "/sdcard/EAITERM/ASR";
constexpr const char *STORAGE_TTS_DIR = "/sdcard/EAITERM/TTS";
constexpr const char *STORAGE_CFG_DIR = "/sdcard/EAITERM/CFG";
constexpr const char *STORAGE_OTA_DIR = "/sdcard/EAITERM/OTA";
constexpr const char *STORAGE_TMP_DIR = "/sdcard/EAITERM/TMP";
constexpr const char *STORAGE_TEST_FILE = "/sdcard/EAITERM/TMP/STEST.TXT";
constexpr size_t kPathBufSize = 192;

static bool s_inited = false;
static bool s_mounted = false;
static sdmmc_card_t *s_card = nullptr;
static storage_state_t s_state = STORAGE_STATE_UNINIT;
static uint64_t s_total_bytes = 0;
static uint64_t s_free_bytes = 0;
static uint32_t s_revision = 0;
static char s_last_event[96] = "UNINIT";
static char s_last_error[128] = "None";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void storage_touch_revision_locked(void)
{
    ++s_revision;
}

static const char *storage_state_to_string(storage_state_t state)
{
    switch (state) {
    case STORAGE_STATE_UNINIT:
        return "Uninit";
    case STORAGE_STATE_INIT:
        return "Init";
    case STORAGE_STATE_NO_CARD:
        return "No Card";
    case STORAGE_STATE_MOUNTING:
        return "Mounting";
    case STORAGE_STATE_MOUNTED:
        return "Mounted";
    case STORAGE_STATE_INIT_ERROR:
        return "Init Error";
    case STORAGE_STATE_MOUNT_ERROR:
        return "Mount Error";
    case STORAGE_STATE_FS_ERROR:
        return "FS Error";
    case STORAGE_STATE_RW_ERROR:
        return "RW Error";
    case STORAGE_STATE_READY:
        return "Ready";
    case STORAGE_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static void storage_set_state(storage_state_t state)
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    changed = (s_state != state);
    s_state = state;
    if (changed) {
        storage_touch_revision_locked();
    }
    portEXIT_CRITICAL(&s_lock);

    if (changed) {
        ESP_LOGI(TAG, "state -> %s", storage_state_to_string(state));
    }
}

static void storage_set_event(const char *event_text)
{
    if (event_text == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_last_event, event_text, sizeof(s_last_event));
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
}

static void storage_set_error(esp_err_t err, const char *context)
{
    portENTER_CRITICAL(&s_lock);
    snprintf(s_last_error,
             sizeof(s_last_error),
             "%s: %s(0x%x)",
             context != nullptr ? context : "Error",
             esp_err_to_name(err),
             static_cast<unsigned>(err));
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "%s", s_last_error);
}

static void storage_set_error_text(const char *text)
{
    if (text == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_last_error, text, sizeof(s_last_error));
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "%s", text);
}

static void storage_clear_error(void)
{
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_last_error, "None", sizeof(s_last_error));
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
}

static bool storage_error_looks_like_no_card(esp_err_t err)
{
    return err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t storage_build_path(const char *path, char *out, size_t out_size)
{
    if (path == nullptr || out == nullptr || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 文件接口允许传入绝对路径，也允许传入相对项目目录的路径。
    // 例如 "TMP/A.TXT" 会展开为 /sdcard/EAITERM/TMP/A.TXT。
    // 当前保持 8.3 短文件名：这样不需要启用 FATFS LFN，可降低内存占用并提升嵌入式稳定性。
    // 如果后续确实需要长文件名，可在 menuconfig 中启用：
    // Component config -> FAT Filesystem support -> Long filename support，并建议使用 heap buffer。
    int written = 0;
    if (path[0] == '/') {
        written = snprintf(out, out_size, "%s", path);
    } else {
        written = snprintf(out, out_size, "%s/%s", STORAGE_ROOT_DIR, path);
    }
    if (written < 0 || static_cast<size_t>(written) >= out_size) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void storage_log_parent_state(const char *parent_path)
{
    if (parent_path == nullptr) {
        return;
    }

    struct stat parent_st = {};
    errno = 0;
    if (stat(parent_path, &parent_st) == 0) {
        ESP_LOGI(TAG,
                 "parent check: %s exists type=%s",
                 parent_path,
                 S_ISDIR(parent_st.st_mode) ? "dir" : "not-dir");
    } else {
        ESP_LOGW(TAG, "parent check: %s missing errno=%d (%s)", parent_path, errno, strerror(errno));
    }
}

static esp_err_t storage_verify_dir(const char *path)
{
    struct stat st = {};
    errno = 0;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "verify dir failed: %s errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    if (!S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "verify dir failed: %s exists but not dir", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "verify dir ok: %s", path);
    return ESP_OK;
}

static esp_err_t storage_ensure_dir(const char *path, bool allow_create)
{
    struct stat st = {};
    if (!allow_create) {
        ESP_LOGI(TAG, "ensure mount point: %s", path);
    } else {
        ESP_LOGI(TAG, "ensure dir: %s", path);
    }

    errno = 0;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "dir exists: %s", path);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "path exists but is not directory: %s", path);
        return ESP_FAIL;
    }

    if (errno != ENOENT) {
        ESP_LOGW(TAG, "stat failed: %s errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    if (!allow_create) {
        ESP_LOGW(TAG, "path missing but create disabled: %s errno=%d (%s)", path, errno, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "mkdir attempt: %s", path);
    errno = 0;
    if (mkdir(path, 0775) == 0) {
        ESP_LOGI(TAG, "mkdir ok: %s", path);
        return storage_verify_dir(path);
    }

    if (errno == EEXIST) {
        ESP_LOGI(TAG, "mkdir EEXIST, verify existing dir: %s", path);
        return storage_verify_dir(path);
    }
    ESP_LOGW(TAG, "mkdir failed: %s errno=%d (%s)", path, errno, strerror(errno));
    if (errno == ENOENT) {
        ESP_LOGW(TAG, "mkdir ENOENT: parent missing for %s", path);
        storage_log_parent_state(STORAGE_ROOT_DIR);
    }
    if (errno == EINVAL) {
        ESP_LOGW(TAG, "mkdir EINVAL: check FATFS LFN setting or keep every path component in 8.3 short-name form");
    }
    return ESP_FAIL;
}

static esp_err_t storage_refresh_capacity(void)
{
    FATFS *fs = nullptr;
    DWORD free_clusters = 0;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK || fs == nullptr) {
        ESP_LOGW(TAG, "f_getfree failed: %d", static_cast<int>(res));
        return ESP_FAIL;
    }

    const uint64_t sector_size = fs->ssize != 0 ? fs->ssize : 512;
    const uint64_t cluster_bytes = static_cast<uint64_t>(fs->csize) * sector_size;
    uint64_t total = static_cast<uint64_t>(fs->n_fatent - 2) * cluster_bytes;
    uint64_t free_b = static_cast<uint64_t>(free_clusters) * cluster_bytes;

    portENTER_CRITICAL(&s_lock);
    s_total_bytes = total;
    s_free_bytes = free_b;
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG,
             "capacity total=%llu free=%llu",
             static_cast<unsigned long long>(total),
             static_cast<unsigned long long>(free_b));
    return ESP_OK;
}

static esp_err_t storage_run_rw_test(void)
{
    ESP_LOGI(TAG, "storage test parent check before write");
    storage_log_parent_state(STORAGE_ROOT_DIR);
    storage_log_parent_state(STORAGE_TMP_DIR);
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_ROOT_DIR, true), TAG, "storage test project root missing");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_TMP_DIR, true), TAG, "storage test tmp dir missing");

    char timestamp[64] = {0};
    time_t now = time(nullptr);
    struct tm tm_now = {};
    if (now > 1700000000 && localtime_r(&now, &tm_now) != nullptr) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
    } else {
        snprintf(timestamp, sizeof(timestamp), "boot_us=%lld", static_cast<long long>(esp_timer_get_time()));
    }

    char content[256] = {0};
    snprintf(content,
             sizeof(content),
             "boot timestamp: %s\nstorage mounted: yes\ntotal bytes: %llu\nfree bytes: %llu\n",
             timestamp,
             static_cast<unsigned long long>(service_storage_get_total_bytes()),
             static_cast<unsigned long long>(service_storage_get_free_bytes()));

    esp_err_t err = service_storage_write_text_file(STORAGE_TEST_FILE, content);
    if (err != ESP_OK) {
        storage_set_error(err, "write STEST.TXT failed");
        return err;
    }

    char readback[256] = {0};
    err = service_storage_read_text_file(STORAGE_TEST_FILE, readback, sizeof(readback));
    if (err != ESP_OK) {
        storage_set_error(err, "read STEST.TXT failed");
        return err;
    }

    ESP_LOGI(TAG, "STEST.TXT readback ok, first line: %.48s", readback);
    storage_set_event("RW_TEST_OK");
    return ESP_OK;
}

} // namespace

/**
 * @brief 初始化 SD/TF 外部存储服务。
 *
 * 该函数会尝试挂载 TF 卡、创建项目目录并执行一次最小读写测试。
 * 如果没有插卡或挂载失败，函数仍返回 ESP_OK，让系统继续启动 UI/Wi-Fi/PWR/Audio。
 */
esp_err_t service_storage_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "storage init start");
    storage_set_state(STORAGE_STATE_INIT);
    storage_set_event("INIT");
    storage_clear_error();
    s_inited = true;

    esp_err_t err = service_storage_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "storage degraded: SD unavailable, system continues");
        return ESP_OK;
    }

    err = service_storage_create_project_dirs();
    if (err != ESP_OK) {
        storage_set_state(STORAGE_STATE_FS_ERROR);
        storage_set_error(err, "create project dirs failed");
        return ESP_OK;
    }

    err = storage_run_rw_test();
    if (err != ESP_OK) {
        storage_set_state(STORAGE_STATE_RW_ERROR);
        return ESP_OK;
    }

    storage_set_state(STORAGE_STATE_READY);
    storage_set_event("READY");
    ESP_LOGI(TAG, "storage ready");
    return ESP_OK;
}

/**
 * @brief 挂载 FAT32 TF 卡到 /sdcard。
 *
 * 当前 Waveshare ESP32-S3-Touch-LCD-3.49 旧工程使用 SDMMC 1-bit：
 * CMD=GPIO39、D0=GPIO40、CLK=GPIO41。本层只封装挂载，不把缺卡视为致命错误。
 */
esp_err_t service_storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    storage_set_state(STORAGE_STATE_MOUNTING);
    storage_set_event("MOUNTING");
    ESP_LOGI(TAG, "card_detect: no dedicated CD pin configured; detect by SDMMC init result");
    ESP_LOGI(TAG, "interface type: SDMMC 1-bit");
    ESP_LOGI(TAG, "SDSPI pins: MOSI=N/A MISO=N/A SCK=N/A CS=N/A");
    ESP_LOGI(TAG, "SDMMC pins: CMD=%d D0=%d CLK=%d D1=N/A D2=N/A D3=N/A", kSdmmcCmdPin, kSdmmcD0Pin, kSdmmcClkPin);
    ESP_LOGI(TAG, "IO expander for SD: no; TCA9554 EXIO6/EXIO7 untouched");
    ESP_LOGI(TAG,
             "mount SDMMC 1-bit card at %s (cmd=%d d0=%d clk=%d)",
             STORAGE_MOUNT_POINT,
             kSdmmcCmdPin,
             kSdmmcD0Pin,
             kSdmmcClkPin);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 先使用默认频率提高兼容性；后续确认卡和走线稳定后再考虑切回 high speed。
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = kSdmmcClkPin;
    slot_config.cmd = kSdmmcCmdPin;
    slot_config.d0 = kSdmmcD0Pin;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;
    slot_config.cd = SDMMC_SLOT_NO_CD;
    slot_config.wp = SDMMC_SLOT_NO_WP;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(STORAGE_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    ESP_LOGI(TAG, "mount returned: %s(0x%x)", esp_err_to_name(err), static_cast<unsigned>(err));
    if (err != ESP_OK) {
        s_card = nullptr;
        s_mounted = false;
        if (storage_error_looks_like_no_card(err)) {
            storage_set_error(err, "No Card or no SDMMC response");
            storage_set_event("NO_CARD");
            storage_set_state(STORAGE_STATE_NO_CARD);
            ESP_LOGW(TAG, "No TF card detected, or card did not answer SDMMC init");
        } else if (err == ESP_FAIL) {
            storage_set_error_text("Mount Error: ESP_FAIL; card init may have passed, FAT mount may have failed. Check FAT32 format; exFAT/large cards may need FAT32 reformat.");
            storage_set_event("MOUNT_ERROR");
            storage_set_state(STORAGE_STATE_MOUNT_ERROR);
            ESP_LOGW(TAG, "If the card is inserted, verify FAT32 format and try a <=32GB TF card first");
        } else if (err == ESP_ERR_INVALID_STATE) {
            storage_set_error(err, "Mount Error invalid state");
            storage_set_event("MOUNT_ERROR");
            storage_set_state(STORAGE_STATE_MOUNT_ERROR);
        } else {
            storage_set_error(err, "Init Error SDMMC communication failed");
            storage_set_event("INIT_ERROR");
            storage_set_state(STORAGE_STATE_INIT_ERROR);
            ESP_LOGW(TAG, "If card is inserted, check SDMMC pins, socket contact, and card compatibility");
        }
        return err;
    }

    s_mounted = true;
    storage_set_state(STORAGE_STATE_MOUNTED);
    storage_set_event("MOUNT_SUCCESS");
    ESP_LOGI(TAG, "mount success");
    sdmmc_card_print_info(stdout, s_card);

    err = storage_refresh_capacity();
    if (err != ESP_OK) {
        storage_set_error(err, "capacity refresh failed");
        return err;
    }
    return ESP_OK;
}

esp_err_t service_storage_unmount(void)
{
    if (!s_mounted || s_card == nullptr) {
        return ESP_OK;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(STORAGE_MOUNT_POINT, s_card);
    if (err != ESP_OK) {
        storage_set_error(err, "unmount failed");
        return err;
    }
    s_card = nullptr;
    s_mounted = false;
    portENTER_CRITICAL(&s_lock);
    s_total_bytes = 0;
    s_free_bytes = 0;
    storage_touch_revision_locked();
    portEXIT_CRITICAL(&s_lock);
    storage_set_state(STORAGE_STATE_INIT);
    storage_set_event("UNMOUNTED");
    ESP_LOGI(TAG, "unmounted");
    return ESP_OK;
}

bool service_storage_is_mounted(void)
{
    return s_mounted;
}

storage_state_t service_storage_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    storage_state_t state = s_state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

const char *service_storage_get_state_string(void)
{
    return storage_state_to_string(service_storage_get_state());
}

const char *service_storage_get_last_event(void)
{
    static char event_buf[96] = {0};
    portENTER_CRITICAL(&s_lock);
    strlcpy(event_buf, s_last_event, sizeof(event_buf));
    portEXIT_CRITICAL(&s_lock);
    return event_buf;
}

uint64_t service_storage_get_total_bytes(void)
{
    portENTER_CRITICAL(&s_lock);
    uint64_t value = s_total_bytes;
    portEXIT_CRITICAL(&s_lock);
    return value;
}

uint64_t service_storage_get_free_bytes(void)
{
    portENTER_CRITICAL(&s_lock);
    uint64_t value = s_free_bytes;
    portEXIT_CRITICAL(&s_lock);
    return value;
}

const char *service_storage_get_base_path(void)
{
    return STORAGE_ROOT_DIR;
}

/**
 * @brief 创建项目标准目录。
 *
 * 目录统一放在 /sdcard/EAITERM 下，后续音频缓存、日志落盘、
 * 配置备份和 OTA 包缓存都从这里分区使用。创建失败只影响存储功能。
 *
 * 当前目录全部使用 8.3 短文件名，避免依赖 CONFIG_FATFS_LONG_FILENAMES。
 * 这样可以减少 FatFS 长文件名缓冲带来的 RAM 占用，更适合嵌入式长期运行。
 */
esp_err_t service_storage_create_project_dirs(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_MOUNT_POINT, false), TAG, "mount point check failed");

    ESP_LOGI(TAG, "ensure project root: %s", STORAGE_ROOT_DIR);
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_ROOT_DIR, true), TAG, "create root dir failed");
    ESP_RETURN_ON_ERROR(storage_verify_dir(STORAGE_ROOT_DIR), TAG, "verify root dir failed");

    ESP_LOGI(TAG, "verify project root before LOGS: %s", STORAGE_ROOT_DIR);
    ESP_RETURN_ON_ERROR(storage_verify_dir(STORAGE_ROOT_DIR), TAG, "root dir missing before LOGS");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_LOG_DIR, true), TAG, "create LOGS dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_AUDIO_DIR, true), TAG, "create AUDIO dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_ASR_DIR, true), TAG, "create ASR dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_TTS_DIR, true), TAG, "create TTS dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_CFG_DIR, true), TAG, "create CFG dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_OTA_DIR, true), TAG, "create OTA dir failed");
    ESP_RETURN_ON_ERROR(storage_ensure_dir(STORAGE_TMP_DIR, true), TAG, "create TMP dir failed");

    storage_set_event("DIRS_READY");
    ESP_LOGI(TAG, "project dirs ready under %s", STORAGE_ROOT_DIR);
    return ESP_OK;
}

esp_err_t service_storage_write_text_file(const char *path, const char *text)
{
    if (!s_mounted || text == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[kPathBufSize] = {0};
    ESP_RETURN_ON_ERROR(storage_build_path(path, full_path, sizeof(full_path)), TAG, "build path failed");

    FILE *fp = fopen(full_path, "w");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "open write failed: %s errno=%d (%s)", full_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    fputs(text, fp);
    fclose(fp);
    ESP_LOGI(TAG, "write text file: %s", full_path);
    return ESP_OK;
}

esp_err_t service_storage_append_text_file(const char *path, const char *text)
{
    if (!s_mounted || text == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[kPathBufSize] = {0};
    ESP_RETURN_ON_ERROR(storage_build_path(path, full_path, sizeof(full_path)), TAG, "build path failed");

    FILE *fp = fopen(full_path, "a");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "open append failed: %s errno=%d (%s)", full_path, errno, strerror(errno));
        return ESP_FAIL;
    }
    fputs(text, fp);
    fclose(fp);
    return ESP_OK;
}

esp_err_t service_storage_read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    if (!s_mounted || buffer == nullptr || buffer_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[kPathBufSize] = {0};
    ESP_RETURN_ON_ERROR(storage_build_path(path, full_path, sizeof(full_path)), TAG, "build path failed");

    FILE *fp = fopen(full_path, "r");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "open read failed: %s errno=%d (%s)", full_path, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t read_len = fread(buffer, 1, buffer_size - 1, fp);
    buffer[read_len] = '\0';
    fclose(fp);
    ESP_LOGI(TAG, "read text file: %s bytes=%u", full_path, static_cast<unsigned>(read_len));
    return ESP_OK;
}

esp_err_t service_storage_write_binary_file(const char *path, const void *data, size_t len)
{
    if (!s_mounted || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[kPathBufSize] = {0};
    ESP_RETURN_ON_ERROR(storage_build_path(path, full_path, sizeof(full_path)), TAG, "build path failed");

    FILE *fp = fopen(full_path, "wb");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "open binary write failed: %s errno=%d (%s)", full_path, errno, strerror(errno));
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    if (written != len) {
        ESP_LOGW(TAG, "binary write short: %s written=%u expected=%u",
                 full_path,
                 static_cast<unsigned>(written),
                 static_cast<unsigned>(len));
        return ESP_FAIL;
    }
    return ESP_OK;
}

const char *service_storage_get_last_error(void)
{
    static char error_buf[128] = {0};
    portENTER_CRITICAL(&s_lock);
    strlcpy(error_buf, s_last_error, sizeof(error_buf));
    portEXIT_CRITICAL(&s_lock);
    return error_buf;
}

uint32_t service_storage_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}
