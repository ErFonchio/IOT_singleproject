#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <arduinoFFT.h>
#include <driver/adc.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_sleep.h>

#include "secrets.h"

// ADC setup and timing constants for the final receiver flow.
constexpr uint8_t ADC_PIN = 7;
constexpr adc1_channel_t ADC_CHANNEL = ADC1_CHANNEL_6;
constexpr uint32_t MAX_SAMPLE_RATE_HZ = 15000;
constexpr uint32_t ANALYSIS_WINDOW_MS = 5000;
constexpr uint32_t AGGREGATION_WINDOW_MS = 5000;
constexpr uint16_t FFT_SIZE = 1024;
constexpr uint32_t ANALYSIS_SAMPLE_TARGET = (MAX_SAMPLE_RATE_HZ * ANALYSIS_WINDOW_MS) / 1000;
constexpr uint32_t MIN_ADAPTIVE_RATE_HZ = 1;
constexpr uint32_t DEFAULT_ADAPTIVE_RATE_HZ = 256;

// The receiver alternates between an FFT analysis phase and an adaptive aggregation phase.
enum SamplingPhase {
  PHASE_ANALYSIS = 0,
  PHASE_AGGREGATION = 1,
};

// Timer and buffers used to collect samples and run the FFT locally.
static hw_timer_t *sampleTimer = nullptr;
static volatile SamplingPhase currentPhase = PHASE_ANALYSIS;
static volatile bool phaseComplete = false;
static volatile uint32_t analysisIndex = 0;
static volatile uint32_t aggregationIndex = 0;
static volatile uint32_t aggregationTargetSamples = 0;
static volatile uint64_t aggregationSum = 0;

// Analysis samples are stored once, then reused to estimate the dominant frequency.
static uint16_t analysisSamples[ANALYSIS_SAMPLE_TARGET];
static double fftReal[FFT_SIZE];
static double fftImag[FFT_SIZE];
static ArduinoFFT<double> fft(fftReal, fftImag, FFT_SIZE, MAX_SAMPLE_RATE_HZ);

// WiFi and MQTT are only needed after the local signal processing is complete.
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

// Latest measurements collected during the experiment.
static double lastDominantFrequencyHz = 0.0;
static uint32_t lastAdaptiveRateHz = DEFAULT_ADAPTIVE_RATE_HZ;
static float lastAggregatedAverage = 0.0f;
static uint32_t lastAnalysisElapsedUs = 0;
static uint32_t lastAggregationElapsedUs = 0;

// Separate tasks keep sampling and networking isolated on two cores.
static TaskHandle_t samplingTaskHandle = nullptr;
static TaskHandle_t wifiTaskHandle = nullptr;

// Forward declarations for the task entrypoints and logging helper.
void taskSampling(void *pvParameters);
void taskWifiPublish(void *pvParameters);
void logPhase(const char *phase, const char *detail1 = nullptr, const char *detail2 = nullptr, const char *detail3 = nullptr);

void IRAM_ATTR onSampleTimer() {
  // The timer ISR only reads the ADC and stores the value for the current phase.
  const uint16_t sample = static_cast<uint16_t>(adc1_get_raw(ADC_CHANNEL));

  if (currentPhase == PHASE_ANALYSIS) {
    if (analysisIndex < ANALYSIS_SAMPLE_TARGET) {
      analysisSamples[analysisIndex++] = sample;
      if (analysisIndex >= ANALYSIS_SAMPLE_TARGET) {
        phaseComplete = true;
      }
    }
    return;
  }

  if (aggregationIndex < aggregationTargetSamples) {
    aggregationSum += sample;
    aggregationIndex++;
    if (aggregationIndex >= aggregationTargetSamples) {
      phaseComplete = true;
    }
  }
}

static void configureTimer(uint32_t sampleRateHz) {
  if (sampleRateHz == 0) {
    sampleRateHz = 1;
  }

  // Reprogram the hardware timer with the requested sampling period.
  const uint32_t periodUs = max(1UL, 1000000UL / sampleRateHz);
  timerAlarmDisable(sampleTimer);
  timerWrite(sampleTimer, 0);
  timerAlarmWrite(sampleTimer, periodUs, true);
}

static void startAnalysisCapture() {
  // Start the fixed-rate capture used to feed the FFT window.
  currentPhase = PHASE_ANALYSIS;
  phaseComplete = false;
  analysisIndex = 0;
  configureTimer(MAX_SAMPLE_RATE_HZ);
  timerAlarmEnable(sampleTimer);
}

