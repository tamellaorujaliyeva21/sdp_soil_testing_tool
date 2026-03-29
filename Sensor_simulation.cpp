#include <SoftwareSerial.h>

// RX on Pin 10, TX on Pin 11
SoftwareSerial sensorSim(10, 11); 

// Dummy NPK + 4 values (7 total)
// Format: Humidity, Temp, EC, pH, N, P, K
// this represents the 19 bytes of sensor data
byte sensorResponse[] = {0x01, 0x03, 0x0E, 0x00, 0x14, 0x00, 0xFA, 0x00, 0x32, 0x00, 0x46, 0x00, 0x1E, 0x00, 0x28, 0x00, 0x32, 0x12, 0x34};

void setup() {
  Serial.begin(9600);    // Debugging
  sensorSim.begin(9600); // UART Comm to Pi
  Serial.println("NPK Sensor Simulator Ready...");
}

void loop() {
  if (sensorSim.available() > 0) {
    // Read the incoming request from Pi (usually 8 bytes)
    while(sensorSim.available()) {
      sensorSim.read(); 
    }
    
    // Small delay to simulate sensor processing
    delay(50);
    
    // Send the dummy data back
    sensorSim.write(sensorResponse, sizeof(sensorResponse));
    Serial.println("Data sent to Raspberry Pi");
  }
}
