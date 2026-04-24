#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <driver/dac.h>
#include <esp_sleep.h>
#include <math.h>

namespace {

// The sender produces the signal used by the final receiver experiment.
constexpr uint32_t kSamplingFrequencyHz = 10000;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;

// Each component contributes one sine term to the generated waveform.
struct Component {
  double amplitude;
  double frequency;
  double phase;
  double phaseIncrement;
};

// Two-tone signal used to create a stable, non-trivial input for the receiver.
Component components[] = {
    {2.0, 30.0, 0.0, 0.0},
    {4.0, 60.0, 0.0, 0.0},
};

// The signal is generated at 10 kHz and scaled to the DAC output range.
constexpr uint8_t kComponentCount = sizeof(components) / sizeof(components[0]);
constexpr double kOutputScale = 20.0;

// Hardware timer and INA219 used to drive the waveform and measure current.
hw_timer_t *signalTimer = nullptr;
volatile uint32_t sampleCount = 0;
volatile uint8_t lastDacValue = 128;
Adafruit_INA219 ina219;

void configureIna219() {
  // The INA219 is wired over I2C and used to observe the sender current draw.
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
  // Build the next DAC sample by summing the two waveform components.
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
  // Convert the component frequencies into phase increments for the timer ISR.
  for (uint8_t index = 0; index < kComponentCount; ++index) {
    components[index].phase = 0.0;
    components[index].phaseIncrement = (2.0 * M_PI * components[index].frequency) / kSamplingFrequencyHz;
  }

  // Enable the DAC output and attach the ISR that feeds it.
  dac_output_enable(DAC_CHANNEL_1);
  signalTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(signalTimer, &onTimer, true);
  timerAlarmWrite(signalTimer, 1000000 / kSamplingFrequencyHz, true);
}

void startSignalGenerator() {
  // Reset the generator state so the waveform starts from a clean phase.
  sampleCount = 0;
  lastDacValue = 128;
  timerWrite(signalTimer, 0);
  timerAlarmEnable(signalTimer);
}

} // namespace

void setup() {
  // Serial is only used to report the setup status and the measured current.
  Serial.begin(115200);
  delay(100);

  // Bring up the current meter, configure the waveform and start generation.
  configureIna219();
  configureSignalGenerator();
  startSignalGenerator();
}

void loop() {
  // The loop reads the current consumption while the signal keeps running.
  const float current_mA = ina219.getCurrent_mA();
  Serial.printf("%.3f\t%u\n", current_mA, static_cast<unsigned int>(lastDacValue));
  delay(50);
}
