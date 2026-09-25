# CowNect Simple Firmware (Version 2): What Is Needed vs. What Can Come Later

This is the **student-sized** version of the collar firmware: about 1,960 lines (with comments) in 18 files. The full
firmware (`firmware-collar/`) is about 12,800 lines in 133 files. Both do the same basic job;
the simple one leaves out the "production" extras.

Status legend: ✅ written and compiles · 🔧 written, needs a value from you or a hardware test · ⬜ not done

---

## Part A: Basic requirements (needed for a working system)

### A1. Board basics
| # | Item | Status | Where |
|---|---|---|---|
| 1 | Sensor power rail on/off (GPIO4) with 100 ms settle time | ✅ | `main.c` |
| 2 | PROG switch (GPIO15): LOW = idle so you can re-flash, HIGH = run | ✅ | `main.c` |
| 3 | PSRAM check (the microphone recording needs ~1.9 MB) | ✅ | `analog.c` |

### A2. Collecting the data correctly (30 s capture)
| # | Item | Status | Where |
|---|---|---|---|
| 4 | Accelerometer LIS2DW12: check ID, 50 Hz, ±4 g, FIFO read every 100 ms, overflow counted | ✅ | `accel.c` |
| 5 | GPS ATGM336H: UART 9600, NMEA checksum, RMC (position/speed) + GGA (quality/satellites) | ✅ | `gps.c` |
| 6 | Microphone: 32 kHz via continuous ADC + DMA, stored in PSRAM, overflow counted | ✅ | `analog.c` |
| 7 | Board temperature MCP9700: 1 value per second, calibrated mV → °C | ✅ | `analog.c` |
| 8 | Cow temperature MF58 thermistor: 1 value per second, mV → resistance → °C (Beta formula) | ✅ | `analog.c` |
| 9 | All sensors recorded **at the same time** in one loop, for exactly 30 s | ✅ | `capture.c` |
| 10 | Serial log after each capture: sample counts vs expected, overflows, valid GPS fixes | ✅ | `capture.c` |

### A3. Processing
| # | Item | Status | Where |
|---|---|---|---|
| 11 | Summary: last GPS fix, mean temperatures, accel magnitude mean/RMS, microphone RMS, error flags | ✅ | `capture.c` |
| 11a | Problem checks sent with the data: samples lost (accel/mic), GPS wiring vs no fix, bad GPS text, cow probe fault, cycle overrun, **microphone clipping** | ✅ | `capture.c`, `analog.c` |

### A4. Sending the data
| # | Item | Status | Where |
|---|---|---|---|
| 12 | 49-byte LoRa telemetry packet (same format as the full firmware / gateway) | ✅ | `data_format.c` |
| 13 | SX1262 LoRa: reset, configure, send one packet, wait for TX done with a timeout | 🔧 needs the RF settings in `lora.h` | `lora.c` |
| 14 | Raw capture stream v1 (same format the Jetson receiver expects) | ✅ | `data_format.c` |
| 15 | Wi-Fi connect + TCP upload of the raw capture to the Jetson | 🔧 needs SSID/password/Jetson IP/port in `wifi_upload.h` | `wifi_upload.c` |
| 15a | Choose the mode in `config.h`: `COMM_MODE = COMM_HYBRID` (items 12–15) or `COMM_LORA_FULL` (items 15b–15c) | ✅ | `config.h`, `main.c` |
| 15b | **LORA_FULL**: whole raw capture over LoRa, cut into fragments (40-byte header + data), CRC-32 over the whole stream, same format as the full firmware | 🔧 needs the LoRa RF values + fragment size | `lora_full.c` |
| 15c | LORA_FULL reliability: receiver answers every fragment with FULL_ACK; resend up to N times, otherwise stop the transfer (ACK can be switched off) | 🔧 needs ACK timeout + retries | `lora_full.c`, `lora.c` (receive) |

### A5. Sleep and the 120 s cycle
| # | Item | Status | Where |
|---|---|---|---|
| 16 | Sleep time = 120 s − time actually spent awake (not a fixed 90 s) | ✅ | `main.c` |
| 17 | Timer deep sleep, rail switched off before sleep | ✅ | `main.c` |
| 18 | Capture number kept across deep sleep (RTC memory) | ✅ | `main.c` |
| 19 | Overrun: if a cycle takes over 120 s, start the next one straight away and flag it | ✅ | `main.c` |
| 20 | Bench option `DEBUG_NO_DEEP_SLEEP` so the USB monitor stays connected | ✅ | `config.h` |
| 21 | Each module's settings (sample rates, ranges, radio, network) at the top of its own `.h` file; invalid values stop the build | ✅ | `*.h` |

### A6. Values you must fill in (`main/lora.h`, `main/lora_full.h`, `main/wifi_upload.h`)
Nothing is guessed. Until these are set, the matching step is **skipped with a log message**.
The rest of the cycle still runs.

