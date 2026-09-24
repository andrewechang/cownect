# CowNect hardware / configuration question log

Material 4 section 18. Items are never deleted: when resolved, mark them RESOLVED with the
source of confirmation. Status terms: OPEN, CONFIG_NOT_SET, DEVELOPMENT DEFAULT,
NEEDS_HARDWARE_VALIDATION, RESOLVED.

## Power / board

| # | Item | Status | Notes |
|---|---|---|---|
| 1 | PWRSRC schematic "Pos. 1 USB / Pos. 3 battery" vs terminal numbering (2-1 = battery, 2-3 = USB) | OPEN | Use electrical contact descriptions until measured on the assembled board. |
| 2 | PWRSRC HX MINI MSK12CO2 rated 12 V / 50 mA vs board input current (ESP32 + GPS + Ra-01SH TX) | OPEN | Hardware reliability item; does not block firmware. |
| 3 | Safe GPIO states in deep sleep | OPEN | Firmware only drives PERIPH_EN LOW before sleep (R2 100k pulldown). No other pin state or gpio hold is applied until leakage testing. |
| 4 | SWITCHED_3V3 stabilization delay | DEVELOPMENT DEFAULT 100 ms (user, 2026-09-24) / NEEDS_HARDWARE_VALIDATION | `CONFIG_COWNECT_PERIPH_STABILIZE_MS`. |
| 5 | MODE_SW (GPIO15) debounce | DEVELOPMENT DEFAULT 50 ms (user, 2026-09-24) / NEEDS_HARDWARE_VALIDATION | No internal pull enabled; a floating read during break-before-make is reported as UNSTABLE. Confirm the switch never leaves GPIO15 floating long enough to matter. |
| 6 | ECO forced-on vs GPIO-controlled | NEEDS_HARDWARE_VALIDATION | Firmware reports only the PERIPH_EN command; sleep-current tests need ECO in the GPIO-controlled position. |
| 7 | device_id provisioning | DEVELOPMENT DEFAULT 0x00000001 (user, 2026-09-24) | Not a production scheme. |
| 8 | capture_id persistence | Retained in RTC slow memory only | Survives timer deep sleep, resets on power loss. Persistent scheme not approved. |
| 9 | Absolute time source (GNSS UTC / synced clock) | CONFIG_NOT_SET | Records carry capture_id + monotonic time only; GNSS UTC is exposed with validity flags and never sets the system clock. |

## ADC / analog

| # | Item | Status | Notes |
|---|---|---|---|
| 10 | ADC1 attenuation (common to mic + both temperatures, continuous and one-shot) | PROVISIONAL 12 dB (user, 2026-09-24) / NEEDS_HARDWARE_VALIDATION | `CONFIG_COWNECT_ADC_ATTEN_*`. 3.3 V cannot be measured accurately; top-rail saturation is only used as "open suspected". Material 9 common-attenuation A/B test pending. |
| 11 | ADC bit width | 12-bit (user, 2026-09-24) | ESP32-S3 default width. |
| 12 | ADC continuous DMA frame / pool size | DEVELOPMENT DEFAULT 1024 B / 16384 B | `CONFIG_COWNECT_ADC_CONV_FRAME_BYTES`, `CONFIG_COWNECT_ADC_POOL_BYTES`. Verify zero pool overflow at 64 kS/s. |
| 13 | Material 9 4-slot pattern MIC,TB,MIC,TC at 64 kS/s on installed IDF | NEEDS_HARDWARE_VALIDATION | `test adc_pattern`. If `adc_continuous_config()` rejects it, the exact error is logged and temperatures fall back to one-shot sampling while the microphone is marked unavailable. |
| 14 | Cross-channel contamination of the scanned ADC | NEEDS_HARDWARE_VALIDATION | Material 9 section 28. |
| 15 | Microphone near-rail threshold | Margin 0 codes (exact rail codes only) | `CONFIG_COWNECT_ADC_NEAR_RAIL_MARGIN_CODES`. |
| 16 | MF58 open/short thresholds | CONFIG_NOT_SET (0) | Only rail codes and divider-math invalidity are used. Validate with unplug/short bench tests, then set `CONFIG_COWNECT_COW_OPEN/SHORT_THRESHOLD_MV`. |
| 17 | MF58 divider excitation | NOMINAL 3300 mV (not measured) | Rail variation is part of the error budget. Self-heating (~0.27 mW) not compensated. |
| 18 | Temperature trailing partial bucket | Design rule | Only complete 1 s buckets become logical samples; the <1 s remainder at stop is kept as diagnostics (`*_partial_bucket_conversions`). Timestamps = bucket END offset. |
| 19 | One-shot temperature averaging count | 1 (single conversion) | `CONFIG_COWNECT_TEMP_ONESHOT_AVERAGE_COUNT`. |

