"""receiver.py - stands in for the Jetson: receives the collar's Wi-Fi uploads.

The collar (UPLOAD_TARGET = UPLOAD_TO_JETSON in main/wifi_upload.h) opens a plain
TCP connection to JETSON_IP:JETSON_RAW_PORT, sends one upload, then closes:
    21-byte upload header  (magic "CNUP", version, device id, capture id, stream bytes)
    capture stream         (stream bytes long)
TEST 9 only opens and closes the connection, so it arrives with 0 bytes.

Run:   python receiver.py            (listens on port 5000)
       python receiver.py 6000       (another port; must match JETSON_RAW_PORT)
Stop:  Ctrl+C
Every upload with data is saved to uploads/<date>_<time>_dev<id>_cap<id>.bin
"""
import socket
import struct
import sys
from datetime import datetime
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
UPLOAD_DIR = Path(__file__).resolve().parent / "uploads"
HEADER = struct.Struct("<IBQII")   # magic, version, device id, capture id, stream bytes = 21 bytes
MAGIC = 0x50554E43                 # "CNUP"
IDLE_TIMEOUT_S = 30                # give up on a connection that stops sending


def now():
    return datetime.now().strftime("%H:%M:%S")


def receive_all(conn):
    """Reads until the collar closes the connection (or goes quiet for IDLE_TIMEOUT_S)."""
    conn.settimeout(IDLE_TIMEOUT_S)
    chunks, total = [], 0
    try:
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
    except socket.timeout:
        print(f"[{now()}]   no data for {IDLE_TIMEOUT_S} s - closing (upload may be incomplete)")
    return b"".join(chunks)


def report(data):
    if not data:
        print(f"[{now()}]   0 bytes: connection test only (TEST 9) - OK")
        return
    print(f"[{now()}]   received {len(data):,} bytes")
    if len(data) < HEADER.size:
        print(f"[{now()}]   too short for the {HEADER.size}-byte upload header - not saved")
        return

    magic, version, device_id, capture_id, stream_bytes = HEADER.unpack_from(data)
    if magic != MAGIC:
        print(f"[{now()}]   no 'CNUP' magic at the start (got 0x{magic:08X}) - not a collar upload?")
    else:
        got = len(data) - HEADER.size
        state = "complete" if got == stream_bytes else f"INCOMPLETE: {got:,} of {stream_bytes:,} stream bytes"
        print(f"[{now()}]   header v{version} | device {device_id} | capture {capture_id} | {state}")

    UPLOAD_DIR.mkdir(exist_ok=True)
    name = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    if magic == MAGIC:
        name += f"_dev{device_id}_cap{capture_id}"
    path = UPLOAD_DIR / f"{name}.bin"
    path.write_bytes(data)
    print(f"[{now()}]   saved to {path}")


def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", PORT))
    server.listen(1)
    server.settimeout(1.0)   # wake up every second so Ctrl+C works on Windows
    print(f"[{now()}] listening on port {PORT} - waiting for the collar (Ctrl+C to stop)")
    try:
        while True:
            try:
                conn, (ip, port) = server.accept()
            except socket.timeout:
                continue
            with conn:
                print(f"[{now()}] connection from {ip}")
                report(receive_all(conn))
    except KeyboardInterrupt:
        print(f"\n[{now()}] stopped")
    finally:
        server.close()


if __name__ == "__main__":
    main()
