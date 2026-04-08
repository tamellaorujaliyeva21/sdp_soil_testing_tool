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
    port='/dev/ttyS0', # Or /dev/serial0 depending on your Pi
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=2  # Increased timeout to allow for frame syncing
)

# ====== SENSOR READ FUNCTION ======
def read_sensor():
    # 1. Clear out the old, misaligned data from the buffer
    ser.reset_input_buffer()
    
    # 2. Since the Arduino sends every 2 seconds, we read a large chunk (40 bytes) 
    # to guarantee we capture at least one complete 19-byte frame inside it.
    time.sleep(2.5) 
    raw_data = ser.read(40)

    # 3. Hunt for the start of the frame: Device ID (0x01), Function (0x03), Length (0x0E)
    start_idx = raw_data.find(b'\x01\x03\x0e')

    if start_idx != -1 and len(raw_data) >= start_idx + 19:
        # Extract the perfectly aligned 19-byte frame
        response = raw_data[start_idx : start_idx + 19]
        
        try:
            # Parse values
            hum  = int.from_bytes(response[3:5],  byteorder='big') / 10.0
            temp = int.from_bytes(response[5:7],  byteorder='big') / 10.0
            ec   = int.from_bytes(response[7:9],  byteorder='big')
            ph   = int.from_bytes(response[9:11], byteorder='big') / 10.0
            n    = int.from_bytes(response[11:13], byteorder='big')
            p    = int.from_bytes(response[13:15], byteorder='big')
            k    = int.from_bytes(response[15:17], byteorder='big')

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
        print("\nError: Could not sync to the start of a data frame. Retrying...")
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

        # We only sleep 1 second here because read_sensor() now inherently 
        # waits 2.5 seconds to gather a full buffer frame from the Arduino.
        time.sleep(1)  

except KeyboardInterrupt:
    ser.close()
    print("\nSerial closed.")
