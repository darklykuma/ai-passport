// main/main.c — MAFA CHRONICLE application entry (PRD_MAFA_CHRONICLE 1.4).
// The FOG MARCH code stays compiled in the tree for reference and host
// tests, but the device boots into MAFA CHRONICLE.
#include "bsp_display.h"
#include "esp_log.h"
#include "mafa_app.h"

static const char *TAG = "mafa_main";

void app_main(void) {
    ESP_LOGI(TAG, "MAFA CHRONICLE (玛法战纪) 启动");

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，无法进入游戏");
        return;
    }
    bsp_display_backlight(100);

    mafa_app_boot();
}
