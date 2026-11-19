"""decode.py - prints what is inside one collar upload saved by receiver.py,
and optionally exports it to CSV (open in Excel) and WAV (play the microphone).

Run:   python decode.py                     newest file in uploads/
       python decode.py uploads/<file>.bin  a specific file
       python decode.py --export            also write uploads/<file name>/ with:
           accel.csv        time_s, x_g, y_g, z_g, total_g
           board_temp.csv   time_s, adc_raw, mv, temp_c, status
           cow_temp.csv     time_s, adc_raw, mv, resistance_ohm, temp_c, status
           gps.csv          time_s, lat, lon, speed_mps, course_deg, quality, satellites, valid
           mic.wav          the recording, 16-bit mono (DC removed, 16x louder)
       python decode.py --export --all      export every file in uploads/

Layout (main/data_format.c, all little-endian):
    21-byte upload header "CNUP"
    capture stream "CNCS": 68-byte header, then sections
        1 accelerometer  6 bytes each: x, y, z raw words (14-bit, left-justified)
        2 GPS           24 bytes each
        3 board temp    20 bytes each
        4 cow temp      24 bytes each
        5 microphone     2 bytes each: ADC value 0..4095 (0 samples in a no-mic Full LoRa recording)
        (diagnostics section, empty)
"""
import csv
import math
import struct
import sys
import wave
from array import array
from pathlib import Path

ACCEL_MG_PER_DIGIT = 0.488   # +/-4 g (main/accel.h)
WAV_GAIN = 16                # 12-bit ADC -> 16-bit WAV (x16 = full 12-bit range fills the WAV)
FLAGS = ["ACCEL_OK", "GPS_UART_OK", "GPS_HAD_FIX", "MIC_OK", "BOARD_TEMP_OK", "COW_TEMP_OK",
         "ADC_OK", "ACCEL_LOST", "MIC_LOST", "GPS_BAD_TEXT", "COW_PROBE_FAULT", "MIC_CLIPPED"]
TEMP_STATUS = {0: "ok", 2: "no ADC calibration", 3: "voltage out of range",
               5: "DISCONNECTED (open circuit)", 6: "SHORTED"}   # main/analog.h temp_status_t
UPLOAD_DIR = Path(__file__).resolve().parent / "uploads"


def stats(values):
    n = len(values)
    mean = sum(values) / n
    rms = math.sqrt(sum((v - mean) ** 2 for v in values) / n)
    return min(values), max(values), mean, rms


def parse(path):
    """Returns a dict with the header fields and every sensor's records."""
    data = path.read_bytes()
    magic, version, device, capture, stream_bytes = struct.unpack_from("<IBQII", data, 0)
    if magic != 0x50554E43:
        raise ValueError("not a collar upload (no 'CNUP' magic)")
    s = data[21:]
    (cmagic, cver, _, hdr_len, _, _, start_us, end_us, _, _, _, _, _, _, mic_hz,
     flags) = struct.unpack_from("<IBBHQIQQIIIIIIII", s, 0)
    if cmagic != 0x53434E43:
        raise ValueError("capture stream has no 'CNCS' magic")

    sections, p = {}, hdr_len
    while p + 12 <= len(s):
        typ, _, rec, count, size = struct.unpack_from("<BBHII", s, p)
        sections[typ] = (p + 12, rec, count)
        p += 12 + size

    def records(typ, fmt):
        off, rec, count = sections.get(typ, (0, 0, 0))
        return [struct.unpack_from(fmt, s, off + i * rec) for i in range(count)]

    accel = [tuple((v >> 2) * ACCEL_MG_PER_DIGIT / 1000 for v in xyz) for xyz in records(1, "<hhh")]
    mic = array("H")
    off, _, count = sections.get(5, (0, 2, 0))
    mic.frombytes(s[off:off + count * 2])
    return {
        "file": path, "size": len(data), "version": version, "device": device, "capture": capture,
        "complete": len(s) == stream_bytes, "stream_bytes": stream_bytes, "have": len(s),
        "stream_version": cver, "duration_s": (end_us - start_us) / 1e6, "flags": flags,
        "accel": accel,                                   # (x, y, z) in g
        "gps": records(2, "<IiiffBBBB"),                  # time_ms, lat_e7, lon_e7, speed, course, q, sats, valid, 0
        "board": records(3, "<IIifBBH"),                  # time_ms, raw, mv, temp_c, status, ...
        "cow": records(4, "<IIiffBBH"),                   # time_ms, raw, mv, ohm, temp_c, status, ...
        "mic": mic, "mic_hz": mic_hz,
    }


