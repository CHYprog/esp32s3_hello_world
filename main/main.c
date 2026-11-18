

#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sh1106.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // 等待OLED供电稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 初始化OLED
    ESP_ERROR_CHECK(
        SH1106_Init()
    );

    ESP_LOGI(TAG, "OLED Init OK");

    uint32_t count = 0;
    char text[32];

    while (1)
    {
        // 清空显示缓冲区
        SH1106_Clear();

        // 显示固定内容
        SH1106_DrawString(0, 0, "你好 ESP32-S3");
        SH1106_DrawString(0, 16, "显示正常");

        // 显示动态计数
        snprintf(
            text,
            sizeof(text),
            "计数: %lu",
            (unsigned long)count++
        );

        SH1106_DrawString(0, 32, text);

        // 一次性刷新到OLED
        ESP_ERROR_CHECK(SH1106_Update());

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
