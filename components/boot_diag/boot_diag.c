#include "boot_diag.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "BOOT_DIAG";

#define BOOT_DIAG_MAGIC_NUMBER 0xDEADBEEF
#define NVS_NAMESPACE "boot_diag"
#define NVS_KEY_COUNTERS "counters"
#define NVS_KEY_BOOTS    "boot_count"

// Persistent storage across resets (RTC memory, survives everything except power loss)
RTC_NOINIT_ATTR static uint32_t s_magic_number;
RTC_NOINIT_ATTR static uint32_t s_boot_count;
RTC_NOINIT_ATTR static uint32_t s_consecutive_crash_count;
RTC_NOINIT_ATTR static esp_reset_reason_t s_last_reset_reason;
RTC_NOINIT_ATTR static reset_counters_t s_reset_counters;

// Convert enum -> string
const char* reset_reason_to_string(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_UNKNOWN:    return "UNKNOWN";
        case ESP_RST_POWERON:    return "POWER_ON";
        case ESP_RST_EXT:        return "EXTERNAL_PIN";
        case ESP_RST_SW:         return "SOFTWARE_RESTART";
        case ESP_RST_PANIC:      return "SOFTWARE_PANIC";
        case ESP_RST_INT_WDT:    return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:   return "TASK_WATCHDOG";
        case ESP_RST_WDT:        return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP:  return "DEEP_SLEEP_EXIT";
        case ESP_RST_BROWNOUT:   return "BROWNOUT";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_EFUSE:      return "EFUSE_ERROR";
        case ESP_RST_PWR_GLITCH: return "POWER_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
        default:                 return "UNDEFINED";
    }
}

// Explicit whitelist of reset reasons considered an actual crash.
// Using a whitelist instead of a blacklist avoids miscounting normal
static bool reset_reason_is_crash(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_PANIC:
        case ESP_RST_TASK_WDT:
        case ESP_RST_INT_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
        case ESP_RST_CPU_LOCKUP:
        case ESP_RST_PWR_GLITCH:
            return true;
        default:
            return false;
    }
}

static esp_err_t init_nvs_storage(void);
esp_err_t boot_diag_save_nvs(void);

// Analyse and update the boot/crash counters
static esp_err_t boot_diag_init(void)
{
    esp_err_t err = init_nvs_storage();
    if (err != ESP_OK) return err;

    esp_reset_reason_t current_reason = esp_reset_reason();

    if (s_magic_number != BOOT_DIAG_MAGIC_NUMBER) {
        // COLD BOOT (RTC memory was not retained)
        ESP_LOGW(TAG, ">>> COLD BOOT detected (RTC memory was cleared) <<<");
        s_magic_number = BOOT_DIAG_MAGIC_NUMBER;
        s_consecutive_crash_count = 0;
        s_last_reset_reason = current_reason;

        nvs_handle_t nvs_handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
            uint32_t saved_boots = 0;
            size_t size = sizeof(reset_counters_t);

            if (nvs_get_u32(nvs_handle, NVS_KEY_BOOTS, &saved_boots) == ESP_OK) {
                s_boot_count = saved_boots + 1;
            } else {
                s_boot_count = 1;
            }

            if (nvs_get_blob(nvs_handle, NVS_KEY_COUNTERS, &s_reset_counters, &size) != ESP_OK) {
                s_reset_counters = (reset_counters_t){0};
            }
            nvs_close(nvs_handle);
        } else {
            // NVS empty or not yet initialized (first boot ever)
            s_boot_count = 1;
            s_reset_counters = (reset_counters_t){0};
        }
    } else {
        // WARM BOOT (RTC memory retained since last reset)
        s_boot_count++;

        // Tally the reset cause
        switch (current_reason) {
            case ESP_RST_PANIC:      s_reset_counters.panic++; break;
            case ESP_RST_TASK_WDT:
            case ESP_RST_INT_WDT:
            case ESP_RST_WDT:        s_reset_counters.wdt++; break;
            case ESP_RST_BROWNOUT:   s_reset_counters.brownout++; break;
            case ESP_RST_SW:         s_reset_counters.sw++; break;
            default: break;
        }

        // Consecutive-crash tracking (RTC RAM only).
        // NOTE: s_last_reset_reason is updated in boot_diag_log(), not here,
        if (reset_reason_is_crash(current_reason) &&
            current_reason == s_last_reset_reason) {
            s_consecutive_crash_count++;
        } else {
            s_consecutive_crash_count = 0;
        }
    }

    return boot_diag_save_nvs();
}

// Display diagnostic data
static void boot_diag_log(void)
{
    esp_reset_reason_t current_reason = esp_reset_reason();

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Boot count           : %lu", s_boot_count);
    ESP_LOGI(TAG, "Current reset reason : %s", reset_reason_to_string(current_reason));
    ESP_LOGI(TAG, "Previous reset reason: %s", reset_reason_to_string(s_last_reset_reason));

    if (s_consecutive_crash_count > 0) {
        ESP_LOGW(TAG, "Consecutive crashes  : %lu", s_consecutive_crash_count + 1);
    }
    ESP_LOGI(TAG, "====================================");

    // Update for the next reset's comparison
    s_last_reset_reason = current_reason;
}

// Entry point for global use
esp_err_t boot_diag_process(void)
{
    esp_err_t err = boot_diag_init();
    if (err != ESP_OK) {
        return err;
    }
    boot_diag_log();
    return ESP_OK;
}

// NVS init
static esp_err_t init_nvs_storage(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition has no free pages, or holds data in a format this
        // build doesn't recognize, erase and reinit.
        ESP_LOGW(TAG, "NVS error, reformatting partition");
        ret = nvs_flash_erase();
        if (ret != ESP_OK) return ret; // Erase failed
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t boot_diag_save_nvs(void)
{
    nvs_handle_t nvs_handle;

    // Open namespace (read/write)
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Write values, checking each result
    err = nvs_set_u32(nvs_handle, NVS_KEY_BOOTS, s_boot_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing boot count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_COUNTERS, &s_reset_counters, sizeof(reset_counters_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing reset counters: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit to NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t boot_diag_get_snapshot(boot_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_snapshot->boot_count = s_boot_count;
    out_snapshot->consecutive_crash_count = s_consecutive_crash_count;
    out_snapshot->last_reset_reason = s_last_reset_reason;
    out_snapshot->reset_counters = s_reset_counters;

    return ESP_OK;
}

// Resets all crash/boot statistics, both in RTC RAM and in NVS.
esp_err_t boot_diag_reset_counters(void)
{
    ESP_LOGW(TAG, "Resetting all boot/crash counters");

    s_boot_count = 0;
    s_consecutive_crash_count = 0;
    s_reset_counters = (reset_counters_t){0};

    return boot_diag_save_nvs();
}