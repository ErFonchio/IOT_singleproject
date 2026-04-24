#include <Arduino.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <arduinoFFT.h>

#include <array>

#include "secrets.h"
#include "../../shared/bonus_signal.h"

namespace {

constexpr uint32_t kFilterSampleRateHz = 15000;
constexpr uint32_t kAnalysisBaselineRateHz = 15000;
constexpr uint32_t kAnalysisWindowSeconds = 5;
constexpr size_t kAnalysisSampleCount = kAnalysisBaselineRateHz * kAnalysisWindowSeconds;
constexpr size_t kAnalysisFftSize = 8192;
constexpr uint32_t kMinAdaptiveRateHz = 1;
constexpr uint32_t kDefaultAdaptiveRateHz = 10;
constexpr size_t kMaxWindowSamples = 1024;
constexpr size_t kWindowCount = 4;
constexpr uint16_t kWindowSizes[kWindowCount] = {128, 256, 512, 1024};
constexpr bonus::SignalProfile kProfiles[] = {
    bonus::SignalProfile::CleanSine,
    bonus::SignalProfile::NoisyMix,
    bonus::SignalProfile::NoisyAnomaly,
};
constexpr bonus::FilterMode kFilters[] = {
    bonus::FilterMode::None,
    bonus::FilterMode::ZScore,
    bonus::FilterMode::Hampel,
};
constexpr size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);
constexpr size_t kFilterCount = sizeof(kFilters) / sizeof(kFilters[0]);
constexpr uint32_t kWindowSeed = bonus::kDefaultSignalSeed;
constexpr float kNoiseSigma = 0.15f;
constexpr float kAnomalyProbability = 0.03f;
constexpr float kZScoreThreshold = 3.0f;
constexpr size_t kHampelRadius = 8;
constexpr float kHampelThreshold = 3.0f;

static float rawSamples[kMaxWindowSamples];
static float filteredSamples[kMaxWindowSamples];
static float cleanSamples[kMaxWindowSamples];
static bool truthFlags[kMaxWindowSamples];
static bool detectedFlags[kMaxWindowSamples];

static float analysisRawSamples[kAnalysisFftSize];
static float analysisFilteredSamples[kAnalysisFftSize];
static float adaptiveRawSamples[kAnalysisFftSize];
static float adaptiveFilteredSamples[kAnalysisFftSize];
static float fftReal[kAnalysisFftSize];
static float fftImag[kAnalysisFftSize];
static ArduinoFFT<float> fft(fftReal, fftImag, kAnalysisFftSize, kAnalysisBaselineRateHz);

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

struct ExperimentResult {
  const char *profileName;
  const char *filterName;
  uint16_t windowSize = 0;
  uint32_t sampleRateHz = 0;
  uint32_t analysisSampleRateHz = 0;
  uint32_t adaptiveSampleRateHz = 0;
  uint32_t analysisWindowSamples = 0;
  uint32_t adaptiveWindowSamples = 0;
  uint32_t adaptiveRateHz = 0;
  float rawDominantFrequencyHz = 0.0f;
  float filteredDominantFrequencyHz = 0.0f;
  float adaptiveRawDominantFrequencyHz = 0.0f;
  float adaptiveFilteredDominantFrequencyHz = 0.0f;
  float rawMeanAbsoluteError = 0.0f;
  float filteredMeanAbsoluteError = 0.0f;
  float meanErrorReduction = 0.0f;
  bonus::DetectionStats detection;
  uint32_t filterElapsedUs = 0;
  uint32_t rawFftElapsedUs = 0;
  uint32_t filteredFftElapsedUs = 0;
  uint32_t adaptiveWindowMs = 0;
};

const char *profileName(bonus::SignalProfile profile) {
  switch (profile) {
    case bonus::SignalProfile::CleanSine:
      return "clean_sine";
    case bonus::SignalProfile::NoisyMix:
      return "noisy_mix";
    case bonus::SignalProfile::NoisyAnomaly:
    default:
      return "noisy_anomaly";
  }
}

const char *filterName(bonus::FilterMode mode) {
  switch (mode) {
    case bonus::FilterMode::None:
      return "none";
    case bonus::FilterMode::ZScore:
      return "zscore";
    case bonus::FilterMode::Hampel:
    default:
      return "hampel";
  }
}

