#include <Arduino.h>

#include <Adafruit_INA219.h>
#include <Wire.h>

#include <driver/dac.h>

#include "../../shared/bonus_signal.h"

namespace {

constexpr uint32_t kSamplingFrequencyHz = 250;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr bonus::WaveformType kWaveformType = bonus::WaveformType::Square; // Sine / Square / Triangle
constexpr uint16_t kWaveformLength = 1024;
constexpr float kWaveformFrequencyHz = 5.0f;

hw_timer_t *signalTimer = nullptr;
volatile uint32_t sampleIndex = 0;
volatile uint8_t lastDacValue = 128;
Adafruit_INA219 ina219;
uint8_t waveform[kWaveformLength];

void configureIna219() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(400000);

  if (!ina219.begin()) {
    Serial.println("INA219 not found");
    while (true) {
      delay(10);
    }
  }

  Serial.println("INA219 ready");
}

void precomputeWaveform() {
  for (uint16_t index = 0; index < kWaveformLength; ++index) {
    const float sample = bonus::generateWaveformSample(kWaveformType,
                                                       index,
                                                       kSamplingFrequencyHz,
                                                       kWaveformFrequencyHz);

    int dacValue = static_cast<int>(128 + 10.0f * sample);
    if (dacValue < 0) {
      dacValue = 0;
    }
    if (dacValue > 255) {
      dacValue = 255;
    }

    waveform[index] = static_cast<uint8_t>(dacValue);
  }
}

void IRAM_ATTR onTimer() {
  const uint32_t waveformIndex = sampleIndex % kWaveformLength;
  lastDacValue = waveform[waveformIndex];
  dac_output_voltage(DAC_CHANNEL_1, lastDacValue);
  ++sampleIndex;
}

void configureSignalGenerator() {
  dac_output_enable(DAC_CHANNEL_1);
  signalTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(signalTimer, &onTimer, true);
  timerAlarmWrite(signalTimer, 1000000 / kSamplingFrequencyHz, true);
}

void startSignalGenerator() {
  sampleIndex = 0;
  lastDacValue = 128;
  timerWrite(signalTimer, 0);
  timerAlarmEnable(signalTimer);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.printf("bonus sender waveform=%s\n", bonus::waveformName(kWaveformType));
  configureIna219();
  precomputeWaveform();
  configureSignalGenerator();
  startSignalGenerator();
}

void loop() {
  const float currentMilliAmps = ina219.getCurrent_mA();
  Serial.print(currentMilliAmps);
  Serial.println();
  delay(50);
}