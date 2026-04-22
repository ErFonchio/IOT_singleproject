#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <driver/dac.h>

namespace {

constexpr uint8_t kStartButtonPin = 0;
constexpr uint8_t kControlPin = 4;
constexpr uint32_t kSignalFrequencyHz = 10000;
constexpr uint32_t kPowerMonitorPeriodMs = 1;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;

struct ExperimentCase {
  const char *label;
  uint32_t targetHz;
  uint32_t durationUs;
};

constexpr ExperimentCase kCases[] = {
    {"10k", 10000, 102400},
    {"256", 256, 4000000},
};

Adafruit_INA219 ina219;

struct Component {
  double amplitude;
  double freq;
  double phase;
  double phaseInc;
};

Component components[] = {
    {2.0, 3.0, 0.0, 0.0},
    {4.0, 5.0, 0.0, 0.0},
};

constexpr uint8_t kComponentCount = sizeof(components) / sizeof(components[0]);
constexpr double kOutputScale = 20.0;

hw_timer_t *signalTimer = nullptr;
volatile uint32_t signalSampleCount = 0;
volatile uint8_t lastDacValue = 128;
volatile bool signalRunning = false;

struct EnergyWindow {
  double energy_mJ = 0.0;
  double current_mAs = 0.0;
  double lastPower_mW = 0.0;
  double lastCurrent_mA = 0.0;
  uint32_t lastSampleUs = 0;
  bool hasSample = false;
};

portMUX_TYPE energyMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool monitorRunning = true;
volatile bool windowOpen = false;
EnergyWindow energyWindow;
TaskHandle_t monitorTaskHandle = nullptr;

void configureIna219() {
  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Wire.setClock(400000);
  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    while (true) {
      delay(10);
    }
  }
  Serial.println("INA219 ready");
}

void resetEnergyWindow() {
  taskENTER_CRITICAL(&energyMux);
  energyWindow.energy_mJ = 0.0;
  energyWindow.current_mAs = 0.0;
  energyWindow.lastPower_mW = 0.0;
  energyWindow.lastCurrent_mA = 0.0;
  energyWindow.lastSampleUs = micros();
  energyWindow.hasSample = false;
  taskEXIT_CRITICAL(&energyMux);
}

void readInaSnapshot(double &power_mW, double &current_mA) {
  power_mW = ina219.getPower_mW();
  current_mA = ina219.getCurrent_mA();
}

void seedEnergyWindow() {
  double power_mW = 0.0;
  double current_mA = 0.0;
  readInaSnapshot(power_mW, current_mA);

  taskENTER_CRITICAL(&energyMux);
  energyWindow.lastSampleUs = micros();
  energyWindow.lastPower_mW = power_mW;
  energyWindow.lastCurrent_mA = current_mA;
  energyWindow.hasSample = true;
  taskEXIT_CRITICAL(&energyMux);
}

void powerMonitorTask(void *) {
  while (monitorRunning) {
    uint32_t nowUs = micros();
    double power_mW = 0.0;
    double current_mA = 0.0;
    readInaSnapshot(power_mW, current_mA);

    taskENTER_CRITICAL(&energyMux);
    if (windowOpen) {
      if (energyWindow.hasSample) {
        double deltaSeconds = (nowUs - energyWindow.lastSampleUs) / 1000000.0;
        energyWindow.energy_mJ += energyWindow.lastPower_mW * deltaSeconds;
        energyWindow.current_mAs += energyWindow.lastCurrent_mA * deltaSeconds;
      }
      energyWindow.lastSampleUs = nowUs;
      energyWindow.lastPower_mW = power_mW;
      energyWindow.lastCurrent_mA = current_mA;
      energyWindow.hasSample = true;
    } else {
      energyWindow.lastSampleUs = nowUs;
      energyWindow.lastPower_mW = power_mW;
      energyWindow.lastCurrent_mA = current_mA;
      energyWindow.hasSample = false;
    }
    taskEXIT_CRITICAL(&energyMux);

    vTaskDelay(pdMS_TO_TICKS(kPowerMonitorPeriodMs));
  }

  vTaskDelete(nullptr);
}

void startMonitorTask() {
  xTaskCreatePinnedToCore(powerMonitorTask, "powerMonitor", 4096, nullptr, 1, &monitorTaskHandle, 0);
}

