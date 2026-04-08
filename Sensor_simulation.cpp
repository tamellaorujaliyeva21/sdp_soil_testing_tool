// Define valid ranges
const int TempMin = 10, TempMax = 35;       // °C
const int MoistureMin = 20, MoistureMax = 80; // %RH
const int PhMin = 5, PhMax = 8;
const int ConductivityMin = 100, ConductivityMax = 2000; // µS/cm
const int NpkMin = 10, NpkMax = 50; // mg/kg

void setup() {
  // Communicate at 9600 baud over the main TX/RX pins (Pin 1 / Pin 0)
  Serial.begin(9600);    
  randomSeed(analogRead(A0)); 
}

uint16_t rand16(int minVal, int maxVal) {
  return (uint16_t)random(minVal, maxVal + 1);
}

void loop() {
  uint16_t humidity = rand16(MoistureMin, MoistureMax);
  uint16_t temperature = rand16(TempMin, TempMax);
  uint16_t ec = rand16(ConductivityMin, ConductivityMax);
  uint16_t ph = rand16(PhMin * 10, PhMax * 10); 
  uint16_t npkN = rand16(NpkMin, NpkMax);
  uint16_t npkP = rand16(NpkMin, NpkMax);
  uint16_t npkK = rand16(NpkMin, NpkMax);

  byte sensorResponse[19];
  sensorResponse[0] = 0x01; 
  sensorResponse[1] = 0x03; 
  sensorResponse[2] = 0x0E; 

  sensorResponse[3] = humidity >> 8;
  sensorResponse[4] = humidity & 0xFF;

  sensorResponse[5] = temperature >> 8;
  sensorResponse[6] = temperature & 0xFF;

  sensorResponse[7] = ec >> 8;
  sensorResponse[8] = ec & 0xFF;

  sensorResponse[9]  = ph >> 8;
  sensorResponse[10] = ph & 0xFF;

  sensorResponse[11] = npkN >> 8;
  sensorResponse[12] = npkN & 0xFF;

  sensorResponse[13] = npkP >> 8;
  sensorResponse[14] = npkP & 0xFF;

  sensorResponse[15] = npkK >> 8;
  sensorResponse[16] = npkK & 0xFF;

  sensorResponse[17] = 0x12;
  sensorResponse[18] = 0x34;

  // Broadcast ONLY the raw binary Modbus array out of Pin 1 (TX)
  Serial.write(sensorResponse, sizeof(sensorResponse));

  delay(2000);
}
