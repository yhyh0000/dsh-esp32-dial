/* Xiaolan: an always-on desktop pet driven by the Codex bridge state. */
#include "xiaolan_widget.hpp"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_brookesia.hpp"
#include "nvs.h"
#include "assets/xiaolan_assets.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr char TAG[] = "Xiaolan";
constexpr uint32_t kFrameMs = 125;
constexpr uint32_t kPingMs = 10000;
constexpr uint32_t kOutfitMs = 12000;

static uint32_t nowMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static const char *jsonString(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
}
}

namespace esp_brookesia::apps {

bool XiaolanWidget::begin(lv_obj_t *parent)
{
    if (!parent || _image) return false;
    _image = lv_image_create(parent);
    if (!_image) return false;
    _image_width = 168;
    _image_height = 182;
    lv_obj_set_size(_image, _image_width, _image_height);
    const lv_coord_t parent_width = lv_obj_get_content_width(parent);
    const lv_coord_t parent_height = lv_obj_get_content_height(parent);
    lv_obj_set_pos(_image,
                   parent_width > _image_width ? (parent_width - _image_width) / 2 : 0,
                   parent_height > _image_height ? (parent_height - _image_height) / 2 : 0);
    lv_image_set_antialias(_image, false);
    lv_image_set_scale(_image, 448); // 1.75x nearest-neighbour enlargement
    _outfit = XIAOLAN_DEFAULT_OUTFIT;
    selectAnimation("idle");
    _outfit_changed_ms = nowMs();
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_image, onTouch, LV_EVENT_CLICKED, this);
    _timer = lv_timer_create(onTick, kFrameMs, this);
    loadBridgeConfig();
    startTransport();
    ESP_LOGI(TAG, "desktop pet ready (%dx%d)", _image_width, _image_height);
    return _timer != nullptr;
}

void XiaolanWidget::selectAnimation(const char *phase)
{
    int pose = 0; // calm idle
    if (phase && (strcmp(phase, "working") == 0 || strcmp(phase, "streaming") == 0 ||
                  strcmp(phase, "done") == 0 || strcmp(phase, "success") == 0)) {
        pose = 4; // cheerful
    } else if (phase && (strcmp(phase, "thinking") == 0 || strcmp(phase, "active") == 0)) {
        pose = 3; // thinking
    } else if (phase && (strcmp(phase, "waiting") == 0 || strcmp(phase, "error") == 0 ||
                         strcmp(phase, "failed") == 0)) {
        pose = 2; // resting
    }
    if (phase) {
        strncpy(_phase, phase, sizeof(_phase) - 1);
        _phase[sizeof(_phase) - 1] = '\0';
    }
    const lv_image_dsc_t *frames = xiaolan_outfits[_outfit];
    if (frames != _frames || pose != _pose) {
        _frames = frames;
        _pose = pose;
        _frame_count = XIAOLAN_POSES;
        _frame = 0;
    }
    if (_image && _frames) lv_image_set_src(_image, &_frames[_pose % _frame_count]);
}

void XiaolanWidget::setState(const char *phase)
{
    if (!_image) return;
    char normalized[16] = {};
    if (phase) {
        size_t length = strlen(phase);
        if (length >= sizeof(normalized)) length = sizeof(normalized) - 1;
        for (size_t i = 0; i < length; ++i) {
            char ch = phase[i];
            normalized[i] = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
        }
    }
    selectAnimation(normalized);
}

void XiaolanWidget::onTick(lv_timer_t *timer)
{
    auto *self = static_cast<XiaolanWidget *>(timer ? timer->user_data : nullptr);
    if (!self || !self->_image || !self->_frames || self->_frame_count <= 0) return;
    const uint32_t tick_ms = nowMs();
    if (tick_ms - self->_outfit_changed_ms >= kOutfitMs) {
        self->_outfit = (self->_outfit + 1) % XIAOLAN_OUTFITS;
        self->_outfit_changed_ms = tick_ms;
        self->selectAnimation(self->_phase);
    } else {
        // Each column is a deliberate pose, so keep the current pose stable
        // until Codex changes phase or the next outfit is selected.
        lv_image_set_src(self->_image, &self->_frames[self->_pose]);
    }
    if (self->_connected && self->_ws && tick_ms - self->_last_frame_ms >= kPingMs) {
        constexpr char ping[] = "{\"t\":\"ping\",\"battery\":0,\"charging\":false}";
        esp_websocket_client_send_text(self->_ws, ping, sizeof(ping) - 1, pdMS_TO_TICKS(1000));
        self->_last_frame_ms = tick_ms;
    }
}

