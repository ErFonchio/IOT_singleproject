#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/adc.h>

#include <esp_wifi.h>
#include <esp_bt.h>

namespace {

constexpr uint8_t kAdcPin = 7; // Heltec WiFi LoRa 32 V4: A6 / GPIO7
constexpr adc1_channel_t kAdcChannel = ADC1_CHANNEL_6;
constexpr uint32_t kStartupDelayMs = 2000;
constexpr uint32_t kFrequencyOptionsHz[] = {250, 500, 1000, 2000};

enum class ReadMode {
    Raw,
    Analog,
};

constexpr ReadMode kReadMode = ReadMode::Analog;
constexpr uint8_t kFrequencySelection = 1; // 0=250 Hz, 1=500 Hz, 2=1000 Hz, 3=2000 Hz

constexpr uint32_t selectedFrequencyHz() {
    return kFrequencyOptionsHz[kFrequencySelection];
}

void disableRadios() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    esp_wifi_deinit();
    btStop();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
}

void waitUntilDeadline(uint32_t deadlineUs) {
    uint32_t nowUs = micros();
    if (nowUs < deadlineUs) {
        uint64_t sleepUs = static_cast<uint64_t>(deadlineUs - nowUs);
        if (sleepUs > 0) {
            esp_sleep_enable_timer_wakeup(sleepUs);
            esp_light_sleep_start();
        }
    }
}

int readSelectedSample(ReadMode readMode) {
    if (readMode == ReadMode::Raw) {
        return adc1_get_raw(kAdcChannel);
    }

    return analogRead(kAdcPin);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(kStartupDelayMs);

    const ReadMode readMode = kReadMode;
    const uint32_t frequencyHz = selectedFrequencyHz();
    const uint32_t samplePeriodUs = 1000000UL / frequencyHz;

    disableRadios();

    pinMode(kAdcPin, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(kAdcPin, ADC_11db);

    if (readMode == ReadMode::Raw) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(kAdcChannel, ADC_ATTEN_DB_12);
    }

    Serial.printf("Pronto: %s + light sleep a %lu Hz\n",
                  readMode == ReadMode::Raw ? "raw read" : "analogRead",
                  static_cast<unsigned long>(frequencyHz));
    Serial.flush();

    uint32_t nextSampleUs = micros();
    while (true) {
        (void)readSelectedSample(readMode);
        nextSampleUs += samplePeriodUs;
        waitUntilDeadline(nextSampleUs);
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}