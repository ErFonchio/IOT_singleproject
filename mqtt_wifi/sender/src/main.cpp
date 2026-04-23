#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <driver/dac.h>

namespace {

constexpr uint8_t kStartButtonPin = 0;
constexpr uint8_t kStartPulsePin = 4;
constexpr uint32_t kPowerMonitorPeriodMs = 6;
constexpr uint32_t kEnergyWarmupMs = 200;
constexpr uint32_t kMqttProbeWindowMs = 250;
constexpr uint32_t kWifiReconnectPeriodMs = 5000;
constexpr float kMaxSanePower_mW = 5000.0f;
constexpr float kMaxSaneCurrent_mA = 2000.0f;
constexpr uint32_t kSignalFrequencyHz = 10000;
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint32_t kExperimentDurationUs = 5UL * 1000000UL;
constexpr char kWifiSsid[] = "Vodafone-mango";
constexpr char kWifiPass[] = "Mangoblu2020";
constexpr char kMqttHost[] = "192.168.1.13";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttClientId[] = "mqtt_wifi_sender";
constexpr char kMqttProbeTopic[] = "mqtt_wifi/current_probe";

Adafruit_INA219 ina219;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

struct ExperimentCase {
  const char *label;
  uint32_t targetHz;
};

constexpr ExperimentCase kCases[] = {
    {"10k", 10000},
    {"256", 256},
};

struct EnergyWindow {
  float energy_mJ = 0.0f;
  float current_mAs = 0.0f;
  float lastPower_mW = 0.0f;
  float lastCurrent_mA = 0.0f;
  float lastValidPower_mW = 0.0f;
  float lastValidCurrent_mA = 0.0f;
  uint32_t lastSampleUs = 0;
  bool hasSample = false;
  uint32_t powerSamples = 0;
};

portMUX_TYPE energyMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool monitorRunning = true;
volatile bool windowOpen = false;
EnergyWindow energyWindow;
TaskHandle_t monitorTaskHandle = nullptr;

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPass);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

bool connectMqtt() {
  mqttClient.setServer(kMqttHost, kMqttPort);
  mqttClient.setBufferSize(256);
  mqttClient.setKeepAlive(60);

  if (!mqttClient.connected()) {
    if (!mqttClient.connect(kMqttClientId)) {
      return false;
    }
  }

  return true;
}

struct Component {
  double amplitude;
  double freq;
  double phase;
  double phaseInc;
};

Component components[] = {
    {2.0, 30.0, 0.0, 0.0},
    {4.0, 60.0, 0.0, 0.0},
};

constexpr uint8_t kComponentCount = sizeof(components) / sizeof(components[0]);
constexpr double kOutputScale = 20.0;

hw_timer_t *signalTimer = nullptr;
volatile uint32_t signalSampleCount = 0;
volatile uint8_t lastDacValue = 128;
volatile bool signalRunning = false;

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

void stopSignalGenerator() {
  timerAlarmDisable(signalTimer);
  signalRunning = false;
}

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
  energyWindow.energy_mJ = 0.0f;
  energyWindow.current_mAs = 0.0f;
  energyWindow.lastPower_mW = 0.0f;
  energyWindow.lastCurrent_mA = 0.0f;
  energyWindow.lastValidPower_mW = 0.0f;
  energyWindow.lastValidCurrent_mA = 0.0f;
  energyWindow.lastSampleUs = micros();
  energyWindow.hasSample = false;
  energyWindow.powerSamples = 0;
  taskEXIT_CRITICAL(&energyMux);
}

float sanitizePowerReading(float power_mW) {
  if (power_mW >= 0.0f && power_mW <= kMaxSanePower_mW) {
    return power_mW;
  }

  return energyWindow.lastValidPower_mW;
}

float sanitizeCurrentReading(float current_mA) {
  if (current_mA >= 0.0f && current_mA <= kMaxSaneCurrent_mA) {
    return current_mA;
  }

  return energyWindow.lastValidCurrent_mA;
}

void readSanitizedIna219(float &power_mW, float &current_mA) {
  float rawPower_mW = ina219.getPower_mW();
  float rawCurrent_mA = ina219.getCurrent_mA();
  float safePower_mW = sanitizePowerReading(rawPower_mW);
  float safeCurrent_mA = sanitizeCurrentReading(rawCurrent_mA);

  if (safePower_mW != rawPower_mW || safeCurrent_mA != rawCurrent_mA) {
    power_mW = energyWindow.lastValidPower_mW;
    current_mA = energyWindow.lastValidCurrent_mA;
    return;
  }

  power_mW = safePower_mW;
  current_mA = safeCurrent_mA;
}

