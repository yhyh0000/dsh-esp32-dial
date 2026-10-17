#include "device_provisioning.h"

#include "device_config.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "provisioning";
static httpd_handle_t s_http_server = NULL;
static bool s_ap_active = false;

static void copy_text(char *out, size_t capacity, const char *value)
{
    if (!out || capacity == 0) return;
    if (!value) value = "";
    size_t length = strlen(value);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void url_decode(const char *input, size_t length, char *output, size_t capacity)
{
    size_t written = 0;
    for (size_t i = 0; i < length && written + 1 < capacity; ++i) {
        if (input[i] == '+' && written + 1 < capacity) {
            output[written++] = ' ';
        } else if (input[i] == '%' && i + 2 < length) {
            const int high = hex_value(input[i + 1]);
            const int low = hex_value(input[i + 2]);
            if (high >= 0 && low >= 0) {
                output[written++] = (char)((high << 4) | low);
                i += 2;
            } else {
                output[written++] = input[i];
            }
        } else {
            output[written++] = input[i];
        }
    }
    output[written] = '\0';
}

static bool form_value(const char *body, const char *key, char *output, size_t capacity)
{
    if (!body || !key || !output || capacity == 0) return false;
    const size_t key_length = strlen(key);
    const char *cursor = body;
    while (*cursor) {
        const char *equals = strchr(cursor, '=');
        const char *ampersand = strchr(cursor, '&');
        const char *end = ampersand ? ampersand : cursor + strlen(cursor);
        if (equals && equals < end && (size_t)(equals - cursor) == key_length &&
            strncmp(cursor, key, key_length) == 0) {
            url_decode(equals + 1, (size_t)(end - equals - 1), output, capacity);
            return true;
        }
        if (!ampersand) break;
        cursor = ampersand + 1;
    }
    return false;
}

static void html_escape(const char *input, char *output, size_t capacity)
{
    size_t written = 0;
    if (!input) input = "";
    for (size_t i = 0; input[i] && written + 1 < capacity; ++i) {
        const char *replacement = NULL;
        switch (input[i]) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: output[written++] = input[i]; break;
        }
        if (replacement) {
            const size_t length = strlen(replacement);
            if (written + length >= capacity) break;
            memcpy(output + written, replacement, length);
            written += length;
        }
    }
    output[written] = '\0';
}

