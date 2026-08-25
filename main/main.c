#include "esp_log.h"
#include "boot_diag.h"
#include "crash_sim.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    boot_diag_process();
    
    ESP_LOGI(TAG, "Application initialization complete.");
}