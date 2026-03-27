void setup() {
  Serial.begin(9600); // UART communication
}

void loop() {
  // Fake sensor values
  int nitrogen = random(10, 100);
  int phosphorus = random(10, 100);
  int potassium = random(10, 100);
  float temperature = random(200, 350) / 10.0; // 20.0–35.0
  float humidity = random(300, 800) / 10.0;    // 30–80%
  float ph = random(50, 80) / 10.0;            // 5.0–8.0
  float conductivity = random(100, 1000);      // µS/cm

  // Format similar to real sensor output
  Serial.print("N:");
  Serial.print(nitrogen);
  Serial.print(",P:");
  Serial.print(phosphorus);
  Serial.print(",K:");
  Serial.print(potassium);
  Serial.print(",T:");
  Serial.print(temperature);
  Serial.print(",H:");
  Serial.print(humidity);
  Serial.print(",pH:");
  Serial.print(ph);
  Serial.print(",EC:");
  Serial.println(conductivity);

  delay(2000); // send every 2 seconds
}