static void startAggregationCapture(uint32_t sampleRateHz) {
  // Start the second capture, this time at the adaptive rate chosen by the FFT.
  currentPhase = PHASE_AGGREGATION;
  phaseComplete = false;
  aggregationIndex = 0;
  aggregationSum = 0;
  aggregationTargetSamples = max(1UL, (sampleRateHz * AGGREGATION_WINDOW_MS) / 1000UL);
  configureTimer(sampleRateHz);
  timerAlarmEnable(sampleTimer);
}

static void waitUntilDeadline(uint64_t deadlineUs) {
  const uint64_t nowUs = static_cast<uint64_t>(micros());
  if (nowUs < deadlineUs) {
    // Light sleep is used only to wait between scheduled samples.
    const uint64_t sleepUs = deadlineUs - nowUs;
    if (sleepUs > 0) {
      esp_sleep_enable_timer_wakeup(sleepUs);
      esp_light_sleep_start();
    }
  }
}

static uint32_t captureAggregationWithDeadline(uint32_t sampleRateHz, uint32_t &elapsedUs) {
  // Collect a full adaptive window while minimizing active waiting.
  aggregationIndex = 0;
  aggregationSum = 0;
  aggregationTargetSamples = max(1UL, (sampleRateHz * AGGREGATION_WINDOW_MS) / 1000UL);

  const uint64_t safeSampleRateHz = sampleRateHz == 0 ? 1ULL : static_cast<uint64_t>(sampleRateHz);
  const uint64_t periodUs = max(1ULL, 1000000ULL / safeSampleRateHz);
  const uint64_t startUs = static_cast<uint64_t>(micros());
  uint64_t nextSampleDeadlineUs = startUs;
  uint32_t missedDeadlines = 0;

  for (uint32_t i = 0; i < aggregationTargetSamples; ++i) {
    const uint64_t beforeSampleUs = static_cast<uint64_t>(micros());
    if (beforeSampleUs > nextSampleDeadlineUs) {
      ++missedDeadlines;
    } else {
      waitUntilDeadline(nextSampleDeadlineUs);
    }

    aggregationSum += static_cast<uint16_t>(adc1_get_raw(ADC_CHANNEL));
    aggregationIndex++;

    nextSampleDeadlineUs += periodUs;
  }

  elapsedUs = static_cast<uint32_t>(static_cast<uint64_t>(micros()) - startUs);
  return missedDeadlines;
}

static void waitForPhaseCompletion() {
  // The sampling task waits here until the ISR marks the phase as complete.
  while (!phaseComplete) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  timerAlarmDisable(sampleTimer);
}

static void enterDeepSleepAfterExperiment() {
  // After the publish step, the board disconnects and enters deep sleep.
  Serial.println("[final] first experiment completed, going to deep sleep");

  timerAlarmDisable(sampleTimer);
  mqttClient.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  esp_deep_sleep_start();
}

static void keepWifiOffDuringSampling() {
  // WiFi is kept off while the device is only sampling and computing locally.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[wifi] off during sampling phase");
}

static void enableWifiForPublish() {
  // WiFi is turned on only when the receiver is ready to publish the result.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.println("[wifi] on for publish phase");
}

static void connectWifi() {
  // Connect to the local network just before the MQTT publish.
  enableWifiForPublish();
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print('.');
  }
  Serial.println();
  Serial.print("[wifi] connected: ");
  Serial.println(WiFi.localIP());
}

void logPhase(const char *phase, const char *detail1, const char *detail2, const char *detail3) {
  // Compact phase log used to make the execution flow readable on Serial.
  Serial.printf("[phase] %s", phase);
  if (detail1) {
    Serial.printf(" | %s", detail1);
  }
  if (detail2) {
    Serial.printf(" | %s", detail2);
  }
  if (detail3) {
    Serial.printf(" | %s", detail3);
  }
  Serial.println();
}

