#include "csv_logger.h"

#include <ArduinoJson.h>
#include <time.h>

#if USE_SD_CARD
#include <SPI.h>
#include <SD.h>
#endif

static const long GMT_OFFSET_SEC      = 8 * 3600;
static const int  DAYLIGHT_OFFSET_SEC = 0;
static const char* const NTP_SERVER_1 = "pool.ntp.org";
static const char* const NTP_SERVER_2 = "time.nist.gov";

static const char* const CSV_HEADER =
    "datetime,temperature_c,humidity_pct,lux,water_raw,fill_light";

static PubSubClient* g_mqttClient = nullptr;
static bool          g_timeConfigured = false;
static bool          g_timeReady        = false;

#if USE_SD_CARD
static bool g_sdReady = false;
static bool g_sdMountInProgress = false;
static bool g_sdBootMountDone = false;
static unsigned long g_bootMs = 0;
static unsigned long g_lastSdRetryMs = 0;
static const uint32_t SD_RETRY_INTERVAL_MS = 30000;
static SPIClass g_sdSpi(FSPI);
static File g_sdLogFile;

static uint8_t g_sdRowsSinceFlush = 0;
static const uint8_t SD_FLUSH_EVERY_N_ROWS = 6;

static void closeSdLogFile() {
  if (g_sdLogFile) {
    g_sdLogFile.flush();
    g_sdLogFile.close();
    g_sdRowsSinceFlush = 0;
  }
}

static void invalidateSdMount() {
  if (g_sdReady) {
    Serial.println("CSV logger: SD mount invalidated");
  }
  closeSdLogFile();
  g_sdReady = false;
  SD.end();
  g_sdSpi.end();
  g_lastSdRetryMs = millis();
  delay(100);
}

static bool sdMountAlive() {
  return g_sdReady && static_cast<bool>(g_sdLogFile);
}

static bool sdEnsureLogFile() {
  if (SD.exists(CSV_LOG_FILENAME)) {
    return true;
  }

  File file = SD.open(CSV_LOG_FILENAME, "w");
  if (!file) {
    return false;
  }
  file.println(CSV_HEADER);
  file.close();
  Serial.println("CSV logger: created SD log file with header");
  return true;
}

static bool openSdLogFileForAppend() {
  closeSdLogFile();
  if (!sdEnsureLogFile()) {
    return false;
  }
  g_sdLogFile = SD.open(CSV_LOG_FILENAME, "a");
  return static_cast<bool>(g_sdLogFile);
}

static const char* sdCardTypeName(uint8_t cardType) {
  switch (cardType) {
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SD";
    case CARD_SDHC: return "SDHC";
    default:        return "UNKNOWN";
  }
}

static void logSdWiringHint() {
  Serial.println("CSV logger: SD wiring check:");
  Serial.println("  GND -> GND");
  Serial.println("  VCC -> 3.3V (not 5V) — add 100 uF + 0.1 uF cap near module if writes fail");
  Serial.println("  MISO -> GPIO 13");
  Serial.println("  MOSI -> GPIO 11");
  Serial.println("  SCK  -> GPIO 12");
  Serial.println("  CS   -> GPIO 10");
  Serial.println("  Match pins by label, not header position.");
  Serial.println("  Format the card as FAT32.");
}