void fillWindow(bonus::SignalProfile profile, uint16_t windowSize, uint32_t sampleRateHz) {
  for (uint16_t index = 0; index < windowSize; ++index) {
    bool isAnomaly = false;
    rawSamples[index] = bonus::generateSample(profile,
                                              index,
                                              sampleRateHz,
                                              kWindowSeed,
                                              kNoiseSigma,
                                              kAnomalyProbability,
                                              &isAnomaly);
    cleanSamples[index] = bonus::expectedCleanSample(profile, index, sampleRateHz);
    truthFlags[index] = isAnomaly;
    detectedFlags[index] = false;
  }
}

void fillProjectedWindow(bonus::SignalProfile profile,
                         uint32_t logicalSampleCount,
                         uint32_t sampleRateHz,
                         float *targetSamples) {
  const uint32_t projectionCount = std::min<uint32_t>(logicalSampleCount, kAnalysisFftSize);
  if (logicalSampleCount <= kAnalysisFftSize) {
    for (uint32_t index = 0; index < projectionCount; ++index) {
      targetSamples[index] = bonus::generateSample(profile,
                                                   index,
                                                   sampleRateHz,
                                                   kWindowSeed,
                                                   kNoiseSigma,
                                                   kAnomalyProbability,
                                                   nullptr);
    }
    for (uint32_t index = projectionCount; index < kAnalysisFftSize; ++index) {
      targetSamples[index] = 0.0f;
    }
    return;
  }

  const uint32_t step = std::max<uint32_t>(1U, logicalSampleCount / kAnalysisFftSize);
  for (uint32_t index = 0; index < kAnalysisFftSize; ++index) {
    const uint32_t sampleIndex = std::min(logicalSampleCount - 1U, index * step);
    targetSamples[index] = bonus::generateSample(profile,
                                                 sampleIndex,
                                                 sampleRateHz,
                                                 kWindowSeed,
                                                 kNoiseSigma,
                                                 kAnomalyProbability,
                                                 nullptr);
  }
}

void applyFilterWindow(const float *input,
                       float *output,
                       uint32_t sampleCount,
                       bonus::FilterMode filterMode) {
  if (filterMode == bonus::FilterMode::None) {
    for (uint32_t index = 0; index < sampleCount; ++index) {
      output[index] = input[index];
    }
    return;
  }

  if (filterMode == bonus::FilterMode::ZScore) {
    bonus::applyZScoreFilter(input, output, nullptr, sampleCount, kZScoreThreshold);
    return;
  }

  bonus::applyHampelFilter(input,
                           output,
                           nullptr,
                           sampleCount,
                           kHampelRadius,
                           kHampelThreshold);
}

