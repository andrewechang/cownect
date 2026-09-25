# CowNect Firmware Build and Validation Report

_Last updated: 2026-09-24 09:25. This update covers the `app_main.cpp` bring-up restructure (Materials 4–15 as commented test calls, plus `run_cownect_firmware()`)._
_Update this same file after every significant build or PCB test. Hardware results are never marked PASS until measured on the physical PCB._

---

## 1. Environment

| Item | Value |
|---|---|
| ESP-IDF version | **v5.5.5** (pinned; `D:\esp\v5.5.5\esp-idf`). `CMakeLists.txt` warns if built with any other version. Do not migrate to v6.x. |
| Target chip | ESP32-S3 (`CONFIG_IDF_TARGET="esp32s3"`) |
| Module | ESP32-S3-WROOM-1-N16R8 |
| Flash size | 16 MB, DIO mode, 80 MHz, partition table *Single factory app (large)* |
| PSRAM size | 8 MB octal PSRAM (`CONFIG_SPIRAM_MODE_OCT`, 80 MHz). The size is verified at runtime by M4.2 `test_psram()`. |
| Console | USB-Serial-JTAG (native USB-C) primary; UART0 backup/ROM download |
| Compiler | xtensa-esp32s3-elf GCC 14.2.0 (`esp-14.2.0_20260121`) |
| Build system | CMake 3.30.2 + Ninja 1.12.1, ccache 4.12.1 |
| Python | ESP-IDF venv `D:\Espressif\tools\python\v5.5.5\venv` |
| Tools location | EIM install under `D:\Espressif\tools` (`IDF_TOOLS_PATH=D:\Espressif\tools`). `esp-idf\export.ps1` alone fails: it looks for the venv under `%USERPROFILE%\.espressif`. **Use the Desktop shortcut `IDF_v5.5.5_Powershell`** (runs `C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1`; sets `IDF_PATH`, the tools, and git). |
| Git | Not on PATH (CMake warns; harmless) |
| Vendored code | Semtech `sx126x_driver` v2.5.0 (commit `a10c5df`), unmodified |
| Last build | 2026-09-24 09:24, `idf.py build` (default `app_main.cpp`: all tests commented) |

## 2. Build Result

### ✅ BUILD PASS

* Default configuration (every test call and `run_cownect_firmware()` commented): no errors and no warnings. The app is 0x50FD0 bytes (78% of the 0x177000 partition free). It is small because the linker drops the unreferenced test code.
* **Link check with all 68 calls uncommented** (65 Material tests, `test_unit_all`, `test_start_console`, `run_cownect_firmware`): no errors and no warnings. The app is 0xF42C0 bytes (35% free). `app_main.cpp` was restored to the all-commented default and rebuilt afterwards.
* Bootloader: 0x5170 bytes (36% free).

## 3. Implementation Status

The Software test column shows compile/link results only. No test has been executed on the target yet.

| Material | Subsystem | Implementation | Software test | Hardware validation |
|---|---|---|---|---|
| 4 | Board bring-up: pins, MODE_SW, PERIPH_EN, I2C, ADC1 ownership, PSRAM, boot report | Implemented | Builds; not executed | NOT TESTED |
| 5 | LIS2DW12 accelerometer (I2C, FIFO) | Implemented | Builds; not executed | NOT TESTED |
| 6 | Temperature: MCP9700 (board) + MF58 (cow), one-shot ADC | Implemented | Builds; not executed | NOT TESTED |
| 7 | ATGM336H GPS (UART2, NMEA RMC/GGA) | Implemented | Builds; not executed | NOT TESTED |
| 8 | Analog microphone (ADC continuous, 32 kS/s) | Implemented | Builds; not executed | NOT TESTED |
| 9 | Integrated CaptureSession (MIC,TB,MIC,TC @ 64 kS/s) | Implemented | Builds; not executed | NOT TESTED |
| 10 | CycleScheduler, Power/SleepManager (120 s / 30 s, timer deep sleep) | Implemented | Builds; not executed | NOT TESTED |
| 11 | SimpleFeatureProcessor / TelemetryRecord | Implemented | Builds; not executed | NOT TESTED |
| 12 | SX1262 / Ra-01SH LoRa radio | Implemented; **RF TX blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |
| 13 | Capture stream codec + LORA_FULL | Implemented; **RF blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |
| 14 | Wi-Fi STA + prototype TCP upload (Rev B) | Implemented; **blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |
| 15 | Hybrid: LoRa telemetry then Wi-Fi raw upload | Implemented; **blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |

