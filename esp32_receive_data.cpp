#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "esp_sleep.h"

// ===================== HOTSPOT CONFIG =====================
const char* WIFI_SSID = "ADA_Event";
const char* WIFI_PASSWORD = "R7h588aW8";

// ===================== SERVER CONFIG =====================
const char* SERVER_URL = "https://soil-repo-gcp-git-678290165816.europe-west1.run.app/readings/batch";
const char* API_KEY = "smksmKDMkcsmaskamAK12021SKMS1";

// ===================== DEVICE =====================
const char* DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41";

// ===================== SERIAL =====================
HardwareSerial SensorSerial(2);
uint8_t REQUEST_FRAME[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

// ===================== SLEEP / BATCH =====================
const uint64_t SLEEP_SECONDS = 300;   // 5 minutes
const uint64_t uS_TO_S_FACTOR = 1000000ULL;
const int READINGS_PER_BATCH = 12;    // 12 readings = 1 hour

// ===================== STORAGE =====================
Preferences prefs;

// ===================== DATA STRUCT =====================
struct Reading {
  float temperature;
  float moisture;
  float ph;
  uint16_t ec;
  uint16_t n;
  uint16_t p;
  uint16_t k;
  char reading_time[25];
};

// ===================== BATCH =====================
Reading batch[READINGS_PER_BATCH];
int batchCount = 0;

// ===================== HELPERS =====================
void printDivider() {
  Serial.println("--------------------------------------------------");
}

void printReading(const Reading& r, int index) {
  Serial.printf(
    "[%d] %s | Temp: %.2f C | Moist: %.2f %% | pH: %.2f | EC:%u | N:%u P:%u K:%u\n",
    index,
    r.reading_time,
    r.temperature,
    r.moisture,
    r.ph,
    r.ec,
    r.n,
    r.p,
    r.k
  );
}

bool isReasonableReading(const Reading& r) {
  if (r.temperature < -40 || r.temperature > 80) return false;
  if (r.moisture < 0 || r.moisture > 100) return false;
  if (r.ph < 3.0 || r.ph > 9.0) return false;
  return true;
}

// ===================== TIME =====================
void connectNtpIfPossible() {
  if (WiFi.status() != WL_CONNECTED) return;

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  Serial.println("NTP sync requested");
}

bool waitForNtpSync(unsigned long timeoutMs = 5000) {
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      return true;
    }
    delay(200);
  }

  return false;
}

void makeUtcTimestamp(char* out, size_t len) {
  time_t now = time(nullptr);

  if (now > 1700000000) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return;
  }

  // fallback while NTP not yet synced
  snprintf(out, len, "2026-04-21T00:00:00Z");
}

// ===================== PREFERENCES =====================
void saveBatchToPreferences() {
  prefs.begin("soilbatch", false);
  prefs.putInt("count", batchCount);
  prefs.putBytes("batch", batch, sizeof(batch));
  prefs.end();

  Serial.printf("Saved %d readings to Preferences\n", batchCount);
}

void loadBatchFromPreferences() {
  prefs.begin("soilbatch", true);

  int storedCount = prefs.getInt("count", 0);
  if (storedCount < 0 || storedCount > READINGS_PER_BATCH) {
    storedCount = 0;
  }

  size_t expected = sizeof(batch);
  size_t actual = prefs.getBytesLength("batch");

  if (actual == expected) {
    prefs.getBytes("batch", batch, sizeof(batch));
    batchCount = storedCount;
  } else {
    memset(batch, 0, sizeof(batch));
    batchCount = 0;
  }

  prefs.end();

  Serial.printf("Loaded %d readings from Preferences\n", batchCount);
  for (int i = 0; i < batchCount; i++) {
    printReading(batch[i], i);
  }
}

void clearBatchAndPreferences() {
  batchCount = 0;
  memset(batch, 0, sizeof(batch));

  prefs.begin("soilbatch", false);
  prefs.putInt("count", 0);
  prefs.remove("batch");
  prefs.end();

  Serial.println("Cleared batch and Preferences");
}

