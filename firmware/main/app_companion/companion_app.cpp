/* First real Companion vertical slice for the Waveshare circular display. */
#include "companion_app.hpp"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "esp_brookesia.hpp"

#include <cstdio>
#include <ctime>
#include <cstring>

namespace {
constexpr char TAG[] = "CompanionApp";
constexpr uint32_t kIdleTimeoutMs = 18000;

static esp_brookesia::systems::base::App::Config makeCompanionCoreConfig()
{
    return esp_brookesia::systems::base::App::Config::SIMPLE_CONSTRUCTOR("Companion", nullptr, false);
}

static esp_brookesia::systems::phone::App::Config makeCompanionPhoneConfig(bool use_status_bar, bool use_navigation_bar)
{
    auto config = esp_brookesia::systems::phone::App::Config::SIMPLE_CONSTRUCTOR(nullptr, use_status_bar, use_navigation_bar);
    config.app_launcher_page_index = 1;
    return config;
}

static lv_color_t color(uint32_t hex)
{
    return lv_color_hex(hex);
}

static const lv_font_t *clockFont(uint16_t weight)
{
    // The stock LVGL font set has no variable font axis. Three overlaid
    // labels below provide a stable, memory-light approximation of weight.
    (void)weight;
    return &lv_font_montserrat_32;
}

static uint32_t nowMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static const char *jsonString(cJSON *root, const char *key)
{
    if (!root) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}

static cJSON *jsonObject(cJSON *root, const char *key)
{
    if (!root) return nullptr;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return item && cJSON_IsObject(item) ? item : nullptr;
}

static int jsonInt(cJSON *root, const char *key, int fallback = -1)
{
    if (!root) return fallback;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : fallback;
}

static int remainingPercent(cJSON *window)
{
    if (!window) return -1;
    int remaining = jsonInt(window, "remainingPercent");
    if (remaining < 0) remaining = jsonInt(window, "remaining_percent");
    if (remaining < 0) {
        const int used = jsonInt(window, "usedPercent");
        if (used >= 0) remaining = 100 - used;
    }
    return remaining < 0 ? -1 : (remaining > 100 ? 100 : remaining);
}

static void formatCountdown(int seconds, char *buffer, size_t length)
{
    if (seconds < 0) {
        snprintf(buffer, length, "NEXT RESET --:--:--");
        return;
    }
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remainder = seconds % 60;
    snprintf(buffer, length, "NEXT RESET %02d:%02d:%02d", hours, minutes, remainder);
}
} // namespace

namespace esp_brookesia::apps {

CompanionApp *CompanionApp::_instance = nullptr;

CompanionApp *CompanionApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new CompanionApp(use_status_bar, use_navigation_bar);
    return _instance;
}

CompanionApp::CompanionApp(bool use_status_bar, bool use_navigation_bar):
    App(makeCompanionCoreConfig(), makeCompanionPhoneConfig(use_status_bar, use_navigation_bar))
{
}

void CompanionApp::loadAppearance()
{
    nvs_handle_t nvs = 0;
    if (nvs_open("companion", NVS_READONLY, &nvs) != ESP_OK) return;

    uint16_t value16 = 0;
    uint32_t value32 = 0;
    if (nvs_get_u16(nvs, "clock_y", &value16) == ESP_OK) _appearance.clock_y = value16;
    if (nvs_get_u32(nvs, "clock_color", &value32) == ESP_OK) _appearance.clock_color = value32;
    if (nvs_get_u16(nvs, "clock_weight", &value16) == ESP_OK) _appearance.clock_weight = value16;
    nvs_close(nvs);

    if (_appearance.clock_y < 32 || _appearance.clock_y > 120) _appearance.clock_y = 58;
    if (_appearance.clock_weight < 300 || _appearance.clock_weight > 700) _appearance.clock_weight = 500;
}

