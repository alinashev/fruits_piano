#include "fruits.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fruit_piano_main";

void app_main(void)
{
    ESP_LOGI(TAG, "starting fruit piano");
    ESP_ERROR_CHECK(fruits_init());
    ESP_LOGI(TAG, "fruit piano ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
