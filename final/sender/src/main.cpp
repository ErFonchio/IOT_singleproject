#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <driver/dac.h>
#include <esp_sleep.h>
#include <math.h>

namespace {

constexpr uint32_t kSamplingFrequencyHz = 10000;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;

struct Component {
  double amplitude;
  double frequency;
  double phase;
  double phaseIncrement;
};

Component components[] = {
    {2.0, 30.0, 0.0, 0.0},
    {4.0, 60.0, 0.0, 0.0},
};

constexpr uint8_t kComponentCount = sizeof(components) / sizeof(components[0]);
constexpr double kOutputScale = 20.0;

hw_timer_t *signalTimer = nullptr;
volatile uint32_t sampleCount = 0;
volatile uint8_t lastDacValue = 128;
Adafruit_INA219 ina219;

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

void IRAM_ATTR onTimer() {
  double sample = 0.0;
  for (uint8_t index = 0; index < kComponentCount; ++index) {
    sample += components[index].amplitude * sin(components[index].phase);
    components[index].phase += components[index].phaseIncrement;
    if (components[index].phase >= 2.0 * M_PI) {
      components[index].phase -= 2.0 * M_PI;
    }
  }

  int dacValue = static_cast<int>(128 + kOutputScale * sample);
  if (dacValue < 0) {
    dacValue = 0;
  }
  if (dacValue > 255) {
    dacValue = 255;
  }

  lastDacValue = static_cast<uint8_t>(dacValue);
  dac_output_voltage(DAC_CHANNEL_1, lastDacValue);
  ++sampleCount;
}

void configureSignalGenerator() {
  for (uint8_t index = 0; index < kComponentCount; ++index) {
    components[index].phase = 0.0;
    components[index].phaseIncrement = (2.0 * M_PI * components[index].frequency) / kSamplingFrequencyHz;
  }

  dac_output_enable(DAC_CHANNEL_1);
  signalTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(signalTimer, &onTimer, true);
  timerAlarmWrite(signalTimer, 1000000 / kSamplingFrequencyHz, true);
}

void startSignalGenerator() {
  sampleCount = 0;
  lastDacValue = 128;
  timerWrite(signalTimer, 0);
  timerAlarmEnable(signalTimer);
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  configureIna219();
  configureSignalGenerator();
  startSignalGenerator();
}

void loop() {
  const float current_mA = ina219.getCurrent_mA();
  Serial.printf("%.3f\t%u\n", current_mA, static_cast<unsigned int>(lastDacValue));
  delay(50);
}
