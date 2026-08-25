#include "crash_sim.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CRASH_SIM";

void crash_sim_panic(void)
{
    ESP_LOGW(TAG, "Triggering NULL pointer panic...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    int *ptr = NULL;
    *ptr = 42;
}

void crash_sim_wdt(void)
{
    ESP_LOGW(TAG, "Triggering Task Watchdog reset...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    while (1) {
        // CPU stuck
    }
}