static void ensureMqttConnection() {
  // MQTT is configured on demand so it only becomes active in the publish phase.
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setKeepAlive(30);
  mqttClient.setBufferSize(256);

  while (!mqttClient.connected()) {
    Serial.print("[mqtt] connecting... ");
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("ok");
      return;
    }

    Serial.print("failed, state=");
    Serial.println(mqttClient.state());
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static void formatFloat(char *buffer, size_t bufferSize, double value, uint8_t decimals) {
  snprintf(buffer, bufferSize, "%.*f", decimals, value);
}

static void formatUint(char *buffer, size_t bufferSize, uint32_t value) {
  snprintf(buffer, bufferSize, "%lu", static_cast<unsigned long>(value));
}


static double computeDominantFrequencyHz() {
  // The FFT is run on each captured frame after removing the DC offset.
  const uint32_t frameCount = analysisIndex / FFT_SIZE;
  double highestObservedFrequency = 0.0;
  double highestObservedMagnitude = 0.0;

  for (uint32_t frame = 0; frame < frameCount; frame++) {
    const uint32_t offset = frame * FFT_SIZE;

    // Remove the DC component before windowing and FFT.
    double mean = 0.0;
    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        mean += analysisSamples[offset + i];
    }
    mean /= FFT_SIZE;

    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        fftReal[i] = analysisSamples[offset + i] - mean;
        fftImag[i] = 0.0;
    }

    fft.windowing(fftReal, FFT_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    fft.compute(fftReal, fftImag, FFT_SIZE, FFT_FORWARD);
    fft.complexToMagnitude(fftReal, fftImag, FFT_SIZE);

    double frameMagnitude = 0.0;
    uint16_t framePeakIndex = 1;
    for (uint16_t bin = 1; bin < FFT_SIZE / 2; bin++) {
      if (fftReal[bin] > frameMagnitude) {
        frameMagnitude = fftReal[bin];
        framePeakIndex = bin;
      }
    }

    const double frameFrequency = (framePeakIndex * static_cast<double>(MAX_SAMPLE_RATE_HZ)) / FFT_SIZE;
    if (frameMagnitude > highestObservedMagnitude) {
      highestObservedMagnitude = frameMagnitude;
      highestObservedFrequency = frameFrequency;
    }
  }

  if (highestObservedFrequency <= 0.0) {
    highestObservedFrequency = DEFAULT_ADAPTIVE_RATE_HZ / 2.0;
  }

  return highestObservedFrequency;
}

static float computeAverage() {
  // Average of the adaptive capture window used for the final payload.
  if (aggregationIndex == 0) {
    return 0.0f;
  }
  return static_cast<float>(aggregationSum) / static_cast<float>(aggregationIndex);
}

static bool publishTelemetry(double dominantFrequencyHz, uint32_t adaptiveRateHz, float averageValue, uint32_t sampleCount, uint32_t analysisElapsedUs, uint32_t aggregationElapsedUs) {
  // Serialize the final metrics in the compact JSON payload published over MQTT.
    char payload[256];
    snprintf(payload, sizeof(payload),
            "{\"adc_pin\":%u,\"dominant_hz\":%.2f,\"adaptive_hz\":%u,\"adc_avg\":%.2f,\"sample_count\":%u,\"analysis_us\":%lu,\"aggregation_us\":%lu}",
            ADC_PIN,
            dominantFrequencyHz,
            adaptiveRateHz,
            averageValue,
            sampleCount,
            static_cast<unsigned long>(analysisElapsedUs),
            static_cast<unsigned long>(aggregationElapsedUs));

    Serial.print("[mqtt] telemetry: ");
    Serial.println(payload);
    return mqttClient.publish(TELEMETRY_TOPIC, payload);
}

void setup() {
  // Initial hardware setup runs before the actual experiment phases begin.
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(1000));
    Serial.println();
    Serial.println("[final] boot");

    pinMode(ADC_PIN, INPUT);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);
    logPhase("boot", "init wifi/mqtt", "adc pin 7", "setup");

    keepWifiOffDuringSampling();

    // The timer interrupt drives all subsequent sampling phases.
    sampleTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(sampleTimer, &onSampleTimer, true);

    // Sampling runs on core 1, leaving core 0 available for networking.
    xTaskCreatePinnedToCore(taskSampling, "taskSampling", 8192, nullptr, 2, &samplingTaskHandle, 1);
}

void loop() {
    vTaskDelete(nullptr);
}

