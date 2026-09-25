# CowNect collar firmware (prototype)

ESP32-S3-WROOM-1-N16R8, ESP-IDF **v5.5.5** (pinned; do not migrate to v6.x), C++.
Implements Materials 4-15. The Heltec gateway (Material 16) and Jetson receiver (Material 17)
are separate projects and are not part of this repository.

## Build

```
idf.py set-target esp32s3
idf.py menuconfig      # Component config -> "CowNect collar configuration"
idf.py build
idf.py -p <port> flash monitor
```

`sdkconfig` is git-ignored because menuconfig stores the Wi-Fi password in it.

## PCB bring-up sequence (`main/app_main.cpp`)

`app_main.cpp` is the bring-up checklist. It holds one commented call per test, in Material
order, and ends with the commented final firmware call `// run_cownect_firmware();`.
By default everything is commented. The firmware then only does the safe base init
(PERIPH_EN commanded OFF) and idles, so no sensors, Wi-Fi, LoRa TX or scheduler start.

Workflow: uncomment **one** call, then `idf.py build flash monitor`, then read the banner.
Each test prints `MATERIAL n TEST Mn.k: <title>` at the start and
`Mn.k RESULT: PASS | FAIL | BLOCKED | NEEDS_HARDWARE_VALIDATION` at the end, followed by a
`Reason:` or the operator checks. Record the result in `BUILD_AND_VALIDATION_REPORT.md`,
comment the call again, and move to the next one. Each test initializes what it needs itself.
The test bodies live in `components/devtest` (one file per Material) and call the real
drivers and managers.

| Stage | Test functions (in order) |
|---|---|
| Material 4: board | `test_boot_report` M4.1, `test_psram` M4.2, `test_board_gpio` M4.3, `test_operation_mode` M4.4, `test_peripheral_enable` M4.5, `test_adc_smoke` M4.6, `test_config_status` M4.7 |
| Material 5: LIS2DW12 | `test_accelerometer` M5.1, `test_accelerometer_overrun` M5.2, `test_accelerometer_repeat` M5.3, `test_accelerometer_power_cycle` M5.4, `test_accelerometer_decode` M5.5 |
| Material 6: temperature | `test_temperature` M6.1, `test_mcp9700` M6.2, `test_thermistor` M6.3, `test_adc_conflict` M6.4, `test_temperature_math` M6.5 |
| Material 7: GPS | `test_gps` M7.1, `test_gps_discovery` M7.2, `test_gps_capture` M7.3, `test_gps_power_cycle` M7.4, `test_nmea_parser` M7.5 |
| Material 8: microphone | `test_microphone` M8.1, `test_mic_stream` M8.2, `test_mic_overflow` M8.3, `test_mic_power_cycle` M8.4, `test_mic_dump` M8.5 |
| Material 9: integrated capture | `test_analog_adc_engine` M9.1, `test_sensor_manager` M9.2, `test_capture` M9.3, `test_capture_repeat` M9.4, `test_capture_power_cycle` M9.5 |
| Material 10: scheduler / power | `test_scheduler` M10.1, `test_power_manager` M10.2, `test_sleep_manager` M10.3*, `test_scheduler_cycle` M10.4*, `test_scheduler_repeat` M10.5*, `test_scheduler_overrun` M10.6, `test_scheduler_production_cycle` M10.7* |
| Material 11: features / telemetry | `test_feature_processor` M11.1, `test_telemetry` M11.2 |
| Material 12: SX1262 LoRa | `test_lora_spi` M12.1, `test_lora_reset` M12.2, `test_lora_busy` M12.3, `test_lora_configuration` M12.4, `test_lora_tx` M12.5†, `test_lora_rx` M12.6, `test_lora_ping` M12.7†, `test_lora_power_cycle` M12.8 |
| Material 13: serialization / LORA_FULL | `test_serialization` M13.1, `test_crc32` M13.2, `test_fragmentation` M13.3, `test_packet_codecs` M13.4, `test_lora_full_small` M13.5†, `test_lora_full` M13.6†, `test_lora_full_loss` M13.7†, `test_lora_full_overrun` M13.8† |
| Material 14: Wi-Fi / TCP / upload | `test_wifi_connect` M14.1, `test_wifi_tcp` M14.2, `test_wifi_small_upload` M14.3, `test_wifi_capture_upload` M14.4, `test_wifi_repeat` M14.5 |
| Material 15: hybrid | `test_hybrid_short` M15.1†, `test_hybrid_full` M15.2†, `test_hybrid_repeat` M15.3† |
| Final firmware | `run_cownect_firmware()` |

\* Deep sleep test: set PROG to NORMAL and run it alone. After the timer wake the chip
reboots and the same test prints the result.
† RF TX test: prints `VERIFY EXTERNAL ANTENNA IS CONNECTED BEFORE RF TX`. It transmits only
when the RF profile is complete and `COWNECT_LORA_ANTENNA_VERIFIED` is set; otherwise it
reports `BLOCKED`. Wi-Fi tests report `BLOCKED` until SSID/password (and Jetson IP/port for
TCP/upload) are set.

Optional tools in `app_main.cpp`: `test_unit_all()` (every pure-function test) and
`test_start_console()` (the development console described below).

## Final firmware (`run_cownect_firmware()`, GPIO15 / PROG switch)

`run_cownect_firmware()` (in `components/cownect_app`) runs initialization, reads the
operation mode, and then:

