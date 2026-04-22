#include <Arduino.h>
#include <esp_sleep.h>

namespace {

constexpr uint8_t kAdcPin = 7;
constexpr uint8_t kStartPin = 5;
constexpr uint32_t kExperimentDurationUs = 60UL * 1000000UL;

struct ExperimentCase {
  const char *label;
  uint32_t targetHz;
  bool useLightSleep;
};

constexpr ExperimentCase kCases[] = {
    {"10k_no_sleep", 10000, false},
    {"10k_light_sleep", 10000, true},
    {"256_no_sleep", 256, false},
    {"256_light_sleep", 256, true},
};

void configureAdc() {
  pinMode(kAdcPin, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

void waitForStartPulse() {
  Serial.println("Waiting for START pulse on GPIO5...");
  while (digitalRead(kStartPin) == LOW) {
    delay(1);
  }
  while (digitalRead(kStartPin) == HIGH) {
    delay(1);
  }
  Serial.println("START pulse received");
}

void waitUntilDeadline(uint32_t deadlineUs, bool useLightSleep) {
  if (useLightSleep) {
    uint32_t nowUs = micros();
    if (nowUs < deadlineUs) {
      uint64_t sleepUs = static_cast<uint64_t>(deadlineUs - nowUs);
      if (sleepUs > 0) {
        esp_sleep_enable_timer_wakeup(sleepUs);
        esp_light_sleep_start();
      }
    }
    return;
  }

  while ((int32_t)(micros() - deadlineUs) < 0) {
    yield();
  }
}

void runCase(const ExperimentCase &experiment) {
  const uint32_t periodUs = 1000000UL / experiment.targetHz;
  uint32_t lastRawValue = 0;
  uint32_t missedDeadlines = 0;
  uint32_t sampleCount = 0;

  uint32_t startUs = micros();
  uint32_t deadlineUs = startUs;

  while ((uint32_t)(micros() - startUs) < kExperimentDurationUs) {
    deadlineUs += periodUs;
    uint32_t beforeWaitUs = micros();
    waitUntilDeadline(deadlineUs, experiment.useLightSleep);
    uint32_t afterWaitUs = micros();

    if ((int32_t)(afterWaitUs - deadlineUs) > 0) {
      ++missedDeadlines;
    } else if (!experiment.useLightSleep && (beforeWaitUs > deadlineUs)) {
      ++missedDeadlines;
    }

    lastRawValue = analogRead(kAdcPin);
    ++sampleCount;
  }

  uint32_t elapsedUs = micros() - startUs;
  float achievedHz = elapsedUs > 0 ? (sampleCount * 1000000.0f / elapsedUs) : 0.0f;

  Serial.printf(
      "sampling_receiver,label=%s,target_hz=%lu,sleep=%s,samples=%lu,period_us=%lu,elapsed_us=%lu,achieved_hz=%.2f,missed_deadlines=%lu,last_raw=%lu\n",
      experiment.label,
      static_cast<unsigned long>(experiment.targetHz),
      experiment.useLightSleep ? "light" : "none",
      static_cast<unsigned long>(sampleCount),
      static_cast<unsigned long>(periodUs),
      static_cast<unsigned long>(elapsedUs),
      achievedHz,
      static_cast<unsigned long>(missedDeadlines),
      static_cast<unsigned long>(lastRawValue));
}

void runAllExperiments() {
  waitForStartPulse();

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

  configureAdc();
  pinMode(kStartPin, INPUT_PULLDOWN);

  Serial.println("Sampling receiver starting");
  runAllExperiments();
  Serial.println("Sampling receiver completed");
}

void loop() {
  delay(1000);
}
