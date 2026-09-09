#include "device_config.h"

#include "nvs.h"

#include <string.h>

static const char *TAG_NAMESPACE = "dsh-config";

static void read_string(nvs_handle_t handle, const char *key, char *out, size_t capacity)
{
    size_t length = capacity;
    if (nvs_get_str(handle, key, out, &length) != ESP_OK) out[0] = '\0';
    out[capacity - 1] = '\0';
}

bool device_config_load(device_service_config_t *config)
{
    if (!config) return false;
    memset(config, 0, sizeof(*config));
    config->bridge_port = 3082;

    nvs_handle_t handle = 0;
    if (nvs_open(TAG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return true;

    read_string(handle, "bridge_host", config->bridge_host, sizeof(config->bridge_host));
    uint16_t port = 0;
    if (nvs_get_u16(handle, "bridge_port", &port) == ESP_OK && port > 0) config->bridge_port = port;
    read_string(handle, "bridge_token", config->bridge_token, sizeof(config->bridge_token));
    read_string(handle, "qq_email", config->qq_email, sizeof(config->qq_email));
    read_string(handle, "qq_app_pw", config->qq_app_password, sizeof(config->qq_app_password));
    read_string(handle, "gmail_email", config->gmail_email, sizeof(config->gmail_email));
    read_string(handle, "gmail_app_pw", config->gmail_app_password, sizeof(config->gmail_app_password));
    read_string(handle, "sub2api_url", config->sub2api_url, sizeof(config->sub2api_url));
    read_string(handle, "sub2api_token", config->sub2api_token, sizeof(config->sub2api_token));
    nvs_close(handle);
    return true;
}

esp_err_t device_config_save(const device_service_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(TAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "bridge_host", config->bridge_host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "bridge_port", config->bridge_port > 0 ? config->bridge_port : 3082);
    if (err == ESP_OK) err = nvs_set_str(handle, "bridge_token", config->bridge_token);
    if (err == ESP_OK) err = nvs_set_str(handle, "qq_email", config->qq_email);
    if (err == ESP_OK) err = nvs_set_str(handle, "qq_app_pw", config->qq_app_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "gmail_email", config->gmail_email);
    if (err == ESP_OK) err = nvs_set_str(handle, "gmail_app_pw", config->gmail_app_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "sub2api_url", config->sub2api_url);
    if (err == ESP_OK) err = nvs_set_str(handle, "sub2api_token", config->sub2api_token);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