| Mode | Behavior |
|---|---|
| PROGRAMMING (LOW) | Development console. **Nothing runs until a `test ...` command.** |
| NORMAL (HIGH) | Production scheduler: power and initialize sensors, 30 s capture, feature processing, communication per menuconfig mode, rail off, timer deep sleep for the remainder of the 120 s cycle. |
| UNSTABLE | Stays awake, re-checks; never enters deep sleep. |

## Configuration gating

Every unresolved value is `CONFIG_NOT_SET` by default and blocks only its feature:

- LoRa RF profile (frequency, BW, SF, CR, preamble, sync word, CRC, IQ, power, PA, ramp, regulator,
  TCXO, DIO2) and TX/RX timeouts -> `test lora_tx`, LoRa telemetry and LORA_FULL refuse with
  `CONFIG_NOT_SET`.
- RF transmission additionally needs `COWNECT_LORA_ANTENNA_VERIFIED` (operator confirms the
  external antenna is attached). Never transmit without the antenna.
- Wi-Fi SSID/password, Jetson IP/port and timeouts -> uploads refuse with `CONFIG_NOT_SET`.
- Scheduler communication defaults to **Disabled** until the above are configured.

Development defaults (non-production, NEEDS_HARDWARE_VALIDATION): device_id 0x00000001,
rail stabilization 100 ms, mode debounce 50 ms, ADC attenuation 12 dB (provisional), 12-bit ADC.
Run `test status` to print the current state. Open items: `hardware_questions.md`.

## Development console (optional, same test bodies)

The console is available from `test_start_console()` in `app_main.cpp` or from the final
firmware in PROGRAMMING mode. Each command calls the same real production code as the
`app_main` tests. It prints `SOFTWARE_RESULT` plus a separate
`HARDWARE_VALIDATION=NEEDS_HARDWARE_VALIDATION` line, because firmware never claims a hardware PASS.

| Material | Commands |
|---|---|
| 4 board | `test board`, `test gpio`, `test mode`, `test rail on|off`, `test adc_smoke` |
| 5 accelerometer | `test accel [s]`, `test accel_overrun`, `test accel_repeat [n]`, `test accel_power` |
| 6 temperature | `test temperature [s]`, `test temp_board`, `test temp_cow`, `test adc_conflict` |
| 7 GPS | `test gps [s]`, `test gps_discovery`, `test gps_capture [s]`, `test gps_power` |
| 8 microphone | `test microphone [s]`, `test mic_stream`, `test mic_overflow`, `test mic_power`, `test mic_dump` |
| 9 capture | `test capture [s]`, `test adc_pattern [s]`, `test capture_repeat`, `test capture_power` |
| 10 scheduler | `test scheduler [cycle_ms capture_ms]`, `test cycle`, `test cycle_repeat`, `test sleep [s]`, `test overrun`, `test cycle_production`, `test power_idle` |
| 11 telemetry | `test telemetry [s]` |
| 12 LoRa | `test lora_spi`, `test lora_reset`, `test lora` (non-radiating), `test lora_config`, `test lora_tx`, `test lora_rx`, `test lora_ping`, `test lora_power` |
| 13 LORA_FULL | `test full_serializer`, `test full_fragment`, `test full_small`, `test full_loss`, `test lora_full`, `test full_overrun` |
| 14 Wi-Fi | `test wifi_connect`, `test wifi_tcp`, `test wifi_small_upload`, `test wifi_capture_upload`, `test wifi_repeat` |
| 15 hybrid | `test hybrid_short`, `test hybrid_full`, `test hybrid_repeat` |
| pure functions | `test unit` (no hardware needed) |

## Layout

```
main/app_main.cpp                     bring-up checklist: commented test calls M4-M15, then run_cownect_firmware()
components/cownect_app                cownect_system_init() (safe base init) + run_cownect_firmware()
components/cn_config                  Kconfig + central constants, CONFIG_NOT_SET parsing
components/board                      pins, MODE_SW, PERIPH_EN, I2C bus, ADC1 ownership/calibration, PSRAM check
components/drivers/accelerometer      LIS2DW12 (M5)
components/drivers/temperature        MCP9700 + MF58, one-shot ADC (M6)
components/drivers/gps                ATGM336H UART + NMEA (M7)
components/drivers/analog_adc         shared ADC1 continuous engine (M8/M9)
components/drivers/microphone         analog microphone (M8)
components/data                       CaptureSession, storage, RTC-retained counters
components/sensor_manager             integrated 30 s capture (M9)
components/features                   SimpleFeatureProcessor, TelemetryRecord (M11)
components/capture_stream             explicit LE codec, CRC32, CaptureStreamEncoder (M13)
components/lora_radio                 SX1262 HAL/adapter + vendored Semtech driver (M12)
components/communication/lora_full    fragments, stop-and-wait (M13)
components/communication/wifi_link    Wi-Fi STA manager (M14)
components/communication/tcp_client   prototype TCP client (M14)
components/communication/wifi_upload  streaming capture upload (M14)
components/communication/hybrid       telemetry packet + LoRa-then-Wi-Fi (M15)
components/communication/comm_manager CommunicationMode dispatch
components/power                      PowerManager, SleepManager (M10)
components/scheduler                  CycleScheduler, CycleMetrics (M10)
components/devtest                    bring-up tests (bringup_tests.h, one file per Material) + console
docs/protocols.md                     byte formats for gateway / Jetson
docs/SYSTEM_OVERVIEW.md               high-level explanation of how the firmware works
```
