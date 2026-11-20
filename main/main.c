
#include "sh1106.h"
#include "cat_bitmap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void Cat_Task(void *arg)
{
    const int delay_ms[5] = {
        2500, 80, 120, 80, 100
    };

    while (1)
    {
        for (int i = 0; i < CAT_FRAME_COUNT; i++)
        {
            SH1106_DrawBitmap(cat_frames[i]);
            SH1106_Update();

            vTaskDelay(pdMS_TO_TICKS(delay_ms[i]));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(SH1106_Init());

    xTaskCreate(
        Cat_Task,
        "Cat_Task",
        4096,
        NULL,
        5,
        NULL
    );
}
