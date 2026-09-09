#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "esp_websocket_client.h"
#include "lvgl.h"

namespace esp_brookesia::apps {

class CompanionApp final : public systems::phone::App {
public:
    static CompanionApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);
    ~CompanionApp() override = default;

protected:
    CompanionApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    struct Appearance {
        uint16_t clock_y = 58;
        uint32_t clock_color = 0xFFFFFF;
        uint16_t clock_weight = 500;
    };

    static CompanionApp *_instance;
    static void onRecordClicked(lv_event_t *event);
    static void onIdleClicked(lv_event_t *event);
    static void onIdleTimer(lv_timer_t *timer);
    static void websocketEvent(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

    void loadAppearance();
    void saveAppearance();
    void loadBridgeConfig();
    void sendDeviceConfig();
    void startTransport();
    void stopTransport();
    void handleMessage(const char *text, size_t length);
    void applyCompanion(void *json);
    void setConnection(const char *text, uint32_t color);
    void applyAppearance();
    void wakeFromIdle();
    void toggleRecording();
    void createVoicePage(lv_obj_t *tile);
    void createMailPage(lv_obj_t *tile);
    void createQuotaPage(lv_obj_t *tile);
    lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, lv_color_t color, lv_coord_t width = LV_SIZE_CONTENT);
    void makePageHeader(lv_obj_t *parent, const char *title, const char *status, lv_color_t accent, lv_obj_t **status_out = nullptr);
    void makePageDots(lv_obj_t *parent, uint8_t active_index);

    lv_obj_t *_screen = nullptr;
    lv_obj_t *_tileview = nullptr;
    lv_obj_t *_idle_overlay = nullptr;
    lv_obj_t *_idle_pet = nullptr;
    lv_obj_t *_idle_clock_layers[3] = {};
    lv_obj_t *_idle_date = nullptr;
    lv_obj_t *_record_button = nullptr;
    lv_obj_t *_record_label = nullptr;
    lv_obj_t *_record_state = nullptr;
    lv_obj_t *_voice_connection = nullptr;
    lv_obj_t *_mail_connection = nullptr;
    lv_obj_t *_quota_connection = nullptr;
    lv_obj_t *_mail_summary = nullptr;
    lv_obj_t *_mail_accounts = nullptr;
    lv_obj_t *_mail_subjects[3] = {};
    lv_obj_t *_mail_times[3] = {};
    lv_obj_t *_quota_ring = nullptr;
    lv_obj_t *_quota_remaining = nullptr;
    lv_obj_t *_quota_window = nullptr;
    lv_obj_t *_quota_secondary = nullptr;
    lv_obj_t *_quota_reset = nullptr;
    lv_timer_t *_idle_timer = nullptr;
    esp_websocket_client_handle_t _ws = nullptr;
    char _uri[256] = {};
    char _host[96] = {};
    char _token[128] = {};
    uint16_t _port = 3082;
    bool _transport_started = false;
    bool _connected = false;
    uint32_t _last_frame_ms = 0;
    uint32_t _last_ping_ms = 0;
    Appearance _appearance;
    bool _idle = true;
    bool _recording = false;
    uint32_t _last_activity_ms = 0;
    uint32_t _record_started_ms = 0;
    uint8_t _idle_pet_pose = 0;
    uint8_t _idle_pet_outfit = 0;
    uint32_t _idle_pet_started_ms = 0;
};

} // namespace esp_brookesia::apps
