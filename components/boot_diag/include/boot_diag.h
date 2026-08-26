#ifndef BOOT_DIAG_H
#define BOOT_DIAG_H

#include "esp_system.h"

// public Types
typedef struct {
    uint32_t panic;
    uint32_t wdt;
    uint32_t brownout;
    uint32_t sw;
} reset_counters_t;

// In boot_diag.h
typedef struct {
    uint32_t boot_count;
    uint32_t consecutive_crash_count;
    esp_reset_reason_t last_reset_reason;
    reset_counters_t reset_counters;
} boot_diag_snapshot_t;

// public API 
const char* reset_reason_to_string(esp_reset_reason_t reason);
esp_err_t boot_diag_process(void);

esp_err_t boot_diag_get_snapshot(boot_diag_snapshot_t *out_snapshot);

#endif // BOOT_DIAG_H