// ===================== WIFI =====================
bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("Connecting WiFi...");
  WiFi.disconnect(true, true);
  delay(300);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 20;
  while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("WiFi status: ");
    Serial.println(WiFi.status());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());

    connectNtpIfPossible();
    bool ntpOk = waitForNtpSync(5000);
    Serial.print("NTP synced: ");
    Serial.println(ntpOk ? "YES" : "NO");

    return true;
  }

  Serial.println("WiFi FAILED");
  return false;
}

// ===================== SENSOR =====================
bool readSensor(Reading &r) {
  while (SensorSerial.available()) {
    SensorSerial.read();
  }

  SensorSerial.write(REQUEST_FRAME, sizeof(REQUEST_FRAME));
  delay(500);

  if (SensorSerial.available() < 19) {
    Serial.println("No response from sensor");
    return false;
  }

  uint8_t buf[19];
  size_t readLen = SensorSerial.readBytes(buf, 19);

  if (readLen != 19) {
    Serial.printf("Incomplete response: %u bytes\n", (unsigned)readLen);
    return false;
  }

  if (!(buf[0] == 0x01 && buf[1] == 0x03 && buf[2] == 0x0E)) {
    Serial.println("Invalid frame header");
    return false;
  }

  uint16_t moistureRaw = (buf[3] << 8) | buf[4];
  int16_t  tempRaw     = (int16_t)((buf[5] << 8) | buf[6]);
  uint16_t ecRaw       = (buf[7] << 8) | buf[8];
  uint16_t phRaw       = (buf[9] << 8) | buf[10];
  uint16_t nRaw        = (buf[11] << 8) | buf[12];
  uint16_t pRaw        = (buf[13] << 8) | buf[14];
  uint16_t kRaw        = (buf[15] << 8) | buf[16];

  r.moisture = moistureRaw / 10.0f;
  r.temperature = tempRaw / 10.0f;
  r.ec = ecRaw;
  r.ph = phRaw / 100.0f;
  r.n = nRaw;
  r.p = pRaw;
  r.k = kRaw;
  makeUtcTimestamp(r.reading_time, sizeof(r.reading_time));

  Serial.printf("Temp: %.1f°C, Moist: %.1f%%, pH: %.2f\n",
                r.temperature, r.moisture, r.ph);
  Serial.printf("Raw -> moisture:%u temp:%d ec:%u ph:%u n:%u p:%u k:%u\n",
                moistureRaw, tempRaw, ecRaw, phRaw, nRaw, pRaw, kRaw);

  if (!isReasonableReading(r)) {
    Serial.println("Rejected unreasonable reading");
    return false;
  }

  return true;
}

// ===================== BATCH =====================
bool addReadingToBatch(const Reading& r) {
  if (batchCount >= READINGS_PER_BATCH) {
    Serial.println("Batch is full");
    return false;
  }

  batch[batchCount] = r;
  batchCount++;

  Serial.printf("Stored %d/%d readings\n", batchCount, READINGS_PER_BATCH);
  saveBatchToPreferences();
  return true;
}

// ===================== JSON =====================
String buildBatchJson() {
  DynamicJsonDocument doc(1024);

  doc["device_id"] = DEVICE_ID;
  JsonArray readings = doc.createNestedArray("readings");

  float sumTemp = 0.0f;
  float sumMoist = 0.0f;
  float sumPh = 0.0f;
  uint32_t sumEc = 0;
  uint32_t sumN = 0;
  uint32_t sumP = 0;
  uint32_t sumK = 0;

  for (int i = 0; i < batchCount; i++) {
    sumTemp += batch[i].temperature;
    sumMoist += batch[i].moisture;
    sumPh += batch[i].ph;
    sumEc += batch[i].ec;
    sumN += batch[i].n;
    sumP += batch[i].p;
    sumK += batch[i].k;
  }

  JsonObject r = readings.createNestedObject();

  r["reading_time"] = batch[batchCount - 1].reading_time;
  r["temperature"] = sumTemp / batchCount;
  r["moisture"] = sumMoist / batchCount;
  r["conductivity"] = (uint16_t)(sumEc / batchCount);
  r["ph_value"] = sumPh / batchCount;
  r["npk_n"] = (uint16_t)(sumN / batchCount);
  r["npk_p"] = (uint16_t)(sumP / batchCount);
  r["npk_k"] = (uint16_t)(sumK / batchCount);

  String json;
  serializeJson(doc, json);
  return json;
}