void XiaolanWidget::loadBridgeConfig()
{
    char host[96] = {};
    char token[128] = {};
    uint16_t port = 3082;
    nvs_handle_t nvs = 0;
    if (nvs_open("dsh-dial", NVS_READONLY, &nvs) == ESP_OK) {
        size_t length = sizeof(host);
        nvs_get_str(nvs, "host", host, &length);
        uint16_t stored_port = 0;
        if (nvs_get_u16(nvs, "port", &stored_port) == ESP_OK && stored_port > 0) port = stored_port;
        length = sizeof(token);
        nvs_get_str(nvs, "token", token, &length);
        nvs_close(nvs);
    }
    if (!host[0] || !token[0]) {
        _uri[0] = '\0';
        ESP_LOGW(TAG, "Bridge config missing; set host, port and token in NVS");
        return;
    }
    snprintf(_uri, sizeof(_uri), "ws://%s:%u/dev?token=%s", host, port, token);
}

void XiaolanWidget::startTransport()
{
    if (_uri[0] == '\0') {
        ESP_LOGW(TAG, "Bridge transport disabled until host and token are configured");
        return;
    }
    esp_websocket_client_config_t config = {};
    config.uri = _uri;
    config.buffer_size = 4096;
    config.task_stack = 6144;
    config.task_prio = 5;
    config.reconnect_timeout_ms = 3000;
    config.network_timeout_ms = 5000;
    config.ping_interval_sec = 10;
    _ws = esp_websocket_client_init(&config);
    if (!_ws) return;
    esp_websocket_register_events(_ws, WEBSOCKET_EVENT_ANY, websocketEvent, this);
    if (esp_websocket_client_start(_ws) != ESP_OK) {
        esp_websocket_client_destroy(_ws);
        _ws = nullptr;
    }
}

void XiaolanWidget::websocketEvent(void *handler_args, esp_event_base_t, int32_t event_id, void *event_data)
{
    auto *self = static_cast<XiaolanWidget *>(handler_args);
    if (!self) return;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        self->_connected = true;
        self->_last_frame_ms = nowMs();
        constexpr char hello[] = "{\"t\":\"hello\",\"fw\":\"xiaolan-brookesia/1.1\",\"board\":\"ESP32-S3-Touch-LCD-1.85B\"}";
        esp_websocket_client_send_text(self->_ws, hello, sizeof(hello) - 1, pdMS_TO_TICKS(1000));
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_ERROR || event_id == WEBSOCKET_EVENT_CLOSED) {
        self->_connected = false;
        esp_brookesia::gui::LvLockGuard guard;
        self->setState("waiting");
    } else if (event_id == WEBSOCKET_EVENT_DATA && event_data) {
        auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
        if (data->op_code == 0x1 && data->data_ptr && data->payload_offset == 0 && data->fin) {
            self->handleMessage(data->data_ptr, data->data_len);
        }
    }
}

void XiaolanWidget::handleMessage(const char *text, size_t length)
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
    if (strcmp(jsonString(root, "t"), "state") == 0) {
        esp_brookesia::gui::LvLockGuard guard;
        setState(jsonString(root, "phase"));
    }
    cJSON_Delete(root);
}

void XiaolanWidget::onTouch(lv_event_t *event)
{
    auto *self = static_cast<XiaolanWidget *>(lv_event_get_user_data(event));
    if (!self || !self->_image) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (!self->_dragging) {
            self->_dragging = true;
            lv_area_t area;
            lv_obj_get_coords(self->_image, &area);
            self->_drag_offset.x = point.x - area.x1;
            self->_drag_offset.y = point.y - area.y1;
        }
        lv_obj_t *parent = lv_obj_get_parent(self->_image);
        const int parent_width = parent ? lv_obj_get_content_width(parent) : 360;
        const int parent_height = parent ? lv_obj_get_content_height(parent) : 360;
        const int max_x = parent_width > self->_image_width ? parent_width - self->_image_width : 0;
        const int max_y = parent_height > self->_image_height ? parent_height - self->_image_height : 0;
        int x = point.x - self->_drag_offset.x;
        int y = point.y - self->_drag_offset.y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > max_x) x = max_x;
        if (y > max_y) y = max_y;
        lv_obj_set_pos(self->_image, x, y);
    } else if (code == LV_EVENT_CLICKED) {
        self->_dragging = false;
        self->setState("success");
    } else if (code == LV_EVENT_RELEASED) {
        self->_dragging = false;
    }
}

} // namespace esp_brookesia::apps
