#pragma once

#include <Arduino.h>

#include <cmath>
#include <cstdint>

namespace bonus {

enum class WaveformType : uint8_t {
  Sine = 0,
  Square = 1,
  Triangle = 2,
};

static constexpr float kTwoPi = 6.28318530717958647692f;

inline const char *waveformName(WaveformType waveformType) {
  switch (waveformType) {
    case WaveformType::Square:
      return "square";
    case WaveformType::Triangle:
      return "triangle";
    case WaveformType::Sine:
    default:
      return "sine";
  }
}

inline float sineWaveSample(uint32_t sampleIndex, uint32_t sampleRateHz, float frequencyHz = 5.0f) {
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  return 4.0f * std::sin(kTwoPi * frequencyHz * time);
}

inline float squareWaveSample(uint32_t sampleIndex, uint32_t sampleRateHz, float frequencyHz = 5.0f) {
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  const float phase = std::fmod(frequencyHz * time, 1.0f);
  return phase < 0.5f ? 4.0f : -4.0f;
}

inline float triangleWaveSample(uint32_t sampleIndex, uint32_t sampleRateHz, float frequencyHz = 5.0f) {
  const float time = static_cast<float>(sampleIndex) / static_cast<float>(sampleRateHz);
  const float phase = std::fmod(frequencyHz * time, 1.0f);
  if (phase < 0.25f) {
    return -4.0f + 32.0f * phase;
  }
  if (phase < 0.75f) {
    return 4.0f - 16.0f * (phase - 0.25f);
  }
  return -4.0f + 32.0f * (phase - 0.75f);
}

inline float generateWaveformSample(WaveformType waveformType,
                                    uint32_t sampleIndex,
                                    uint32_t sampleRateHz,
                                    float frequencyHz = 5.0f) {
  switch (waveformType) {
    case WaveformType::Square:
      return squareWaveSample(sampleIndex, sampleRateHz, frequencyHz);
    case WaveformType::Triangle:
      return triangleWaveSample(sampleIndex, sampleRateHz, frequencyHz);
    case WaveformType::Sine:
    default:
      return sineWaveSample(sampleIndex, sampleRateHz, frequencyHz);
  }
}

}  // namespace bonus