#pragma once

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace bonus {

enum class SignalProfile : uint8_t {
  CleanSine = 0,
  NoisyMix = 1,
  NoisyAnomaly = 2,
};

enum class FilterMode : uint8_t {
  None = 0,
  ZScore = 1,
  Hampel = 2,
};

struct DetectionStats {
  uint32_t truePositive = 0;
  uint32_t falsePositive = 0;
  uint32_t trueNegative = 0;
  uint32_t falseNegative = 0;

  float truePositiveRate = 0.0f;
  float falsePositiveRate = 0.0f;
};

struct WindowStats {
  float mean = 0.0f;
  float stddev = 0.0f;
};

struct FilterOutput {
  uint32_t elapsedUs = 0;
  DetectionStats detection;
  float meanAbsoluteError = 0.0f;
  float meanAbsoluteErrorReduction = 0.0f;
};

static constexpr uint32_t kDefaultSignalRateHz = 10000;
static constexpr float kDefaultNoiseSigma = 0.2f;
static constexpr uint32_t kDefaultSpikePeriodSamples = 256U;
static constexpr uint32_t kDefaultSignalSeed = 0x4b1d4f2du;
static constexpr float kTwoPi = 6.28318530717958647692f;

inline float deterministicNoise(uint32_t sampleIndex, uint32_t sampleRateHz, float sigma) {
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  const float slowWobble = 0.65f * std::sin(kTwoPi * 37.0f * time);
  const float fastRipple = 0.35f * std::sin(kTwoPi * 113.0f * time + 0.3f);
  const float envelope = 0.5f + 0.5f * std::sin(kTwoPi * 1.5f * time + 1.2f);
  return sigma * envelope * (slowWobble + fastRipple);
}

inline bool isDeterministicSpike(uint32_t sampleIndex, uint32_t periodSamples = kDefaultSpikePeriodSamples) {
  return periodSamples != 0U && (sampleIndex % periodSamples) == 0U;
}

inline float deterministicSpike(uint32_t sampleIndex) {
  const float sign = ((sampleIndex / kDefaultSpikePeriodSamples) % 2U) == 0U ? 1.0f : -1.0f;
  const float magnitude = 7.0f + 3.0f * std::sin(kTwoPi * 0.5f * static_cast<float>(sampleIndex) / static_cast<float>(kDefaultSpikePeriodSamples));
  return sign * magnitude;
}

inline float cleanSineSample(uint32_t sampleIndex, uint32_t sampleRateHz) {
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  return 4.0f * std::sin(kTwoPi * 5.0f * time);
}

inline float noisyMixSample(uint32_t sampleIndex,
                            uint32_t sampleRateHz,
                            uint32_t seed,
                            float noiseSigma = kDefaultNoiseSigma) {
  (void)seed;
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  const float base = 2.0f * std::sin(kTwoPi * 3.0f * time) + 4.0f * std::sin(kTwoPi * 5.0f * time);
  return base + deterministicNoise(sampleIndex, sampleRateHz, noiseSigma);
}

inline float noisyAnomalySample(uint32_t sampleIndex,
                                uint32_t sampleRateHz,
                                uint32_t seed,
                                float noiseSigma = kDefaultNoiseSigma,
                                float anomalyProbability = 0.0f,
                                bool *isAnomaly = nullptr) {
  (void)seed;
  (void)anomalyProbability;
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  float sample = 2.0f * std::sin(kTwoPi * 3.0f * time) + 4.0f * std::sin(kTwoPi * 5.0f * time);
  sample += deterministicNoise(sampleIndex, sampleRateHz, noiseSigma);

  const bool anomaly = isDeterministicSpike(sampleIndex);
  if (isAnomaly != nullptr) {
    *isAnomaly = anomaly;
  }

  if (anomaly) {
    sample += deterministicSpike(sampleIndex);
  }

  return sample;
}

inline float expectedCleanSample(SignalProfile profile,
                                 uint32_t sampleIndex,
                                 uint32_t sampleRateHz) {
  switch (profile) {
    case SignalProfile::CleanSine:
      return cleanSineSample(sampleIndex, sampleRateHz);
    case SignalProfile::NoisyMix:
    case SignalProfile::NoisyAnomaly:
    default:
      return 2.0f * std::sin(kTwoPi * 3.0f * static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz))
           + 4.0f * std::sin(kTwoPi * 5.0f * static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz));
  }
}

inline float generateSample(SignalProfile profile,
                            uint32_t sampleIndex,
                            uint32_t sampleRateHz,
                            uint32_t seed = kDefaultSignalSeed,
                            float noiseSigma = kDefaultNoiseSigma,
                            float anomalyProbability = 0.0f,
                            bool *isAnomaly = nullptr) {
  (void)seed;
  switch (profile) {
    case SignalProfile::CleanSine:
      if (isAnomaly != nullptr) {
        *isAnomaly = false;
      }
      return cleanSineSample(sampleIndex, sampleRateHz);
    case SignalProfile::NoisyMix:
      if (isAnomaly != nullptr) {
        *isAnomaly = false;
      }
      return noisyMixSample(sampleIndex, sampleRateHz, seed, noiseSigma);
    case SignalProfile::NoisyAnomaly:
    default:
      return noisyAnomalySample(sampleIndex,
                                sampleRateHz,
                                seed,
                                noiseSigma,
                                anomalyProbability,
                                isAnomaly);
  }
}