def print_summary(c):
    print(f"file: {c['file'].name}  ({c['size']:,} bytes)\n")
    state = "complete" if c["complete"] else f"INCOMPLETE, have {c['have']:,}"
    print(f"upload header : v{c['version']}, device {c['device']}, capture {c['capture']}, "
          f"stream {c['stream_bytes']:,} bytes ({state})")
    dur = c["duration_s"]
    on = [name for i, name in enumerate(FLAGS) if c["flags"] & (1 << i)]
    print(f"capture       : v{c['stream_version']}, {dur:.1f} s recorded")
    print(f"flags         : 0x{c['flags']:03X} = {', '.join(on) or 'none'}\n")

    if c["accel"]:
        print(f"accelerometer : {len(c['accel'])} samples ({len(c['accel']) / dur:.1f} Hz)")
        axes = list(zip(*c["accel"]))
        for name, axis in zip("xyz", axes):
            lo, hi, mean, sd = stats(axis)
            print(f"   {name}: mean {mean:+.3f} g   min {lo:+.3f}   max {hi:+.3f}   spread (SD) {sd:.3f}")
        mag = [math.sqrt(x * x + y * y + z * z) for x, y, z in c["accel"]]
        print(f"   |a| mean {sum(mag) / len(mag):.3f} g  (~1.0 g = lying still)")
    else:
        print("accelerometer : no samples")

    valid = sum(g[7] for g in c["gps"])
    print(f"\nGPS           : {len(c['gps'])} position messages, {valid} with a fix")

    good = [t[3] for t in c["board"] if t[4] == 0]
    print(f"board temp    : {len(c['board'])} readings, {len(good)} valid"
          + (f", mean {sum(good) / len(good):.2f} C (min {min(good):.2f}, max {max(good):.2f})" if good else ""))

    good = [t[4] for t in c["cow"] if t[5] == 0]
    print(f"cow temp      : {len(c['cow'])} readings, {len(good)} valid"
          + (f", mean {sum(good) / len(good):.2f} C (min {min(good):.2f}, max {max(good):.2f})" if good else ""))
    for t in c["cow"]:
        if t[5] != 0:
            print(f"   {t[0] / 1000:5.1f} s: raw ADC {t[1]:4d} -> INVALID: {TEMP_STATUS.get(t[5], f'status {t[5]}')}")

    if c["mic"]:
        lo, hi, mean, rms = stats(c["mic"])
        clipped = sum(1 for v in c["mic"] if v == 0 or v == 4095)
        print(f"\nmicrophone    : {len(c['mic']):,} samples = {len(c['mic']) / c['mic_hz']:.1f} s at {c['mic_hz']} Hz")
        print(f"   ADC min {lo}  max {hi}  mean {mean:.0f}  RMS {rms:.1f}  clipped {clipped}")
    else:
        print("\nmicrophone    : no samples")


def write_csv(path, header, rows):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def export(c):
    out = c["file"].with_suffix("")          # uploads/<file name>/
    out.mkdir(exist_ok=True)
    dur = c["duration_s"]

    rate = len(c["accel"]) / dur if dur else 50
    write_csv(out / "accel.csv", ["time_s", "x_g", "y_g", "z_g", "total_g"],
              [(f"{i / rate:.3f}", f"{x:.4f}", f"{y:.4f}", f"{z:.4f}", f"{math.sqrt(x * x + y * y + z * z):.4f}")
               for i, (x, y, z) in enumerate(c["accel"])])

    def temp(v):                              # NaN (invalid) -> empty cell
        return "" if math.isnan(v) else f"{v:.2f}"

    write_csv(out / "board_temp.csv", ["time_s", "adc_raw", "mv", "temp_c", "status"],
              [(t[0] / 1000, t[1], t[2], temp(t[3]), TEMP_STATUS.get(t[4], t[4])) for t in c["board"]])
    write_csv(out / "cow_temp.csv", ["time_s", "adc_raw", "mv", "resistance_ohm", "temp_c", "status"],
              [(t[0] / 1000, t[1], t[2], "" if math.isnan(t[3]) else f"{t[3]:.0f}", temp(t[4]),
                TEMP_STATUS.get(t[5], t[5])) for t in c["cow"]])
    write_csv(out / "gps.csv", ["time_s", "lat", "lon", "speed_mps", "course_deg", "quality", "satellites", "valid"],
              [(g[0] / 1000, f"{g[1] / 1e7:.7f}", f"{g[2] / 1e7:.7f}", f"{g[3]:.2f}", f"{g[4]:.1f}", g[5], g[6], g[7])
               for g in c["gps"]])

    if c["mic"]:
        mean = sum(c["mic"]) / len(c["mic"])
        pcm = array("h", (max(-32768, min(32767, round((v - mean) * WAV_GAIN))) for v in c["mic"]))
        if sys.byteorder == "big":
            pcm.byteswap()
        with wave.open(str(out / "mic.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(c["mic_hz"])
            w.writeframes(pcm.tobytes())

    print(f"\nexported to {out}\\")
    for f in sorted(out.iterdir()):
        print(f"   {f.name:15s} {f.stat().st_size:>12,} bytes")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    do_export, do_all = "--export" in sys.argv, "--all" in sys.argv
    if args:
        files = [Path(args[0])]
    else:
        files = sorted(UPLOAD_DIR.glob("*.bin"))
        if not files:
            sys.exit("no .bin files in uploads/")
        if not do_all:
            files = files[-1:]

    for i, path in enumerate(files):
        if i:
            print("\n" + "=" * 70 + "\n")
        try:
            c = parse(path)
        except ValueError as e:
            print(f"{path.name}: {e}")
            continue
        print_summary(c)
        if do_export:
            export(c)


if __name__ == "__main__":
    main()
