#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_STATE_IDLE = 0,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_GOT_IP,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_ERROR,
} network_state_t;

esp_err_t service_network_init(void);
network_state_t service_network_get_state(void);
const char *service_network_get_state_str(network_state_t state);
const char *service_network_get_state_string(void);
void service_network_get_ip(char *ip_buf, size_t ip_buf_size);
const char *service_network_get_ip_string(void);
const char *service_network_get_ssid(void);
void service_network_get_last_event(char *event_buf, size_t event_buf_size);
bool service_network_is_connected(void);
uint32_t service_network_get_revision(void);

#ifdef __cplusplus
}
#endif
