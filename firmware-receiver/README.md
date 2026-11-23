This folder is for the firmware that we create for our ESP32 on a Heltec LoRa receiver that communicates with our Jetson Nano using USB Serial <br>

Instructions: <br>
- Create a new ESP-IDF project and place all of the files from the /main folder into the project /main folder <br>
- Use the function "ESP-IDF: Select Device Target" and select ESP32s3 <br>
- Select the correct COM port to use based on your device plugged into your computer <br>
- Build and flash the project <br>
Output (one JSON line per LoRa packet on USB serial, 115200 baud): <br>
- `"type":"telemetry"` - 49-byte summary from the collar in HYBRID mode (averages, GPS, temperatures, flags) <br>
- `"type":"features"` - 157-byte packet (version 2) from the collar in LORA_FULL mode (15 s recording every minute): GPS, temperatures (null when not valid), sound events, `mic_rms` and `accel_x/y/z_derivative` (averages for the database, null when not valid), and per 3 s block the tilt X/Y/Z, movement (odba_mg), biggest jolt (peak_mg), movement bursts (moves), acceleration derivative per axis (jerk_x/y/z_gps, g/s) and sound level in three bands; plus `loudness_db`, one value per second <br>
- `"type":"lora_invalid"` - a packet that is neither <br>