// ===================== UPLOAD =====================
bool uploadBatch() {
  if (batchCount == 0) {
    Serial.println("No readings to upload");
    return false;
  }

  if (!ensureWiFi()) {
    Serial.println("Cannot upload: WiFi unavailable");
    return false;
  }

  String json = buildBatchJson();

  WiFiClientSecure client;
  client.setInsecure(); // testing only

  HTTPClient http;
  bool success = false;

  for (int attempt = 1; attempt <= 3; attempt++) {
    printDivider();
    Serial.printf("Upload attempt %d...\n", attempt);
    Serial.print("Free heap before HTTP: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("Sending averaged JSON:");
    Serial.println(json);

    if (!http.begin(client, SERVER_URL)) {
      Serial.println("http.begin() failed");
      delay(3000);
      continue;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Api-Key", API_KEY);

    int code = http.POST((uint8_t*)json.c_str(), json.length());

    Serial.print("HTTP Code: ");
    Serial.println(code);

    if (code <= 0) {
      Serial.print("HTTP error: ");
      Serial.println(http.errorToString(code));
    } else {
      String response = http.getString();
      Serial.println("Response body:");
      Serial.println(response);
    }

    http.end();

    if (code >= 200 && code < 300) {
      Serial.println("Upload SUCCESS");
      success = true;
      break;
    }

    if (code >= 400 && code < 500) {
      Serial.println("Client-side/validation error. Stopping retries.");
      break;
    }

    Serial.println("Upload failed, retrying...");
    delay(3000);
  }

  if (!success) {
    Serial.println("Upload FAILED");
  }

  return success;
}

// ===================== SLEEP =====================
void goToDeepSleep() {
  Serial.println();
  Serial.printf("Going to deep sleep for %llu seconds...\n", SLEEP_SECONDS);
  Serial.flush();

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

// ===================== MAIN CYCLE =====================
void runCycle() {
  printDivider();
  Serial.println("Wake -> read -> store -> maybe upload -> sleep");

  // Recovery path:
  // if a full batch was already saved from before, upload it first
  if (batchCount >= READINGS_PER_BATCH) {
    Serial.println("Recovered full stored batch -> uploading...");
    if (uploadBatch()) {
      clearBatchAndPreferences();
    } else {
      Serial.println("Upload failed, keeping stored batch");
      goToDeepSleep();
      return;
    }
  }

  Reading r;
  if (!readSensor(r)) {
    Serial.println("Sensor read failed");
    goToDeepSleep();
    return;
  }

  if (!addReadingToBatch(r)) {
    Serial.println("Failed to add reading");
    goToDeepSleep();
    return;
  }

  // If this wake collected the 6th reading, upload immediately
  if (batchCount >= READINGS_PER_BATCH) {
    Serial.println("6th reading collected on this wake -> uploading average...");
    if (uploadBatch()) {
      clearBatchAndPreferences();
    } else {
      Serial.println("Upload failed, keeping stored batch");
    }
  } else {
    Serial.println("Batch not full yet -> sleeping");
  }

  goToDeepSleep();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);

  Serial.println("Booting...");
  loadBatchFromPreferences();

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Woke from timer deep sleep");
  } else {
    Serial.println("Normal power-on/reset boot");
  }

  runCycle();
}

// ===================== LOOP =====================
void loop() {
  // not used; device sleeps at end of each cycle
}
