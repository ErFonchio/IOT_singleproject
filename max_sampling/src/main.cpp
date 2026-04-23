#include <Arduino.h>
#include <driver/adc.h>

namespace {

constexpr uint8_t kAdcPin = 7; // Heltec WiFi LoRa 32 V4: A6 / GPIO7
constexpr adc1_channel_t kAdcChannel = ADC1_CHANNEL_6;
constexpr uint32_t kSamplesPerRun = 10000;
constexpr uint8_t kRepeatsPerMethod = 5;

struct BenchmarkResult {
    const char *method;
    uint32_t runIndex;
    uint32_t samples;
    uint32_t elapsedUs;
    float sampleRateHz;
    int lastValue;
};

BenchmarkResult results[2 * kRepeatsPerMethod];

BenchmarkResult runAnalogReadBenchmark(uint32_t runIndex) {
    int lastValue = 0;
    const uint32_t startUs = micros();
    for (uint32_t sampleIndex = 0; sampleIndex < kSamplesPerRun; ++sampleIndex) {
        lastValue = analogRead(kAdcPin);
    }
    const uint32_t elapsedUs = micros() - startUs;
    const float sampleRateHz = elapsedUs > 0 ? (kSamplesPerRun * 1000000.0f) / elapsedUs : 0.0f;

    return {"analogRead", runIndex, kSamplesPerRun, elapsedUs, sampleRateHz, lastValue};
}

BenchmarkResult runRawReadBenchmark(uint32_t runIndex) {
    int lastValue = 0;
    const uint32_t startUs = micros();
    for (uint32_t sampleIndex = 0; sampleIndex < kSamplesPerRun; ++sampleIndex) {
        lastValue = adc1_get_raw(kAdcChannel);
    }
    const uint32_t elapsedUs = micros() - startUs;
    const float sampleRateHz = elapsedUs > 0 ? (kSamplesPerRun * 1000000.0f) / elapsedUs : 0.0f;

    return {"adc1_get_raw", runIndex, kSamplesPerRun, elapsedUs, sampleRateHz, lastValue};
}

void printResult(const BenchmarkResult &result) {
    Serial.printf("sampling_benchmark,method=%s,run=%lu,samples=%lu,elapsed_us=%lu,rate_hz=%.2f,last=%d\n",
                                result.method,
                                static_cast<unsigned long>(result.runIndex),
                                static_cast<unsigned long>(result.samples),
                                static_cast<unsigned long>(result.elapsedUs),
                                result.sampleRateHz,
                                result.lastValue);
}

void printSummary(const char *method, const BenchmarkResult *methodResults, uint8_t count) {
    uint64_t totalElapsedUs = 0;
    float minRateHz = 0.0f;
    float maxRateHz = 0.0f;
    int lastValue = 0;

    for (uint8_t index = 0; index < count; ++index) {
        totalElapsedUs += methodResults[index].elapsedUs;
        lastValue = methodResults[index].lastValue;
        if (index == 0 || methodResults[index].sampleRateHz < minRateHz) {
            minRateHz = methodResults[index].sampleRateHz;
        }
        if (index == 0 || methodResults[index].sampleRateHz > maxRateHz) {
            maxRateHz = methodResults[index].sampleRateHz;
        }
    }

    const float averageElapsedUs = static_cast<float>(totalElapsedUs) / count;
    const float averageRateHz = averageElapsedUs > 0.0f ? (kSamplesPerRun * 1000000.0f) / averageElapsedUs : 0.0f;

    Serial.printf("sampling_benchmark_summary,method=%s,runs=%u,total_elapsed_us=%lu,avg_elapsed_us=%.2f,avg_rate_hz=%.2f,min_rate_hz=%.2f,max_rate_hz=%.2f,last=%d\n",
                                method,
                                static_cast<unsigned int>(count),
                                static_cast<unsigned long>(totalElapsedUs),
                                averageElapsedUs,
                                averageRateHz,
                                minRateHz,
                                maxRateHz,
                                lastValue);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("max_sampling benchmark starting");
    Serial.printf("board=Heltec WiFi LoRa 32 V4, adc_pin=%u, adc_channel=%d\n", kAdcPin, static_cast<int>(kAdcChannel));
    Serial.printf("samples_per_run=%lu, repeats_per_method=%u\n",
                                static_cast<unsigned long>(kSamplesPerRun),
                                static_cast<unsigned int>(kRepeatsPerMethod));

    pinMode(kAdcPin, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(kAdcPin, ADC_11db);

    Serial.println("experiment=analogRead");
    for (uint8_t runIndex = 0; runIndex < kRepeatsPerMethod; ++runIndex) {
        results[runIndex] = runAnalogReadBenchmark(runIndex + 1);
        printResult(results[runIndex]);
        delay(250);
    }
    printSummary("analogRead", results, kRepeatsPerMethod);

    Serial.println("experiment=adc1_get_raw");
    for (uint8_t runIndex = 0; runIndex < kRepeatsPerMethod; ++runIndex) {
        results[kRepeatsPerMethod + runIndex] = runRawReadBenchmark(runIndex + 1);
        printResult(results[kRepeatsPerMethod + runIndex]);
        delay(250);
    }
    printSummary("adc1_get_raw", &results[kRepeatsPerMethod], kRepeatsPerMethod);

    Serial.println("max_sampling benchmark completed");
}

void loop() {
    delay(1000);
}
