#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_CONFIG_HOST_LEN 96
#define DEVICE_CONFIG_TOKEN_LEN 128
#define DEVICE_CONFIG_EMAIL_LEN 128
#define DEVICE_CONFIG_PASSWORD_LEN 128
#define DEVICE_CONFIG_URL_LEN 192
#define DEVICE_CONFIG_SUB2API_TOKEN_LEN 256

typedef struct {
    char bridge_host[DEVICE_CONFIG_HOST_LEN];
    uint16_t bridge_port;
    char bridge_token[DEVICE_CONFIG_TOKEN_LEN];
    char qq_email[DEVICE_CONFIG_EMAIL_LEN];
    char qq_app_password[DEVICE_CONFIG_PASSWORD_LEN];
    char gmail_email[DEVICE_CONFIG_EMAIL_LEN];
    char gmail_app_password[DEVICE_CONFIG_PASSWORD_LEN];
    char sub2api_url[DEVICE_CONFIG_URL_LEN];
    char sub2api_token[DEVICE_CONFIG_SUB2API_TOKEN_LEN];
} device_service_config_t;

bool device_config_load(device_service_config_t *config);
esp_err_t device_config_save(const device_service_config_t *config);

#ifdef __cplusplus
}
#endif
