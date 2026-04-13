#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===================== HOTSPOT CONFIG =====================
const char* WIFI_SSID = "YourPhoneHotspotName";
const char* WIFI_PASSWORD = "YourHotspotPassword";

// ===================== SERVER CONFIG =====================
const char* SERVER_URL = "https://soil-repo-gcp-git-678290165816.europe-west1.run.app/readings/batch";
const char* API_KEY = "smksmKDMkcsmaskamAK12021SKMS1";

// ===================== DEVICE =====================
String DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41";

// ===================== SERIAL =====================
HardwareSerial SensorSerial(2); // UART2

// Arduino request frame
uint8_t REQUEST_FRAME[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

// ===================== DATA STRUCT =====================
struct Reading {
  float temperature;
  float moisture;
  float ph;
  int ec;
  int n;
  int p;
  int k;
};

Reading batch[12];
int batchIndex = 0;

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  // UART to Arduino
  SensorSerial.begin(9600, SERIAL_8N1, 16, 17); 
  // RX=16, TX=17 (change if needed)

  // ================= WIFI HOTSPOT =================
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to hotspot");

  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Failed!");
  }
}

// ================= READ SENSOR =================
bool readSensor(Reading &r) {
  SensorSerial.flush();

  // send request
  SensorSerial.write(REQUEST_FRAME, sizeof(REQUEST_FRAME));
  delay(500);

  if (SensorSerial.available() < 19) {
    Serial.println("No response / incomplete data");
    return false;
  }

  uint8_t buf[19];
  SensorSerial.readBytes(buf, 19);

  if (!(buf[0] == 0x01 && buf[1] == 0x03)) {
    Serial.println("Invalid frame");
    return false;
  }

  r.moisture = ((buf[3] << 8) | buf[4]) / 10.0;
  r.temperature = ((buf[5] << 8) | buf[6]) / 10.0;
  r.ec = (buf[7] << 8) | buf[8];
  r.ph = ((buf[9] << 8) | buf[10]) / 10.0;
  r.n = (buf[11] << 8) | buf[12];
  r.p = (buf[13] << 8) | buf[14];
  r.k = (buf[15] << 8) | buf[16];

  Serial.printf("Temp: %.1f, Hum: %.1f, pH: %.1f\n",
                r.temperature, r.moisture, r.ph);

  return true;
}

// ================= UPLOAD BATCH =================
void uploadBatch() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("X-Api-Key", API_KEY);

  StaticJsonDocument<4096> doc;

  doc["device_id"] = DEVICE_ID;

  JsonArray readings = doc.createNestedArray("readings");

  for (int i = 0; i < batchIndex; i++) {
    JsonObject r = readings.createNestedObject();

    r["temperature"] = batch[i].temperature;
    r["moisture"] = batch[i].moisture;
    r["conductivity"] = batch[i].ec;
    r["ph_value"] = batch[i].ph;
    r["npk_n"] = batch[i].n;
    r["npk_p"] = batch[i].p;
    r["npk_k"] = batch[i].k;
  }

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);

  Serial.print("HTTP Response: ");
  Serial.println(code);

  http.end();
}

// ================= LOOP =================
void loop() {

  Reading r;

  if (readSensor(r)) {
    batch[batchIndex++] = r;

    if (batchIndex >= 12) {
      Serial.println("Uploading batch...");
      uploadBatch();
      batchIndex = 0;
    }
  }

  delay(300000); // 5 minutes
}
