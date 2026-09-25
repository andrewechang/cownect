# CowNect Collar: Simple Prototype Firmware

A small, readable version of the collar firmware (ESP32-S3, ESP-IDF v5.5.5, plain C).
Every 120 s it records all sensors for 30 s, sends the data, then deep-sleeps.
Two ways to send, chosen with `COMM_MODE` in `main/config.h`:

- `COMM_HYBRID` (default): 49-byte summary over LoRa + raw recording over Wi-Fi to the Jetson
- `COMM_LORA_FULL`: the whole raw recording over LoRa in fragments with ACK/retry (research comparison; very slow)

**Status:** compiles with no warnings. **Not yet tested on the hardware.**
See `CHECKLIST.md` for what is done, what still needs values/testing, and future ideas.

## Build and flash

```
idf.py set-target esp32s3      (first time only)
idf.py build
idf.py -p COMx flash monitor
```

1. Fill in the values marked `TODO` in `lora.h`, `lora_full.h` and `wifi_upload.h`. Until then
   those steps are skipped with a log message and everything else still runs.
2. For bench testing with USB, set `DEBUG_NO_DEEP_SLEEP 1` so the serial monitor stays connected.
3. PROG switch LOW = idle (safe to flash). PROG switch HIGH = run the cycle.

## Where the settings are

Each module keeps its own settings at the top of its `.h` file, between the
`SETTINGS` lines, so you can change them for testing without searching the code:

| File | Settings |
|---|---|
| `main/config.h` | Device ID, cycle time (120 s), capture time (30 s), bench mode, `COMM_MODE`, pin map |
| `main/accel.h` | Accelerometer rate (25/50/100/200 Hz), range (2/4/8/16 g), FIFO poll interval, I2C address/clock |
| `main/gps.h` | GPS UART port, baud rate, receive buffer |
| `main/analog.h` | Microphone sample rate (max 41 kHz), temperature averaging period, ADC attenuation, sensor constants |
| `main/lora.h` | LoRa frequency, bandwidth, SF, CR, power, sync word, TCXO/DIO2/regulator, TX timeout, antenna flag |
| `main/lora_full.h` | LORA_FULL fragment size, ACK on/off, ACK timeout, retries |
| `main/wifi_upload.h` | Wi-Fi SSID/password, Jetson IP/port, connect/send timeouts |

Buffer sizes follow the settings automatically. An invalid value (for example an
unsupported accelerometer rate) stops the build with a clear `#error` message.
Every function has a comment above it explaining what it does.

## Files

| File | What it does |
|---|---|
| `main/config.h` | All pins, timings and settings in one place |
| `main/main.c` | The 120 s cycle: rail on → capture → summary → LoRa → Wi-Fi → deep sleep |
| `main/capture.c/.h` | Runs all sensors together for 30 s; the data structure for one capture |
| `main/accel.c/.h` | LIS2DW12 accelerometer (I2C, 50 Hz, FIFO) |
| `main/gps.c/.h` | ATGM336H GPS (UART, NMEA RMC + GGA) |
| `main/analog.c/.h` | Microphone 32 kHz + both temperature sensors on the shared ADC |
| `main/summary.c/.h` | Averages / RMS / flags for the telemetry |
| `main/packets.c/.h` | Byte formats: LoRa telemetry (49 B), raw capture stream, upload header |
| `main/lora.c/.h` | Minimal SX1262 driver: configure, send packets, receive packets (for ACKs) |
| `main/lora_full.c/.h` | LORA_FULL: fragments the raw capture, sends each fragment, waits for ACK, retries |
| `main/wifi_upload.c/.h` | Wi-Fi connect + TCP upload to the Jetson |

The packet formats are the same as the full firmware (`../firmware-collar/docs/protocols.md`),
so the same Heltec gateway and Jetson receiver code can be used.

## What you should see in the serial log

```
I (..) accel: LIS2DW12 ready: 50 Hz, +/-4 g, FIFO
I (..) gps: UART ready at 9600 baud
I (..) analog: ADC running at 64000 Hz (mic 32000 Hz)
I (..) capture: capture 1 done in 30000 ms
I (..) capture:   accel 1500 samples (overruns 0) | gps 30 fixes, 0 valid (bad sentences 0)
I (..) capture:   mic 960000 samples (expected ~960000, ADC overflows 0) | board temp 30 | cow temp 30
I (..) summary: board 24.10 C | cow 23.80 C | accel mean 1.002 g rms 1.002 g | mic rms 12.3 | flags 0x004
W (..) lora: LoRa skipped: LORA_FREQUENCY_HZ is not set in config.h
W (..) wifi: Wi-Fi upload skipped: WIFI_SSID is not set in config.h
I (..) main: cycle 1 awake for 30250 ms
I (..) main: deep sleep for 89750 ms
```
(The numbers above show what to expect; they are not real measurements.)
