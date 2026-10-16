#include "device_config.h"

#include "nvs.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("device_build_defaults.h")
#include "device_build_defaults.h"
#endif
#endif

#ifndef DSH_DEFAULT_WIFI_SSID
#define DSH_DEFAULT_WIFI_SSID ""
#endif
#ifndef DSH_DEFAULT_WIFI_PASSWORD
#define DSH_DEFAULT_WIFI_PASSWORD ""
#endif
#ifndef DSH_DEFAULT_GATEWAY_HOST
#define DSH_DEFAULT_GATEWAY_HOST ""
#endif
#ifndef DSH_DEFAULT_GATEWAY_PORT
#define DSH_DEFAULT_GATEWAY_PORT 8883
#endif
#ifndef DSH_DEFAULT_TENANT_ID
#define DSH_DEFAULT_TENANT_ID "personal"
#endif
#ifndef DSH_DEFAULT_DEVICE_ID
#define DSH_DEFAULT_DEVICE_ID ""
#endif
#ifndef DSH_DEFAULT_DEVICE_CREDENTIAL
#define DSH_DEFAULT_DEVICE_CREDENTIAL ""
#endif
#ifndef DSH_DEFAULT_BRIDGE_HOST
#define DSH_DEFAULT_BRIDGE_HOST ""
#endif
#ifndef DSH_DEFAULT_BRIDGE_PORT
#define DSH_DEFAULT_BRIDGE_PORT 3082
#endif
#ifndef DSH_DEFAULT_BRIDGE_TOKEN
#define DSH_DEFAULT_BRIDGE_TOKEN ""
#endif
#ifndef DSH_DEFAULT_QQ_EMAIL
#define DSH_DEFAULT_QQ_EMAIL ""
#endif
#ifndef DSH_DEFAULT_QQ_APP_PASSWORD
#define DSH_DEFAULT_QQ_APP_PASSWORD ""
#endif
#ifndef DSH_DEFAULT_GMAIL_EMAIL
#define DSH_DEFAULT_GMAIL_EMAIL ""
#endif
#ifndef DSH_DEFAULT_GMAIL_APP_PASSWORD
#define DSH_DEFAULT_GMAIL_APP_PASSWORD ""
#endif
#ifndef DSH_DEFAULT_SUB2API_URL
#define DSH_DEFAULT_SUB2API_URL "https://api.suhm.top/api/v1/admin/accounts/1/usage?source=active&force=true&timezone=Asia%2FShanghai"
#endif
#ifndef DSH_DEFAULT_SUB2API_RESET_URL
#define DSH_DEFAULT_SUB2API_RESET_URL "https://api.suhm.top/api/v1/admin/openai/accounts/1/quota/refresh"
#endif
#ifndef DSH_DEFAULT_SUB2API_TOKEN
#define DSH_DEFAULT_SUB2API_TOKEN ""
#endif

static const char *TAG_NAMESPACE = "dsh-config";

static void read_string(nvs_handle_t handle, const char *key, char *out, size_t capacity)
{
    size_t length = capacity;
    if (nvs_get_str(handle, key, out, &length) != ESP_OK) out[0] = '\0';
    out[capacity - 1] = '\0';
}

