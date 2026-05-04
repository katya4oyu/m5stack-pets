#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "m5stack-pets";

void app_main(void)
{
    ESP_LOGI(TAG, "starting basic pet");

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, ":)");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_center(label);
    bsp_display_unlock();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