## Accelerometer / GPS

| # | Item | Status | Notes |
|---|---|---|---|
| 20 | LIS2DW12 raw -> g conversion (word >> 2 x 0.488 mg) | NEEDS_HARDWARE_VALIDATION | Material 5 orientation test; only the helper changes if wrong. |
| 21 | I2C clock | Design default 100 kHz | `CONFIG_COWNECT_I2C_CLOCK_HZ`. |
| 22 | ATGM336H default NMEA sentence set | OPEN | CASIC protocol spec not supplied. Parser handles standard RMC/GGA for any talker; run `test gps_discovery` and record the identifiers here. |
| 23 | GPS UART controller | Design decision UART2 | UART0 reserved for the backup debug header. |
| 24 | NMEA max sentence length | Design default 128 B | Confirm with discovery output. |

## Radio (Ra-01SH / SX1262)

| # | Item | Status | Notes |
|---|---|---|---|
| 25 | Ra-01SH ANT pin 1 shows no ordinary net label on the schematic | PCB VERIFICATION ITEM | User confirmed (2026-09-24) the hardware uses an EXTERNAL antenna. Not a firmware hard block. Transmission is gated by `CONFIG_COWNECT_LORA_ANTENNA_VERIFIED` (operator confirms antenna attached / path verified). Never transmit without the antenna. |
| 26 | Ra-01SH frequency band: datasheet lists 803-930 MHz and 410-525 MHz | OPEN (documentation conflict) | Confirm module marking/variant. |
| 27 | Heltec WiFi LoRa 32 V3 purchased band variant | OPEN | Must match the Ra-01SH band. |
| 28 | RF profile: frequency, bandwidth, SF, CR, preamble, sync word, CRC, IQ, TX power, PA duty/hpMax, ramp | CONFIG_NOT_SET | menuconfig strings, all empty. Legal regional channel must be selected. |
| 29 | Oscillator: TCXO on DIO3 (voltage/startup) or crystal | CONFIG_NOT_SET | DIO3 is not wired to the ESP32; whether the module uses it internally is unknown. `CONFIG_COWNECT_LORA_TCXO` must be `NONE` or a voltage. |
| 30 | RF switch: DIO2 control and TXEN/RXEN left unconnected | CONFIG_NOT_SET / NEEDS_HARDWARE_VALIDATION | Material 12 section 13: if SPI works but RF does not, investigate TXEN/RXEN (possible hardware revision). |
| 31 | SX1262 regulator mode (LDO / DC-DC) | CONFIG_NOT_SET | Depends on the module's DC-DC inductor fitting. |
| 32 | TX / RX operation timeouts | CONFIG_NOT_SET | TX is refused without a TX timeout. |
| 33 | BUSY wait safety bound | DEVELOPMENT DEFAULT 100 ms / NEEDS_HARDWARE_VALIDATION | Bounds every SPI command; not an RF parameter. |
| 34 | SPI clock | Design default 1 MHz (module max 10 MHz) | `CONFIG_COWNECT_LORA_SPI_CLOCK_HZ`. |
| 35 | NRESET pulse | 1 ms low (datasheet minimum 100 us) then bounded BUSY wait | |
| 36 | LORA_FULL fragment payload, ACK timeout, retry count | CONFIG_NOT_SET | header (40 B) + payload must be <= 255 B; oversize is rejected. |
| 37 | LoRa TX during capture (analog interference A/B test) | Not enabled | `LORA_TX_DURING_CAPTURE_DEFAULT = false`; hybrid sends one packet after capture. |

## Network

| # | Item | Status | Notes |
|---|---|---|---|
| 38 | Wi-Fi SSID / password | CONFIG_NOT_SET | Password is never logged. `sdkconfig` is git-ignored because it would contain them. |
| 39 | Jetson IP / TCP port | CONFIG_NOT_SET | |
| 40 | Wi-Fi connect, TCP connect, socket write timeouts | CONFIG_NOT_SET | Uploads are refused until set. |
| 41 | Raw-data policy when Wi-Fi fails | RESOLVED (Material 14 Rev B) | Report failure, continue cycle, no backlog. |
| 42 | NVS partition erase on `ESP_ERR_NVS_NO_FREE_PAGES/NEW_VERSION_FOUND` | Design note | Required by the Wi-Fi driver; CowNect stores nothing else in NVS. |
