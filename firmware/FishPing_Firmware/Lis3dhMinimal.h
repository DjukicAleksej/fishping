#ifndef FISHPING_LIS3DH_MINIMAL_H
#define FISHPING_LIS3DH_MINIMAL_H

#include <Arduino.h>
#include <Wire.h>

struct Lis3dhSample {
  float xG;
  float yG;
  float zG;
};

class Lis3dhMinimal {
public:
  bool begin(TwoWire& wire = Wire) {
    wire_ = &wire;

    if (!probe(0x18) && !probe(0x19)) {
      return false;
    }

    writeRegister(0x20, 0x47); // 50 Hz, X/Y/Z enabled.
    writeRegister(0x21, 0x00); // High-pass filter off.
    writeRegister(0x22, 0x00); // Hardware interrupts off; firmware detects pulls.
    writeRegister(0x23, 0x98); // BDU, +/-4g, high-resolution mode.
    writeRegister(0x24, 0x00);
    delay(10);

    return true;
  }

  uint8_t address() const {
    return address_;
  }

  bool read(Lis3dhSample& sample) {
    uint8_t buffer[6] = {0};

    if (!readBytes(0x28 | 0x80, buffer, sizeof(buffer))) {
      return false;
    }

    const int16_t rawX = toRaw12(buffer[0], buffer[1]);
    const int16_t rawY = toRaw12(buffer[2], buffer[3]);
    const int16_t rawZ = toRaw12(buffer[4], buffer[5]);

    sample.xG = rawX * 0.002f;
    sample.yG = rawY * 0.002f;
    sample.zG = rawZ * 0.002f;

    return true;
  }

private:
  static int16_t toRaw12(uint8_t low, uint8_t high) {
    int16_t value = (int16_t)(((uint16_t)high << 8) | low);
    value >>= 4;
    return value;
  }

  bool probe(uint8_t address) {
    address_ = address;
    const uint8_t whoAmI = readRegister(0x0F);
    return whoAmI == 0x33;
  }

  uint8_t readRegister(uint8_t reg) {
    if (!wire_) {
      return 0;
    }

    wire_->beginTransmission(address_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) {
      return 0;
    }

    if (wire_->requestFrom((int)address_, 1) != 1) {
      return 0;
    }

    return wire_->read();
  }

  bool readBytes(uint8_t startRegister, uint8_t* buffer, size_t length) {
    if (!wire_) {
      return false;
    }

    wire_->beginTransmission(address_);
    wire_->write(startRegister);
    if (wire_->endTransmission(false) != 0) {
      return false;
    }

    const uint8_t count = wire_->requestFrom((int)address_, (int)length);
    if (count != length) {
      return false;
    }

    for (size_t i = 0; i < length; i++) {
      buffer[i] = wire_->read();
    }

    return true;
  }

  void writeRegister(uint8_t reg, uint8_t value) {
    if (!wire_) {
      return;
    }

    wire_->beginTransmission(address_);
    wire_->write(reg);
    wire_->write(value);
    wire_->endTransmission();
  }

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0x18;
};

#endif
