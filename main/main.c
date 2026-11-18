
#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sh1106.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // 1. 等待 OLED 供电稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 2. 初始化 OLED
    ESP_ERROR_CHECK(SH1106_Init());

    ESP_LOGI(TAG, "OLED Init OK");

    // 3. 清空显示缓冲区
    SH1106_Clear();

    // 4. 绘制四行文字
    SH1106_DrawString(0, 0,  "ESP32-S3");
    SH1106_DrawString(0, 16, "Hello OLED");
    SH1106_DrawString(0, 32, "显示正常");
    SH1106_DrawString(0, 48, "Scroll Test");

    // 5. 将初始画面发送到屏幕
    ESP_ERROR_CHECK(SH1106_Update());

    // 6. 主循环
    while (1)
    {
        // 第二行向左移动1像素
        SH1106_ScrollHorizontalArea(16, 32, -1);

        // 第四行向右移动1像素
        SH1106_ScrollHorizontalArea(48, 64, 1);

        // 刷新OLED
        ESP_ERROR_CHECK(SH1106_Update());

        // 延时20ms
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
