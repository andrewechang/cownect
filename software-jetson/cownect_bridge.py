import json
import os
import time
from datetime import datetime, timezone
from dotenv import load_dotenv
dotenv()

import requests
import serial


# Change this if the Heltec appears under a different device name.
SERIAL_PORT = os.getenv("COWNect_SERIAL_PORT")

# This must match the baud rate used by the Heltec receiver.
BAUD_RATE = 115200

# Replace this with your actual Render backend URL.
API_URL = os.getenv("COWNect_API_URL")

def create_database_reading(telemetry):
    """
    Converts the JSON format printed by the Heltec into the
    JSON format expected by the FastAPI backend.
    """

    return {
        # The backend expects collar_id to be a string.
        "collar_id": str(telemetry["device_id"]),

        # The Heltec packet does not currently include a timestamp,
        # so the Jetson records the time when it received the packet.
        "recorded_at": datetime.now(timezone.utc).isoformat(),

        "latitude": telemetry.get("latitude"),
        "longitude": telemetry.get("longitude"),

        "board_temperature_c": telemetry.get("board_temperature_c"),
        "cow_temperature_c": telemetry.get("cow_temperature_c"),

        # The current receiver packet provides microphone RMS.
        "microphone_level_derivative": telemetry.get("mic_rms"),

        # These fields are not currently included in the receiver's
        # JSON packet, so they remain empty in PostgreSQL.
        "accel_x_derivative": None,
        "accel_y_derivative": None,
        "accel_z_derivative": None,
        "battery_voltage": None,
        "activity": None,
        "confidence": None,
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

            # Only process valid telemetry JSON objects.
            if telemetry.get("type") != "telemetry":
                continue

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