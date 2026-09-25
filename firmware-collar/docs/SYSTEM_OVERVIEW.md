# CowNect Collar Firmware: System Overview

> **Status: PROTOTYPE, NOT HARDWARE-TESTED.**
> The firmware builds with ESP-IDF v5.5.5 but has not run on the physical collar PCB yet.
> Everything below describes how the firmware is *designed* to work. Timings, thresholds and
> RF behaviour still need hardware validation, and several network and radio values are not
> configured yet (see section 8). Firmware version: `0.1.0-proto`.

This document gives teammates a picture of how the collar firmware fits together without
walking through every file. For build steps and the test list, see `README.md`. For
test results, see `BUILD_AND_VALIDATION_REPORT.md`. For byte-level packet formats, see
`docs/protocols.md`.

---

## 1. What the collar does

The collar is a battery-powered sensor node worn by a cow. It repeats a **120-second cycle**:

1. Wake up and switch on the sensor power rail.
2. **Record all sensors for 30 seconds**: accelerometer, GPS, microphone, board temperature and
   cow (skin/probe) temperature.
3. Reduce the recording to a small **telemetry summary** (averages, RMS, position, temperatures,
   health flags).
4. **Send data out**: a short LoRa telemetry packet, the full raw recording, or both,
   depending on the communication mode.
5. Switch the rail off and **deep sleep** for the rest of the 120 s.

On the receiving side, a Heltec LoRa gateway (Material 16) and a Jetson receiver (Material 17)
decode the data. Both are separate projects outside this repository.

```
 ┌──────────────── one 120 s cycle ────────────────────────────────────────┐
 │ rail ON │ capture 30 s │ features │ communicate │ rail OFF │ deep sleep │
 └─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Hardware at a glance

| Block | Part | Interface | ESP32-S3 pins |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB PSRAM) | — | — |
| Accelerometer | LIS2DW12 (50 Hz, ±4 g) | I²C (addr 0x18) | SDA 6, SCL 7 |
| GPS | ATGM336H (9600 baud, 1 Hz) | UART | RX 17, TX 18 |
| Microphone | Analog mic + MCP6001 amplifier | ADC1 | GPIO 5 |
| Board temperature | MCP9700 | ADC1 | GPIO 1 |
| Cow temperature | MF58 10 k NTC thermistor (divider) | ADC1 | GPIO 2 |
| LoRa radio | Ra-01SH (SX1262), **external antenna** | SPI | CS 9, MOSI 10, SCK 11, MISO 12, RST 13, DIO1 14, BUSY 21 |
| Sensor power rail | TPS22919 load switch (`SWITCHED_3V3`) | GPIO | PERIPH_EN 4 |
| Mode switch | PROG switch | GPIO | 15 (LOW = programming, HIGH = normal) |

All pin numbers are defined in a single file, `components/board/include/board_pins.h`.
No other file hard-codes a GPIO number.

---

## 3. Software layers

The firmware is C++ on ESP-IDF, split into components. Each layer only calls the layers
below it:

```
                ┌──────────────────────────────────────────────┐
  Entry         │ main/app_main.cpp   (bring-up checklist)     │
                │ cownect_app         (run_cownect_firmware)   │
                └───────────────┬──────────────────────────────┘
                                │
  Control       ┌───────────────▼──────────────┐   ┌──────────────┐
                │ scheduler (CycleScheduler)   │──▶│ power        │ rail on/off,
                └───┬──────────┬───────────┬───┘   │ sleep        │ deep sleep
                    │          │           │       └──────────────┘
  Work          ┌───▼────┐ ┌───▼──────┐ ┌──▼──────────────────────┐
                │ sensor │ │ features │ │ communication           │
                │ manager│ │ (summary)│ │ comm_manager → mode:    │
                └───┬────┘ └──────────┘ │  lora_full / hybrid     │
                    │                   │  wifi_link, tcp_client, │
                    │                   │  wifi_upload            │
                    │                   └──┬──────────────────────┘
  Data          ┌───▼───────────────────┐  │  ┌──────────────────┐
                │ data (CaptureSession, │◀─┼──│ capture_stream   │ serializer + CRC32
                │ RTC-retained counters)│  │  └──────────────────┘
                └───────────────────────┘  │
  Drivers       ┌──────────────────────────▼──────────────────────────────┐
                │ accelerometer · gps · temperature · microphone ·        │
                │ analog_adc (shared ADC engine) · lora_radio (SX1262)    │
                └──────────────────────────┬──────────────────────────────┘
  Board         ┌──────────────────────────▼──────────────────────────────┐
                │ board: pins, mode switch, PERIPH_EN, I²C bus, ADC1      │
                │ ownership + calibration, PSRAM check                    │
                └─────────────────────────────────────────────────────────┘
  Config          cn_config: menuconfig options + central constants (used by all)
  Test            devtest: bring-up tests + development console (uses the real code)