Materials 16 (Heltec gateway) and 17 (Jetson receiver) are separate projects.

## 4. Compile/API Problems Found

| # | Problem | Affected file | Cause | Fix |
|---|---|---|---|---|
| 1 | `-Werror=format-truncation`: `'%s' directive output may be truncated writing up to 319 bytes into a region of size 316` | `components/devtest/test_lora.cpp` (`cmd_lora_config`) | The BLOCKED reason buffer was smaller than `LoraConfigReport::missing` (320 B) plus its prefix | Buffer sized `sizeof(rep.missing) + 32` |
| 2 | `export.ps1`: "ESP-IDF Python virtual environment ... not found" (environment, not code) | build environment | The EIM install keeps its tools in `D:\Espressif\tools`, not `%USERPROFILE%\.espressif` | Set `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and PATH to the `D:\Espressif\tools` binaries (see section 1) |

## 5. app_main.cpp Test Checklist

The list follows the order in `main/app_main.cpp`. `cownect_system_init()` (safe base init, PERIPH_EN OFF) and `bringup_print_summary()` always run.
**Currently enabled: none.** All test calls and `run_cownect_firmware()` are commented, so the board idles after the base init.

| ID | Call | Purpose | Gated / notes |
|---|---|---|---|
| M4.1 | `test_boot_report()` | FW/IDF version, reset + wake cause, RTC state, mode | — |
| M4.2 | `test_psram()` | PSRAM self-test + mic capture buffer | — |
| M4.3 | `test_board_gpio()` | Pin map, BOOT / MODE_SW levels (15 s watch) | operator |
| M4.4 | `test_operation_mode()` | Debounced PROG switch (20 s) | operator |
| M4.5 | `test_peripheral_enable()` | PERIPH_EN ON 10 s / OFF 10 s | operator, measure rail |
| M4.6 | `test_adc_smoke()` | Raw GPIO1 / GPIO2 / GPIO5 | — |
| M4.7 | `test_config_status()` | CONFIG_NOT_SET overview | — |
| M5.1 | `test_accelerometer()` | WHO_AM_I, register readback, 30 s capture | — |
| M5.2 | `test_accelerometer_overrun()` | Deliberate FIFO overrun | — |
| M5.3 | `test_accelerometer_repeat()` | 3 × 5 s captures | — |
| M5.4 | `test_accelerometer_power_cycle()` | Rail off/on re-init | — |
| M5.5 | `test_accelerometer_decode()` | Decode math | no hardware |
| M6.1 | `test_temperature()` | Board + cow, 1 Hz, 30 s | — |
| M6.2 | `test_mcp9700()` | MCP9700 only | — |
| M6.3 | `test_thermistor()` | MF58 only; unplug/short | — |
| M6.4 | `test_adc_conflict()` | ADC1 conflict refused | — |
| M6.5 | `test_temperature_math()` | Conversion math | no hardware |
| M7.1 | `test_gps()` | UART/NMEA traffic | — |
| M7.2 | `test_gps_discovery()` | Sentence identifiers | — |
| M7.3 | `test_gps_capture()` | Fix retention | outdoor for a fix |
| M7.4 | `test_gps_power_cycle()` | VCC off/on, VBAT retained | — |
| M7.5 | `test_nmea_parser()` | NMEA parser | no hardware |
| M8.1 | `test_microphone()` | 32 kS/s capture, 30 s | — |
| M8.2 | `test_mic_stream()` | ADC stream identification | — |
| M8.3 | `test_mic_overflow()` | Deliberate slow consumer | — |
| M8.4 | `test_mic_power_cycle()` | Rail off/on re-init | — |
| M8.5 | `test_mic_dump()` | 1 s capture, first/last 32 samples | operator |
| M9.1 | `test_analog_adc_engine()` | MIC,TB,MIC,TC pattern | — |
| M9.2 | `test_sensor_manager()` | 5 s integrated capture | — |
| M9.3 | `test_capture()` | Full-length (30 s) capture | — |
| M9.4 | `test_capture_repeat()` | 3 × 5 s captures | — |
| M9.5 | `test_capture_power_cycle()` | Capture, rail off/on, capture | — |
| M10.1 | `test_scheduler()` | Awake cycle 20 s / 5 s | — |
| M10.2 | `test_power_manager()` | Capture then rail OFF | — |
| M10.3 | `test_sleep_manager()` | 10 s timer deep sleep | deep sleep, PROG = NORMAL, run alone |
| M10.4 | `test_scheduler_cycle()` | 20 s / 5 s cycle with sleep | deep sleep, run alone |
| M10.5 | `test_scheduler_repeat()` | 3 cycles | deep sleep, run alone |
| M10.6 | `test_scheduler_overrun()` | Forced overrun | — |
| M10.7 | `test_scheduler_production_cycle()` | One 120 s / 30 s cycle | deep sleep, run alone |
| M11.1 | `test_feature_processor()` | Feature math on fixture | no hardware |
| M11.2 | `test_telemetry()` | Real capture → TelemetryRecord | — |
| M12.1 | `test_lora_spi()` | SPI GetStatus | non-radiating |
| M12.2 | `test_lora_reset()` | NRESET → STDBY_RC | non-radiating |
| M12.3 | `test_lora_busy()` | BUSY handshake + standby | non-radiating |
| M12.4 | `test_lora_configuration()` | RF profile / TX gate; applies profile | **RF profile** (non-radiating) |
| M12.5 | `test_lora_tx()` | 10 PING packets | **RF TX gate** |
| M12.6 | `test_lora_rx()` | 30 s receive | **RF profile** |
| M12.7 | `test_lora_ping()` | PING/ACK | **RF TX gate + RX timeout** |
| M12.8 | `test_lora_power_cycle()` | Rail off/on re-init | non-radiating |
| M13.1 | `test_serialization()` | Capture stream round trip | no hardware |
| M13.2 | `test_crc32()` | CRC-32 check value | no hardware |
| M13.3 | `test_fragmentation()` | Fragment reconstruction | no hardware |
| M13.4 | `test_packet_codecs()` | Telemetry / test / ACK / upload header | no hardware |
| M13.5 | `test_lora_full_small()` | 4096 B synthetic transfer | **LORA_FULL + RF TX gate** |
| M13.6 | `test_lora_full()` | Real 30 s capture transfer | **LORA_FULL + RF TX gate** |
| M13.7 | `test_lora_full_loss()` | Deliberate loss | **LORA_FULL + RF TX gate** |
| M13.8 | `test_lora_full_overrun()` | Beyond a 20 s cycle | **LORA_FULL + RF TX gate** |
| M14.1 | `test_wifi_connect()` | Join AP | **Wi-Fi** |
| M14.2 | `test_wifi_tcp()` | 17-byte message to Jetson | **Wi-Fi + Jetson** |
| M14.3 | `test_wifi_small_upload()` | 64 KiB generated stream | **Wi-Fi + Jetson** |
| M14.4 | `test_wifi_capture_upload()` | Real 30 s capture upload | **Wi-Fi + Jetson** |
| M14.5 | `test_wifi_repeat()` | 3 × 5 s uploads | **Wi-Fi + Jetson** |
| M15.1 | `test_hybrid_short()` | 5 s capture → LoRa → Wi-Fi | **RF TX gate + Wi-Fi + Jetson** |
| M15.2 | `test_hybrid_full()` | 30 s capture → LoRa → Wi-Fi | **RF TX gate + Wi-Fi + Jetson** |
| M15.3 | `test_hybrid_repeat()` | 3 short hybrid cycles | **RF TX gate + Wi-Fi + Jetson** |
| U.1 | `test_unit_all()` | All pure-function tests | tool, no hardware |
| — | `test_start_console()` | Development console | tool |
| — | `run_cownect_firmware()` | Final product | commented until bring-up is complete |

Every gated test reports `RESULT: BLOCKED` with the missing item named, before any capture or TX.

## 6. CONFIG_NOT_SET

Set these values in `idf.py menuconfig` → **Component config → CowNect collar configuration** (or press `/` and search the option name).

**ADC**
* `COWNECT_COW_OPEN_THRESHOLD_MV` = 0, `COWNECT_COW_SHORT_THRESHOLD_MV` = 0 (until set, only rail codes and divider math are checked)

**LoRa (RF profile, Material 12)**: the collar and the Heltec gateway must use identical values.
* `FREQUENCY_HZ`, `BANDWIDTH_HZ`, `SPREADING_FACTOR`, `CODING_RATE`, `PREAMBLE_SYMBOLS`, `SYNC_WORD`, `CRC_ENABLED`, `INVERT_IQ`: empty
* `TX_POWER_DBM`, `PA_DUTY_CYCLE`, `PA_HP_MAX`, `RAMP_TIME_US`, `REGULATOR_MODE`, `TCXO`, `TCXO_STARTUP_US`, `DIO2_RF_SWITCH`: empty
* `LORA_ANTENNA_VERIFIED` = n. This is the operator's confirmation that the external antenna is connected, not a hardware defect flag.

**LORA_FULL (Material 13)**
* `LORA_FULL_FRAGMENT_PAYLOAD_BYTES`: empty (the 40 B header plus the payload must be ≤ 255 B)
* `LORA_FULL_ACK_TIMEOUT_MS`, `LORA_FULL_MAX_RETRIES`: empty (only needed in ACK mode, which is currently off)

**Wi-Fi (Material 14)**
* `WIFI_SSID`, `WIFI_PASSWORD`: empty

**TCP / Jetson**
* `JETSON_IP`: empty; `JETSON_PORT` = 0

**Timing**
* `LORA_TX_TIMEOUT_MS`, `LORA_RX_TIMEOUT_MS`: empty
* `WIFI_CONNECT_TIMEOUT_MS`, `TCP_CONNECT_TIMEOUT_MS`, `SOCKET_WRITE_TIMEOUT_MS` = 0

**Other**
* Absolute time source: not set (records carry capture_id plus monotonic time only)
* `COWNECT_COMM_MODE` = **Disabled** (the final firmware's scheduler does no communication)
* Persistent capture_id scheme: not approved (RTC memory only). Production device_id provisioning: not defined (0x00000001).

## 7. NEEDS_HARDWARE_VALIDATION

| Item | Current value | Validated by |
|---|---|---|
| SWITCHED_3V3 stabilization delay | 100 ms (dev default) | M4.5, M5.4, M7.4 |
| MODE_SW debounce, floating during break-before-make | 50 ms, no internal pull | M4.3, M4.4 |
| ECO jumper: forced-on vs GPIO-controlled | unknown | M4.5, M10.2 |
| Safe GPIO states / leakage in deep sleep | only PERIPH_EN driven LOW | M10.3 (sleep current) |
| PWRSRC position numbering (USB vs battery) | unverified | Board inspection |
| ADC1 attenuation (common to all analog channels) | 12 dB, provisional | M4.6, M6.x, M8.x, M9.1 |
| ADC DMA frame / pool size at 64 kS/s | 1024 B / 16384 B | M9.1, M8.3 |
| 4-slot MIC,TB,MIC,TC pattern accepted by IDF | unverified | M9.1 |
| Cross-channel ADC contamination | unverified | M9.1 |
| Microphone near-rail margin | 0 codes | M8.1, M8.5 |
| MF58 open/short thresholds; divider excitation | not set | M6.3 |
| LIS2DW12 raw-to-g conversion, orientation | unverified | M5.1 |
| ATGM336H default NMEA sentence set / max length | unknown / 128 B | M7.2 |
| SX1262 BUSY timeout bound | 100 ms (dev default) | M12.2, M12.3 |
| Ra-01SH RF switch (DIO2; TXEN/RXEN unconnected) | unknown | M12.5–M12.7 |
| External antenna connected | operator check before every RF TX test | Visual check, then M12.5 |
| LoRa TX interference with analog capture | not enabled (TX after capture) | Future A/B test |

## 8. Hardware Test Results

Status values: **NOT TESTED**, **PASS**, **FAIL**, **BLOCKED** (CONFIG_NOT_SET), **PARTIAL**. "BUILT" means the test compiles and links (all-enabled link check). The IDs were renumbered in this update to follow the `app_main.cpp` order. No hardware results existed before, so nothing was lost.

| Test ID | Test | Software | Hardware | Measured values | Notes |
|---|---|---|---|---|---|
| M4.1 | `test_boot_report` | BUILT | NOT TESTED | — | — |
| M4.2 | `test_psram` | BUILT | NOT TESTED | — | Expect 8 MB |
| M4.3 | `test_board_gpio` | BUILT | NOT TESTED | — | Press BOOT, move PROG |
| M4.4 | `test_operation_mode` | BUILT | NOT TESTED | — | — |
| M4.5 | `test_peripheral_enable` | BUILT | NOT TESTED | — | Measure SWITCHED_3V3 |
| M4.6 | `test_adc_smoke` | BUILT | NOT TESTED | — | — |
| M4.7 | `test_config_status` | BUILT | NOT TESTED | — | — |
| M5.1 | `test_accelerometer` | BUILT | NOT TESTED | — | — |
| M5.2 | `test_accelerometer_overrun` | BUILT | NOT TESTED | — | — |
| M5.3 | `test_accelerometer_repeat` | BUILT | NOT TESTED | — | — |
| M5.4 | `test_accelerometer_power_cycle` | BUILT | NOT TESTED | — | — |
| M5.5 | `test_accelerometer_decode` | BUILT | NOT TESTED | — | No hardware |
| M6.1 | `test_temperature` | BUILT | NOT TESTED | — | — |
| M6.2 | `test_mcp9700` | BUILT | NOT TESTED | — | — |
| M6.3 | `test_thermistor` | BUILT | NOT TESTED | — | Include unplug/short |
| M6.4 | `test_adc_conflict` | BUILT | NOT TESTED | — | — |
| M6.5 | `test_temperature_math` | BUILT | NOT TESTED | — | No hardware |
| M7.1 | `test_gps` | BUILT | NOT TESTED | — | — |
| M7.2 | `test_gps_discovery` | BUILT | NOT TESTED | — | Record sentence IDs |
| M7.3 | `test_gps_capture` | BUILT | NOT TESTED | — | — |
| M7.4 | `test_gps_power_cycle` | BUILT | NOT TESTED | — | — |
| M7.5 | `test_nmea_parser` | BUILT | NOT TESTED | — | No hardware |
| M8.1 | `test_microphone` | BUILT | NOT TESTED | — | — |
| M8.2 | `test_mic_stream` | BUILT | NOT TESTED | — | — |
| M8.3 | `test_mic_overflow` | BUILT | NOT TESTED | — | — |
| M8.4 | `test_mic_power_cycle` | BUILT | NOT TESTED | — | — |
| M8.5 | `test_mic_dump` | BUILT | NOT TESTED | — | — |
| M9.1 | `test_analog_adc_engine` | BUILT | NOT TESTED | — | — |
| M9.2 | `test_sensor_manager` | BUILT | NOT TESTED | — | — |
| M9.3 | `test_capture` | BUILT | NOT TESTED | — | — |
| M9.4 | `test_capture_repeat` | BUILT | NOT TESTED | — | — |
| M9.5 | `test_capture_power_cycle` | BUILT | NOT TESTED | — | — |
| M10.1 | `test_scheduler` | BUILT | NOT TESTED | — | — |
| M10.2 | `test_power_manager` | BUILT | NOT TESTED | — | — |
| M10.3 | `test_sleep_manager` | BUILT | NOT TESTED | — | NORMAL mode; measure sleep current |
| M10.4 | `test_scheduler_cycle` | BUILT | NOT TESTED | — | — |
| M10.5 | `test_scheduler_repeat` | BUILT | NOT TESTED | — | — |
| M10.6 | `test_scheduler_overrun` | BUILT | NOT TESTED | — | — |
| M10.7 | `test_scheduler_production_cycle` | BUILT | NOT TESTED | — | — |
| M11.1 | `test_feature_processor` | BUILT | NOT TESTED | — | No hardware |
| M11.2 | `test_telemetry` | BUILT | NOT TESTED | — | — |
| M12.1 | `test_lora_spi` | BUILT | NOT TESTED | — | Non-radiating |
| M12.2 | `test_lora_reset` | BUILT | NOT TESTED | — | Non-radiating |
| M12.3 | `test_lora_busy` | BUILT | NOT TESTED | — | Non-radiating |
| M12.4 | `test_lora_configuration` | BUILT | BLOCKED | — | RF profile CONFIG_NOT_SET |
| M12.5 | `test_lora_tx` | BUILT | BLOCKED | — | RF profile + antenna confirmation |
| M12.6 | `test_lora_rx` | BUILT | BLOCKED | — | RF profile |
| M12.7 | `test_lora_ping` | BUILT | BLOCKED | — | RF profile + antenna + RX timeout |
| M12.8 | `test_lora_power_cycle` | BUILT | NOT TESTED | — | Non-radiating |
| M13.1 | `test_serialization` | BUILT | NOT TESTED | — | No hardware |
| M13.2 | `test_crc32` | BUILT | NOT TESTED | — | No hardware |
| M13.3 | `test_fragmentation` | BUILT | NOT TESTED | — | No hardware |
| M13.4 | `test_packet_codecs` | BUILT | NOT TESTED | — | No hardware |
| M13.5 | `test_lora_full_small` | BUILT | BLOCKED | — | LORA_FULL + RF |
| M13.6 | `test_lora_full` | BUILT | BLOCKED | — | LORA_FULL + RF |
| M13.7 | `test_lora_full_loss` | BUILT | BLOCKED | — | LORA_FULL + RF |
| M13.8 | `test_lora_full_overrun` | BUILT | BLOCKED | — | LORA_FULL + RF |
| M14.1 | `test_wifi_connect` | BUILT | BLOCKED | — | SSID/password |
| M14.2 | `test_wifi_tcp` | BUILT | BLOCKED | — | + Jetson IP/port |
| M14.3 | `test_wifi_small_upload` | BUILT | BLOCKED | — | — |
| M14.4 | `test_wifi_capture_upload` | BUILT | BLOCKED | — | — |
| M14.5 | `test_wifi_repeat` | BUILT | BLOCKED | — | — |
| M15.1 | `test_hybrid_short` | BUILT | BLOCKED | — | LoRa + Wi-Fi config |
| M15.2 | `test_hybrid_full` | BUILT | BLOCKED | — | — |
| M15.3 | `test_hybrid_repeat` | BUILT | BLOCKED | — | — |
| U.1 | `test_unit_all` | BUILT | NOT TESTED | — | Runs on target, no peripherals |

## 9. Known Hardware Questions

Summarized from `hardware_questions.md`, which is the full log.

* **Power:** PWRSRC terminal numbering (#1); the PWRSRC switch rating vs Ra-01SH TX current (#2); safe deep-sleep GPIO states (#3); ECO jumper position (#6).
* **ADC:** 12 dB attenuation is provisional (#10); whether IDF accepts the 4-slot 64 kS/s pattern (#13); cross-channel contamination (#14); MF58 fault thresholds unset (#16).
* **GPS:** the ATGM336H default sentence set is unknown (#22).
* **Radio:** the board uses an **external antenna** (confirmed). The unlabeled ANT pin on the schematic is a PCB verification item, not a firmware block (#25). Every RF TX test warns `VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX`. Other open items: Ra-01SH band conflict (#26); Heltec V3 band variant (#27); TCXO vs crystal (#29); DIO2 RF switch, with TXEN/RXEN unconnected (#30); LDO vs DC-DC (#31).
* **Network:** SSID, password, Jetson IP/port and timeouts not supplied (#38–#40).

## 10. Protocol / Interface Changes

No on-air or on-wire byte format changed in this update. `docs/protocols.md` is still the reference for M16/M17.

| Area | Change | Note |
|---|---|---|
| `main/app_main.cpp` | Rewritten as the bring-up checklist: base init, 65 commented Material test calls in order, 2 commented tools, summary, then `// run_cownect_firmware();` | Default flash starts nothing |
| `components/devtest/include/bringup_tests.h` | **New** public API: `test_*()` functions with IDs M4.1–M15.3 | Implementations sit in the per-Material `test_*.cpp` next to the existing console bodies |
| Result banner | `MATERIAL n TEST Mn.k: …` … `Mn.k RESULT: PASS/FAIL/BLOCKED/NEEDS_HARDWARE_VALIDATION` + `Reason:` | `report_fail/blocked/hw` now record their text for the final `Reason:`. Failure paths that were silent before now give a reason. |
| `components/cownect_app` | **New**: `cownect_system_init()` (idempotent safe init) + `run_cownect_firmware()` (the former `app_main` mode routing + scheduler loop) | Product behavior is unchanged |
| Deep-sleep tests | Resume from the RTC context when app_main calls them again after the timer wake | `devtest_rtc_pending()` / `devtest_rtc_resume()` |
| New console tests | `test gpio`, `test lora_spi`, `test lora_reset` | The console is kept |
| `lora_config` test | Applies the complete RF profile to the chip (non-radiating) and reports BLOCKED when the profile is incomplete | — |
| Hybrid tests | Preflight: BLOCKED before capture if the LoRa TX gate or Wi-Fi/Jetson config is missing | Gates are still enforced again in the adapter and uploader |
| `full_overrun` test | `allow_deep_sleep = false` | Prevents a sleep → reboot → RF re-run loop |
| RF TX tests | Antenna warning in the banner + 3 s countdown after the gates pass | TX gates unchanged |
| Antenna gate wording | Kconfig prompt / reason: "external antenna connection not confirmed (set COWNECT_LORA_ANTENNA_VERIFIED)" | Same gate, no longer worded as a hardware defect |
| `data::rtc_state_valid_at_boot()` | New accessor used by the boot report | — |

## 11. Next Recommended Test

**M4.1 `test_boot_report()`, then M4.2 `test_psram()`.** Both are safe: no rail, no RF.

1. In `main/app_main.cpp`, uncomment `// test_boot_report();` (only that line).
2. Connect the native USB-C port (USB-Serial-JTAG).
3. Build, flash and monitor. Expect `M4.1 RESULT: PASS`, then check that the reset reason and mode match the board.
4. Comment it again, uncomment `test_psram();`, and repeat. Expect 8 MB and `M4.2 RESULT: PASS`.
5. Record both in section 8. Continue with M4.3 `test_board_gpio()`.

From the Desktop shortcut **`IDF_v5.5.5_Powershell`**, after `cd E:\Cownect\Firmware`:

Build:
```
idf.py build
```

Flash:
```
idf.py -p COMx flash
```

Monitor:
```
idf.py -p COMx monitor
```

Or combined:
```
idf.py -p COMx flash monitor
```

Replace `COMx` with the port Windows assigns to the board (Device Manager → Ports). The actual port is not known yet.
