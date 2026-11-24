# CowNect Collar: Simple Firmware, Version 2

ESP32-S3, ESP-IDF v5.5.5, plain C.

## What it does
Every 60 s: sensor power on → record all sensors for 15 s → check the data and make a
summary → send → sensor power off → deep sleep.

- `COMM_LORA_FULL` (default): LoRa only. The collar works out values from the recording
  (`features.c`: per 3 s block tilt, movement (ODBA), biggest jolt, movement bursts, jerk X/Y/Z
  and sound in 3 bands; loudness per second, loud sound events, GPS, temperatures) and sends them
  in ONE 157-byte packet (version 2, ~0.4 s on air). No raw data is sent. Awake ~19.4 s per cycle
  (15 s recording + ~0.9 s calculation + send), asleep the rest.
- `COMM_HYBRID`: 49-byte summary over LoRa + full raw recording over Wi-Fi to the Jetson (TCP)
  or a website (HTTPS + API key), chosen by `UPLOAD_TARGET` in `wifi_upload.h` (for training data)

## Build and flash
```
idf.py set-target esp32s3      (first time only)
idf.py build
idf.py -p COMx flash monitor
```
Fill in the `TODO` values in `lora.h` and `wifi_upload.h`; until then those
steps are skipped with a log message. `DEBUG_NO_DEEP_SLEEP 1` in `config.h` keeps the USB
monitor connected. PROG switch LOW = idle (safe to flash), HIGH = run.

## Files (20)
| File | What it does | Settings at the top |
|---|---|---|
| `config.h` | Board-wide settings and pins | device ID, cycle/capture time, bench mode, send mode |
| `main.c` | The 60 s cycle, PROG switch, deep sleep | |
| `capture.c/.h` | Records all sensors together; problem flags; summary | |
| `accel.c/.h` | LIS2DW12 accelerometer, keeps its data in `accel_data_t` | rate, range, read interval |
| `gps.c/.h` | ATGM336H GPS (RMC + GGA), keeps its data in `gps_data_t`. Pins: RX = GPIO39, TX = GPIO40 (`config.h`) | UART port, baud |
| `analog.c/.h` | Microphone + both temperatures on the ADC, data in `analog_data_t` | mic rate, temp interval, clip limits, sensor constants |
| `data_format.c/.h` | Telemetry packet, feature packet, raw data stream, upload header | |
| `features.c/.h` | Values for the COMM_LORA_FULL packet | block length, sound bands, movement / sound-event thresholds |
| `lora.c/.h` | SX1262 radio: send and receive packets | RF settings (Ra-01SH hardware values filled in) |
| `wifi_upload.c/.h` | Wi-Fi + upload to the Jetson (TCP) or a website (HTTPS) | SSID, password, target, Jetson IP/port or URL + API key, timeouts |
| `test_mode.c/.h` | Hardware bring-up tests, one module at a time (test 6 = GPS: checks both pins, tries them swapped) | `TEST_NUMBER` (0 = normal firmware) |


## Serial log after each capture
```
I (..) capture: capture 3 done in 15000 ms
I (..) capture:   accel: 750 samples (expected ~750), FIFO overruns 0
I (..) capture:   gps:   15 fixes, 14 valid, bad sentences 0
I (..) capture:   mic:   480000 samples (expected ~480000), ADC overflows 0, clipped 0 (0.000%)
I (..) capture:   temps: board 15 values, cow 15 values (15 valid)
```
(Example of what to expect, not a real measurement.) If the GPS sends nothing at all, the gps
line ends with `<- NO GPS TEXT (wiring/power?)`.
