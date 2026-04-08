#!/usr/bin/env python3

import serial
import time
import json
import urllib.request
import urllib.error
from datetime import datetime, timezone

DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41"

ser = serial.Serial(
    port='/dev/serial0', 
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=2  
)

REQUEST_FRAME = b'\x01\x03\x00\x00\x00\x07\x04\x08'

def read_sensor():
    ser.reset_input_buffer()
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Sending request to Arduino...")
    ser.write(REQUEST_FRAME)
    time.sleep(0.5) 
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
            print(f"[{datetime.now().strftime('%H:%M:%S')}] SUCCESS: Temp: {temp}°C, Hum: {hum}%, pH: {ph}")
            return reading
        except Exception as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] ERROR PARSING: {e}")
            return None
    else:
        if len(response) == 0:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] ERROR: Dead Silence (0 bytes received). Arduino not responding.")
        else:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] ERROR: Garbled Data ({len(response)} bytes). Hex: {response.hex(' ')}")
        return None

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
        headers={"Content-Type": "application/json", "Accept": "application/json", "X-Api-Key": key},
    )

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            result = resp.read().decode("utf-8")
            return json.loads(result)
    except urllib.error.HTTPError as e:
        print(f"HTTP Error: {e.code} - {e.read().decode()}")
        return None
    except urllib.error.URLError as e:
        print(f"Connection Error: {e}")
        return None
    except Exception as e:
        print(f"Unexpected Upload Error: {e}")
        return None

try:
    print("Starting Debug Data Collection...")
    READ_INTERVAL_SECONDS = 300  
    READINGS_PER_BATCH = 12      
    
    while True:
        batch_array = []
        i = 0
        
        while i < READINGS_PER_BATCH:
            reading = read_sensor()
            
            if reading:
                batch_array.append(reading)
                i += 1 
                print(f"   --> Added to batch ({i}/{READINGS_PER_BATCH}). Sleeping 5 minutes...")
                
                if i < READINGS_PER_BATCH:
                    time.sleep(READ_INTERVAL_SECONDS)
            else:
                print("   --> Failed to read. Retrying in 5 seconds...")
                time.sleep(5)

        if batch_array:
            print(f"\n>>> 1 HOUR ELAPSED. Uploading {len(batch_array)} readings...")
            response = upload_readings_batch(DEVICE_ID, batch_array)
            if response:
                print(f">>> Uploaded Successfully: {response}\n")
            else:
                print(">>> Upload Failed.\n")

except KeyboardInterrupt:
    ser.close()
    print("\nScript closed safely.")
