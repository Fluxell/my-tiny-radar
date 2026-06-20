#include "imu.h"
#include "config.h"
#include <Wire.h>

#define REG_WHO_AM_I  0x00
#define REG_CTRL2     0x03   // Accelerometer: full-scale + ODR
#define REG_CTRL7     0x08   // Enable sensors
#define REG_AZ_L      0x39   // Z-axis low byte (AZ_H follows at 0x3A)

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

bool checkSetupGesture() {
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);

    uint8_t whoami = readReg(REG_WHO_AM_I);
    if (whoami != 0x05) {
        Serial.printf("[imu] WHO_AM_I=0x%02X (expected 0x05) — skipping gesture check\n", whoami);
        Wire.end();
        return false;
    }

    writeReg(REG_CTRL2, 0x23);  // ±8 g, 59 Hz ODR
    writeReg(REG_CTRL7, 0x01);  // enable accelerometer only
    delay(50);                   // wait for first sample (~17 ms at 59 Hz)

    Wire.beginTransmission(IMU_ADDR);
    Wire.write(REG_AZ_L);
    Wire.endTransmission(false);
    Wire.requestFrom(IMU_ADDR, (uint8_t)2);

    int16_t az_raw = 0;
    if (Wire.available() >= 2) {
        az_raw = (int16_t)(Wire.read() | (Wire.read() << 8));
    }

    writeReg(REG_CTRL7, 0x00);  // disable accelerometer
    Wire.end();

    // ±8 g → 4096 LSB/g.  Face-up: Z ≈ +4096.  Face-down: Z ≈ -4096.
    // Trigger setup when Z < -0.7 g (raw < -2867) — allows ~45° tilt margin.
    float az_g = az_raw / 4096.0f;
    Serial.printf("[imu] az=%.3f g  raw=%d\n", az_g, az_raw);
    return az_g < -0.7f;
}