static esp_err_t send_text(httpd_req_t *request, const char *type, const char *body, bool ok)
{
    httpd_resp_set_status(request, ok ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(request, type);
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_page_handler(httpd_req_t *request)
{
    device_service_config_t config;
    device_config_load(&config);
    wifi_config_t wifi = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wifi);
    device_config_apply_wifi_defaults((char *)wifi.sta.ssid, sizeof(wifi.sta.ssid),
                                      (char *)wifi.sta.password, sizeof(wifi.sta.password));

    char ssid[96], gateway[192], tenant[128], device_id[160], bridge[192];
    html_escape((const char *)wifi.sta.ssid, ssid, sizeof(ssid));
    html_escape(config.gateway_host, gateway, sizeof(gateway));
    html_escape(config.bridge_host, bridge, sizeof(bridge));
    html_escape(config.tenant_id, tenant, sizeof(tenant));
    html_escape(config.device_id, device_id, sizeof(device_id));

    const size_t page_capacity = 16384;
    char *page = malloc(page_capacity);
    if (!page) return send_text(request, "text/plain; charset=utf-8", "内存不足，请重试", false);
    snprintf(page, page_capacity,
        "<!doctype html><html lang='zh-CN'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Agent Satellite 设备设置</title><style>"
        "*{box-sizing:border-box}body{margin:0;background:#101612;color:#f4f3ec;font:15px system-ui,-apple-system,Segoe UI,sans-serif}"
        "main{max-width:720px;margin:0 auto;padding:24px 18px 42px}header{padding:8px 0 18px}h1{font-size:27px;letter-spacing:.2px;margin:0 0 7px}h2{font-size:16px;margin:0 0 12px;color:#c4ed72}p{color:#a1aaa1;line-height:1.55;margin:7px 0}"
        ".eyebrow{color:#87988a;font-size:11px;font-weight:700;letter-spacing:1.5px}.status{display:grid;grid-template-columns:1fr 1fr;gap:9px;margin:14px 0 6px}.status-item{border:1px solid #2b352e;border-radius:8px;background:#151d18;padding:10px 12px}.status-item span{display:block;color:#87988a;font-size:12px;margin-bottom:4px}.status-item b{font-size:14px;font-weight:600}"
        "form{display:block}section{border-top:1px solid #2b352e;padding:20px 0 4px}label{display:block;margin:13px 0 5px;color:#dbe3d7}input{width:100%%;padding:12px;border:1px solid #3a463d;border-radius:7px;background:#18221c;color:#fff;font-size:16px}input:focus{outline:2px solid #789e48;outline-offset:1px;border-color:#c4ed72}"
        ".row{display:grid;grid-template-columns:1fr 130px;gap:10px}.hint{font-size:13px;color:#7f8e82}.section-note{font-size:13px;margin:-2px 0 8px}.actions{padding-top:18px}button{width:100%%;padding:14px;border:0;border-radius:7px;background:#c4ed72;color:#15200f;font-weight:700;font-size:16px;cursor:pointer}"
        "code{color:#c4ed72}a{color:#c4ed72}@media(max-width:480px){.row{grid-template-columns:1fr 100px}.status{grid-template-columns:1fr}main{padding-left:15px;padding-right:15px}}"
        "</style><main><header><div class='eyebrow'>AGENT SATELLITE / DEVICE SETUP</div><h1>设备设置</h1><p>通过 Wi‑Fi 连接 Agent Satellite Gateway。设备只保存自己的身份与配对凭据，不保存 DSH、Codex、邮箱或主机密码。</p><div class='status'><div class='status-item'><span>配置热点</span><b><code>CODEX-DIAL-SETUP</code></b></div><div class='status-item'><span>配置地址</span><b><code>192.168.4.1</code></b></div></div></header>"
        "<form method='post' action='/save' autocomplete='off'><section><h2>1 · Wi‑Fi 网络</h2>"
        "<label>Wi‑Fi 名称<input name='wifi_ssid' value='%s' maxlength='32' required></label>"
        "<label>Wi‑Fi 密码<input name='wifi_password' type='password' maxlength='64' autocomplete='new-password' placeholder='首次配置必填；已配置可留空'></label>"
        "</section><section><h2>2 · Agent Gateway</h2><p class='section-note'>设备与所有 DSH/Codex 主机均通过网关路由通信；不需要填写电脑 IP，也不使用蓝牙。</p>"
        "<div class='row'><label>Gateway 域名<input name='gateway_host' value='%s' maxlength='95' placeholder='例如 mqtt.example.com'></label>"
        "<label>MQTTS 端口<input name='gateway_port' type='number' value='%u' min='1' max='65535'></label></div>"
        "<label>Tenant / 空间 ID<input name='tenant_id' value='%s' maxlength='63' placeholder='例如 personal'></label>"
        "<label>设备 ID<input name='device_id' value='%s' maxlength='79' placeholder='例如 badge-185b-office'></label>"
        "<label>配对凭据<input name='device_credential' type='password' maxlength='127' autocomplete='new-password' placeholder='扫码或从 Gateway 控制台获取；已配置可留空'></label>"
        "</section><section><h2>3 · 本地 DSH Bridge（当前固件必填）</h2><p class='section-note'>DSH Guard 当前通过已验证的局域网 WebSocket Bridge 工作。Gateway/MQTTS 是下一阶段的远程路由通道；在其数据面完成前，本栏用于真实 DSH 状态和审批。</p>"
        "<div class='row'><label>Bridge 主机/IP<input name='bridge_host' value='%s' maxlength='95' placeholder='例如 192.168.1.10'></label>"
        "<label>端口<input name='bridge_port' type='number' value='%u' min='1' max='65535'></label></div>"
        "<label>设备 Token<input name='bridge_token' type='password' maxlength='127' autocomplete='new-password' placeholder='运行 node bridge.js 后显示；已配置可留空'></label>"
        "</section><div class='actions'><button type='submit'>保存并连接 DSH Guard</button><p class='hint'>当前 DSH Guard 使用本地 Bridge 显示 DSH 状态和审批。Gateway 字段保留给后续远程 MQTTS 路由，二者不会共享 DSH 凭据。</p></div></form>"
        "<p class='hint'>操作：连接热点 <code>CODEX-DIAL-SETUP</code>（密码 <code>codexsetup</code>），打开 <code>http://192.168.4.1/settings</code>。</p></main></html>",
        ssid, gateway, config.gateway_port, tenant, device_id, bridge, config.bridge_port);
    esp_err_t result = send_text(request, "text/html; charset=utf-8", page, true);
    free(page);
    return result;
}

static esp_err_t read_request_body(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= capacity) return ESP_ERR_INVALID_SIZE;
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += (size_t)count;
    }
    body[received] = '\0';
    return ESP_OK;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1800));
    esp_restart();
}

