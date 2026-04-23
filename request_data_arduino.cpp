#include <SoftwareSerial.h>

SoftwareSerial sensorSim(10, 11);

// Human-readable target ranges
const int TempMin = 10, TempMax = 35;           // °C
const int MoistureMin = 20, MoistureMax = 80;   // %
const int PhMin = 5, PhMax = 8;                 // pH
const int ConductivityMin = 100, ConductivityMax = 2000;
const int NpkMin = 10, NpkMax = 50;

void setup() {
  Serial.begin(9600);
  sensorSim.begin(9600);

  Serial.println("\n--- 1-Hour Batch System Ready ---");
  Serial.println("Listening for ESP32 requests on Pin 10...");
  randomSeed(analogRead(A0));
}

uint16_t rand16(int minVal, int maxVal) {
  return (uint16_t)random(minVal, maxVal + 1);
}

void loop() {
  if (sensorSim.available() >= 8) {
    byte request[8];
    for (int i = 0; i < 8; i++) {
      request[i] = sensorSim.read();
    }

    Serial.println("\n[!] Request received! Generating data...");

    // Generate values in the SAME RAW SCALE expected by ESP32 code
    uint16_t humidityRaw    = rand16(MoistureMin * 10, MoistureMax * 10);  // 20.0..80.0%
    uint16_t temperatureRaw = rand16(TempMin * 10, TempMax * 10);          // 10.0..35.0°C
    uint16_t ecRaw          = rand16(ConductivityMin, ConductivityMax);    // unchanged
    uint16_t phRaw          = rand16(PhMin * 100, PhMax * 100);            // 5.00..8.00
    uint16_t npkN           = rand16(NpkMin, NpkMax);
    uint16_t npkP           = rand16(NpkMin, NpkMax);
    uint16_t npkK           = rand16(NpkMin, NpkMax);

    byte sensorResponse[19];
    sensorResponse[0]  = 0x01;
    sensorResponse[1]  = 0x03;
    sensorResponse[2]  = 0x0E;

    sensorResponse[3]  = humidityRaw >> 8;
    sensorResponse[4]  = humidityRaw & 0xFF;

    sensorResponse[5]  = temperatureRaw >> 8;
    sensorResponse[6]  = temperatureRaw & 0xFF;

    sensorResponse[7]  = ecRaw >> 8;
    sensorResponse[8]  = ecRaw & 0xFF;

    sensorResponse[9]  = phRaw >> 8;
    sensorResponse[10] = phRaw & 0xFF;

    sensorResponse[11] = npkN >> 8;
    sensorResponse[12] = npkN & 0xFF;

    sensorResponse[13] = npkP >> 8;
    sensorResponse[14] = npkP & 0xFF;

    sensorResponse[15] = npkK >> 8;
    sensorResponse[16] = npkK & 0xFF;

    sensorResponse[17] = 0x12; // dummy CRC
    sensorResponse[18] = 0x34; // dummy CRC

    sensorSim.write(sensorResponse, sizeof(sensorResponse));

    Serial.print("--> Data Sent: Temp: ");
    Serial.print(temperatureRaw / 10.0, 1);
    Serial.print(" C | Hum: ");
    Serial.print(humidityRaw / 10.0, 1);
    Serial.print(" % | pH: ");
    Serial.println(phRaw / 100.0, 2);
  }
}