static void copy_default(char *out, size_t capacity, const char *value)
{
    if (!out || capacity == 0 || out[0] != '\0' || !value || value[0] == '\0') return;
    size_t length = strlen(value);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

static void apply_service_defaults(device_service_config_t *config)
{
    copy_default(config->gateway_host, sizeof(config->gateway_host), DSH_DEFAULT_GATEWAY_HOST);
    if (config->gateway_port == 8883 && DSH_DEFAULT_GATEWAY_PORT > 0) {
        config->gateway_port = (uint16_t)DSH_DEFAULT_GATEWAY_PORT;
    }
    copy_default(config->tenant_id, sizeof(config->tenant_id), DSH_DEFAULT_TENANT_ID);
    copy_default(config->device_id, sizeof(config->device_id), DSH_DEFAULT_DEVICE_ID);
    copy_default(config->device_credential, sizeof(config->device_credential), DSH_DEFAULT_DEVICE_CREDENTIAL);
    copy_default(config->bridge_host, sizeof(config->bridge_host), DSH_DEFAULT_BRIDGE_HOST);
    if (config->bridge_port == 3082 && DSH_DEFAULT_BRIDGE_PORT > 0) {
        config->bridge_port = (uint16_t)DSH_DEFAULT_BRIDGE_PORT;
    }
    copy_default(config->bridge_token, sizeof(config->bridge_token), DSH_DEFAULT_BRIDGE_TOKEN);
    copy_default(config->qq_email, sizeof(config->qq_email), DSH_DEFAULT_QQ_EMAIL);
    copy_default(config->qq_app_password, sizeof(config->qq_app_password), DSH_DEFAULT_QQ_APP_PASSWORD);
    copy_default(config->gmail_email, sizeof(config->gmail_email), DSH_DEFAULT_GMAIL_EMAIL);
    copy_default(config->gmail_app_password, sizeof(config->gmail_app_password), DSH_DEFAULT_GMAIL_APP_PASSWORD);
    copy_default(config->sub2api_url, sizeof(config->sub2api_url), DSH_DEFAULT_SUB2API_URL);
    copy_default(config->sub2api_reset_url, sizeof(config->sub2api_reset_url), DSH_DEFAULT_SUB2API_RESET_URL);
    copy_default(config->sub2api_token, sizeof(config->sub2api_token), DSH_DEFAULT_SUB2API_TOKEN);
}

bool device_config_load(device_service_config_t *config)
{
    if (!config) return false;
    memset(config, 0, sizeof(*config));
    config->gateway_port = 8883;
    config->bridge_port = 3082;

    nvs_handle_t handle = 0;
    if (nvs_open(TAG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        apply_service_defaults(config);
        return true;
    }

    read_string(handle, "gateway_host", config->gateway_host, sizeof(config->gateway_host));
    uint16_t gateway_port = 0;
    if (nvs_get_u16(handle, "gateway_port", &gateway_port) == ESP_OK && gateway_port > 0) config->gateway_port = gateway_port;
    read_string(handle, "tenant_id", config->tenant_id, sizeof(config->tenant_id));
    read_string(handle, "device_id", config->device_id, sizeof(config->device_id));
    read_string(handle, "device_cred", config->device_credential, sizeof(config->device_credential));
    read_string(handle, "bridge_host", config->bridge_host, sizeof(config->bridge_host));
    uint16_t port = 0;
    if (nvs_get_u16(handle, "bridge_port", &port) == ESP_OK && port > 0) config->bridge_port = port;
    read_string(handle, "bridge_token", config->bridge_token, sizeof(config->bridge_token));
    read_string(handle, "qq_email", config->qq_email, sizeof(config->qq_email));
    read_string(handle, "qq_app_pw", config->qq_app_password, sizeof(config->qq_app_password));
    read_string(handle, "gmail_email", config->gmail_email, sizeof(config->gmail_email));
    read_string(handle, "gmail_app_pw", config->gmail_app_password, sizeof(config->gmail_app_password));
    read_string(handle, "sub2api_url", config->sub2api_url, sizeof(config->sub2api_url));
    read_string(handle, "sub2api_reset", config->sub2api_reset_url, sizeof(config->sub2api_reset_url));
    read_string(handle, "sub2api_token", config->sub2api_token, sizeof(config->sub2api_token));
    nvs_close(handle);
    apply_service_defaults(config);
    return true;
}

void device_config_apply_wifi_defaults(char *ssid, size_t ssid_capacity,
                                       char *password, size_t password_capacity)
{
    copy_default(ssid, ssid_capacity, DSH_DEFAULT_WIFI_SSID);
    copy_default(password, password_capacity, DSH_DEFAULT_WIFI_PASSWORD);
}

esp_err_t device_config_save(const device_service_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(TAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "gateway_host", config->gateway_host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "gateway_port", config->gateway_port > 0 ? config->gateway_port : 8883);
    if (err == ESP_OK) err = nvs_set_str(handle, "tenant_id", config->tenant_id);
    if (err == ESP_OK) err = nvs_set_str(handle, "device_id", config->device_id);
    if (err == ESP_OK) err = nvs_set_str(handle, "device_cred", config->device_credential);
    if (err == ESP_OK) err = nvs_set_str(handle, "bridge_host", config->bridge_host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "bridge_port", config->bridge_port > 0 ? config->bridge_port : 3082);
    if (err == ESP_OK) err = nvs_set_str(handle, "bridge_token", config->bridge_token);
    if (err == ESP_OK) err = nvs_set_str(handle, "qq_email", config->qq_email);
    if (err == ESP_OK) err = nvs_set_str(handle, "qq_app_pw", config->qq_app_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "gmail_email", config->gmail_email);
    if (err == ESP_OK) err = nvs_set_str(handle, "gmail_app_pw", config->gmail_app_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "sub2api_url", config->sub2api_url);
    if (err == ESP_OK) err = nvs_set_str(handle, "sub2api_reset", config->sub2api_reset_url);
    if (err == ESP_OK) err = nvs_set_str(handle, "sub2api_token", config->sub2api_token);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
