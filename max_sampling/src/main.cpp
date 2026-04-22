#include <Arduino.h>

#define ADC_PIN 7 // V4 ADC pin range on GPIO1-7

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Receiver starting...");
    pinMode(ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // range 0V - 3.1V
    Serial.print("Using ADC pin: ");
    Serial.println(ADC_PIN);
}

void loop() {
    const int SAMPLES = 10000;
    unsigned long start = micros();
    int rawValue = 0;
    for (int i = 0; i < SAMPLES; i++) {
        rawValue = analogRead(ADC_PIN);
    }
    unsigned long elapsed = micros() - start;
    float sampleRate = elapsed > 0 ? (SAMPLES * 1000000.0f / elapsed) : 0;

    Serial.printf("ADC last=%d  rate=%.0f Hz  elapsed=%lums\n", rawValue, sampleRate, elapsed / 1000);
    delay(1000);

    // max rate is 16640 Hz
}