static bool mountSdCard(bool verbose) {
  if (g_sdMountInProgress) {
    return false;
  }
  g_sdMountInProgress = true;

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(10);

  pinMode(SD_MISO_PIN, INPUT_PULLUP);

  closeSdLogFile();
  g_sdReady = false;
  SD.end();
  g_sdSpi.end();
  delay(100);

  g_sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  delay(500);

  if (verbose) {
    Serial.printf("CSV logger: SD MISO idle=%s\n",
                  digitalRead(SD_MISO_PIN) == HIGH ? "HIGH" : "LOW");
  }

  bool mounted = false;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    SD.end();
    delay(50);

    if (verbose) {
      Serial.printf("CSV logger: SD init attempt %u @ %u Hz\n",
                    attempt + 1, SD_SPI_HZ);
    }

    if (!SD.begin(SD_CS_PIN, g_sdSpi, SD_SPI_HZ)) {
      delay(150);
      continue;
    }

    const uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      SD.end();
      delay(150);
      continue;
    }

    if (!openSdLogFileForAppend()) {
      SD.end();
      delay(150);
      continue;
    }

    g_sdReady = true;
    mounted = true;

    if (verbose) {
      Serial.printf("CSV logger: SD card ready (%s, %llu MB, CS=%u MOSI=%u MISO=%u SCK=%u)\n",
                    sdCardTypeName(cardType),
                    static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)),
                    SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
    }
    break;
  }

  if (!mounted && verbose) {
    Serial.printf("CSV logger: SD card init failed (CS=%u MOSI=%u MISO=%u SCK=%u)\n",
                  SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
    logSdWiringHint();
    g_lastSdRetryMs = millis();
  }

  g_sdMountInProgress = false;
  return mounted;
}

static void maintainSdReady() {
  if (sdMountAlive()) {
    return;
  }

  if (g_sdReady) {
    invalidateSdMount();
  }

  const unsigned long now = millis();
  if (now - g_lastSdRetryMs < SD_RETRY_INTERVAL_MS) {
    return;
  }
  g_lastSdRetryMs = now;

  Serial.println("CSV logger: retrying SD card init...");
  mountSdCard(true);
}
#endif

#if USE_SD_CARD
static bool appendRowToSd(const String& csvRow) {
  if (!sdMountAlive()) {
    return false;
  }

  if (g_sdLogFile.println(csvRow) == 0) {
    Serial.println("CSV logger: SD println failed");
    invalidateSdMount();
    return false;
  }

  ++g_sdRowsSinceFlush;
  if (g_sdRowsSinceFlush >= SD_FLUSH_EVERY_N_ROWS) {
    g_sdLogFile.flush();
    g_sdRowsSinceFlush = 0;
  }

  return true;
}
#endif

static String readLineFromFile(File& file) {
  String line = file.readStringUntil('\n');
  line.trim();
  return line;
}

static void publishDownloadMessage(const char* status,
                                   const char* data,
                                   uint32_t lineNumber) {
  if (g_mqttClient == nullptr || !g_mqttClient->connected()) {
    return;
  }

  DynamicJsonDocument doc(768);
  doc["status"] = status;
  if (data != nullptr) {
    doc["data"] = data;
  }
  if (lineNumber > 0) {
    doc["line"] = lineNumber;
  }

  String payload;
  serializeJson(doc, payload);
  g_mqttClient->publish(MQTT_TOPIC_LOG_DOWNLOAD, payload.c_str());
}

static void publishDownloadFromFile(File& file, const char* sourceLabel) {
  uint32_t lineNumber = 0;
  publishDownloadMessage("start", CSV_LOG_FILENAME, 0);
  Serial.printf("CSV logger: MQTT download started from %s\n", sourceLabel);

  while (file.available()) {
    String line = readLineFromFile(file);
    if (line.length() == 0) {
      continue;
    }

    ++lineNumber;
    publishDownloadMessage("line", line.c_str(), lineNumber);
    yield();
  }

  DynamicJsonDocument doc(128);
  doc["status"] = "end";
  doc["lines"]  = lineNumber;

  String payload;
  serializeJson(doc, payload);
  g_mqttClient->publish(MQTT_TOPIC_LOG_DOWNLOAD, payload.c_str());

  Serial.printf("CSV logger: MQTT download finished (%u lines)\n", lineNumber);
}

void csvLoggerInit(PubSubClient* mqttClient) {
  g_mqttClient = mqttClient;
  g_bootMs = millis();
}

void csvLoggerMaintainStorage() {
#if USE_SD_CARD
  if (!g_sdBootMountDone && millis() - g_bootMs >= 3000) {
    g_sdBootMountDone = true;
    g_lastSdRetryMs = millis();
    mountSdCard(true);
  }
  maintainSdReady();
#endif
}