float computeDominantFrequencyHz(const float *samples, uint32_t sampleCount, uint32_t sampleRateHz) {
  double mean = 0.0;
  for (uint32_t index = 0; index < sampleCount; ++index) {
    mean += samples[index];
  }
  mean /= static_cast<double>(sampleCount);

  for (uint32_t index = 0; index < sampleCount; ++index) {
    fftReal[index] = samples[index] - mean;
    fftImag[index] = 0.0;
  }

  for (uint32_t index = sampleCount; index < kAnalysisFftSize; ++index) {
    fftReal[index] = 0.0f;
    fftImag[index] = 0.0f;
  }

  fft.windowing(fftReal, kAnalysisFftSize, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  fft.compute(fftReal, fftImag, kAnalysisFftSize, FFT_FORWARD);
  fft.complexToMagnitude(fftReal, fftImag, kAnalysisFftSize);

  uint16_t peakIndex = 1;
  float peakMagnitude = fftReal[1];
  for (uint32_t index = 2; index < kAnalysisFftSize / 2U; ++index) {
    if (fftReal[index] > peakMagnitude) {
      peakMagnitude = fftReal[index];
      peakIndex = static_cast<uint16_t>(index);
    }
  }

  return static_cast<float>(static_cast<double>(peakIndex) * static_cast<double>(sampleRateHz) / static_cast<double>(kAnalysisFftSize));
}

ExperimentResult runExperiment(bonus::SignalProfile profile, bonus::FilterMode filterMode, uint16_t windowSize) {
  ExperimentResult result;
  result.profileName = profileName(profile);
  result.filterName = filterName(filterMode);
  result.windowSize = windowSize;
  result.sampleRateHz = kFilterSampleRateHz;
  result.analysisSampleRateHz = kAnalysisBaselineRateHz;
  result.analysisWindowSamples = kAnalysisSampleCount;

  fillWindow(profile, windowSize, result.sampleRateHz);
  fillProjectedWindow(profile, kAnalysisSampleCount, result.analysisSampleRateHz, analysisRawSamples);

  result.rawMeanAbsoluteError = bonus::meanAbsoluteError(rawSamples, cleanSamples, windowSize);

  const uint32_t filterStartUs = micros();
  if (filterMode == bonus::FilterMode::None) {
    for (uint16_t index = 0; index < windowSize; ++index) {
      filteredSamples[index] = rawSamples[index];
      detectedFlags[index] = false;
    }
  } else if (filterMode == bonus::FilterMode::ZScore) {
    bonus::applyZScoreFilter(rawSamples,
                             filteredSamples,
                             detectedFlags,
                             windowSize,
                             kZScoreThreshold);
  } else {
    bonus::applyHampelFilter(rawSamples,
                             filteredSamples,
                             detectedFlags,
                             windowSize,
                             kHampelRadius,
                             kHampelThreshold);
  }
  result.filterElapsedUs = micros() - filterStartUs;

  result.filteredMeanAbsoluteError = bonus::meanAbsoluteError(filteredSamples, cleanSamples, windowSize);
  result.meanErrorReduction = bonus::percentageReduction(result.rawMeanAbsoluteError, result.filteredMeanAbsoluteError);
  result.detection = bonus::computeDetectionStats(truthFlags, detectedFlags, windowSize);

  applyFilterWindow(analysisRawSamples, analysisFilteredSamples, kAnalysisFftSize, filterMode);

  const float dominantForAdaptation = computeDominantFrequencyHz(analysisRawSamples,
                                                                 kAnalysisFftSize,
                                                                 result.analysisSampleRateHz);
  result.adaptiveRateHz = bonus::clampAdaptiveRateHz(dominantForAdaptation, result.analysisSampleRateHz);
  if (result.adaptiveRateHz < kMinAdaptiveRateHz) {
    result.adaptiveRateHz = kMinAdaptiveRateHz;
  }
  if (result.adaptiveRateHz < kDefaultAdaptiveRateHz) {
    result.adaptiveRateHz = kDefaultAdaptiveRateHz;
  }
  result.adaptiveSampleRateHz = result.adaptiveRateHz;
  result.adaptiveWindowSamples = result.adaptiveRateHz * kAnalysisWindowSeconds;

  fillProjectedWindow(profile, result.adaptiveWindowSamples, result.adaptiveSampleRateHz, adaptiveRawSamples);
  applyFilterWindow(adaptiveRawSamples, adaptiveFilteredSamples, kAnalysisFftSize, filterMode);

  const uint32_t rawFftStartUs = micros();
  result.rawDominantFrequencyHz = computeDominantFrequencyHz(analysisRawSamples,
                                                             kAnalysisSampleCount,
                                                             result.analysisSampleRateHz);
  result.rawFftElapsedUs = micros() - rawFftStartUs;

  const uint32_t filteredFftStartUs = micros();
  result.filteredDominantFrequencyHz = computeDominantFrequencyHz(analysisFilteredSamples,
                                                                  kAnalysisSampleCount,
                                                                  result.analysisSampleRateHz);
  result.filteredFftElapsedUs = micros() - filteredFftStartUs;

  const uint32_t adaptiveRawFftStartUs = micros();
  result.adaptiveRawDominantFrequencyHz = computeDominantFrequencyHz(adaptiveRawSamples,
                                                                      kAnalysisFftSize,
                                                                      result.adaptiveSampleRateHz);
  const uint32_t adaptiveRawFftElapsedUs = micros() - adaptiveRawFftStartUs;

  const uint32_t adaptiveFilteredFftStartUs = micros();
  result.adaptiveFilteredDominantFrequencyHz = computeDominantFrequencyHz(adaptiveFilteredSamples,
                                                                          kAnalysisFftSize,
                                                                          result.adaptiveSampleRateHz);
  const uint32_t adaptiveFilteredFftElapsedUs = micros() - adaptiveFilteredFftStartUs;

  result.adaptiveWindowMs = static_cast<uint32_t>((static_cast<uint64_t>(result.adaptiveWindowSamples) * 1000ULL) / result.adaptiveSampleRateHz);
  return result;
}

void printResult(const ExperimentResult &result) {
  Serial.printf("profile=%s filter=%s window=%u sample_rate=%lu fft_window_samples=%lu fft_window_s=%lu raw_dom_15khz=%.3f filtered_dom_15khz=%.3f raw_dom_adaptive=%.3f filtered_dom_adaptive=%.3f adaptive_rate=%lu raw_mae=%.4f filtered_mae=%.4f reduction=%.2f tp=%lu fp=%lu tn=%lu fn=%lu tpr=%.3f fpr=%.3f filter_us=%lu fft_raw_us=%lu fft_filtered_us=%lu adaptive_window_ms=%lu\n",
                result.profileName,
                result.filterName,
                static_cast<unsigned int>(result.windowSize),
                static_cast<unsigned long>(result.sampleRateHz),
                static_cast<unsigned long>(result.analysisWindowSamples),
                static_cast<unsigned long>(kAnalysisWindowSeconds),
                result.rawDominantFrequencyHz,
                result.filteredDominantFrequencyHz,
                result.adaptiveRawDominantFrequencyHz,
                result.adaptiveFilteredDominantFrequencyHz,
                static_cast<unsigned long>(result.adaptiveRateHz),
                result.rawMeanAbsoluteError,
                result.filteredMeanAbsoluteError,
                result.meanErrorReduction,
                static_cast<unsigned long>(result.detection.truePositive),
                static_cast<unsigned long>(result.detection.falsePositive),
                static_cast<unsigned long>(result.detection.trueNegative),
                static_cast<unsigned long>(result.detection.falseNegative),
                result.detection.truePositiveRate,
                result.detection.falsePositiveRate,
                static_cast<unsigned long>(result.filterElapsedUs),
                static_cast<unsigned long>(result.rawFftElapsedUs),
                static_cast<unsigned long>(result.filteredFftElapsedUs),
                static_cast<unsigned long>(result.adaptiveWindowMs));
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 10000UL) {
    delay(250);
  }
}

