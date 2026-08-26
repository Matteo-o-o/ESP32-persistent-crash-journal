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


// Storage persistant (RTC Memory)
RTC_NOINIT_ATTR static uint32_t s_magic_number;
RTC_NOINIT_ATTR static uint32_t s_boot_count;
RTC_NOINIT_ATTR static uint32_t s_consecutive_crash_count;
RTC_NOINIT_ATTR static esp_reset_reason_t s_last_reset_reason;

RTC_NOINIT_ATTR static reset_counters_t s_reset_counters;

// Convertisser enum -> chaîne
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

// Analyse and update the variables
static esp_err_t boot_diag_init(void)
{
    esp_reset_reason_t current_reason = esp_reset_reason();

    // Cold boot 
    if (s_magic_number != BOOT_DIAG_MAGIC_NUMBER) {
        s_magic_number = BOOT_DIAG_MAGIC_NUMBER;
        s_boot_count = 1;
        s_consecutive_crash_count = 0;
    }
    // Warm boot 
    else {
        s_boot_count++;
        switch (current_reason) {
            case ESP_RST_PANIC:
                s_reset_counters.panic++;
                break;
            case ESP_RST_TASK_WDT:
            case ESP_RST_INT_WDT:
            case ESP_RST_WDT:
                s_reset_counters.wdt++;
                break;
            case ESP_RST_BROWNOUT:
                s_reset_counters.brownout++;
                break;
            case ESP_RST_SW:
                s_reset_counters.sw++;
                break;
            default:
                break;
        }

        // consecutive crash count (for later)
        if (current_reason == s_last_reset_reason && current_reason != ESP_RST_SW && current_reason != ESP_RST_POWERON) {
            s_consecutive_crash_count++;
        } else {
            s_consecutive_crash_count = 0;
        }
    }

    return ESP_OK;
}

// Display data 
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

    // update for next crash
    s_last_reset_reason = current_reason;
}

// function : global use
esp_err_t boot_diag_process(void)
{
    esp_err_t err = boot_diag_init();
    if (err != ESP_OK) {
        return err;
    }
    
    boot_diag_log();
    return ESP_OK;
}


// nvs init 
static esp_err_t init_nvs_storage(void){
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){ // NVS partition doesn't contain any empty pages OR NVS partition contains data in new format and cannot be recognized by this version of code
        ESP_LOGW(TAG,"NVS error, reformatting parition");
        ret = nvs_flash_erase();
        if(ret != ESP_OK) return ret; // Fail erase
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t boot_diag_save_nvs(void){
    nvs_handle_t nvs_handle;

    // open namespace (Write/Read)
    esp_err_t err = nvs_open(NVS_NAMESPACE,NVS_READWRITE,&nvs_handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG,"Error opening handle NVS %s",esp_err_to_name(err));
        return err;
    }
    // Write value
    nvs_set_u32(nvs_handle,NVS_KEY_BOOTS,s_boot_count);
    nvs_set_blob(nvs_handle, NVS_KEY_COUNTERS, &s_reset_counters, sizeof(reset_counters_t));

    // Commit on NVS
    err = nvs_commit(nvs_handle);

    nvs_close(nvs_handle);
    
    return err;
}