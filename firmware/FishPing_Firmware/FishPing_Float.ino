#include <ArduinoBLE.h>
#include <Wire.h>

#include "BiteDetector.h"
#include "Lis3dhMinimal.h"

constexpr uint8_t LIS3DH_INT_PIN = D0;
constexpr uint8_t STATUS_LED_PIN = D9;
constexpr bool STATUS_LED_ACTIVE_HIGH = true;
constexpr uint32_t SAMPLE_PERIOD_MS = 20;
constexpr uint32_t STATS_PERIOD_MS = 1000;
constexpr uint32_t BITE_LED_MS = 240;

const char* DEVICE_NAME = "FishPing Float";
const char* SERVICE_UUID = "f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1000";
const char* STATS_UUID = "f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1001";
const char* EVENT_UUID = "f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1002";
const char* COMMAND_UUID = "f15bb8d0-7a6f-4f3c-9a91-0fd76a6f1003";

BLEService fishService(SERVICE_UUID);
BLEStringCharacteristic statsCharacteristic(STATS_UUID, BLERead | BLENotify, 180);
BLEByteCharacteristic eventCharacteristic(EVENT_UUID, BLERead | BLENotify);
BLEStringCharacteristic commandCharacteristic(COMMAND_UUID, BLEWrite, 64);

Lis3dhMinimal accelerometer;
BiteDetector detector;

bool sensorReady = false;
uint32_t lastSampleMs = 0;
uint32_t lastStatsMs = 0;
uint32_t ledOffAtMs = 0;

void setStatusLed(bool on) {
  digitalWrite(STATUS_LED_PIN, on == STATUS_LED_ACTIVE_HIGH ? HIGH : LOW);
}

void blinkError() {
  for (uint8_t i = 0; i < 3; i++) {
    setStatusLed(true);
    delay(80);
    setStatusLed(false);
    delay(120);
  }
}

int readBatteryMv() {
#if defined(PIN_VBAT) && defined(VBAT_ENABLE)
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  delayMicroseconds(200);
  analogReadResolution(12);
  const int raw = analogRead(PIN_VBAT);
  digitalWrite(VBAT_ENABLE, HIGH);
  return (int)((raw * 3300.0f / 4095.0f) * 3.0f);
#elif defined(PIN_VBAT)
  analogReadResolution(12);
  const int raw = analogRead(PIN_VBAT);
  return (int)((raw * 3300.0f / 4095.0f) * 3.0f);
#else
  return -1;
#endif
}

String buildStatsJson() {
  const BiteDetector::State& state = detector.state();
  const uint32_t nowMs = millis();
  const uint32_t sinceLastBiteMs = state.lastBiteMs == 0 ? 0 : nowMs - state.lastBiteMs;

  String json = "{";
  json += "\"c\":";
  json += state.biteCount;
  json += ",\"e\":";
  json += (int)state.eventId;
  json += ",\"last\":";
  json += state.lastBiteMs;
  json += ",\"since\":";
  json += sinceLastBiteMs;
  json += ",\"interval\":";
  json += state.lastIntervalMs;
  json += ",\"armed\":";
  json += state.armed ? "1" : "0";
  json += ",\"sens\":";
  json += (int)state.sensitivity;
  json += ",\"cool\":";
  json += state.cooldownMs;
  json += ",\"v\":";
  json += readBatteryMv();
  json += ",\"m\":";
  json += String(state.magnitudeG, 2);
  json += ",\"j\":";
  json += String(state.jerkG, 2);
  json += ",\"addr\":";
  json += sensorReady ? (int)accelerometer.address() : 0;
  json += ",\"err\":";
  json += sensorReady ? "0" : "1";
  json += "}";

  return json;
}

void publishStats() {
  const String json = buildStatsJson();
  statsCharacteristic.writeValue(json.c_str());
}

void applyCommand(String command) {
  command.trim();
  command.toUpperCase();

  if (command == "RESET") {
    detector.reset(millis());
    publishStats();
    return;
  }

  if (command == "ARM") {
    detector.setArmed(true);
    publishStats();
    return;
  }

  if (command == "DISARM") {
    detector.setArmed(false);
    publishStats();
    return;
  }

  if (command == "CAL" || command == "CALIBRATE") {
    detector.calibrate(millis());
    publishStats();
    return;
  }

  if (command.startsWith("SENS=")) {
    detector.setSensitivity((uint8_t)command.substring(5).toInt());
    publishStats();
    return;
  }

  if (command.startsWith("COOLDOWN=")) {
    detector.setCooldownMs((uint32_t)command.substring(9).toInt());
    publishStats();
    return;
  }
}

void setupBle() {
  if (!BLE.begin()) {
    while (true) {
      blinkError();
      delay(500);
    }
  }

  BLE.setLocalName(DEVICE_NAME);
  BLE.setDeviceName(DEVICE_NAME);
  BLE.setAdvertisedService(fishService);

  fishService.addCharacteristic(statsCharacteristic);
  fishService.addCharacteristic(eventCharacteristic);
  fishService.addCharacteristic(commandCharacteristic);
  BLE.addService(fishService);

  eventCharacteristic.writeValue((byte)0);
  publishStats();
  BLE.advertise();
}

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(LIS3DH_INT_PIN, INPUT);
  setStatusLed(false);

  Serial.begin(115200);
  Wire.begin();

  sensorReady = accelerometer.begin(Wire);
  if (!sensorReady) {
    blinkError();
  }

  detector.setSensitivity(6);
  detector.setCooldownMs(2500);
  detector.reset(millis());

  setupBle();
  lastSampleMs = millis();
  lastStatsMs = millis();
}

void loop() {
  BLE.poll();

  if (commandCharacteristic.written()) {
    applyCommand(commandCharacteristic.value());
  }

  const uint32_t nowMs = millis();

  if (sensorReady && nowMs - lastSampleMs >= SAMPLE_PERIOD_MS) {
    lastSampleMs = nowMs;

    Lis3dhSample sample;
    if (accelerometer.read(sample)) {
      const BiteDetector::State state =
        detector.sample(sample.xG, sample.yG, sample.zG, nowMs);

      if (state.biteNow) {
        eventCharacteristic.writeValue((byte)state.eventId);
        setStatusLed(true);
        ledOffAtMs = nowMs + BITE_LED_MS;
        publishStats();
        lastStatsMs = nowMs;
      }
    }
  }

  if (ledOffAtMs != 0 && nowMs >= ledOffAtMs) {
    setStatusLed(false);
    ledOffAtMs = 0;
  }

  if (nowMs - lastStatsMs >= STATS_PERIOD_MS) {
    publishStats();
    lastStatsMs = nowMs;
  }
}
