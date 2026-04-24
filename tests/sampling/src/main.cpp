#include <Arduino.h>
#include <driver/adc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t kAdcPin = 7; // Heltec WiFi LoRa 32 V4: A6 / GPIO7
constexpr adc1_channel_t kAdcChannel = ADC1_CHANNEL_6;
constexpr bool kUseAnalogRead = true;
constexpr uint32_t kTaskStackWords = 4096;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY + 1;
constexpr BaseType_t kTaskCore = 1;
TaskHandle_t benchmarkTaskHandle = nullptr;

int readSelectedSample() {
    if (kUseAnalogRead) {
        return analogRead(kAdcPin);
    }

    return adc1_get_raw(kAdcChannel);
}

const char *selectedMethodName() {
    return kUseAnalogRead ? "analogRead" : "adc1_get_raw";
}

void runBenchmarkTask(void *pvParameters) {
    (void)pvParameters;

    Serial.printf("started,%s\n", selectedMethodName());

    while (true) {
        (void)readSelectedSample();
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(kAdcPin, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(kAdcPin, ADC_11db);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(kAdcChannel, ADC_ATTEN_DB_12);

    xTaskCreatePinnedToCore(
        runBenchmarkTask,
        "maxSamplingBenchmark",
        kTaskStackWords,
        nullptr,
        kTaskPriority,
        &benchmarkTaskHandle,
        kTaskCore);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
