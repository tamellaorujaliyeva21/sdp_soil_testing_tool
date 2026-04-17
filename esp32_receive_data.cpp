#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// ===================== HOTSPOT CONFIG =====================
const char* WIFI_SSID = "Malik 1";
const char* WIFI_PASSWORD = "19681113";

// ===================== SERVER CONFIG =====================
const char* SERVER_URL = "https://soil-repo-gcp-git-678290165816.europe-west1.run.app/readings/batch";
const char* API_KEY = "smksmKDMkcsmaskamAK12021SKMS1";

// ===================== DEVICE =====================
const char* DEVICE_ID = "8f3c1a7d2b6e4c90a5d1f8b73e2c6a41";

// ===================== SERIAL =====================
HardwareSerial SensorSerial(2);
uint8_t REQUEST_FRAME[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};

// ===================== TIMING =====================
const unsigned long READ_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes
const int READINGS_PER_BATCH = 12;                          // 12 * 5 min = 60 min
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000UL;

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
  char reading_time[25]; // YYYY-MM-DDTHH:MM:SSZ
};

// ===================== BATCH =====================
Reading batch[READINGS_PER_BATCH];
int batchCount = 0;

// ===================== SCHEDULING =====================
unsigned long lastReadAt = 0;
unsigned long lastWiFiRetryAt = 0;

// ===================== HELPERS =====================
void printDivider() {
  Serial.println("--------------------------------------------------");
}

void printReading(const Reading& r, int index) {
  Serial.printf(
    "[%d] %s | Temp: %.2f C | Moist: %.2f %% | pH: %.2f | EC: %u | N:%u P:%u K:%u\n",
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

void makeUtcTimestamp(char* out, size_t len) {
  time_t now = time(nullptr);

  // sanity check so we don't use epoch/garbage before sync
  if (now > 1700000000) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return;
  }

  // fallback if real time is not available yet
  unsigned long totalSeconds = millis() / 1000UL;
  unsigned long seconds = totalSeconds % 60UL;
  unsigned long minutes = (totalSeconds / 60UL) % 60UL;
  unsigned long hours   = (totalSeconds / 3600UL) % 24UL;
  snprintf(out, len, "2026-04-17T%02lu:%02lu:%02luZ", hours, minutes, seconds);
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
bool ensureWiFi(bool verbose = true) {
  if (WiFi.status() == WL_CONNECTED) return true;

  unsigned long now = millis();

  // allow immediate first attempt after boot
  if (lastWiFiRetryAt != 0 && now - lastWiFiRetryAt < WIFI_RETRY_INTERVAL_MS) {
    return false;
  }

  lastWiFiRetryAt = now;

  if (verbose) {
    Serial.println("Reconnecting WiFi...");
  }

  WiFi.disconnect(true, true);
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 15;
  while (WiFi.status() != WL_CONNECTED && retries-- > 0) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi reconnected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    connectNtpIfPossible();
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
  DynamicJsonDocument doc(4096);

  doc["device_id"] = DEVICE_ID;
  JsonArray readings = doc.createNestedArray("readings");

  for (int i = 0; i < batchCount; i++) {
    JsonObject r = readings.createNestedObject();
    r["reading_time"] = batch[i].reading_time;
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

  HTTPClient http;
  bool success = false;

  for (int attempt = 1; attempt <= 3; attempt++) {
    printDivider();
    Serial.printf("Upload attempt %d...\n", attempt);
    Serial.println("Sending JSON:");
    Serial.println(json);

    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Api-Key", API_KEY);

    int code = http.POST((uint8_t*)json.c_str(), json.length());

    Serial.print("HTTP Code: ");
    Serial.println(code);

    String response = http.getString();
    Serial.println("Response body:");
    Serial.println(response);

    http.end();

    if (code >= 200 && code < 300) {
      Serial.println("Upload SUCCESS");
      success = true;
      break;
    }

    if (code >= 400 && code < 500) {
      Serial.println("Client-side/validation error. Stopping retries for this cycle.");
      break;
    }

    Serial.println("Upload failed, retrying...");
    delay(3000);
    ensureWiFi(false);
  }

  if (!success) {
    Serial.println("Upload FAILED");
  }

  return success;
}

// ===================== WORK =====================
void performScheduledRead() {
  printDivider();
  Serial.println("Scheduled interval reached");

  ensureWiFi();

  Reading r;
  if (!readSensor(r)) {
    Serial.println("Sensor read failed");
    return;
  }

  if (!addReadingToBatch(r)) {
    Serial.println("Failed to add reading to batch");
    return;
  }

  if (batchCount >= READINGS_PER_BATCH) {
    Serial.println("Batch full -> uploading...");
    if (uploadBatch()) {
      clearBatchAndPreferences();
    } else {
      Serial.println("Keeping stored batch for future retry");
    }
  }
}

void tryUploadPendingBatch() {
  if (batchCount < READINGS_PER_BATCH) return;
  if (WiFi.status() != WL_CONNECTED) return;

  printDivider();
  Serial.println("Pending full batch found, trying upload...");

  if (uploadBatch()) {
    clearBatchAndPreferences();
  } else {
    Serial.println("Pending batch upload still failing");
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  SensorSerial.begin(9600, SERIAL_8N1, 16, 17);

  Serial.println("Booting...");
  loadBatchFromPreferences();

  ensureWiFi();
  lastReadAt = millis() - READ_INTERVAL_MS; // first read immediately

  // Uncomment ONCE only if you need to erase old bad saved data:
  // clearBatchAndPreferences();
}

// ===================== LOOP =====================
void loop() {
  unsigned long now = millis();

  ensureWiFi(false);
  tryUploadPendingBatch();

  if (now - lastReadAt >= READ_INTERVAL_MS) {
    lastReadAt = now;
    performScheduledRead();
  }

  delay(200);
}
