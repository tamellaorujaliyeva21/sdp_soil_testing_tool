import serial
import time

# Configure the serial port
# For Pi 3/4/5
ser = serial.Serial(
    port='/dev/ttyS0',
    baudrate=9600,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS,
    timeout=1
)

# Modbus request frame: [Address, Function, Reg_High, Reg_Low, Qty_High, Qty_Low, CRC, CRC]
request_frame = b'\x01\x03\x00\x00\x00\x07\x04\x08'

def read_sensor():
    print("Sending request to simulated sensor...")
    ser.write(request_frame)
    
    # Wait for response (19 bytes: 3 header + 14 data (7*2) + 2 CRC)
    response = ser.read(19)
    
    if len(response) == 19:
        # Extracting values 2 bytes each
        hum  = int.from_bytes(response[3:5],  byteorder='big')
        temp = int.from_bytes(response[5:7],  byteorder='big')
        ec   = int.from_bytes(response[7:9],  byteorder='big')
        ph   = int.from_bytes(response[9:11], byteorder='big') / 10.0
        n    = int.from_bytes(response[11:13], byteorder='big')
        p    = int.from_bytes(response[13:15], byteorder='big')
        k    = int.from_bytes(response[15:17], byteorder='big')
        
        print(f"--- Sensor Data ---")
        print(f"Nitrogen: {n} mg/kg | Phosphorus: {p} mg/kg | Potassium: {k} mg/kg")
        print(f"pH: {ph} | Temp: {temp/10.0}°C | Humidity: {hum/10.0}%")
        #print(f"Conductivity (EC): {ec} µS/cm")
    else:
        print("Error: No response or incomplete data.")

try:
    while True:
        read_sensor()
        time.sleep(2)
except KeyboardInterrupt:
    ser.close()
    print("Serial closed.")
