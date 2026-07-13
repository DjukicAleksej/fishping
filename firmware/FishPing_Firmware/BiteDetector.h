#ifndef FISHPING_BITE_DETECTOR_H
#define FISHPING_BITE_DETECTOR_H

#include <Arduino.h>
#include <math.h>

class BiteDetector {
public:
  struct State {
    bool biteNow;
    uint32_t biteCount;
    uint8_t eventId;
    uint32_t lastBiteMs;
    uint32_t lastIntervalMs;
    float magnitudeG;
    float jerkG;
    float baselineG;
    bool armed;
    uint8_t sensitivity;
    uint32_t cooldownMs;
    uint32_t warmupMs;
    float jerkThresholdG;
    float impulseThresholdG;
  };

  BiteDetector() {
    reset(0);
  }

  void reset(uint32_t nowMs) {
    state_.biteNow = false;
    state_.biteCount = 0;
    state_.eventId = 0;
    state_.lastBiteMs = 0;
    state_.lastIntervalMs = 0;
    state_.magnitudeG = 0.0f;
    state_.jerkG = 0.0f;
    state_.baselineG = 1.0f;
    state_.armed = true;
    state_.sensitivity = sensitivity_;
    state_.cooldownMs = cooldownMs_;
    state_.warmupMs = warmupMs_;
    applySensitivity();
    startedAtMs_ = nowMs;
    initialized_ = false;
    lastX_ = 0.0f;
    lastY_ = 0.0f;
    lastZ_ = 0.0f;
  }

  void calibrate(uint32_t nowMs) {
    state_.biteNow = false;
    state_.jerkG = 0.0f;
    state_.baselineG = 1.0f;
    startedAtMs_ = nowMs;
    initialized_ = false;
  }

  void setArmed(bool armed) {
    state_.armed = armed;
  }

  bool isArmed() const {
    return state_.armed;
  }

  void setSensitivity(uint8_t sensitivity) {
    sensitivity_ = constrain(sensitivity, 1, 10);
    state_.sensitivity = sensitivity_;
    applySensitivity();
  }

  void setCooldownMs(uint32_t cooldownMs) {
    cooldownMs_ = constrain(cooldownMs, 500UL, 30000UL);
    state_.cooldownMs = cooldownMs_;
  }

  State sample(float xG, float yG, float zG, uint32_t nowMs) {
    const float magnitudeG = sqrtf(xG * xG + yG * yG + zG * zG);

    state_.biteNow = false;
    state_.magnitudeG = magnitudeG;

    if (!initialized_) {
      initialized_ = true;
      lastX_ = xG;
      lastY_ = yG;
      lastZ_ = zG;
      state_.baselineG = magnitudeG > 0.05f ? magnitudeG : 1.0f;
      return state_;
    }

    const float dx = xG - lastX_;
    const float dy = yG - lastY_;
    const float dz = zG - lastZ_;
    const float jerkG = sqrtf(dx * dx + dy * dy + dz * dz);
    const float previousBaselineG = state_.baselineG;
    const float impulseG = fabsf(magnitudeG - previousBaselineG);

    state_.jerkG = jerkG;
    state_.baselineG =
      previousBaselineG * (1.0f - smoothingAlpha_) + magnitudeG * smoothingAlpha_;

    const bool warm = nowMs - startedAtMs_ >= warmupMs_;
    const bool outsideCooldown =
      state_.lastBiteMs == 0 || nowMs - state_.lastBiteMs >= cooldownMs_;
    const bool looksLikeBite =
      jerkG >= state_.jerkThresholdG || impulseG >= state_.impulseThresholdG;

    if (state_.armed && warm && outsideCooldown && looksLikeBite) {
      const uint32_t previousBiteMs = state_.lastBiteMs;
      state_.biteNow = true;
      state_.biteCount++;
      state_.eventId = state_.eventId >= 255 ? 1 : state_.eventId + 1;
      state_.lastBiteMs = nowMs;
      state_.lastIntervalMs = previousBiteMs == 0 ? 0 : nowMs - previousBiteMs;
    }

    lastX_ = xG;
    lastY_ = yG;
    lastZ_ = zG;

    return state_;
  }

  const State& state() const {
    return state_;
  }

private:
  void applySensitivity() {
    const float normalized = (float)(sensitivity_ - 1) / 9.0f;
    state_.jerkThresholdG = 1.15f - normalized * 0.70f;
    state_.impulseThresholdG = 0.85f - normalized * 0.57f;
  }

  State state_;
  bool initialized_ = false;
  float lastX_ = 0.0f;
  float lastY_ = 0.0f;
  float lastZ_ = 0.0f;
  uint32_t startedAtMs_ = 0;
  uint8_t sensitivity_ = 6;
  uint32_t cooldownMs_ = 2500;
  uint32_t warmupMs_ = 800;
  float smoothingAlpha_ = 0.12f;
};

#endif