void CompanionApp::saveAppearance()
{
    nvs_handle_t nvs = 0;
    if (nvs_open("companion", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u16(nvs, "clock_y", _appearance.clock_y);
    nvs_set_u32(nvs, "clock_color", _appearance.clock_color);
    nvs_set_u16(nvs, "clock_weight", _appearance.clock_weight);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void CompanionApp::loadBridgeConfig()
{
    _host[0] = '\0';
    _token[0] = '\0';
    _port = 3082;

    nvs_handle_t nvs = 0;
    if (nvs_open("dsh-dial", NVS_READONLY, &nvs) == ESP_OK) {
        size_t length = sizeof(_host);
        nvs_get_str(nvs, "host", _host, &length);
        uint16_t port = 0;
        if (nvs_get_u16(nvs, "port", &port) == ESP_OK && port > 0) _port = port;
        length = sizeof(_token);
        nvs_get_str(nvs, "token", _token, &length);
        nvs_close(nvs);
    }
    if (!_host[0] || !_token[0]) {
        _uri[0] = '\0';
        return;
    }
    snprintf(_uri, sizeof(_uri), "ws://%s:%u/dev?token=%s", _host, _port, _token);
    ESP_LOGI(TAG, "Bridge endpoint: ws://%s:%u/dev", _host, _port);
}

void CompanionApp::setConnection(const char *text, uint32_t text_color)
{
    const lv_color_t color_value = color(text_color);
    lv_obj_t *labels[] = {_voice_connection, _mail_connection, _quota_connection};
    for (lv_obj_t *label : labels) {
        if (label) {
            lv_label_set_text(label, text);
            lv_obj_set_style_text_color(label, color_value, 0);
        }
    }
}

void CompanionApp::startTransport()
{
    if (_transport_started || !_uri[0]) return;
    esp_websocket_client_config_t config = {};
    config.uri = _uri;
    config.buffer_size = 4096;
    config.task_stack = 6144;
    config.task_prio = 5;
    config.reconnect_timeout_ms = 3000;
    config.network_timeout_ms = 5000;
    config.ping_interval_sec = 10;
    _ws = esp_websocket_client_init(&config);
    if (!_ws) {
        setConnection("SOCKET INIT FAILED", 0xE74C3C);
        return;
    }
    esp_websocket_register_events(_ws, WEBSOCKET_EVENT_ANY, websocketEvent, this);
    if (esp_websocket_client_start(_ws) != ESP_OK) {
        esp_websocket_client_destroy(_ws);
        _ws = nullptr;
        setConnection("CONNECT FAILED", 0xE74C3C);
        return;
    }
    _transport_started = true;
    setConnection("CONNECTING", 0xE0A34A);
}

void CompanionApp::stopTransport()
{
    if (!_ws) return;
    esp_websocket_client_stop(_ws);
    esp_websocket_client_destroy(_ws);
    _ws = nullptr;
    _transport_started = false;
    _connected = false;
}

void CompanionApp::websocketEvent(void *handler_args, esp_event_base_t, int32_t event_id, void *event_data)
{
    auto *self = static_cast<CompanionApp *>(handler_args);
    if (!self) return;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        self->_connected = true;
        self->_last_frame_ms = nowMs();
        {
            esp_brookesia::gui::LvLockGuard guard;
            self->setConnection("BRIDGE CONNECTED", 0x3CB371);
        }
        constexpr char hello[] = "{\"t\":\"hello\",\"fw\":\"companion-brookesia/1.1\",\"board\":\"ESP32-S3-Touch-LCD-1.85B\"}";
        esp_websocket_client_send_text(self->_ws, hello, sizeof(hello) - 1, pdMS_TO_TICKS(1000));
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR || event_id == WEBSOCKET_EVENT_CLOSED) {
        self->_connected = false;
        esp_brookesia::gui::LvLockGuard guard;
        self->setConnection("BRIDGE OFFLINE", 0xE0A34A);
    } else if (event_id == WEBSOCKET_EVENT_DATA && event_data) {
        auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
        if (data->op_code == 0x1 && data->data_ptr && data->payload_offset == 0 && data->fin) {
            self->handleMessage(data->data_ptr, data->data_len);
        }
    }
}

void CompanionApp::handleMessage(const char *text, size_t length)
{
    if (!text || !length) return;
    char *copy = static_cast<char *>(malloc(length + 1));
    if (!copy) return;
    memcpy(copy, text, length);
    copy[length] = '\0';
    cJSON *root = cJSON_Parse(copy);
    free(copy);
    if (!root) return;

    _last_frame_ms = nowMs();
    const char *type = jsonString(root, "t");
    esp_brookesia::gui::LvLockGuard guard;
    if (strcmp(type, "companion") == 0) {
        applyCompanion(root);
    } else if (strcmp(type, "pong") == 0) {
        setConnection("BRIDGE CONNECTED", 0x3CB371);
    }
    cJSON_Delete(root);
}

void CompanionApp::applyCompanion(void *json)
{
    auto *root = static_cast<cJSON *>(json);

    cJSON *recording = jsonObject(root, "recording");
    if (recording && _record_state) {
        const char *state = jsonString(recording, "state");
        if (strcmp(state, "recording") == 0) lv_label_set_text(_record_state, "RECORDING ON DEVICE");
        else if (strcmp(state, "queued") == 0 || strcmp(state, "uploading") == 0) lv_label_set_text(_record_state, "AUDIO QUEUED FOR UPLOAD");
        else if (strcmp(state, "sent") == 0) lv_label_set_text(_record_state, "LAST AUDIO SENT");
        else if (strcmp(state, "failed") == 0) lv_label_set_text(_record_state, "AUDIO SEND FAILED");
    }

    cJSON *mail = jsonObject(root, "mail");
    if (mail) {
        int unread = jsonInt(mail, "unread", -1);
        cJSON *accounts = jsonObject(mail, "accounts");
        const int qq = accounts ? jsonInt(jsonObject(accounts, "qq"), "unread", 0) : -1;
        const int gmail = accounts ? jsonInt(jsonObject(accounts, "gmail"), "unread", 0) : -1;
        if (unread < 0 && accounts) unread = (qq < 0 ? 0 : qq) + (gmail < 0 ? 0 : gmail);
        char summary[32];
        if (unread < 0) snprintf(summary, sizeof(summary), "-- UNREAD");
        else snprintf(summary, sizeof(summary), "%d UNREAD", unread);
        if (_mail_summary) lv_label_set_text(_mail_summary, summary);
        char account_text[48];
        snprintf(account_text, sizeof(account_text), "QQ %s   GOOGLE %s", qq < 0 ? "--" : "", gmail < 0 ? "--" : "");
        if (qq >= 0 && gmail >= 0) snprintf(account_text, sizeof(account_text), "QQ %d   GOOGLE %d", qq, gmail);
        else if (qq >= 0) snprintf(account_text, sizeof(account_text), "QQ %d   GOOGLE --", qq);
        else if (gmail >= 0) snprintf(account_text, sizeof(account_text), "QQ --   GOOGLE %d", gmail);
        if (_mail_accounts) lv_label_set_text(_mail_accounts, account_text);

        for (int i = 0; i < 3; ++i) {
            if (_mail_subjects[i]) lv_label_set_text(_mail_subjects[i], "--");
            if (_mail_times[i]) lv_label_set_text(_mail_times[i], "--");
        }
        cJSON *items = cJSON_GetObjectItemCaseSensitive(mail, "items");
        if (items && cJSON_IsArray(items)) {
            int index = 0;
            cJSON *item = nullptr;
            cJSON_ArrayForEach(item, items) {
                if (index >= 3 || !cJSON_IsObject(item)) break;
                const char *subject = jsonString(item, "subject");
                if (!subject[0]) subject = jsonString(item, "title");
                if (_mail_subjects[index]) lv_label_set_text(_mail_subjects[index], subject[0] ? subject : "--");
                if (_mail_times[index]) lv_label_set_text(_mail_times[index], jsonString(item, "time"));
                ++index;
            }
        }
        const char *mail_status = jsonString(mail, "status");
        if (_mail_connection) {
            lv_label_set_text(_mail_connection, (strcmp(mail_status, "online") == 0 || strcmp(mail_status, "live") == 0) ? "MAIL LIVE" : "MAIL OFFLINE");
        }
    }

    cJSON *quota = jsonObject(root, "quota");
    if (quota) {
        cJSON *windows = jsonObject(quota, "windows");
        cJSON *five_hour = windows ? jsonObject(windows, "fiveHour") : nullptr;
        cJSON *seven_day = windows ? jsonObject(windows, "sevenDay") : nullptr;
        const int five_remaining = remainingPercent(five_hour);
        const int seven_remaining = remainingPercent(seven_day);
        if (_quota_ring) lv_arc_set_value(_quota_ring, five_remaining < 0 ? 0 : five_remaining);
        char remaining[16];
        if (five_remaining < 0) snprintf(remaining, sizeof(remaining), "--%%");
        else snprintf(remaining, sizeof(remaining), "%d%%", five_remaining);
        if (_quota_remaining) lv_label_set_text(_quota_remaining, remaining);
        char secondary[64];
        cJSON *reset_cards = jsonObject(quota, "resetCards");
        const int cards = reset_cards ? jsonInt(reset_cards, "availableCount", 0) : 0;
        if (seven_remaining < 0) snprintf(secondary, sizeof(secondary), "7D --%%     RESET CARDS %d", cards);
        else snprintf(secondary, sizeof(secondary), "7D %d%%     RESET CARDS %d", seven_remaining, cards);
        if (_quota_secondary) lv_label_set_text(_quota_secondary, secondary);
        char countdown[32];
        formatCountdown(jsonInt(five_hour, "remainingSeconds", -1), countdown, sizeof(countdown));
        if (_quota_reset) lv_label_set_text(_quota_reset, countdown);
        const char *status = jsonString(quota, "status");
        if (_quota_connection) {
            if (strcmp(status, "available") == 0 || strcmp(status, "low") == 0) lv_label_set_text(_quota_connection, "SUB2API LIVE");
            else if (strcmp(status, "exhausted") == 0) lv_label_set_text(_quota_connection, "LIMIT REACHED");
            else lv_label_set_text(_quota_connection, "SUB2API OFFLINE");
        }
    }
}

lv_obj_t *CompanionApp::makeLabel(lv_obj_t *parent, const char *text, lv_color_t text_color, lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, text_color, 0);
    if (width != LV_SIZE_CONTENT) {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    }
    return label;
}

void CompanionApp::makePageHeader(lv_obj_t *parent, const char *title, const char *status, lv_color_t accent, lv_obj_t **status_out)
{
    lv_obj_t *heading = makeLabel(parent, title, color(0xF4F3EC), 220);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_18, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *state = makeLabel(parent, status, accent, 220);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_16, 0);
    lv_obj_align(state, LV_ALIGN_TOP_LEFT, 0, 28);
    if (status_out) *status_out = state;
}

