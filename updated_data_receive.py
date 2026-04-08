#!/usr/bin/env python3

import serial
import time
from datetime import datetime, timezone

# ====== CONFIG ======
DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41"

# Serial setup (UART)
ser = serial.Serial(
    port='/dev/ttyS0',
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=1
)

# Modbus request frame
REQUEST_FRAME = b'\x01\x03\x00\x00\x00\x07\x04\x08'


# ====== SENSOR READ FUNCTION ======
def read_sensor():
    ser.write(REQUEST_FRAME)
    response = ser.read(19)

    if len(response) != 19:
        print("Error: No response or incomplete data.")
        return None

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


# ====== API UPLOAD FUNCTION (your function) ======
import json
import os
import urllib.request
import urllib.error


def upload_readings_batch(device_id, readings, base_url=None, api_key=None, timeout=120):
    url = (base_url or os.environ.get("SOIL_API_BASE_URL", "http://localhost:8080")).rstrip("/")
    key = (api_key if api_key else os.environ.get("READINGS_UPLOAD_API_KEY", "")).strip()

    if not key:
        raise RuntimeError("Missing API key.")

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
    while True:
        reading = read_sensor()

        if reading:
            try:
                response = upload_readings_batch(
                    DEVICE_ID,
                    [reading]  # batch format
                )
                print("Uploaded:", response)

            except Exception as e:
                print("Upload failed:", e)

        time.sleep(5)  # wait between readings

except KeyboardInterrupt:
    ser.close()
    print("\nSerial closed.")
