#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/adc.h>

#include <esp_wifi.h>
#include <esp_bt.h>

namespace {

constexpr uint8_t kAdcPin = 7; // Heltec WiFi LoRa 32 V4: A6 / GPIO7
constexpr adc1_channel_t kAdcChannel = ADC1_CHANNEL_6;
constexpr uint32_t kSamplePeriodUs = 4000;
constexpr uint32_t kStartupDelayMs = 2000;

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

} // namespace

void setup() {
    Serial.begin(115200);
    delay(kStartupDelayMs);
    disableRadios();
    
    pinMode(kAdcPin, INPUT);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(kAdcChannel, ADC_ATTEN_DB_12);

    Serial.println("1. Sveglio e pronto per adc1_get_raw + light sleep a 250Hz");
    Serial.flush();

    uint32_t nextSampleUs = micros();
    while (true) {
        (void)adc1_get_raw(kAdcChannel);
        nextSampleUs += kSamplePeriodUs;
        waitUntilDeadline(nextSampleUs);
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}