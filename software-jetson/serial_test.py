# Function of serial_test.py:
# Prints all packets received from the Heltec receiver using USB Serial

import serial

#serial power may change depending on the device, the Heltec ESP32 receiver could show up as /dev/ttyACM0
SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 115200  #make sure this matches up exactly with what's on Heltec ESP32 firmware


print(f"Opening {SERIAL_PORT} at {BAUD_RATE} baud...")

try:
    with serial.Serial( #opens up serial connection
        port = SERIAL_PORT, 
        baudrate = BAUD_RATE, 
        timeout = 1 #waits a second before reading data
    ) as heltec:
        
        print("Waiting for packets. Press Ctrl+C to stop.")

        while True:   #keeps checking for packets until Ctrl+C is hit
            packet = heltec.readline() #result is returned as raw bytes
            if packet:
                #converts raw bytes into readable text, rstrip removes the newline \n type of stuff
                print(packet.decode("utf-8", errors="replace").rstrip())

except KeyboardInterrupt:
    print("\nSerial monitor stopped.") #when Ctrl+C gets pressed
except serial.SerialException as error:
    print(f"Serial connection error: {error}")