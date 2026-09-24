// main/main.c — FOG MARCH application entry (PRD_FOG_MARCH 15.1).
// Launches the redesigned game UI directly; the baseline BSP test menu and
// demo pages are not compiled into this application (mandatory UI redesign).
#include "bsp_display.h"
#include "esp_log.h"
#include "fog_app.h"

static const char *TAG = "fog_main";

void app_main(void) {
    ESP_LOGI(TAG, "FOG MARCH (迷雾三国) P0 启动");

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，无法进入游戏");
        return;
    }
    bsp_display_backlight(100);

    fog_app_boot();
}