inline WindowStats computeWindowStats(const float *samples, size_t count) {
  WindowStats stats;
  if (samples == nullptr || count == 0) {
    return stats;
  }

  double mean = 0.0;
  for (size_t index = 0; index < count; ++index) {
    mean += samples[index];
  }
  mean /= static_cast<double>(count);

  double variance = 0.0;
  for (size_t index = 0; index < count; ++index) {
    const double delta = samples[index] - mean;
    variance += delta * delta;
  }
  variance /= static_cast<double>(count > 1 ? count - 1 : 1);

  stats.mean = static_cast<float>(mean);
  stats.stddev = static_cast<float>(std::sqrt(variance));
  return stats;
}

inline float medianOfCopy(float *values, size_t count) {
  if (count == 0) {
    return 0.0f;
  }

  for (size_t index = 1; index < count; ++index) {
    const float value = values[index];
    size_t position = index;
    while (position > 0 && values[position - 1] > value) {
      values[position] = values[position - 1];
      --position;
    }
    values[position] = value;
  }

  if ((count % 2U) == 0U) {
    return 0.5f * (values[count / 2U - 1U] + values[count / 2U]);
  }
  return values[count / 2U];
}

inline DetectionStats computeDetectionStats(const bool *truth, const bool *detected, size_t count) {
  DetectionStats stats;
  if (truth == nullptr || detected == nullptr || count == 0) {
    return stats;
  }

  for (size_t index = 0; index < count; ++index) {
    if (truth[index] && detected[index]) {
      ++stats.truePositive;
    } else if (!truth[index] && detected[index]) {
      ++stats.falsePositive;
    } else if (!truth[index] && !detected[index]) {
      ++stats.trueNegative;
    } else {
      ++stats.falseNegative;
    }
  }

  const float positiveCount = static_cast<float>(stats.truePositive + stats.falseNegative);
  const float negativeCount = static_cast<float>(stats.trueNegative + stats.falsePositive);

  stats.truePositiveRate = positiveCount > 0.0f ? static_cast<float>(stats.truePositive) / positiveCount : 0.0f;
  stats.falsePositiveRate = negativeCount > 0.0f ? static_cast<float>(stats.falsePositive) / negativeCount : 0.0f;
  return stats;
}

inline void applyZScoreFilter(const float *input,
                             float *output,
                             bool *detected,
                             size_t count,
                             float threshold = 3.0f) {
  const WindowStats stats = computeWindowStats(input, count);
  const float stddev = std::max(stats.stddev, 1.0e-6f);

  for (size_t index = 0; index < count; ++index) {
    const float zScore = std::fabs(input[index] - stats.mean) / stddev;
    const bool isOutlier = zScore > threshold;
    if (detected != nullptr) {
      detected[index] = isOutlier;
    }
    output[index] = isOutlier ? stats.mean : input[index];
  }
}

inline void applyHampelFilter(const float *input,
                              float *output,
                              bool *detected,
                              size_t count,
                              size_t radius = 8,
                              float threshold = 3.0f) {
  constexpr size_t kMaxLocalWindow = 63;
  float localWindow[kMaxLocalWindow];

  const size_t windowSize = std::min(count, radius * 2U + 1U);
  if (windowSize == 0 || windowSize > kMaxLocalWindow) {
    for (size_t index = 0; index < count; ++index) {
      if (detected != nullptr) {
        detected[index] = false;
      }
      output[index] = input[index];
    }
    return;
  }

  for (size_t index = 0; index < count; ++index) {
    const size_t begin = (index < radius) ? 0U : index - radius;
    const size_t end = std::min(count, index + radius + 1U);
    const size_t localCount = end - begin;

    for (size_t localIndex = 0; localIndex < localCount; ++localIndex) {
      localWindow[localIndex] = input[begin + localIndex];
    }

    const float median = medianOfCopy(localWindow, localCount);

    for (size_t localIndex = 0; localIndex < localCount; ++localIndex) {
      localWindow[localIndex] = std::fabs(localWindow[localIndex] - median);
    }
    const float mad = medianOfCopy(localWindow, localCount);
    const float scaledMad = std::max(1.0e-6f, 1.4826f * mad);
    const float deviation = std::fabs(input[index] - median);
    const bool isOutlier = deviation > threshold * scaledMad;

    if (detected != nullptr) {
      detected[index] = isOutlier;
    }
    output[index] = isOutlier ? median : input[index];
  }
}

inline float meanAbsoluteError(const float *a, const float *b, size_t count) {
  if (a == nullptr || b == nullptr || count == 0) {
    return 0.0f;
  }

  double sum = 0.0;
  for (size_t index = 0; index < count; ++index) {
    sum += std::fabs(a[index] - b[index]);
  }
  return static_cast<float>(sum / static_cast<double>(count));
}

inline float percentageReduction(float rawValue, float filteredValue) {
  if (rawValue <= 0.0f) {
    return 0.0f;
  }
  return 100.0f * (rawValue - filteredValue) / rawValue;
}

inline uint32_t clampAdaptiveRateHz(float dominantFrequencyHz, uint32_t maxRateHz) {
  const uint32_t adaptiveRate = static_cast<uint32_t>(dominantFrequencyHz * 2.0f + 0.5f);
  if (adaptiveRate < 1U) {
    return 1U;
  }
  if (adaptiveRate > maxRateHz) {
    return maxRateHz;
  }
  return adaptiveRate;
}

}  // namespace bonus