void IRAM_ATTR onTimer() {
  double sample = 0.0;
  for (uint8_t index = 0; index < kComponentCount; ++index) {
    sample += components[index].amplitude * sin(components[index].phase);
    components[index].phase += components[index].phaseInc;
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
  ++signalSampleCount;
}

void configureSignalGenerator() {
  for (uint8_t index = 0; index < kComponentCount; ++index) {
    components[index].phase = 0.0;
    components[index].phaseInc = (2.0 * M_PI * components[index].freq) / kSignalFrequencyHz;
  }

  dac_output_enable(DAC_CHANNEL_1);
  signalTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(signalTimer, &onTimer, true);
  timerAlarmWrite(signalTimer, 1000000 / kSignalFrequencyHz, true);
}

void startSignalGenerator() {
  signalSampleCount = 0;
  lastDacValue = 128;
  signalRunning = true;
  timerWrite(signalTimer, 0);
  timerAlarmEnable(signalTimer);
}

void waitForReceiverFftStart() {
  while (digitalRead(kControlPin) == HIGH) {
    delayMicroseconds(50);
  }
}

void pulseStart() {
  digitalWrite(kControlPin, LOW);
  delay(20);
  digitalWrite(kControlPin, HIGH);
}

void runCase(const ExperimentCase &experiment) {
  uint32_t startSignalSamples = signalSampleCount;

  resetEnergyWindow();
  seedEnergyWindow();
  pulseStart();

  waitForReceiverFftStart();
  uint32_t startUs = micros();
  windowOpen = true;

  while (digitalRead(kControlPin) == LOW) {
    yield();
  }

  uint32_t elapsedUs = micros() - startUs;
  windowOpen = false;
  delay(kPowerMonitorPeriodMs + 5);

  double energy_mJ = 0.0;
  double current_mAs = 0.0;
  taskENTER_CRITICAL(&energyMux);
  if (energyWindow.hasSample) {
    uint32_t stopUs = micros();
    double tailSeconds = (stopUs - energyWindow.lastSampleUs) / 1000000.0;
    if (tailSeconds > 0.0) {
      energyWindow.energy_mJ += energyWindow.lastPower_mW * tailSeconds;
      energyWindow.current_mAs += energyWindow.lastCurrent_mA * tailSeconds;
    }
  }
  energy_mJ = energyWindow.energy_mJ;
  current_mAs = energyWindow.current_mAs;
  taskEXIT_CRITICAL(&energyMux);

  uint32_t generatedSamples = signalSampleCount - startSignalSamples;
  double generatedHz = elapsedUs > 0 ? (generatedSamples * 1000000.0 / elapsedUs) : 0.0;
  double averagePower_mW = elapsedUs > 0 ? (energy_mJ * 1000000.0 / elapsedUs) : 0.0;
  double averageCurrent_mA = elapsedUs > 0 ? (current_mAs * 1000000.0 / elapsedUs) : 0.0;
  uint32_t expectedSamples = (experiment.durationUs * experiment.targetHz) / 1000000UL;

  Serial.printf(
      "fft_average_sender,label=%s,target_hz=%lu,duration_us=%lu,expected_samples=%lu,elapsed_us=%lu,elapsed_ms=%.4f,generated_samples=%lu,generated_hz=%.4f,avg_current_mA=%.6f,avg_power_mW=%.6f,energy_mJ=%.6f,last_dac=%u\n",
      experiment.label,
      static_cast<unsigned long>(experiment.targetHz),
      static_cast<unsigned long>(experiment.durationUs),
      static_cast<unsigned long>(expectedSamples),
      static_cast<unsigned long>(elapsedUs),
      elapsedUs / 1000.0f,
      static_cast<unsigned long>(generatedSamples),
      generatedHz,
      averageCurrent_mA,
      averagePower_mW,
      energy_mJ,
      static_cast<unsigned long>(lastDacValue));
}

void runAllExperiments() {
  for (const ExperimentCase &experiment : kCases) {
    runCase(experiment);
    delay(250);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  pinMode(kStartButtonPin, INPUT_PULLUP);
  pinMode(kControlPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kControlPin, HIGH);

  configureIna219();
  configureSignalGenerator();
  startSignalGenerator();
  startMonitorTask();

  Serial.println("FFT average sender starting");
  Serial.println("Press BOOT to start the FFT/average run");
}

void loop() {
  static int lastButtonState = HIGH;
  int buttonState = digitalRead(kStartButtonPin);

  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(20);
    if (digitalRead(kStartButtonPin) == LOW) {
      runAllExperiments();
      Serial.println("FFT average sender completed");
    }
  }

  lastButtonState = buttonState;
  delay(5);
}
