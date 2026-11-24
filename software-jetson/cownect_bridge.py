import json
import os
import time
from datetime import datetime, timezone

import requests
import serial


# Change this if the Heltec appears under a different device name.
SERIAL_PORT = "/dev/ttyUSB0"

# This must match the baud rate used by the Heltec receiver.
BAUD_RATE = 115200

# Replace this with your actual Render backend URL.
API_URL = "https://cownect-api.onrender.com/api/v1/readings"


def create_database_reading(telemetry):
    """
    Convert the Heltec's decoded JSON into a database reading.

    The original complete packet is retained in the
    telemetry JSONB column, including the blocks array.
    """

    received_at = datetime.now(timezone.utc).isoformat()

    return {
        "collar_id": str(telemetry["device_id"]),

        # The current LoRa packet does not contain a
        # wall-clock timestamp.
        "recorded_at": received_at,

        "packet_type": telemetry.get("type"),
        "capture_id": telemetry.get("capture_id"),

        "latitude": telemetry.get("latitude"),
        "longitude": telemetry.get("longitude"),

        "gps_satellites": telemetry.get(
            "gps_satellites"
        ),
        "gps_fix_quality": telemetry.get(
            "gps_fix_quality"
        ),

        "board_temperature_c": telemetry.get(
            "board_temperature_c"
        ),
        "cow_temperature_c": telemetry.get(
            "cow_temperature_c"
        ),

        "accel_x_derivative": telemetry.get(
            "accel_x_derivative"
        ),
        "accel_y_derivative": telemetry.get(
            "accel_y_derivative"
        ),
        "accel_z_derivative": telemetry.get(
            "accel_z_derivative"
        ),

        "microphone_level_derivative": telemetry.get(
            "mic_rms"
        ),

        # No battery reading is currently forwarded.
        "battery_voltage": None,

        # The Jetson ML classifier can populate these
        # fields once its output is integrated.
        "activity": None,
        "confidence": None,

        # Store every field printed by the Heltec,
        # including blocks, tilt, movement, jerk,
        # loudness, flags, and capture metadata.
        "telemetry": telemetry,
    }



print(f"Opening Heltec serial port: {SERIAL_PORT}")
print(f"Using baud rate: {BAUD_RATE}")
print(f"Sending readings to: {API_URL}")

try:
    with serial.Serial(
        port=SERIAL_PORT,
        baudrate=BAUD_RATE,
        timeout=1,
    ) as heltec:

        print("Waiting for telemetry packets. Press Ctrl+C to stop.")

        while True:
            raw_line = heltec.readline()

            if not raw_line:
                continue

            line = raw_line.decode("utf-8", errors="replace").strip()

            if not line:
                continue

            print(f"Heltec output: {line}")

            # The Heltec also prints ESP-IDF debug messages.
            # Ignore anything that is not JSON.
            try:
                telemetry = json.loads(line)
            except json.JSONDecodeError:
                continue

            # Only process valid packets: "telemetry" (HYBRID) or "features" (LORA_FULL).
            if telemetry.get("type") not in ("telemetry", "features"):
                continue

            # Keep the full features packet (per-block values) in a local log.
            if telemetry.get("type") == "features":
                try:
                    with open("features_log.jsonl", "a", encoding="utf-8") as log_file:
                        log_file.write(json.dumps({**telemetry, "received_at": datetime.now(timezone.utc).isoformat()}) + "\n")
                except OSError as error:
                    print(f"Could not save features packet: {error}")

            try:
                database_reading = create_database_reading(telemetry)

                response = requests.post(
                    API_URL,
                    json=database_reading,
                    timeout=30,
                )

                if response.status_code == 201:
                    print(
                        "Successfully saved reading for collar "
                        f"{database_reading['collar_id']}"
                    )
                else:
                    print(
                        f"Backend rejected reading: "
                        f"HTTP {response.status_code}"
                    )
                    print(response.text)

            except KeyError as error:
                print(f"Telemetry packet is missing a required field: {error}")

            except requests.RequestException as error:
                print(f"Could not connect to FastAPI backend: {error}")
                print("The bridge will continue waiting for packets.")

except KeyboardInterrupt:
    print("\nCowNect bridge stopped.")

except serial.SerialException as error:
    print(f"Serial connection error: {error}")
    print("Check the USB device name and make sure no other program is using it.")