void seedEnergyWindow() {
  float power_mW = 0.0f;
  float current_mA = 0.0f;
  readSanitizedIna219(power_mW, current_mA);

  taskENTER_CRITICAL(&energyMux);
  energyWindow.lastSampleUs = micros();
  energyWindow.lastPower_mW = power_mW;
  energyWindow.lastCurrent_mA = current_mA;
  energyWindow.lastValidPower_mW = power_mW;
  energyWindow.lastValidCurrent_mA = current_mA;
  energyWindow.hasSample = true;
  taskEXIT_CRITICAL(&energyMux);
}

void powerMonitorTask(void *) {
  while (monitorRunning) {
    uint32_t nowUs = micros();
    float power_mW = 0.0f;
    float current_mA = 0.0f;
    readSanitizedIna219(power_mW, current_mA);

    taskENTER_CRITICAL(&energyMux);
    if (windowOpen) {
      if (energyWindow.hasSample) {
        float deltaSeconds = (nowUs - energyWindow.lastSampleUs) / 1000000.0f;
        energyWindow.energy_mJ += energyWindow.lastPower_mW * deltaSeconds;
        energyWindow.current_mAs += energyWindow.lastCurrent_mA * deltaSeconds;
      }
      energyWindow.powerSamples++;
      energyWindow.lastSampleUs = nowUs;
      energyWindow.lastPower_mW = power_mW;
      energyWindow.lastCurrent_mA = current_mA;
      energyWindow.lastValidPower_mW = power_mW;
      energyWindow.lastValidCurrent_mA = current_mA;
      energyWindow.hasSample = true;
    } else {
      energyWindow.lastSampleUs = nowUs;
      energyWindow.lastPower_mW = power_mW;
      energyWindow.lastCurrent_mA = current_mA;
      energyWindow.lastValidPower_mW = power_mW;
      energyWindow.lastValidCurrent_mA = current_mA;
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

void finalizeEnergyWindow(float &energy_mJ, float &current_mAs, uint32_t &powerSamples) {
  taskENTER_CRITICAL(&energyMux);
  if (energyWindow.hasSample) {
    uint32_t stopUs = micros();
    float tailSeconds = (stopUs - energyWindow.lastSampleUs) / 1000000.0f;
    if (tailSeconds > 0.0f) {
      energyWindow.energy_mJ += energyWindow.lastPower_mW * tailSeconds;
      energyWindow.current_mAs += energyWindow.lastCurrent_mA * tailSeconds;
    }
  }
  energy_mJ = energyWindow.energy_mJ;
  powerSamples = energyWindow.powerSamples;
  current_mAs = energyWindow.current_mAs;
  taskEXIT_CRITICAL(&energyMux);
}

void runMqttPublishCurrentProbe() {
  stopSignalGenerator();

  if (!connectWifi()) {
    Serial.println("mqtt_current_probe,status=wifi_failed");
    return;
  }

  if (!connectMqtt()) {
    Serial.println("mqtt_current_probe,status=mqtt_failed");
    return;
  }

  resetEnergyWindow();
  seedEnergyWindow();

  taskENTER_CRITICAL(&energyMux);
  windowOpen = true;
  taskEXIT_CRITICAL(&energyMux);

  const uint32_t startUs = micros();
  const String payload = String("{\"probe\":\"publish\",\"sent_us\":") + String(startUs) + "}";
  const bool published = mqttClient.publish(kMqttProbeTopic, payload.c_str());

  const uint32_t windowStartMs = millis();
  while ((millis() - windowStartMs) < kMqttProbeWindowMs) {
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    yield();
  }

  taskENTER_CRITICAL(&energyMux);
  windowOpen = false;
  taskEXIT_CRITICAL(&energyMux);
  delay(kPowerMonitorPeriodMs + 5);

  float energy_mJ = 0.0f;
  float current_mAs = 0.0f;
  uint32_t powerSamples = 0;
  finalizeEnergyWindow(energy_mJ, current_mAs, powerSamples);

  const float elapsedMs = kMqttProbeWindowMs;
  const float averagePower_mW = elapsedMs > 0.0f ? (energy_mJ * 1000.0f) / elapsedMs : 0.0f;
  const float averageCurrent_mA = elapsedMs > 0.0f ? (current_mAs * 1000.0f) / elapsedMs : 0.0f;

  Serial.printf(
      "mqtt_current_probe,published=%s,payload_bytes=%lu,window_ms=%lu,avg_current_mA=%.2f,avg_power_mW=%.2f,energy_mJ=%.2f,power_samples=%lu,payload=%s\n",
      published ? "yes" : "no",
      static_cast<unsigned long>(payload.length()),
      static_cast<unsigned long>(kMqttProbeWindowMs),
      averageCurrent_mA,
      averagePower_mW,
      energy_mJ,
      static_cast<unsigned long>(powerSamples),
      payload.c_str());
}

void pulseStart() {
  digitalWrite(kStartPulsePin, HIGH);
  delay(20);
  digitalWrite(kStartPulsePin, LOW);
}

void runCase(const ExperimentCase &experiment) {
  const uint32_t periodUs = 1000000UL / experiment.targetHz;
  const uint32_t targetSampleCount = kExperimentDurationUs / periodUs;
  uint32_t missedDeadlines = 0;

  resetEnergyWindow();
  seedEnergyWindow();
  startSignalGenerator();

  delay(kEnergyWarmupMs);
  resetEnergyWindow();
  seedEnergyWindow();
  taskENTER_CRITICAL(&energyMux);
  windowOpen = true;
  taskEXIT_CRITICAL(&energyMux);

  uint32_t startUs = micros();
  uint32_t deadlineUs = startUs;

  pulseStart();

  while ((uint32_t)(micros() - startUs) < kExperimentDurationUs) {
    deadlineUs += periodUs;
    if ((int32_t)(micros() - deadlineUs) > 0) {
      ++missedDeadlines;
    } else {
      while ((int32_t)(micros() - deadlineUs) < 0) {
        yield();
      }
    }
  }

  uint32_t elapsedUs = micros() - startUs;
  uint32_t actualSampleCount = signalSampleCount;

  stopSignalGenerator();
  taskENTER_CRITICAL(&energyMux);
  windowOpen = false;
  taskEXIT_CRITICAL(&energyMux);
  delay(kPowerMonitorPeriodMs + 5);

  float energy_mJ = 0.0f;
  uint32_t powerSamples = 0;
  float current_mAs = energyWindow.current_mAs;
  finalizeEnergyWindow(energy_mJ, current_mAs, powerSamples);

  float elapsedMs = elapsedUs / 1000.0f;
  float averagePower_mW = elapsedMs > 0.0f ? (energy_mJ * 1000.0f) / elapsedMs : 0.0f;
  float averageCurrent_mA = elapsedMs > 0.0f ? (current_mAs * 1000.0f) / elapsedMs : 0.0f;
  float generatorHz = elapsedUs > 0 ? (actualSampleCount * 1000000.0f / elapsedUs) : 0.0f;

  Serial.printf(
      "sampling_sender,label=%s,target_hz=%lu,target_samples=%lu,signal_hz=%lu,period_us=%lu,elapsed_us=%lu,generator_hz=%.2f,avg_current_mA=%.2f,avg_power_mW=%.2f,energy_mJ=%.2f,power_samples=%lu,generator_samples=%lu,last_dac=%u,missed_deadlines=%lu\n",
      experiment.label,
      static_cast<unsigned long>(experiment.targetHz),
      static_cast<unsigned long>(targetSampleCount),
      static_cast<unsigned long>(kSignalFrequencyHz),
      static_cast<unsigned long>(periodUs),
      static_cast<unsigned long>(elapsedUs),
      generatorHz,
      averageCurrent_mA,
      averagePower_mW,
      energy_mJ,
      static_cast<unsigned long>(powerSamples),
      static_cast<unsigned long>(signalSampleCount),
      static_cast<unsigned long>(lastDacValue),
      static_cast<unsigned long>(missedDeadlines));
}

void runAllExperiments() {
  uint32_t runStartUs = micros();

  for (const ExperimentCase &experiment : kCases) {
    runCase(experiment);
    delay(250);
  }

  uint32_t runElapsedUs = micros() - runStartUs;
  Serial.printf(
      "sampling_sender_run,total_elapsed_us=%lu,total_elapsed_ms=%.2f\n",
      static_cast<unsigned long>(runElapsedUs),
      runElapsedUs / 1000.0f);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  pinMode(kStartButtonPin, INPUT_PULLUP);
  pinMode(kStartPulsePin, OUTPUT);
  digitalWrite(kStartPulsePin, LOW);

  configureIna219();
  configureSignalGenerator();
  startMonitorTask();

  mqttClient.setBufferSize(256);

  Serial.println("Sampling sender starting");
  Serial.println("Short press PRG for analog run, long press PRG for MQTT current probe");
}

void loop() {
  static int lastButtonState = HIGH;
  static uint32_t buttonDownMs = 0;
  static bool longPressHandled = false;
  int buttonState = digitalRead(kStartButtonPin);

  if (lastButtonState == HIGH && buttonState == LOW) {
    buttonDownMs = millis();
    longPressHandled = false;
    delay(20);
    if (digitalRead(kStartButtonPin) == LOW) {
      // wait for release or long-press handling below
    }
  }

  if (buttonState == LOW && !longPressHandled && (millis() - buttonDownMs) >= 2000) {
    longPressHandled = true;
    runMqttPublishCurrentProbe();
    Serial.println("MQTT current probe completed");
  }

  if (lastButtonState == LOW && buttonState == HIGH && !longPressHandled) {
    runAllExperiments();
    Serial.println("Sampling sender completed");
  }

  lastButtonState = buttonState;
  delay(5);
}
