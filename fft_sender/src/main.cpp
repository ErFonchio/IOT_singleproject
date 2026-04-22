#include <Arduino.h>
#include <driver/dac.h>

#define SAMPLING_FREQUENCY 10000 // Hz

struct Component {
  double amplitude;
  double freq;
  double phase;
  double phaseInc;
};

Component components[] = {
  {2.0, 30.0, 0.0, 0.0},
  {4.0, 60.0, 0.0, 0.0},
};

const int NUM_COMPONENTS = sizeof(components) / sizeof(components[0]);
const double OUTPUT_SCALE = 20.0; // scala la somma dei seni al range DAC

volatile uint32_t sampleCount = 0;
volatile uint8_t lastDacValue = 128;
hw_timer_t *timer = NULL;

void IRAM_ATTR onTimer() {
    double sample = 0.0;
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        sample += components[i].amplitude * sin(components[i].phase);
        components[i].phase += components[i].phaseInc;
        if (components[i].phase >= 2.0 * M_PI) components[i].phase -= 2.0 * M_PI;
    }

    int dac_val = (int)(128 + OUTPUT_SCALE * sample);
    if (dac_val < 0) dac_val = 0;
    if (dac_val > 255) dac_val = 255;

    lastDacValue = (uint8_t)dac_val;
    dac_output_voltage(DAC_CHANNEL_1, lastDacValue);
    sampleCount++;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("Sender avviato");
    Serial.printf("Sampling: %d Hz\n", SAMPLING_FREQUENCY);
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        components[i].phaseInc = (2.0 * M_PI * components[i].freq) / SAMPLING_FREQUENCY;
        Serial.printf("Componente %d: amp=%.1f freq=%.1f Hz\n", i, components[i].amplitude, components[i].freq);
    }

    dac_output_enable(DAC_CHANNEL_1);

    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, 1000000 / SAMPLING_FREQUENCY, true);
    timerAlarmEnable(timer);
}

void loop() {
    static unsigned long lastPrint = 0;
    unsigned long now = millis();
    if (now - lastPrint >= 853) {
        lastPrint = now;
        Serial.printf("Samples inviati: %u, ultimo DAC: %u\n", sampleCount, lastDacValue);
    }
    delay(10);
}