static esp_err_t save_handler(httpd_req_t *request)
{
    char body[4096];
    if (read_request_body(request, body, sizeof(body)) != ESP_OK) {
        return send_text(request, "text/plain; charset=utf-8", "请求太大或读取失败", false);
    }

    device_service_config_t config;
    device_config_load(&config);
    char value[DEVICE_CONFIG_SUB2API_TOKEN_LEN];
    if (form_value(body, "gateway_host", value, sizeof(value))) copy_text(config.gateway_host, sizeof(config.gateway_host), value);
    if (form_value(body, "bridge_host", value, sizeof(value))) copy_text(config.bridge_host, sizeof(config.bridge_host), value);
    if (form_value(body, "bridge_token", value, sizeof(value)) && value[0]) copy_text(config.bridge_token, sizeof(config.bridge_token), value);
    if (form_value(body, "tenant_id", value, sizeof(value))) copy_text(config.tenant_id, sizeof(config.tenant_id), value);
    if (form_value(body, "device_id", value, sizeof(value))) copy_text(config.device_id, sizeof(config.device_id), value);
    if (form_value(body, "device_credential", value, sizeof(value)) && value[0]) copy_text(config.device_credential, sizeof(config.device_credential), value);
    if (form_value(body, "gateway_port", value, sizeof(value))) {
        const unsigned long port = strtoul(value, NULL, 10);
        if (port > 0 && port <= 65535) config.gateway_port = (uint16_t)port;
    }
    if (form_value(body, "bridge_port", value, sizeof(value))) {
        const unsigned long port = strtoul(value, NULL, 10);
        if (port > 0 && port <= 65535) config.bridge_port = (uint16_t)port;
    }

    wifi_config_t wifi = {0};
    esp_wifi_get_config(WIFI_IF_STA, &wifi);
    bool has_ssid = wifi.sta.ssid[0] != '\0';
    if (form_value(body, "wifi_ssid", value, sizeof(value)) && value[0]) {
        copy_text((char *)wifi.sta.ssid, sizeof(wifi.sta.ssid), value);
        has_ssid = true;
    }
    if (form_value(body, "wifi_password", value, sizeof(value)) && value[0]) {
        copy_text((char *)wifi.sta.password, sizeof(wifi.sta.password), value);
    }
    if (!has_ssid) return send_text(request, "text/plain; charset=utf-8", "请填写 Wi‑Fi 名称", false);

    if (device_config_save(&config) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wifi) != ESP_OK) {
        return send_text(request, "text/plain; charset=utf-8", "保存失败，请重试", false);
    }
    const char *response = "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><body style='font:18px system-ui;padding:28px;background:#101612;color:#f4f3ec'><h2 style='color:#c4ed72'>设置已保存</h2><p>Wi‑Fi 与 Agent Gateway 身份已写入设备。设备将在几秒后重启并连接 Gateway；随后在 Gateway 控制台选择 Host、Agent 与 Session。</p></body>";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_task, "config_restart", 2048, NULL, 2, NULL);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    char body[256];
    char ip[16] = "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
        }
    }
    snprintf(body, sizeof(body), "{\"ap\":%s,\"url\":\"http://192.168.4.1/settings\",\"staIp\":\"%s\"}", s_ap_active ? "true" : "false", ip);
    return send_text(request, "application/json", body, true);
}

void device_provisioning_start_http(void)
{
    if (s_http_server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    if (httpd_start(&s_http_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "could not start configuration server");
        return;
    }
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = config_page_handler, .user_ctx = NULL};
    const httpd_uri_t settings = {.uri = "/settings", .method = HTTP_GET, .handler = config_page_handler, .user_ctx = NULL};
    const httpd_uri_t config_page = {.uri = "/config", .method = HTTP_GET, .handler = config_page_handler, .user_ctx = NULL};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_handler, .user_ctx = NULL};
    const httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL};
    httpd_register_uri_handler(s_http_server, &root);
    httpd_register_uri_handler(s_http_server, &settings);
    httpd_register_uri_handler(s_http_server, &config_page);
    httpd_register_uri_handler(s_http_server, &save);
    httpd_register_uri_handler(s_http_server, &status);
    ESP_LOGI(TAG, "configuration page ready on port 80");
}

void device_provisioning_start_ap(void)
{
    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, "CODEX-DIAL-SETUP");
    strcpy((char *)ap.ap.password, "codexsetup");
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.pmf_cfg.required = false;
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        ESP_LOGE(TAG, "could not start setup AP");
        return;
    }
    s_ap_active = true;
    ESP_LOGW(TAG, "setup mode: connect to %s / %s, then open http://192.168.4.1/settings", ap.ap.ssid, ap.ap.password);
}

bool device_provisioning_ap_active(void)
{
    return s_ap_active;
}