void CompanionApp::makePageDots(lv_obj_t *parent, uint8_t active_index)
{
    for (uint8_t i = 0; i < 3; ++i) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, i == active_index ? 18 : 5, 5);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, i == active_index ? color(0xC4ED72) : color(0x56615A), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (i - 1) * 15, -7);
    }
}

void CompanionApp::createVoicePage(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, color(0x121A15), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 31, 0);
    makePageHeader(tile, "VOICE LOG", "BRIDGE OFFLINE", color(0xE0A34A), &_voice_connection);

    _record_state = makeLabel(tile, "READY TO RECORD", color(0xA1AAA1), 250);
    lv_obj_set_style_text_align(_record_state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_record_state, LV_ALIGN_TOP_MID, 0, 73);

    _record_button = lv_button_create(tile);
    lv_obj_set_size(_record_button, 122, 78);
    lv_obj_align(_record_button, LV_ALIGN_TOP_MID, 0, 103);
    lv_obj_set_style_radius(_record_button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_record_button, color(0x2A3924), LV_PART_MAIN);
    lv_obj_set_style_border_color(_record_button, color(0xC4ED72), LV_PART_MAIN);
    lv_obj_set_style_border_width(_record_button, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(_record_button, onRecordClicked, LV_EVENT_CLICKED, this);
    _record_label = makeLabel(_record_button, "RECORD", color(0xF4F3EC), 100);
    lv_obj_set_style_text_align(_record_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(_record_label);

    lv_obj_t *timer = makeLabel(tile, "00:00", color(0xF4F3EC), 250);
    lv_obj_set_style_text_font(timer, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(timer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(timer, LV_ALIGN_TOP_MID, 0, 196);
    lv_obj_t *hint = makeLabel(tile, "Audio will upload after you stop", color(0x667068), 260);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 232);
    makePageDots(tile, 0);
}

void CompanionApp::createMailPage(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, color(0x1A1916), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 31, 0);
    makePageHeader(tile, "MAIL", "MAIL OFFLINE", color(0xE0A34A), &_mail_connection);

    _mail_summary = makeLabel(tile, "-- UNREAD", color(0xFFB968), 260);
    lv_obj_set_style_text_font(_mail_summary, &lv_font_montserrat_24, 0);
    lv_obj_align(_mail_summary, LV_ALIGN_TOP_LEFT, 0, 67);
    _mail_accounts = makeLabel(tile, "QQ --   GOOGLE --", color(0xA1AAA1), 260);
    lv_obj_align(_mail_accounts, LV_ALIGN_TOP_LEFT, 0, 96);

    const char *subjects[] = {"--", "--", "--"};
    const char *times[] = {"--", "--", "--"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *row = lv_button_create(tile);
        lv_obj_set_size(row, 260, 42);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 124 + i * 47);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, color(0x25231F), LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        _mail_subjects[i] = makeLabel(row, subjects[i], color(0xF4F3EC), 190);
        lv_obj_set_style_text_font(_mail_subjects[i], &lv_font_montserrat_16, 0);
        lv_obj_align(_mail_subjects[i], LV_ALIGN_TOP_LEFT, 9, 5);
        _mail_times[i] = makeLabel(row, times[i], color(0x667068), 68);
        lv_obj_set_style_text_align(_mail_times[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(_mail_times[i], LV_ALIGN_TOP_RIGHT, -8, 5);
    }
    makePageDots(tile, 1);
}

void CompanionApp::createQuotaPage(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, color(0x111B1A), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tile, 31, 0);
    makePageHeader(tile, "CODEX QUOTA", "QUOTA UNAVAILABLE", color(0xE0A34A), &_quota_connection);

    _quota_ring = lv_arc_create(tile);
    lv_obj_set_size(_quota_ring, 138, 138);
    lv_obj_align(_quota_ring, LV_ALIGN_TOP_MID, 0, 61);
    lv_arc_set_range(_quota_ring, 0, 100);
    lv_arc_set_value(_quota_ring, 0);
    lv_obj_remove_style(_quota_ring, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(_quota_ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_quota_ring, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_quota_ring, color(0x2C3937), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_quota_ring, color(0x82D8D1), LV_PART_INDICATOR);

    _quota_remaining = makeLabel(tile, "--%", color(0x82D8D1), 130);
    lv_obj_set_style_text_font(_quota_remaining, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_align(_quota_remaining, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_quota_remaining, LV_ALIGN_TOP_MID, 0, 104);
    _quota_window = makeLabel(tile, "5H REMAINING", color(0xA1AAA1), 130);
    lv_obj_set_style_text_align(_quota_window, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_quota_window, LV_ALIGN_TOP_MID, 0, 137);

    _quota_secondary = makeLabel(tile, "7D --%     RESET CARDS 0", color(0xC4ED72), 260);
    lv_obj_set_style_text_align(_quota_secondary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_quota_secondary, LV_ALIGN_TOP_MID, 0, 217);
    _quota_reset = makeLabel(tile, "NEXT RESET --:--:--", color(0x667068), 260);
    lv_obj_set_style_text_align(_quota_reset, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_quota_reset, LV_ALIGN_TOP_MID, 0, 244);
    makePageDots(tile, 2);
}

void CompanionApp::applyAppearance()
{
    if (!_idle_clock_layers[0]) return;
    const uint8_t layer_count = _appearance.clock_weight >= 650 ? 3 : 1;
    const int8_t first_offset = layer_count == 3 ? -1 : 0;
    for (uint8_t i = 0; i < 3; ++i) {
        if (i < layer_count) {
            lv_obj_clear_flag(_idle_clock_layers[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(_idle_clock_layers[i], LV_ALIGN_TOP_MID, first_offset + i, _appearance.clock_y);
        } else {
            lv_obj_add_flag(_idle_clock_layers[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_text_color(_idle_clock_layers[i], color(_appearance.clock_color), 0);
        lv_obj_set_style_text_font(_idle_clock_layers[i], clockFont(_appearance.clock_weight), 0);
    }
    lv_obj_align(_idle_date, LV_ALIGN_TOP_MID, 0, _appearance.clock_y + 42);
    lv_obj_set_style_text_color(_idle_date, color(_appearance.clock_color), 0);
    saveAppearance();
}

void CompanionApp::wakeFromIdle()
{
    _idle = false;
    _last_activity_ms = nowMs();
    if (_idle_overlay) lv_obj_add_flag(_idle_overlay, LV_OBJ_FLAG_HIDDEN);
}

void CompanionApp::toggleRecording()
{
    _recording = !_recording;
    if (_recording) {
        _record_started_ms = nowMs();
        lv_label_set_text(_record_label, "STOP");
        lv_label_set_text(_record_state, "RECORDING ON DEVICE");
        lv_obj_set_style_bg_color(_record_button, color(0x4B2B27), LV_PART_MAIN);
        lv_obj_set_style_border_color(_record_button, color(0xFF7D70), LV_PART_MAIN);
    } else {
        const uint32_t seconds = (nowMs() - _record_started_ms) / 1000;
        char state[48];
        snprintf(state, sizeof(state), "QUEUED FOR UPLOAD  %02luS", static_cast<unsigned long>(seconds));
        lv_label_set_text(_record_label, "RECORD");
        lv_label_set_text(_record_state, state);
        lv_obj_set_style_bg_color(_record_button, color(0x2A3924), LV_PART_MAIN);
        lv_obj_set_style_border_color(_record_button, color(0xC4ED72), LV_PART_MAIN);
    }
}

void CompanionApp::onRecordClicked(lv_event_t *event)
{
    auto *self = static_cast<CompanionApp *>(lv_event_get_user_data(event));
    if (self) {
        self->wakeFromIdle();
        self->toggleRecording();
    }
}

void CompanionApp::onIdleClicked(lv_event_t *event)
{
    auto *self = static_cast<CompanionApp *>(lv_event_get_user_data(event));
    if (self) self->wakeFromIdle();
}

void CompanionApp::onIdleTimer(lv_timer_t *timer)
{
    auto *self = static_cast<CompanionApp *>(timer->user_data);
    if (!self || !self->_idle_overlay) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    for (auto *layer : self->_idle_clock_layers) {
        if (layer) lv_label_set_text(layer, clock);
    }

    const uint32_t tick_ms = nowMs();
    if (self->_connected && self->_ws && tick_ms - self->_last_ping_ms >= 10000) {
        constexpr char ping[] = "{\"t\":\"ping\",\"battery\":0,\"charging\":false}";
        esp_websocket_client_send_text(self->_ws, ping, sizeof(ping) - 1, pdMS_TO_TICKS(1000));
        self->_last_ping_ms = tick_ms;
    }

    if (!self->_idle && nowMs() - self->_last_activity_ms >= kIdleTimeoutMs) {
        self->_idle = true;
        lv_obj_clear_flag(self->_idle_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

bool CompanionApp::run(void)
{
    loadAppearance();
    _screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_screen, color(0x0E100F), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_screen, 0, 0);

    _tileview = lv_tileview_create(_screen);
    lv_obj_set_size(_tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_tileview, color(0x0E100F), 0);
    lv_obj_set_style_bg_opa(_tileview, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_tileview, 0, 0);
    createVoicePage(lv_tileview_add_tile(_tileview, 0, 0, LV_DIR_RIGHT));
    createMailPage(lv_tileview_add_tile(_tileview, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT)));
    createQuotaPage(lv_tileview_add_tile(_tileview, 2, 0, LV_DIR_LEFT));

    _idle_overlay = lv_obj_create(_screen);
    lv_obj_set_size(_idle_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_idle_overlay, color(0x101612), 0);
    lv_obj_set_style_bg_opa(_idle_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_idle_overlay, 0, 0);
    lv_obj_add_event_cb(_idle_overlay, onIdleClicked, LV_EVENT_CLICKED, this);

    for (uint8_t i = 0; i < 3; ++i) {
        _idle_clock_layers[i] = makeLabel(_idle_overlay, "00:00", color(0xFFFFFF), 230);
        lv_obj_set_style_text_align(_idle_clock_layers[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    _idle_date = makeLabel(_idle_overlay, "CODEX COMPANION", color(0xFFFFFF), 230);
    lv_obj_set_style_text_align(_idle_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(_idle_date, LV_OPA_70, 0);
    applyAppearance();

    _last_activity_ms = nowMs();
    _idle_timer = lv_timer_create(onIdleTimer, 1000, this);
    loadBridgeConfig();
    startTransport();
    lv_screen_load(_screen);
    ESP_LOGI(TAG, "Companion app ready; appearance loaded from NVS");
    return true;
}

bool CompanionApp::back(void)
{
    if (_idle_timer) {
        lv_timer_del(_idle_timer);
        _idle_timer = nullptr;
    }
    stopTransport();
    _screen = nullptr;
    return notifyCoreClosed();
}

} // namespace esp_brookesia::apps
