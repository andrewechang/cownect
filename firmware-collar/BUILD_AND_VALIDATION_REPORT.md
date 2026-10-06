# CowNect Firmware Build and Validation Report

_Last updated: 2026-10-05. This update records the first PCB bring-up results (Materials 4–8, 10, 12) from the user's `test_result.cpp`, plus the failure analysis. Follow-up the same day: a built-in test profile (`cn_config/include/test_profile.h`) now fills empty menuconfig settings, so the tests set up LoRa, LORA_FULL and the GPS baud rate themselves. A build-time PSRAM guard was also added. Build PASS._
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
| Last build | 2026-10-05, `idf.py build` (default `app_main.cpp`: all tests commented) |

## 2. Build Result

### ✅ BUILD PASS

* 2026-10-05 (PSRAM-tolerant boot + mic fallback + ADC diagnostics), default configuration: no errors and no warnings. The app is 0x51920 bytes (78% free).
* Link check with M4.2, M7.1, M8.1, M8.2, M12.4, M12.5 and M13.5 uncommented: no errors and no warnings (0x56810 bytes, 77% free). `app_main.cpp` was restored to all-commented and `build/` was rebuilt from it.
* Earlier the same day (test-profile update): default build PASS, 0x51100 bytes.
* The first build attempt failed: `test_gps.cpp` had broken string literals from an editing mistake. Fixed before the passing build.
* Previous all-enabled link check (2026-09-24, 68 calls uncommented): no errors and no warnings. The app was 0xF42C0 bytes (35% free).
* Bootloader: 0x5170 bytes (36% free).
* The build now **refuses** a non-octal PSRAM configuration (`#error` in `cownect_app.cpp`).
* `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` (in `sdkconfig` and `sdkconfig.defaults`): if PSRAM isn't detected, the board finishes booting and reports `PSRAM=NOT DETECTED` instead of aborting in a reset loop.

## 3. Implementation Status

Hardware validation summarizes section 8 (results as reported by the user on the physical PCB).

| Material | Subsystem | Implementation | Software test | Hardware validation |
|---|---|---|---|---|
| 4 | Board bring-up: pins, MODE_SW, PERIPH_EN, I2C, ADC1 ownership, PSRAM, boot report | Implemented | Executed on PCB | **PARTIAL**: 6/7 PASS; M4.6 FAIL (microphone path) |
| 5 | LIS2DW12 accelerometer (I2C, FIFO) | Implemented | Executed on PCB | **PARTIAL**: 4/5 PASS; M5.4 did not run (PSRAM boot loop, wrong image) |
| 6 | Temperature: MCP9700 (board) + MF58 (cow), one-shot ADC | Implemented | Executed on PCB | **PARTIAL**: M6.1–M6.3 PASS; M6.4 FAIL (microphone path); M6.5 not run |
| 7 | ATGM336H GPS (UART2, NMEA RMC/GGA) | Implemented | Executed on PCB | **PARTIAL**: M7.1/M7.2 FAIL (NMEA framing); M7.3–M7.5 PASS |
| 8 | Analog microphone (ADC continuous, 32 kS/s) | Implemented | Executed on PCB | **FAIL**: M8.1/M8.2 `ESP_ERR_NO_MEM`; M8.3–M8.5 not run |
| 9 | Integrated CaptureSession (MIC,TB,MIC,TC @ 64 kS/s) | Implemented | Builds; not executed | NOT TESTED (needs Material 8 first) |
| 10 | CycleScheduler, Power/SleepManager (120 s / 30 s, timer deep sleep) | Implemented | Partly executed | **PARTIAL**: M10.1/M10.2 PASS; M10.3 sleep duration unconfirmed |
| 11 | SimpleFeatureProcessor / TelemetryRecord | Implemented | Builds; not executed | NOT TESTED |
| 12 | SX1262 / Ra-01SH LoRa radio | Implemented; **RF TX blocked by CONFIG_NOT_SET** | Partly executed | **PARTIAL**: M12.1–M12.3 PASS; M12.4 BLOCKED (RF profile) |
| 13 | Capture stream codec + LORA_FULL | Implemented; **RF blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |
| 14 | Wi-Fi STA + prototype TCP upload (Rev B) | Implemented; **blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |
| 15 | Hybrid: LoRa telemetry then Wi-Fi raw upload | Implemented; **blocked by CONFIG_NOT_SET** | Builds; not executed | NOT TESTED |