| Value | Needed for |
|---|---|
| `LORA_FREQUENCY_HZ`, `LORA_BANDWIDTH_KHZ`, `LORA_SPREADING_FACTOR`, `LORA_CODING_RATE`, `LORA_TX_POWER_DBM`, `LORA_SYNC_WORD` | LoRa (must match the Heltec gateway and your region's rules) |
| `LORA_USE_TCXO`, `LORA_USE_DIO2_RF_SWITCH`, `LORA_USE_DCDC` | LoRa (check the Ra-01SH module datasheet / schematic) |
| `LORA_TX_TIMEOUT_MS` | LoRa |
| `LORA_ANTENNA_CONNECTED = 1` | LoRa: set only after checking the antenna is attached |
| `WIFI_SSID`, `WIFI_PASSWORD`, `JETSON_IP`, `JETSON_RAW_PORT` | Wi-Fi upload |
| `WIFI_CONNECT_TIMEOUT_MS`, `TCP_SEND_TIMEOUT_MS` | Wi-Fi upload |
| `LORA_FULL_FRAGMENT_BYTES` (max 215), `LORA_FULL_ACK_TIMEOUT_MS`, `LORA_FULL_MAX_RETRIES` | LORA_FULL mode |

### A7. Hardware testing (not done yet; do in this order)
| # | Test | How you know it works |
|---|---|---|
| T1 | Flash with PROG = LOW | Log repeats "PROGRAMMING mode … idle" |
| T2 | PROG = HIGH, `DEBUG_NO_DEEP_SLEEP 1` | A capture runs every 120 s |
| T3 | Accelerometer | ~1,500 samples, 0 overruns; lying flat, accel mean ≈ 1.0 g |
| T4 | Microphone | ~960,000 samples, 0 ADC overflows; mic RMS rises when you clap |
| T5 | Temperatures | Board ≈ room temperature; cow probe rises when held in your hand |
| T6 | GPS | Outdoors: valid fixes and a sensible position. The very first fix after power-up is a cold start (up to ~35 s, may miss the 30 s window); later cycles should get a fix within seconds (hot start via VBAT) |
| T7 | LoRa SPI | At power-on the log shows "sync word register = 0x1424 … OK" |
| T8 | LoRa TX | Fill in the RF values and attach the antenna, then check the gateway receives 49-byte packets |
| T9 | Wi-Fi | Fill in the network values, then check the Jetson saves ~1.9 MB per capture |
| T10 | Deep sleep | `DEBUG_NO_DEEP_SLEEP 0`: a cycle every 120 s; capture numbers keep counting up |
| T10a | LORA_FULL | Set `COMM_MODE COMM_LORA_FULL` + the LORA_FULL values. The receiver must send FULL_ACKs (or set `LORA_FULL_USE_ACK 0`). Log shows progress 10 %…100 % and "COMPLETE"; the receiver's rebuilt file has the same CRC-32 as the one in the log. Expect it to take a long time (≈ 9,000+ fragments for 1.9 MB) |
| T11 | Long run | Leave it running for a few hours; capture numbers have no gaps and there are no overruns |

---

## Part B: Extra features for the future (not needed for the prototype)

These are in the full firmware or were left out on purpose (LORA_FULL is now included above). Add them only when you need them.

| # | Feature | Why it could be useful | Effort |
|---|---|---|---|
| B1 | Development console (`test ...` commands over serial) | Test one sensor at a time without re-flashing | Medium |
| B2 | Per-Material bring-up test suite with PASS/FAIL reports | Formal hardware validation | Medium |
| B3 | CRC32 for the Wi-Fi upload too (LORA_FULL already sends one) | Detects corrupted uploads (TCP already checks each packet) | Low |
| B4 | Diagnostics record in the stream (timings, error counters) | Easier debugging on the Jetson side | Low |
| B5 | Thermistor open/short detection with measured thresholds | Tells "probe unplugged" apart from a real temperature | Low (after measuring) |
| B6 | Per-cycle timing metrics (capture/comm/awake times) stored and reported | Power and performance study | Low |
| B7 | Measure GPS start-up time over many cycles (first fix, then hot starts) | Confirms the always-on VBAT backup really gives ~1 s hot starts on this PCB | Low |
| B8 | Battery voltage measurement | Know when to charge. The PCB currently has no measurement input | Needs hardware change |
| B9 | Real clock time (GPS time or network time) | Timestamps in real date/time instead of "time since boot" | Low–Medium |
| B10 | Permanent device ID (from the chip MAC or flash) | Many collars without editing `config.h` each time | Low |
| B11 | Settings through menuconfig/NVS instead of the .h files | Change settings without editing code | Medium |
| B12 | Packet encryption / authentication | Security for a real deployment | Medium–High |
| B13 | Over-the-air (OTA) firmware update | Update collars without a cable | High |
| B14 | Receive commands from the gateway (downlink) | Change settings remotely | Medium |
| B15 | Lower-power tuning (lower CPU clock, shorter awake time, RF power tuning) | Longer battery life | Medium |
| B16 | Processing on the collar (e.g. activity classification, rumination detection) | Send results instead of raw audio | High |

---

## Part C: Simple vs. full firmware at a glance

| | Simple v2 (`firmware-collar-simple`) | Full (`firmware-collar`) |
|---|---|---|
| Language | C | C++ |
| Size | ~1,960 lines (with comments), 18 files | ~12,800 lines, 133 files |
| Settings | `config.h` + a SETTINGS block at the top of each module .h | menuconfig (Kconfig) + constants |
| Sensors / rates | same | same |
| Packet formats | same (gateway/Jetson compatible) | same |
| LoRa driver | ~330 lines written directly from the datasheet (send + receive) | Semtech driver + HAL/adapter layers |
| Communication | HYBRID (LoRa summary + Wi-Fi raw upload) or LORA_FULL, chosen in `config.h` | Same two modes, chosen in menuconfig |
| Testing | serial log after each cycle | 65 bring-up tests + console |
| Status | compiles (ESP-IDF v5.5.5), not hardware-tested | compiles, not hardware-tested |