bool connectMqtt() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  if (mqttClient.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  return mqttClient.connect(MQTT_CLIENT_ID);
}

void publishSummary(const ExperimentResult &result) {
  if (!connectMqtt()) {
    Serial.println("mqtt=unavailable");
    return;
  }

  String payload;
  payload.reserve(256);
  payload += "{";
  payload += "\"profile\":\"";
  payload += result.profileName;
  payload += "\",\"filter\":\"";
  payload += result.filterName;
  payload += "\",\"window\":";
  payload += result.windowSize;
  payload += ",\"raw_dom\":";
  payload += String(result.rawDominantFrequencyHz, 3);
  payload += ",\"filtered_dom\":";
  payload += String(result.filteredDominantFrequencyHz, 3);
  payload += ",\"tpr\":";
  payload += String(result.detection.truePositiveRate, 3);
  payload += ",\"fpr\":";
  payload += String(result.detection.falsePositiveRate, 3);
  payload += ",\"reduction\":";
  payload += String(result.meanErrorReduction, 2);
  payload += "}";

  mqttClient.publish(TELEMETRY_TOPIC, payload.c_str(), true);
  Serial.printf("mqtt_published topic=%s\n", TELEMETRY_TOPIC);
}

void runBenchmark() {
  ExperimentResult lastResult;
  for (size_t profileIndex = 0; profileIndex < kProfileCount; ++profileIndex) {
    for (size_t filterIndex = 0; filterIndex < kFilterCount; ++filterIndex) {
      for (size_t windowIndex = 0; windowIndex < kWindowCount; ++windowIndex) {
        lastResult = runExperiment(kProfiles[profileIndex], kFilters[filterIndex], kWindowSizes[windowIndex]);
        printResult(lastResult);
        delay(50);
      }
    }
  }

  publishSummary(lastResult);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("bonus receiver benchmark start");
  connectWifi();
  runBenchmark();
}

void loop() {
  delay(1000);
}