Materials 16 (Heltec gateway) and 17 (Jetson receiver) are separate projects.

## 4. Compile/API Problems Found

| # | Problem | Affected file | Cause | Fix |
|---|---|---|---|---|
| 1 | `-Werror=format-truncation`: `'%s' directive output may be truncated writing up to 319 bytes into a region of size 316` | `components/devtest/test_lora.cpp` (`cmd_lora_config`) | The BLOCKED reason buffer was smaller than `LoraConfigReport::missing` (320 B) plus its prefix | Buffer sized `sizeof(rep.missing) + 32` |
| 2 | `export.ps1`: "ESP-IDF Python virtual environment ... not found" (environment, not code) | build environment | The EIM install keeps its tools in `D:\Espressif\tools`, not `%USERPROFILE%\.espressif` | Set `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH` and PATH to the `D:\Espressif\tools` binaries (see section 1) |
| 3 | **Runtime (M5.4):** boot loop `E quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode` → `Failed to init external RAM!` → `abort()` | flashed image / `sdkconfig` | The message comes only from IDF's **quad** PSRAM driver (`esp_psram_impl_ap_quad.c`). The N16R8 module has **octal** PSRAM, so a quad build can never boot on it. The image flashed for M5.4 was built with `CONFIG_SPIRAM_MODE_QUAD`. The current `sdkconfig` and `build/cownect_collar.bin` are octal, and M4.2 passed with an octal build. The abort happens before `app_main`, so the accelerometer code never ran: this is **not** an accelerometer or PERIPH_EN fault. | A guard was added: the build now stops with `#error` unless `CONFIG_SPIRAM=y` and `CONFIG_SPIRAM_MODE_OCT=y`. Still to do: clean rebuild, flash from the same build directory, confirm the boot log shows `octal_psram`, re-run M5.4. |
| 4 | **Runtime (M4.6, M6.4, M8.1, M8.2):** microphone continuous ADC init/start fails, `ESP_ERR_NO_MEM` | `drivers/microphone/microphone_capture_driver.cpp`, `data/capture_session.cpp` | Common root: all four tests fail at `microphone.init()` / `start_capture()`. The main source of `ESP_ERR_NO_MEM` there is `MicrophoneCaptureDriver::init()` when the 1.93 MB PSRAM mic buffer (`capture_storage_init()`, done at boot) is not allocated. The IDF ADC driver needs only ~22 KB internal RAM, and static internal RAM use is small, so an ADC-driver allocation failure is unlikely. Not confirmed yet: the boot log lines are needed. Problem 3 shows that at least one flashed image had wrong PSRAM settings. | **Diagnostics + fallback added, needs re-test.** (a) Every boot prints a `[BOOT] reset=… PSRAM=… internal free=… mic buffer=…` line. (b) If the PSRAM allocation fails, the mic buffer falls back to 64000 samples (2 s, 128 KB) of internal RAM. Mic tests then shorten their captures to fit and say so. A full 30 s capture still needs PSRAM. (c) Every failure point in `AnalogAdcEngine::start()` now logs a `start: …` line with the error, plus internal heap figures when the ADC driver allocation fails. The next M8.1 log will show the exact cause. Note: an earlier user summary said "PSRAM doesn't seem to be working". With the previous settings a missing PSRAM aborts every boot, so on boards that ran tests, PSRAM had come up. |
| 5 | **Runtime (M7.1, M7.2):** "NMEA issue": no framed sentences / identifiers | `drivers/gps` (UART2 9600 8N1) | M7.4 PASS shows UART bytes arrive after a rail cycle, so wiring and power work. M7.3 PASS does **not** prove NMEA: it passes whenever the capture ends on time. Bytes arriving with no valid sentences points to a **baud-rate mismatch** (the ATGM336H default is not confirmed, hardware question #22) or to checksum/overlength rejection. | **Change made, needs re-test.** Every GPS test now detects the baud rate first. It tries 9600, 115200, 38400, 57600, 19200, 4800 for 2.5 s each, prints one `bytes/sentences/checksum_err/overlength` line per rate, and keeps the first rate that frames ≥ 2 valid sentences. If none works it stays at 9600. Record the `[GPS] using N baud` line. If the rate isn't 9600, the production `GPS_BAUD` must be updated. |
| 6 | **Configuration:** M12.4 (and M12.5–M12.7, M13.5–M13.8) BLOCKED because the RF profile, TX/RX timeouts, antenna confirmation and LORA_FULL fragment size were empty in menuconfig | `lora_radio/lora_config.cpp`, `lora_full/lora_full_config.cpp` | The loaders only read menuconfig strings, which are empty by default | **Built-in test profile** `cn_config/include/test_profile.h` (US915, chosen by the user 2026-10-05) fills every empty field. A value set in menuconfig still wins. M12.4 prints the profile in use and notes when the built-in values were used. |

## 5. app_main.cpp Test Checklist

The list follows the order in `main/app_main.cpp`. `cownect_system_init()` (safe base init, PERIPH_EN OFF) and `bringup_print_summary()` always run.
**Currently enabled:** in `main/app_main.cpp`, **none**: all test calls and `run_cownect_firmware()` are commented, so the board idles after the base init. The user's working copy `test_result.cpp` (annotated results) has **M4.1 `test_boot_report()`** uncommented.

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

**LoRa (RF profile, Material 12): now covered by the built-in test profile** (`components/cn_config/include/test_profile.h`). Menuconfig fields are still empty, and the profile fills them. A menuconfig value always wins. The Heltec gateway must use identical values.
* US915 bench profile: 915.000 MHz, BW 125 kHz, SF9, CR 4/5, preamble 8, sync word 0x12, CRC on, IQ normal
* PA: SetTxParams 22, paDutyCycle 2, hpMax 2 (datasheet +14 dBm row, so the output is ~+14 dBm), ramp 200 µs
* Regulator DCDC, TCXO NONE, DIO2 RF switch 1 (Kconfig defaults, unchanged)
* Antenna confirmation: set by the test profile (external antenna confirmed by the user). RF TX tests still print the warning and a 3 s countdown.

**LORA_FULL (Material 13)**
* `LORA_FULL_FRAGMENT_PAYLOAD_BYTES`: **200** from the test profile (40 B header + 200 B = 240 B ≤ 255 B)
* `LORA_FULL_ACK_TIMEOUT_MS`, `LORA_FULL_MAX_RETRIES`: empty (only needed in ACK mode, which is currently off)

**Wi-Fi (Material 14)**
* `WIFI_SSID`, `WIFI_PASSWORD`: empty

**TCP / Jetson**
* `JETSON_IP`: empty; `JETSON_PORT` = 0

**Timing**
* `LORA_TX_TIMEOUT_MS` = 3000, `LORA_RX_TIMEOUT_MS` = 5000 from the test profile (a 255 B packet at SF9/125 kHz is ~1.25 s on air)
* `WIFI_CONNECT_TIMEOUT_MS`, `TCP_CONNECT_TIMEOUT_MS`, `SOCKET_WRITE_TIMEOUT_MS` = 0

**Other**
* Absolute time source: not set (records carry capture_id plus monotonic time only)
* `COWNECT_COMM_MODE` = **Disabled** (the final firmware's scheduler does no communication)
* Persistent capture_id scheme: not approved (RTC memory only). Production device_id provisioning: not defined (0x00000001).

## 7. NEEDS_HARDWARE_VALIDATION

| Item | Current value | Validated by |
|---|---|---|
| SWITCHED_3V3 stabilization delay | 100 ms (dev default) | M4.5 PASS, M7.4 PASS (works at 100 ms); M5.4 pending; rail voltage not recorded |
| MODE_SW debounce, floating during break-before-make | 50 ms, no internal pull | M4.3 PASS, M4.4 PASS |
| ECO jumper: forced-on vs GPIO-controlled | unknown | M4.5, M10.2 |
| Safe GPIO states / leakage in deep sleep | only PERIPH_EN driven LOW | M10.3 (sleep current; actual sleep duration still to be confirmed) |
| PWRSRC position numbering (USB vs battery) | unverified | Board inspection |
| ADC1 attenuation (common to all analog channels) | 12 dB, provisional | M4.6, M6.x, M8.x, M9.1 |
| ADC DMA frame / pool size at 64 kS/s | 1024 B / 16384 B | M9.1, M8.3 |
| 4-slot MIC,TB,MIC,TC pattern accepted by IDF | unverified | M9.1 |
| Cross-channel ADC contamination | unverified | M9.1 |
| Microphone near-rail margin | 0 codes | M8.1, M8.5 |
| MF58 open/short thresholds; divider excitation | not set | M6.3 |
| LIS2DW12 raw-to-g conversion, orientation | unverified | M5.1 |
| ATGM336H default NMEA sentence set / max length / **baud rate** | unknown / 128 B / 9600 assumed | M7.2 (currently FAIL, see section 4 #5) |
| SX1262 BUSY timeout bound | 100 ms (dev default) | M12.2 PASS, M12.3 PASS |
| Ra-01SH RF switch (DIO2; TXEN/RXEN unconnected) | unknown | M12.5–M12.7 |
| External antenna connected | operator check before every RF TX test | Visual check, then M12.5 |
| LoRa TX interference with analog capture | not enabled (TX after capture) | Future A/B test |

## 8. Hardware Test Results

Status values: **NOT TESTED**, **PASS**, **FAIL**, **BLOCKED** (CONFIG_NOT_SET), **PARTIAL**. "BUILT" means the test compiles and links (all-enabled link check). "RUN" means it was executed on the PCB. The 2026-10-05 results come from the user's notes in `test_result.cpp`. No measured values or serial logs were supplied except the M5.4 boot log. The IDs were renumbered in this update to follow the `app_main.cpp` order. No hardware results existed before, so nothing was lost.

| Test ID | Test | Software | Hardware | Measured values | Notes |
|---|---|---|---|---|---|
| M4.1 | `test_boot_report` | RUN | **PASS** | — | User-reported PASS |
| M4.2 | `test_psram` | RUN | **PASS** | — | User-reported PASS (octal build) |
| M4.3 | `test_board_gpio` | RUN | **PASS** | — | User-reported PASS |
| M4.4 | `test_operation_mode` | RUN | **PASS** | — | User-reported PASS |
| M4.5 | `test_peripheral_enable` | RUN | **PASS** | SWITCHED_3V3 not recorded | User-reported PASS; record rail voltage ON/OFF |
| M4.6 | `test_adc_smoke` | RUN | **FAIL** | — | Microphone init failed (section 4 #4). GPIO1/GPIO2 one-shot part not reported |
| M4.7 | `test_config_status` | RUN | **PASS** | — | User-reported PASS |
| M5.1 | `test_accelerometer` | RUN | **PASS** | — | User-reported PASS |
| M5.2 | `test_accelerometer_overrun` | RUN | **PASS** | — | User-reported PASS |
| M5.3 | `test_accelerometer_repeat` | RUN | **PASS** | — | User-reported PASS |
| M5.4 | `test_accelerometer_power_cycle` | RUN | **FAIL** | — | Boot loop: `quad_psram` not connected, abort before app_main. Wrong PSRAM-mode image; test did not run (section 4 #3). Re-run |
| M5.5 | `test_accelerometer_decode` | RUN | **PASS** | — | User-reported PASS |
| M6.1 | `test_temperature` | RUN | **PASS** | — | User-reported PASS |
| M6.2 | `test_mcp9700` | RUN | **PASS** | — | User-reported PASS |
| M6.3 | `test_thermistor` | RUN | **PASS** | — | User-reported PASS; thresholds still CONFIG_NOT_SET |
| M6.4 | `test_adc_conflict` | RUN | **FAIL** | — | Could not start microphone continuous mode (section 4 #4); the conflict check itself was not reached |
| M6.5 | `test_temperature_math` | BUILT | NOT TESTED | — | No hardware |
| M7.1 | `test_gps` | RUN | **FAIL** | — | NMEA issue: no framed sentences (section 4 #5) |
| M7.2 | `test_gps_discovery` | RUN | **FAIL** | — | NMEA issue: no identifiers (section 4 #5) |
| M7.3 | `test_gps_capture` | RUN | **PASS** | — | User-reported PASS. This test passes whenever the capture ends on time; it does not prove NMEA |
| M7.4 | `test_gps_power_cycle` | RUN | **PASS** | — | User-reported PASS: UART bytes resumed after rail cycle |
| M7.5 | `test_nmea_parser` | RUN | **PASS** | — | User-reported PASS |
| M8.1 | `test_microphone` | RUN | **FAIL** | — | `ESP_ERR_NO_MEM` at init/start (section 4 #4) |
| M8.2 | `test_mic_stream` | RUN | **FAIL** | — | `ESP_ERR_NO_MEM` at init/start (section 4 #4) |
| M8.3 | `test_mic_overflow` | BUILT | NOT TESTED | — | — |
| M8.4 | `test_mic_power_cycle` | BUILT | NOT TESTED | — | — |
| M8.5 | `test_mic_dump` | BUILT | NOT TESTED | — | — |
| M9.1 | `test_analog_adc_engine` | BUILT | NOT TESTED | — | — |
| M9.2 | `test_sensor_manager` | BUILT | NOT TESTED | — | — |
| M9.3 | `test_capture` | BUILT | NOT TESTED | — | — |
| M9.4 | `test_capture_repeat` | BUILT | NOT TESTED | — | — |
| M9.5 | `test_capture_power_cycle` | BUILT | NOT TESTED | — | — |
| M10.1 | `test_scheduler` | RUN | **PASS** | — | User-reported PASS |
| M10.2 | `test_power_manager` | RUN | **PASS** | — | User-reported PASS |
| M10.3 | `test_sleep_manager` | RUN | **PARTIAL** | Actual sleep duration not confirmed | Woke and reported; check the time slept (expect ~10 s) and the sleep current |
| M10.4 | `test_scheduler_cycle` | BUILT | NOT TESTED | — | — |
| M10.5 | `test_scheduler_repeat` | BUILT | NOT TESTED | — | — |
| M10.6 | `test_scheduler_overrun` | BUILT | NOT TESTED | — | — |
| M10.7 | `test_scheduler_production_cycle` | BUILT | NOT TESTED | — | — |
| M11.1 | `test_feature_processor` | BUILT | NOT TESTED | — | No hardware |
| M11.2 | `test_telemetry` | BUILT | NOT TESTED | — | — |
| M12.1 | `test_lora_spi` | RUN | **PASS** | — | User-reported PASS; non-radiating |
| M12.2 | `test_lora_reset` | RUN | **PASS** | — | User-reported PASS; non-radiating |
| M12.3 | `test_lora_busy` | RUN | **PASS** | — | User-reported PASS; non-radiating |
| M12.4 | `test_lora_configuration` | RUN | **BLOCKED** | — | Confirmed on PCB: RF profile missing FREQUENCY_HZ, BANDWIDTH_HZ, SPREADING_FACTOR, CODING_RATE, PREAMBLE_SYMBOLS, SYNC_WORD, CRC_ENABLED, INVERT_IQ, TX_POWER_DBM, PA_DUTY_CYCLE, PA_HP_MAX, RAMP_TIME_US |
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
* **GPS:** the ATGM336H default sentence set is unknown (#22). Its **default baud rate** is also unconfirmed: M7.1/M7.2 see UART bytes but no valid NMEA (section 4 #5).
* **Radio:** the board uses an **external antenna** (confirmed). The unlabeled ANT pin on the schematic is a PCB verification item, not a firmware block (#25). Every RF TX test warns `VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX`. Other open items: Ra-01SH band conflict (#26); Heltec V3 band variant (#27); TCXO vs crystal (#29); DIO2 RF switch, with TXEN/RXEN unconnected (#30); LDO vs DC-DC (#31).
* **Network:** SSID, password, Jetson IP/port and timeouts not supplied (#38–#40).

## 10. Protocol / Interface Changes

No on-air or on-wire byte format changed in this update. `docs/protocols.md` is still the reference for M16/M17. The RF parameters now have values (US915 test profile), so the **Heltec gateway (M16) must be configured with the same frequency, BW, SF, CR, preamble, sync word, CRC and IQ.**

| Area | Change | Note |
|---|---|---|
| `cn_config/include/test_profile.h` | **New (2026-10-05):** built-in bring-up values plus `test_profile::pick(menuconfig, builtin)` | Used only where a menuconfig field is empty |
| `lora_config.cpp` | RF profile and TX/RX timeouts fall back to the test profile. The TX gate accepts the profile's antenna confirmation. `LoraConfigReport::uses_test_profile` added. | Header comment updated: the loader no longer says "never substitutes defaults" |
| `lora_full_config.cpp` | Fragment payload falls back to the test profile (200 B) | ACK mode still off |
| `Atgm336hDriver` | `set_baud()` / `baud()`. The rate is kept across `deinit()/init()` | No command is sent to the GPS |
| GPS tests (M7.x) | Baud detection before each test (once per boot) | M7.1 PASS text shows the detected rate |
| M12.4 / status output | Prints the full profile in use and flags built-in values | — |
| `sx1262_adapter.cpp` | "profile applied" log also shows paDutyCycle / hpMax | Log only |
| `cownect_app.cpp` | `#error` unless octal PSRAM is configured. A `[BOOT]` memory/reset status line is printed at every boot. | Build-time guard + diagnostics |
| `data::capture_storage_init()` | Internal-RAM fallback (`config::MIC_FALLBACK_MAX_SAMPLES` = 64000) when the PSRAM allocation fails. New `capture_storage_microphone_in_psram()`. | Full 30 s capture still needs PSRAM |
| Mic tests M8.1/M8.2 | Capture length shortened to fit the buffer, with a printed notice | Only when the fallback buffer is in use |
| `AnalogAdcEngine::start()` | `ESP_LOGE("start: …")` at every failure return | Log only |
| `sdkconfig(.defaults)` | `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` | No abort loop without PSRAM |
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

**Clean rebuild, then M4.2 `test_psram()` → M8.1 `test_microphone()`, keeping the full serial log.** This one sequence checks the PSRAM image fix (section 4 #3) and locates the microphone `ESP_ERR_NO_MEM` (#4). The microphone path also blocks M4.6, M6.4, Material 8 and all of Material 9.

1. Build from scratch with the current `sdkconfig` (octal PSRAM; do not change it in menuconfig).
2. In `main/app_main.cpp`, uncomment only `test_psram();`. Build, flash, monitor. The boot log must show `octal_psram` (not `quad_psram`) and `CAPTURE: PSRAM microphone buffer 1926400 bytes ... allocated`. Expect `M4.2 RESULT: PASS`.
3. Comment it again, uncomment only `test_microphone();`, flash, monitor. Save the whole log from boot to `M8.1 RESULT`, especially any `CAPTURE`, `MIC`, `ADC_ENGINE` or `adc_continuous` error lines.
4. Re-run M5.4 `test_accelerometer_power_cycle()` alone on the same clean build.
5. Re-run M7.1 `test_gps()`. It now detects the baud rate itself: record the per-rate lines and `[GPS] using N baud`.
6. Run M12.4 `test_lora_configuration()`. Expect `M12.4 RESULT: PASS` with the US915 profile printed (non-radiating). Before M12.5+ (RF TX), connect the external antenna and set the Heltec gateway to the same profile.
7. For M10.3, time the gap between `[sleep] entering 10 s timer deep sleep` and the wake banner (expect ~10 s).

For every run, keep the `[BOOT] …` line and, for M5.4, the first ~20 lines including the ROM `rst:` line. With the new build, a repeating M5.4 output now shows the real reset reason (e.g. `BROWNOUT` when switching the rail on dips the 3.3 V supply). Each reset re-runs the test from `app_main`, which would explain the endless printing.

Paste the actual serial logs (copied from the monitor window, not a summary) into a file, e.g. `log2.txt`.

From the Desktop shortcut **`IDF_v5.5.5_Powershell`**, after `cd E:\Cownect\Firmware`:

Clean build:
```
idf.py fullclean
idf.py build
```

Flash and monitor:
```
idf.py -p COMx flash monitor
```

Replace `COMx` with the port Windows assigns to the board (Device Manager → Ports). The actual port is not known yet.
