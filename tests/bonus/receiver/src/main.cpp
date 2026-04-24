#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/adc.h>

namespace {

constexpr uint8_t kAdcPin = 7;
constexpr adc1_channel_t kAdcChannel = ADC1_CHANNEL_6;
constexpr uint32_t kStartupDelayMs = 2000;
constexpr uint32_t kCaptureWindowMs = 5000;
constexpr uint32_t kFrequencyOptionsHz[] = {250, 500, 1000, 2000};

enum class ReadMode {
  Raw,
  Analog,
};

constexpr ReadMode kReadMode = ReadMode::Analog; // Raw / Analog
constexpr uint8_t kFrequencySelection = 0; // 0=250 Hz, 1=500 Hz, 2=1000 Hz, 3=2000 Hz
constexpr bool kUseLightSleep = true;

constexpr uint32_t selectedFrequencyHz() {
  return kFrequencyOptionsHz[kFrequencySelection];
}

const char *readModeName(ReadMode readMode) {
  switch (readMode) {
    case ReadMode::Raw:
      return "raw read";
    case ReadMode::Analog:
    default:
      return "analogRead";
  }
}

void waitUntilDeadline(uint32_t deadlineUs) {
  const uint32_t nowUs = micros();
  if (nowUs < deadlineUs) {
    const uint64_t sleepUs = static_cast<uint64_t>(deadlineUs - nowUs);
    if (sleepUs > 0) {
      esp_sleep_enable_timer_wakeup(sleepUs);
      esp_light_sleep_start();
    }
  }
}

uint16_t readSelectedSample(ReadMode readMode) {
  if (readMode == ReadMode::Raw) {
    return static_cast<uint16_t>(adc1_get_raw(kAdcChannel));
  }

  return static_cast<uint16_t>(analogRead(kAdcPin));
}

void configureAdc(ReadMode readMode) {
  pinMode(kAdcPin, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(kAdcPin, ADC_11db);

  if (readMode == ReadMode::Raw) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(kAdcChannel, ADC_ATTEN_DB_11);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(kStartupDelayMs);

  const ReadMode readMode = kReadMode;
  const uint32_t frequencyHz = selectedFrequencyHz();
  const uint32_t samplePeriodUs = 1000000UL / frequencyHz;

  configureAdc(readMode);

  Serial.printf("bonus receiver mode=%s sleep=%s rate=%lu Hz\n",
                readModeName(readMode),
                kUseLightSleep ? "yes" : "no",
                static_cast<unsigned long>(frequencyHz));
  Serial.flush();

  uint32_t nextSampleUs = micros();
  uint32_t sampleCount = 0;
  uint32_t sampleSum = 0;
  uint16_t minSample = 0xFFFF;
  uint16_t maxSample = 0;

  while (true) {
    const uint16_t sample = readSelectedSample(readMode);
    sampleSum += sample;
    if (sample < minSample) {
      minSample = sample;
    }
    if (sample > maxSample) {
      maxSample = sample;
    }
    ++sampleCount;

    nextSampleUs += samplePeriodUs;
    if (kUseLightSleep) {
      waitUntilDeadline(nextSampleUs);
    } else {
      while (static_cast<uint32_t>(micros()) < nextSampleUs) {
        yield();
      }
    }

    if (sampleCount >= (selectedFrequencyHz() * kCaptureWindowMs) / 1000UL) {
      const float average = sampleCount > 0 ? static_cast<float>(sampleSum) / static_cast<float>(sampleCount) : 0.0f;
      Serial.printf("samples=%lu avg=%.2f min=%u max=%u\n",
                    static_cast<unsigned long>(sampleCount),
                    average,
                    static_cast<unsigned int>(minSample),
                    static_cast<unsigned int>(maxSample));
      sampleCount = 0;
      sampleSum = 0;
      minSample = 0xFFFF;
      maxSample = 0;
    }
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}