```

Design rules that hold across the codebase:

- **One owner per resource.** The board layer owns ADC1, the I²C bus and the power-enable pin.
  Drivers ask the board layer for access and never configure hardware directly.
- **One set of constants.** Timing, sample rates and buffer sizes live in `cn_config`.
  Anything tunable appears in `idf.py menuconfig` → *CowNect collar configuration*.
- **Tests use production code.** Bring-up tests and console commands call the same drivers
  and managers the real firmware uses. There is no separate "test driver".
- **No guessed values.** Any value that must come from hardware or the network is left as
  `CONFIG_NOT_SET`. The feature that needs it refuses to run and says so, instead of running
  on a guess (section 8).

---

## 4. Start-up and operating modes

`app_main.cpp` is a **bring-up checklist**, not the final program. By default it only does a
safe base init, with the sensor rail commanded OFF, then prints a summary and idles. To bring
up the board, you uncomment one test at a time (Materials 4 → 15) as the PCB is validated.
The last line, `run_cownect_firmware()`, starts the real firmware.

When the real firmware starts, it reads the PROG switch (GPIO 15, debounced):

| Mode | What happens |
|---|---|
| **PROGRAMMING** (switch LOW) | Development console on the serial port. Nothing starts on its own. The operator types `test ...` commands. |
| **NORMAL** (switch HIGH) | The 120 s production cycle starts automatically and repeats through deep sleep. |
| **UNSTABLE** (reading not settled) | Stays awake and re-checks. It never enters deep sleep, so a floating switch can't lock the device out. |

---

## 5. The capture (the core of the system)

The **SensorManager** runs one 30-second capture and stores everything in a single
**CaptureSession**:

| Sensor | Rate | Samples in 30 s | How it is read |
|---|---|---|---|
| Accelerometer | 50 Hz | ~1,500 | LIS2DW12 FIFO, polled every 100 ms over I²C |
| GPS | 1 Hz | up to 40 fixes | UART NMEA stream, parsed in firmware |
| Microphone | 32 kHz, 12-bit | ~960,000 (~1.9 MB) | Continuous ADC with DMA, stored in PSRAM |
| Board temperature | 1 Hz (reported) | ~30 | Shares the continuous ADC and is averaged into 1 s values |
| Cow temperature | 1 Hz (reported) | ~30 | Same as board temperature |

**Shared ADC.** The microphone and both temperature inputs are on ADC1, so one continuous ADC
engine samples at 64 k conversions/s in a repeating pattern
`MIC, BOARD_TEMP, MIC, COW_TEMP`. The microphone gets 32 kHz. The temperature channels are
averaged into one reading per second, which then goes through the normal temperature
conversion code (MCP9700 linear formula, NTC Beta formula). ADC readings go through the
ESP-IDF calibration driver. Attenuation is 12 dB for now, which is a provisional bring-up
setting.

**Health tracking.** Each sensor records its own status: OK, degraded (for example FIFO or DMA
overflow, or no GPS fix) or failed. A capture can complete with some sensors degraded, and the
flags travel with the data so the receiver knows how far to trust each part.

**Identity.** Each capture carries a `device_id` (currently the development default
`0x00000001`) and an incrementing `capture_id`. The capture_id is kept in RTC memory, so it
survives deep sleep but not a full power loss. Timestamps are monotonic microseconds since
boot. No real clock time is used, because no absolute time source is configured yet.

---

## 6. From capture to data out

### 6.1 Features / telemetry (Material 11)
`SimpleFeatureProcessor` reduces the capture to a `TelemetryRecord`. It contains: last valid
GPS position, mean board and cow temperature, accelerometer magnitude mean/RMS/min/max,
microphone mean and AC RMS, and a set of status flags (e.g. `GPS_NO_VALID_FIX`,
`COW_PROBE_FAULT`, `CYCLE_OVERRUN`).

### 6.2 Serialization (Material 13)
To send the full raw capture, `capture_stream` encodes it as a **capture stream v1**: a
68-byte header followed by six typed sections (accel, GPS, board temp, cow temp, microphone,
diagnostics). Every field is explicit little-endian; no memory image of a C struct is ever sent.
The whole stream is protected by CRC-32 (the zlib variant). The data is streamed from the
capture buffers as it is sent, so no second full-size copy is kept in memory.

### 6.3 Communication modes (selected in menuconfig)

| Mode | What is sent | Path |
|---|---|---|
| **Disabled** (current default) | Nothing | — |
| **LORA_FULL** | The entire capture stream | LoRa, split into fragments, stop-and-wait with ACK and retry per fragment |
| **HYBRID_LORA_WIFI** | 1. A 49-byte telemetry packet<br>2. The entire capture stream | 1. LoRa to the Heltec gateway<br>2. Wi-Fi TCP to the Jetson |

The hybrid mode is the intended main path. The small summary goes over long-range LoRa, and
the large raw recording (~2 MB, mostly microphone) goes over Wi-Fi. LORA_FULL exists to test
whether full transfers over LoRa alone are practical. Because of LoRa data rates, it is
expected to be much slower than the 120 s cycle.

The LoRa radio (SX1262) uses a pinned copy of Semtech's official `sx126x_driver`, wrapped behind
our own HAL and adapter (`ILoraRadio`). The rest of the firmware never talks to the chip
directly. All radio operations have timeouts, so a stuck radio cannot hang the cycle.

**RF safety:** The firmware refuses to transmit until the full RF profile is configured and
the operator has set `COWNECT_LORA_ANTENNA_VERIFIED` to confirm the external antenna is
attached. Transmitting without an antenna can damage the radio.

---

## 7. Power and timing

- **PowerManager** switches the peripheral rail (`SWITCHED_3V3`). After switching it on, it
  waits a fixed stabilization delay (development default 100 ms).
- The rail stays on during communication while LoRa needs it. In hybrid mode it can be
  switched off before the Wi-Fi upload, because Wi-Fi is inside the ESP32-S3.
- **SleepManager** uses **timer-only deep sleep**. It measures how long the device has been
  awake and sleeps for the rest of the 120 s. If a cycle ran over 120 s, it records an overrun
  and starts the next cycle immediately instead of sleeping.
- Each cycle's metrics (capture time, communication time, awake time, overrun) are logged and
  kept in RTC memory.

---

## 8. What is still open

**Not configured on purpose.** These values have not been supplied yet, so every feature that
needs one reports `CONFIG_NOT_SET` / `BLOCKED`:

- LoRa RF profile (frequency, bandwidth, SF, CR, power, sync word, TCXO/DIO2, regulator,
  timeouts) and the antenna-confirmed flag
- LORA_FULL fragment size, ACK timeout, retries
- Wi-Fi SSID and password, Jetson IP and port, network timeouts
- Cow thermistor open/short detection thresholds
- Absolute time source

**Development defaults** (labelled `NEEDS_HARDWARE_VALIDATION`): device_id `0x00000001`, 100 ms
rail stabilization, 50 ms mode-switch debounce, ADC 12 dB attenuation, 12-bit.

**Hardware questions** (power switch/ECO jumper behaviour, TCXO, DIO2 RF switch, and others)
are tracked in `hardware_questions.md`.

**Not implemented / out of scope:** the Heltec gateway and Jetson receiver firmware, OTA
updates, permanent device identity, encryption/authentication of packets, and battery
monitoring (the PCB has no rail or battery measurement input).

---

## 9. How to bring it up (short version)

1. `idf.py set-target esp32s3`, then `idf.py menuconfig` (→ *CowNect collar configuration*), then `idf.py build`.
2. In `main/app_main.cpp`, uncomment **one** test, then flash and monitor.
3. Read the `RESULT: PASS | FAIL | BLOCKED | NEEDS_HARDWARE_VALIDATION` line and record it in
   `BUILD_AND_VALIDATION_REPORT.md`.
4. Work through Materials 4 (board) → 15 (hybrid) in order, then enable `run_cownect_firmware()`.

Alternatively, flash the final firmware with the PROG switch in PROGRAMMING and run the same
tests interactively from the console (`test board`, `test capture 30`, `test lora_spi`, ...).
