#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "boot_diag.h"
#include "crash_sim.h"

static const char *TAG = "MAIN";

static void print_snapshot(void)
{
    boot_diag_snapshot_t snap;
    if (boot_diag_get_snapshot(&snap) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get boot_diag snapshot");
        return;
    }

    ESP_LOGI(TAG, "---- Snapshot via getter ----");
    ESP_LOGI(TAG, "boot_count            : %lu", snap.boot_count);
    ESP_LOGI(TAG, "consecutive_crash_cnt : %lu", snap.consecutive_crash_count);
    ESP_LOGI(TAG, "last_reset_reason     : %s", reset_reason_to_string(snap.last_reset_reason));
    ESP_LOGI(TAG, "panic / wdt / brownout/ sw : %lu / %lu / %lu / %lu",
             snap.reset_counters.panic,
             snap.reset_counters.wdt,
             snap.reset_counters.brownout,
             snap.reset_counters.sw);
    ESP_LOGI(TAG, "-----------------------------");
}

void app_main(void)
{
    // Always run this first, before anything else
    boot_diag_process();

    // Prove the getter returns the same data boot_diag_process() just logged
    print_snapshot();

    ESP_LOGI(TAG, "App running normally. Waiting 5s before test crash...");
    vTaskDelay(pdMS_TO_TICKS(500000));

    // Comment/uncomment to choose which crash to test
    crash_sim_panic();
    // crash_sim_wdt();
}