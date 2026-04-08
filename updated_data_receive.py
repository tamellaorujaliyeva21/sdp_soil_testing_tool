#!/usr/bin/env python3

import serial
import time
import json
import urllib.request
import urllib.error
from datetime import datetime, timezone

# ====== CONFIG ======
DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41"

# Serial setup (UART)
ser = serial.Serial(
    port='/dev/serial0',  # <-- Change this line
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=5  # Increased to 5s so it patiently waits for the Arduino's 2s broadcast
)

# ====== SENSOR READ FUNCTION ======
def read_sensor():
    ser.reset_input_buffer()
    
    # Block and wait until the exact 3-byte header is received
    sync = ser.read_until(b'\x01\x03\x0e')
    
    # Check if the header was actually found
    if sync.endswith(b'\x01\x03\x0e'):
        
        # We found the header! The next 16 bytes are exactly our payload + CRC
        payload = ser.read(16)
        
        if len(payload) == 16:
            try:
                # Parse values exactly where they belong
                hum  = int.from_bytes(payload[0:2],  byteorder='big') / 10.0
                temp = int.from_bytes(payload[2:4],  byteorder='big') / 10.0
                ec   = int.from_bytes(payload[4:6],  byteorder='big')
                ph   = int.from_bytes(payload[6:8],  byteorder='big') / 10.0
                n    = int.from_bytes(payload[8:10], byteorder='big')
                p    = int.from_bytes(payload[10:12], byteorder='big')
                k    = int.from_bytes(payload[12:14], byteorder='big')

                reading = {
                    "reading_time": datetime.now(timezone.utc)
                        .replace(microsecond=0)
                        .isoformat()
                        .replace("+00:00", "Z"),
                    "temperature": temp,
                    "moisture": hum,
                    "conductivity": ec,
                    "ph_value": ph,
                    "npk_n": n,
                    "npk_p": p,
                    "npk_k": k
                }

                print("\n--- Sensor Data ---")
                print(f"N: {n} | P: {p} | K: {k}")
                print(f"Temp: {temp}°C | Moisture: {hum}% | pH: {ph} | EC: {ec}")

                return reading

            except Exception as e:
                print("Parsing error:", e)
                return None
        else:
            print(f"Error: Incomplete payload received ({len(payload)} bytes).")
            return None
    else:
        # If it fails, print the raw hex so we can see exactly what the Pi is hearing
        print(f"\n[DEBUG] Sync failed. Raw bytes heard: {sync.hex(' ')}")
        return None

# ====== API UPLOAD FUNCTION ======
def upload_readings_batch(device_id, readings, timeout=120):
    url = "https://soil-repo-gcp-git-678290165816.europe-west1.run.app"
    key = "smksmKDMkcsmaskamAK12021SKMS1"

    body = {
        "device_id": device_id,
        "readings": readings
    }

    data = json.dumps(body).encode("utf-8")

    req = urllib.request.Request(
        f"{url}/readings/batch",
        data=data,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json",
            "X-Api-Key": key,
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            result = resp.read().decode("utf-8")
            return json.loads(result)

    except urllib.error.HTTPError as e:
        print("HTTP Error:", e.read().decode())
    except urllib.error.URLError as e:
        print("Connection Error:", e)

    return None

# ====== MAIN LOOP ======
try:
    print("Starting Continuous Data Sync & Upload...")
    while True:
        reading = read_sensor()

        if reading:
            try:
                response = upload_readings_batch(DEVICE_ID, [reading])
                print("Uploaded:", response)
            except Exception as e:
                print("Upload failed:", e)

        # We removed time.sleep() here!
        # The script now inherently waits for the Arduino's 2-second interval via read_until()

except KeyboardInterrupt:
    ser.close()
    print("\nSerial closed.")
