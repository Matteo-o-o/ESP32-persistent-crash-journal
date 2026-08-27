# ESP32 Persistent Crash Journal

## Overview

This project is a boot diagnostics module for ESP32 (using ESP-IDF framework) that logs the crash history: total boot count, reset causes (panic, watchdog, brownout, software reset), and consecutive crash detection. The history is persisted across both regular reboots and full power loss.

## Why This Project

On a production embedded device, reboot cause is often the most critical log you can have. A single crash is not a concern, but a loop of consecutive crashes can point to a critical bug or a failing sensor that needs to be caught before it becomes a field issue. This module provides that visibility with a small footprint and no external dependency beyond ESP-IDF.

## Usage

This becomes especially valuable once a device is deployed in the field, where plugging in a USB/JTAG cable to read the serial logs is not an option. With this project, the crash history can be forwarded over any wireless link already available on the device, such as Wi-Fi (HTTP, MQTT), Bluetooth, LoRa, or any other radio, without requiring physical access to the device. A support team or a monitoring dashboard can then pull the crash history remotely.

## Quick Start

Add the `boot_diag` component to your project's `components` folder, then call `boot_diag_process()` in your entry point (`app_main()`), before anything else. You can then use the getter `boot_diag_get_snapshot()`, along with the `boot_diag_snapshot_t` type, to retrieve or send the crash history.

```c
#include "boot_diag.h"

void app_main(void)
{
    boot_diag_process();

    boot_diag_snapshot_t snap;
    if (boot_diag_get_snapshot(&snap) == ESP_OK) {
        // send snap over Wi-Fi, Bluetooth, or print it
    }
}
```

## Installation

Clone the repository:

```bash
git clone https://github.com/matteo-o-o/ESP32-persistent-crash-journal
cd ESP32-persistent-crash-journal
```

Requires ESP-IDF v5.3.

A devcontainer is included in `.devcontainer/`, based on the official `espressif/idf:v5.3` image with the toolchain preinstalled. If you use VS Code with Docker, open the project and select "Dev Containers: Reopen in Container": no local ESP-IDF installation is needed, and `idf.py` is ready to use directly in the integrated terminal. The container runs with access to the host's USB devices so it can flash the board/device.

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the actual serial port of your board/device.

## How It Works

The module relies on two complementary levels of persistence.

| Storage | Survives | Does not survive | Purpose |
|---|---|---|---|
| **RTC RAM** | software reset, panic, watchdog, brownout | full power loss | fast cold/warm boot detection, consecutive crash counter |
| **NVS (flash)** | everything, including power loss | flash erase or reformat | long term history (total boot count, per reset type counters) |

A magic number is written to RTC RAM on every boot. If it is missing at startup, RTC RAM was cleared (power loss or first boot), which is treated as a **cold boot**: the counters are restored from NVS. Otherwise, it is a **warm boot**: the RTC RAM counters are simply incremented and synced back to NVS.

A reset only counts as a crash if it belongs to a specific set of causes: panic, watchdog (task, interrupt, or general), brownout, CPU lockup, or a power glitch. These are genuine firmware or hardware failures.

Everything else is treated as a normal reset and does not count as a crash: pressing the reset button, a voluntary software reset such as `esp_restart()` (used for OTA updates, for example), waking up from deep sleep, or the very first power-on. A normal reset also resets the consecutive crash counter back to zero, so a single crash followed by a manual reset is not mistaken for an ongoing crash loop.

## Example Output

Output of `boot_diag_process()` on a cold boot after a full power cycle, followed by the snapshot retrieved via the getter:

```
I (354) BOOT_DIAG: ====================================
I (354) BOOT_DIAG: Boot count           : 19
I (354) BOOT_DIAG: Current reset reason : POWER_ON
I (354) BOOT_DIAG: Previous reset reason: POWER_ON
I (354) BOOT_DIAG: ====================================
I (354) MAIN: ---- Snapshot via getter ----
I (364) MAIN: boot_count            : 19
I (364) MAIN: consecutive_crash_cnt : 0
I (374) MAIN: last_reset_reason     : POWER_ON
I (374) MAIN: panic / wdt / brownout/ sw : 15 / 0 / 0 / 0
I (384) MAIN: -----------------------------
```

After two consecutive panics triggered by `crash_sim_panic()`, the same log shows the crash streak:

```
I (401) BOOT_DIAG: ====================================
I (401) BOOT_DIAG: Boot count           : 21
I (401) BOOT_DIAG: Current reset reason : SOFTWARE_PANIC
I (401) BOOT_DIAG: Previous reset reason: SOFTWARE_PANIC
W (401) BOOT_DIAG: Consecutive crashes  : 2
I (401) BOOT_DIAG: ====================================
```

## Known Limitations

- No automatic action is taken on a high consecutive crash count (no "safe mode" yet).
- No network export (Wi-Fi, MQTT, Bluetooth) is implemented yet, only the getter to retrieve the data locally.
- No automated tests, validation is done manually with the `crash_sim` component.