void taskSampling(void *pvParameters) {
    (void)pvParameters;

  // First phase: fixed-rate capture for the FFT analysis window.
  Serial.printf("[analysis] capture %lu samples at %u Hz for %u ms\n",
                static_cast<unsigned long>(ANALYSIS_SAMPLE_TARGET),
                MAX_SAMPLE_RATE_HZ,
                ANALYSIS_WINDOW_MS);
  logPhase("analysis", "sampling max rate", "window 5s", "collecting adc");
  const uint32_t analysisStartUs = micros();
  startAnalysisCapture();
  waitForPhaseCompletion();
  lastAnalysisElapsedUs = micros() - analysisStartUs;

  lastDominantFrequencyHz = computeDominantFrequencyHz();
  lastAdaptiveRateHz = static_cast<uint32_t>(lastDominantFrequencyHz * 2.0 + 0.5);
  if (lastAdaptiveRateHz < MIN_ADAPTIVE_RATE_HZ) {
    lastAdaptiveRateHz = MIN_ADAPTIVE_RATE_HZ;
  }
  if (lastAdaptiveRateHz > MAX_SAMPLE_RATE_HZ) {
    lastAdaptiveRateHz = MAX_SAMPLE_RATE_HZ;
  }
  if (lastAdaptiveRateHz == 0) {
    lastAdaptiveRateHz = DEFAULT_ADAPTIVE_RATE_HZ;
  }

  Serial.printf("[analysis] dominant=%.2f Hz -> adaptive=%u Hz\n",
                lastDominantFrequencyHz,
                lastAdaptiveRateHz);
  char dominantBuffer[16];
  char adaptiveBuffer[16];
  formatFloat(dominantBuffer, sizeof(dominantBuffer), lastDominantFrequencyHz, 1);
  formatUint(adaptiveBuffer, sizeof(adaptiveBuffer), lastAdaptiveRateHz);
  logPhase("fft done", dominantBuffer, "hz dominant", adaptiveBuffer);

  // Second phase: sample again at the adaptive rate chosen by the FFT.
  Serial.printf("[aggregation] capture at %u Hz for %u ms\n",
                lastAdaptiveRateHz,
                AGGREGATION_WINDOW_MS);
  logPhase("aggregation", "deadline wait between samples", "window 5s", "building average");
    lastAggregationElapsedUs = 0;
    const uint32_t missedDeadlines = captureAggregationWithDeadline(lastAdaptiveRateHz, lastAggregationElapsedUs);
    const float achievedAggregationHz = lastAggregationElapsedUs > 0
      ? (aggregationIndex * 1000000.0f / lastAggregationElapsedUs)
      : 0.0f;

  Serial.printf("[aggregation] target_hz=%u,period_us=%lu,elapsed_us=%lu,achieved_hz=%.2f,missed_deadlines=%lu\n",
                lastAdaptiveRateHz,
                static_cast<unsigned long>(max(1ULL, 1000000ULL / (lastAdaptiveRateHz == 0 ? 1ULL : static_cast<uint64_t>(lastAdaptiveRateHz)))),
          static_cast<unsigned long>(lastAggregationElapsedUs),
                achievedAggregationHz,
                static_cast<unsigned long>(missedDeadlines));

  lastAggregatedAverage = computeAverage();
  Serial.printf("[aggregation] avg=%.2f samples=%u\n",
                lastAggregatedAverage,
                aggregationIndex);
  char averageBuffer[16];
  formatFloat(averageBuffer, sizeof(averageBuffer), lastAggregatedAverage, 1);
  logPhase("publish", "mqtt local broker", "sending average", averageBuffer);

  // WiFi is re-enabled only after the local processing work is finished.
  enableWifiForPublish();

  // Offload the network work to the other core.
  xTaskCreatePinnedToCore(taskWifiPublish, "taskWifiPublish", 8192, nullptr, 2, &wifiTaskHandle, 0);
  vTaskDelete(nullptr);
}

void taskWifiPublish(void *pvParameters) {
  (void)pvParameters;

  // Connect, publish once, and then go to deep sleep.
  connectWifi();
  ensureMqttConnection();

  if (!mqttClient.connected()) {
    ensureMqttConnection();
  }

  const bool publishOk = publishTelemetry(lastDominantFrequencyHz,
                                          lastAdaptiveRateHz,
                                          lastAggregatedAverage,
                                          aggregationIndex,
                                          lastAnalysisElapsedUs,
                                          lastAggregationElapsedUs);
  Serial.printf("[mqtt] publish %s\n", publishOk ? "ok" : "failed");

  mqttClient.loop();
  vTaskDelay(pdMS_TO_TICKS(250));
  enterDeepSleepAfterExperiment();
}
