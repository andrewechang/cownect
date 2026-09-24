# CowNect prototype byte formats (collar side)

All multi-byte fields are **little-endian**. Floats are IEEE-754 binary32. Nothing is ever sent
as a C/C++ memory image. These are the formats the Heltec gateway (Material 16) and Jetson
receiver (Material 17) must decode. Source of truth: the encoder files named below.

## 1. Capture stream v1 (Material 13) - `capture_stream_format.h`

Header (68 B): `u32 magic=0x53434E43 ("CNCS") | u8 version=1 | u8 0 | u16 header_bytes=68 |
u64 device_id | u32 capture_id | u64 capture_start_us | u64 capture_end_us | u32 total_stream_bytes |
u32 accel_count | u32 gps_count | u32 board_temp_count | u32 cow_temp_count | u32 microphone_count |
u32 microphone_sample_rate_hz | u32 capture_status_flags`

Then six sections, each `u8 type | u8 version=1 | u16 record_bytes | u32 record_count | u32 payload_bytes`
followed by the payload. Unknown types are skippable by `payload_bytes`.

| type | name | record |
|---|---|---|
| 1 | ACCEL_RAW | `i16 x | i16 y | i16 z` raw LIS2DW12 words (6 B) |
| 2 | GPS_FIXES | `u32 offset_ms | i32 lat_e7 | i32 lon_e7 | f32 speed_mps | f32 course_deg | u8 fix_quality | u8 satellites | u8 flags(b0 valid, b1 utc_valid) | u8 0` (24 B) |
| 3 | BOARD_TEMP | `u32 ts_ms | i32 adc_raw | i32 mV | f32 temp_c | u8 status | u8 flags(b0 mV valid) | u16 0` (20 B) |
| 4 | COW_TEMP | `u32 ts_ms | i32 adc_raw | i32 mV | f32 resistance_ohm | f32 temp_c | u8 status | u8 flags | u16 0` (24 B) |
| 5 | MICROPHONE_RAW | `u16` raw ADC code (uncentered) (2 B) |
| 6 | DIAGNOSTICS | one 144 B record, field order in `capture_stream_encoder.cpp::encode_diagnostics()` |

Temperature status codes: 0 OK, 1 ADC_ERROR, 2 CAL_UNAVAILABLE, 3 INVALID_VOLTAGE,
4 INVALID_RESISTANCE, 5 OPEN_SUSPECTED, 6 SHORT_SUSPECTED, 7 CONVERSION_ERROR. When status != OK,
temperature (and resistance) are NaN.

Capture status bits: b0 accel_ok, b1 gps_uart_ok, b2 gps_had_valid_fix, b3 mic_ok, b4 board_temp_ok,
b5 cow_temp_ok, b6 adc_shared_ok, b7 accel_degraded, b8 mic_degraded, b9 gps_degraded, b10 cow_probe_fault.

## 2. Wi-Fi raw upload (Material 14 Rev B) - `wifi_capture_uploader.h`

TCP: `u32 magic=0x50554E43 ("CNUP") | u8 version=1 | u64 device_id | u32 capture_id | u32 stream_bytes`
(21 B), then exactly `stream_bytes` of the capture stream, then close.

## 3. LoRa telemetry (Material 15) - `prototype_telemetry_encoder.h`

49 B: `u16 magic=0x4D43 ("CM") | u8 version=1 | u8 type=0x10 | u64 device_id | u32 capture_id |
u8 valid(b0 gps, b1 board, b2 cow, b3 accel, b4 mic) | i32 lat_e7 | i32 lon_e7 | f32 board_c | f32 cow_c |
f32 accel_mean_g | f32 accel_rms_g | f32 mic_rms_ac_raw | u32 status_flags`.
Fields whose valid bit is clear are 0 and must be ignored.

Telemetry status flags (Material 11): b0 CAPTURE_DEGRADED, b1 ACCEL_ERROR, b2 GPS_NO_VALID_FIX,
b3 MIC_ERROR, b4 BOARD_TEMP_ERROR, b5 COW_TEMP_ERROR, b6 COW_PROBE_FAULT, b7 ADC_ERROR, b8 CYCLE_OVERRUN.

## 4. LORA_FULL (Material 13) - `lora_full_protocol.h`

FULL_DATA header (40 B) + payload: `u16 magic=0x4643 ("CF") | u8 version=1 | u8 type=1 | u64 device_id |
u32 capture_id | u32 fragment_index (0..count-1) | u32 fragment_count | u32 stream_offset |
u32 total_stream_bytes | u16 payload_bytes | u16 flags(b0 stream_crc32 present, b1 last) | u32 stream_crc32`.

FULL_ACK (21 B): `u16 magic | u8 version=1 | u8 type=2 | u64 device_id | u32 capture_id | u32 fragment_index | u8 status(0=OK)`.

Stream CRC32 variant: CRC-32/ISO-HDLC (zlib / Python `binascii.crc32`), check("123456789") = 0xCBF43926.

## 5. Development packets

- LoRa link test (Material 12), 20 B: `u16 magic=0x5443 ("CT") | u8 version=1 | u8 type(1 PING, 2 ACK) |
  u64 device_id | u32 sequence | u32 value`. An ACK echoes the PING sequence.
- Synthetic stream (`test full_small`, `test wifi_small_upload`): `byte(i) = (i*31 + (i>>8) + seed) & 0xFF`;
  seed 0x5A for LoRa tests, 0xA5 for Wi-Fi tests.
