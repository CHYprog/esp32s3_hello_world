

#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sh1106.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // 等待 OLED 供电稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 初始化 OLED
    ESP_ERROR_CHECK(SH1106_Init());

    ESP_LOGI(TAG, "OLED Init OK");

    // 清除显示缓冲区
    SH1106_Clear();

    // 绘制四行文字
    SH1106_DrawString(0, 0,  "你好 ESP32-S3");
    SH1106_DrawString(0, 16, "显示正常");
    SH1106_DrawString(0, 32, "计数: 123");
    SH1106_DrawString(0, 48, "Hello OLED");

    // 将文字发送到 OLED
    ESP_ERROR_CHECK(SH1106_Update());

    uint8_t scroll = 0;

    while (1)
    {
        // 设置当前滚动位置
        ESP_ERROR_CHECK(
            SH1106_SetScrollOffset(scroll)
        );

        // 滚动一个像素
        scroll++;

        // 到达64像素后从0重新开始
        if (scroll >= SH1106_HEIGHT)
        {
            scroll = 0;
        }

        // 控制滚动速度
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
