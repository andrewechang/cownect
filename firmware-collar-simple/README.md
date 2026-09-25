# CowNect Collar: Simple Firmware, Version 2

ESP32-S3, ESP-IDF v5.5.5, plain C. Same functions as the earlier simple version (version 1, in the git history of this folder), with a
simpler structure and one new problem check (**microphone clipping**).

**Status:** builds with no warnings in both send modes; data formats tested on a PC.
**Not yet tested on the hardware.**
See `CHECKLIST.md` for what is done, the values still to fill in, the hardware tests, and future ideas.

## What it does
Every 120 s: sensor power on → record all sensors for 30 s → check the data and make a
summary → send → sensor power off → deep sleep.

- `COMM_HYBRID` (default): 49-byte summary over LoRa + full recording over Wi-Fi to the Jetson
- `COMM_LORA_FULL`: full recording over LoRa in fragments with ACK/retry (very slow)

## Build and flash
```
idf.py set-target esp32s3      (first time only)
idf.py build
idf.py -p COMx flash monitor
```
Fill in the `TODO` values in `lora.h`, `lora_full.h` and `wifi_upload.h`; until then those
steps are skipped with a log message. `DEBUG_NO_DEEP_SLEEP 1` in `config.h` keeps the USB
monitor connected. PROG switch LOW = idle (safe to flash), HIGH = run.

## Files (18)
| File | What it does | Settings at the top |
|---|---|---|
| `config.h` | Board-wide settings and pins | device ID, cycle/capture time, bench mode, send mode |
| `main.c` | The 120 s cycle, PROG switch, deep sleep | |
| `capture.c/.h` | Records all sensors together; problem flags; summary | |
| `accel.c/.h` | LIS2DW12 accelerometer, keeps its data in `accel_data_t` | rate, range, read interval |
| `gps.c/.h` | ATGM336H GPS (RMC + GGA), keeps its data in `gps_data_t` | UART port, baud |
| `analog.c/.h` | Microphone + both temperatures on the ADC, data in `analog_data_t` | mic rate, temp interval, clip limits, sensor constants |
| `data_format.c/.h` | Telemetry packet, raw data stream, upload header, CRC-32 | |
| `lora.c/.h` | SX1262 radio: send and receive packets | RF settings (Ra-01SH hardware values filled in) |
| `lora_full.c/.h` | LORA_FULL fragments, ACK, retry | fragment size, ACK timeout, retries |
| `wifi_upload.c/.h` | Wi-Fi + TCP upload to the Jetson | SSID, password, Jetson IP/port, timeouts |

## Changes from version 1
- **Each sensor owns its data.** `accel_data_t`, `gps_data_t` and `analog_data_t` live in the
  sensor's own `.h`, and the sensor functions only touch their own data. The capture is
  simply `{ id, times, accel, gps, analog }`.
- **Fewer files.** The summary moved into `capture.c`; `packets` is now `data_format`.
- **Plainer names** (`accel_read_fifo`, `gps_read`, `analog_read`, `sensor_power`, `run_cycle`).
- **New: microphone clipping counter.** Every sample at 0 or 4095 is counted. The count and
  percentage are printed after each capture, and a flag is sent:
  bit 11 `MIC_CLIPPED` in the raw data header, bit 9 `SUM_MIC_CLIPPED` in the LoRa telemetry.
  These are new bits; receivers that don't know them can ignore them.
- **Kept on purpose:** all version 1 problem flags, GPS GGA (fix quality, satellites), the
  data formats (gateway/Jetson compatible), LORA_FULL, the "not set = skip" rule.

## Serial log after each capture
```
I (..) capture: capture 3 done in 30000 ms
I (..) capture:   accel: 1500 samples (expected ~1500), FIFO overruns 0
I (..) capture:   gps:   30 fixes, 28 valid, bad sentences 0
I (..) capture:   mic:   960000 samples (expected ~960000), ADC overflows 0, clipped 0 (0.000%)
I (..) capture:   temps: board 30 values, cow 30 values
```
(Example of what to expect, not a real measurement.) If the GPS sends nothing at all, the gps
line ends with `<- NO GPS TEXT (wiring/power?)`.
