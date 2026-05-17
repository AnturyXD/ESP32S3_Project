#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_STATE_UNINIT = 0,
    STORAGE_STATE_INIT,
    STORAGE_STATE_NO_CARD,
    STORAGE_STATE_MOUNTING,
    STORAGE_STATE_MOUNTED,
    STORAGE_STATE_INIT_ERROR,
    STORAGE_STATE_MOUNT_ERROR,
    STORAGE_STATE_FS_ERROR,
    STORAGE_STATE_RW_ERROR,
    STORAGE_STATE_READY,
    STORAGE_STATE_ERROR,
} storage_state_t;

/**
 * @brief 初始化外部存储服务。
 *
 * 会尝试挂载 TF 卡、创建项目目录并执行一次读写自检。
 * 没有插卡时不会阻塞系统启动，状态会降级为 STORAGE_STATE_NO_CARD。
 */
esp_err_t service_storage_init(void);

/**
 * @brief 挂载 TF/SD 卡到 /sdcard。
 *
 * 当前硬件按照旧 sdcard_bsp 与板级资料使用 SDMMC 1-bit 接线。
 * 如果未来更换板卡，必须先核对官方示例或 board_cfg 后再修改引脚。
 */
esp_err_t service_storage_mount(void);
esp_err_t service_storage_unmount(void);
bool service_storage_is_mounted(void);
storage_state_t service_storage_get_state(void);
const char *service_storage_get_state_string(void);
const char *service_storage_get_last_event(void);
uint64_t service_storage_get_total_bytes(void);
uint64_t service_storage_get_free_bytes(void);
const char *service_storage_get_base_path(void);
esp_err_t service_storage_create_project_dirs(void);

/**
 * @brief 写入文本文件。
 *
 * path 可以传绝对路径（例如 /sdcard/EAITERM/TMP/A.TXT），
 * 也可以传相对项目目录的路径（例如 TMP/A.TXT）。
 * 当前默认使用 8.3 短文件名，避免启用 FatFS 长文件名以节省 RAM。
 */
esp_err_t service_storage_write_text_file(const char *path, const char *text);
esp_err_t service_storage_append_text_file(const char *path, const char *text);
esp_err_t service_storage_read_text_file(const char *path, char *buffer, size_t buffer_size);
esp_err_t service_storage_write_binary_file(const char *path, const void *data, size_t len);
const char *service_storage_get_last_error(void);
uint32_t service_storage_get_revision(void);

#ifdef __cplusplus
}
#endif
