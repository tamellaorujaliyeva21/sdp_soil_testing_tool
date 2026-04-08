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
    port='/dev/serial0', 
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=2  
)

# Modbus request frame to trigger the Arduino
REQUEST_FRAME = b'\x01\x03\x00\x00\x00\x07\x04\x08'

# ====== SENSOR READ FUNCTION ======
def read_sensor():
    ser.reset_input_buffer()
    
    # 1. Ask Arduino for data
    ser.write(REQUEST_FRAME)
    
    # 2. Give the Arduino half a second to process and reply
    time.sleep(0.5) 
    
    # 3. Read the 19-byte response
    response = ser.read(19)
    
    if len(response) == 19 and response.startswith(b'\x01\x03\x0e'):
        try:
            hum  = int.from_bytes(response[3:5],  byteorder='big') / 10.0
            temp = int.from_bytes(response[5:7],  byteorder='big') / 10.0
            ec   = int.from_bytes(response[7:9],  byteorder='big')
            ph   = int.from_bytes(response[9:11], byteorder='big') / 10.0
            n    = int.from_bytes(response[11:13], byteorder='big')
            p    = int.from_bytes(response[13:15], byteorder='big')
            k    = int.from_bytes(response[15:17], byteorder='big')

            reading = {
                "reading_time": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
                "temperature": temp,
                "moisture": hum,
                "conductivity": ec,
                "ph_value": ph,
                "npk_n": n,
                "npk_p": p,
                "npk_k": k
            }

            print(f"[{datetime.now().strftime('%H:%M:%S')}] Read Success: Temp={temp}°C, Hum={hum}%, pH={ph}")
            return reading

        except Exception as e:
            print("Parsing error:", e)
            return None
    else:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Error: Incomplete or invalid payload received.")
        return None

# ====== API UPLOAD FUNCTION ======
def upload_readings_batch(device_id, readings, timeout=120):
    url = "https://soil-repo-gcp-git-678290165816.europe-west1.run.app"
    key = "smksmKDMkcsmaskamAK12021SKMS1"

    if not readings:
        return None

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
    print("Starting Batch Data Collection...")
    print("Interval: Read every 5 minutes. Upload every 1 hour (12 readings).")
    print("="*60)
    
    # Timing configuration
    READ_INTERVAL_SECONDS = 300  # 5 minutes
    READINGS_PER_BATCH = 12      # 12 readings * 5 mins = 60 mins
    
    while True:
        # Create an empty list to store the hour's worth of data
        batch_array = []
        
        # Loop 12 times
        for i in range(READINGS_PER_BATCH):
            reading = read_sensor()
            if reading:
                batch_array.append(reading)
            
            readings_left = READINGS_PER_BATCH - (i + 1)
            print(f"   --> Sleeping 5 minutes... ({readings_left} readings left until upload)")
            
            # Sleep 5 minutes (unless it is the final reading of the hour)
            if i < READINGS_PER_BATCH - 1:
                time.sleep(READ_INTERVAL_SECONDS)

        # 1 Hour has passed! Upload the accumulated array.
        print("\n" + "="*60)
        print(f">>> 1 HOUR ELAPSED. Uploading batch of {len(batch_array)} readings to GCP...")
        
        if batch_array:
            response = upload_readings_batch(DEVICE_ID, batch_array)
            print(">>> Upload Response:", response)
        else:
            print(">>> No valid readings were collected this hour. Skipping upload.")
            
        print("="*60 + "\n")

        # Sleep for 5 minutes before starting the next hour's cycle
        time.sleep(READ_INTERVAL_SECONDS)

except KeyboardInterrupt:
    ser.close()
    print("\nSerial closed.")
