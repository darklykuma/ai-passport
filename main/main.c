// main/main.c — RIFT BOARD (S16 赛场看板) application entry. The baseline
// hardware-test demo stays in the tree for reference/host tests; the device
// boots into the scoreboard.
#include "bsp_display.h"
#include "esp_log.h"
#include "rift_app.h"

static const char *TAG = "rift_main";

void app_main(void) {
    ESP_LOGI(TAG, "RIFT BOARD (S16 赛场看板) 启动");

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，无法进入看板");
        return;
    }
    bsp_display_backlight(100);

    rift_app_boot();
}