void csvLoggerMaintainTime(bool wifiConnected) {
  if (!wifiConnected || g_timeReady) {
    return;
  }

  if (!g_timeConfigured) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
    g_timeConfigured = true;
    Serial.println("CSV logger: waiting for NTP time sync...");
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1000)) {
    g_timeReady = true;
    Serial.println("CSV logger: NTP time synced");
  }
}

bool csvLoggerTimeReady() {
  return g_timeReady;
}

String csvLoggerFormatTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    return "NOT_SYNCED";
  }

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

String csvLoggerFillLightLabel(bool fillLightOn) {
  return fillLightOn ? "ON" : "OFF";
}

static String buildCsvRow(float temperatureC,
                          float humidityPct,
                          uint16_t lux,
                          uint16_t waterRaw,
                          bool fillLightOn) {
  String row;
  row.reserve(96);
  row += csvLoggerFormatTimestamp();
  row += ',';
  row += String(temperatureC, 2);
  row += ',';
  row += String(humidityPct, 2);
  row += ',';
  row += String(lux);
  row += ',';
  row += String(waterRaw);
  row += ',';
  row += csvLoggerFillLightLabel(fillLightOn);
  return row;
}

static bool appendCsvRow(const String& csvRow) {
#if USE_SD_CARD
  if (appendRowToSd(csvRow)) {
    return true;
  }
  Serial.println("CSV logger: SD write failed (row not saved)");
  return false;
#else
  Serial.println("CSV logger: SD card disabled, row not saved");
  return false;
#endif
}

static void publishCsvRow(const String& csvRow) {
  if (g_mqttClient == nullptr || !g_mqttClient->connected()) {
    Serial.println("CSV logger: MQTT row publish skipped (not connected)");
    return;
  }

  const bool ok = g_mqttClient->publish(MQTT_TOPIC_LOG_CSV, csvRow.c_str());
  Serial.printf("CSV logger: MQTT row publish %s\n", ok ? "OK" : "FAILED");
}

bool csvLoggerRecordReading(float temperatureC,
                            float humidityPct,
                            uint16_t lux,
                            uint16_t waterRaw,
                            bool fillLightOn) {
  const String row = buildCsvRow(temperatureC, humidityPct, lux, waterRaw, fillLightOn);

  const bool ok = appendCsvRow(row);
  publishCsvRow(row);

  if (ok) {
    Serial.print("CSV logger: recorded row -> ");
    Serial.println(row);
  }

  return ok;
}

static void publishFullLogDownload() {
  if (g_mqttClient == nullptr || !g_mqttClient->connected()) {
    Serial.println("CSV logger: download skipped (MQTT not connected)");
    return;
  }

#if USE_SD_CARD
  if (sdMountAlive()) {
    closeSdLogFile();
    File sdFile = SD.open(CSV_LOG_FILENAME, "r");
    if (sdFile) {
      publishDownloadFromFile(sdFile, "SD card");
      sdFile.close();
      openSdLogFileForAppend();
      return;
    }
    Serial.println("CSV logger: SD read failed");
    invalidateSdMount();
  } else if (mountSdCard(false)) {
    File sdFile = SD.open(CSV_LOG_FILENAME, "r");
    if (sdFile) {
      publishDownloadFromFile(sdFile, "SD card");
      sdFile.close();
      openSdLogFileForAppend();
      return;
    }
    invalidateSdMount();
  }
#endif

  publishDownloadMessage("error", "SD card unavailable", 0);
}

void csvLoggerHandleCommand(const String& command) {
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, command);

  if (error) {
    if (command.equalsIgnoreCase("download_log")) {
      publishFullLogDownload();
    }
    return;
  }

  const char* cmd = doc["cmd"] | "";
  if (String(cmd).equalsIgnoreCase("download_log")) {
    publishFullLogDownload();
  }
}
