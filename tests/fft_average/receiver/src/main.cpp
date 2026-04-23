#include <Arduino.h>
#include <arduinoFFT.h>

namespace {

constexpr uint8_t kAdcPin = 7;
constexpr uint8_t kControlPin = 5;

struct ExperimentCase {
  const char *label;
  uint32_t targetHz;
  uint32_t durationUs;
};

constexpr ExperimentCase kCases[] = {
    {"10k", 10000, 102400},
    {"256", 256, 4000000},
};

constexpr size_t kMaxBufferSampleCount = 1024;

uint16_t sampleBuffer[kMaxBufferSampleCount];
uint16_t orderedSamples[kMaxBufferSampleCount];
double fftReal[kMaxBufferSampleCount];
double fftImag[kMaxBufferSampleCount];

hw_timer_t *sampleTimer = nullptr;
volatile size_t writeIndex = 0;
volatile size_t capturedSamples = 0;
volatile size_t expectedSampleCount = 0;
volatile bool captureRunning = false;
volatile bool bufferFull = false;

void IRAM_ATTR onSampleTimer();

void configureAdc() {
  pinMode(kAdcPin, INPUT);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

void configureTimer(uint32_t targetHz) {
  timerAlarmWrite(sampleTimer, 1000000UL / targetHz, true);
}

void IRAM_ATTR onSampleTimer() {
  if (!captureRunning) {
    return;
  }

  sampleBuffer[writeIndex] = static_cast<uint16_t>(analogRead(kAdcPin));
  writeIndex = (writeIndex + 1) % expectedSampleCount;
  if (capturedSamples < expectedSampleCount) {
    ++capturedSamples;
    if (capturedSamples >= expectedSampleCount) {
      bufferFull = true;
    }
  } else {
    bufferFull = true;
  }
}

void startCapture() {
  writeIndex = 0;
  capturedSamples = 0;
  bufferFull = false;
  captureRunning = true;
  timerWrite(sampleTimer, 0);
  timerAlarmEnable(sampleTimer);
}

void stopCapture() {
  timerAlarmDisable(sampleTimer);
  captureRunning = false;
}

void waitForStartPulse() {
  Serial.println("Waiting for START pulse on GPIO5...");
  while (digitalRead(kControlPin) == HIGH) {
    delayMicroseconds(50);
  }
  while (digitalRead(kControlPin) == LOW) {
    delayMicroseconds(50);
  }
  Serial.println("START pulse received");
}

void signalFftStart() {
  digitalWrite(kControlPin, LOW);
}

void signalFftDone() {
  digitalWrite(kControlPin, HIGH);
}

void copyCircularBuffer() {
  size_t oldestIndex = writeIndex;
  for (size_t index = 0; index < expectedSampleCount; ++index) {
    orderedSamples[index] = sampleBuffer[(oldestIndex + index) % expectedSampleCount];
  }
}

void processWindow(const ExperimentCase &experiment) {
  uint32_t fftStartUs = micros();

  double signalSum = 0.0;
  for (size_t index = 0; index < expectedSampleCount; ++index) {
    signalSum += orderedSamples[index];
  }
  double signalAverage = signalSum / static_cast<double>(expectedSampleCount);

  for (size_t index = 0; index < expectedSampleCount; ++index) {
    fftReal[index] = orderedSamples[index] - signalAverage;
    fftImag[index] = 0.0;
  }

  ArduinoFFT<double> FFT(fftReal, fftImag, expectedSampleCount, experiment.targetHz);
  FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();
  double peakFrequencyHz = FFT.majorPeak();

  uint32_t fftElapsedUs = micros() - fftStartUs;
  float fftElapsedMs = fftElapsedUs / 1000.0f;
  uint32_t derivedSamples = (experiment.durationUs * experiment.targetHz) / 1000000UL;

  Serial.printf(
      "fft_average_receiver,label=%s,target_hz=%lu,duration_us=%lu,samples=%lu,buffer_full=%s,fft_elapsed_us=%lu,fft_elapsed_ms=%.2f,signal_avg=%.2f,fft_peak_hz=%.2f\n",
      experiment.label,
      static_cast<unsigned long>(experiment.targetHz),
      static_cast<unsigned long>(experiment.durationUs),
      static_cast<unsigned long>(derivedSamples),
      bufferFull ? "yes" : "no",
      static_cast<unsigned long>(fftElapsedUs),
      fftElapsedMs,
      signalAverage,
      peakFrequencyHz);
}

void runCase(const ExperimentCase &experiment) {
  expectedSampleCount = (experiment.durationUs * experiment.targetHz) / 1000000UL;
  if (expectedSampleCount == 0 || expectedSampleCount > kMaxBufferSampleCount) {
    Serial.printf(
        "fft_average_receiver,label=%s,target_hz=%lu,duration_us=%lu,status=invalid_sample_count,expected_samples=%lu\n",
        experiment.label,
        static_cast<unsigned long>(experiment.targetHz),
        static_cast<unsigned long>(experiment.durationUs),
        static_cast<unsigned long>(expectedSampleCount));
    return;
  }

  configureTimer(experiment.targetHz);
  startCapture();

  waitForStartPulse();

  if (!bufferFull) {
    Serial.printf(
        "fft_average_receiver,label=%s,target_hz=%lu,duration_us=%lu,status=buffer_not_full,captured_samples=%lu,expected_samples=%lu\n",
        experiment.label,
        static_cast<unsigned long>(experiment.targetHz),
        static_cast<unsigned long>(experiment.durationUs),
        static_cast<unsigned long>(capturedSamples),
        static_cast<unsigned long>(expectedSampleCount));
  }

  while (!bufferFull) {
    yield();
  }

  signalFftStart();
  copyCircularBuffer();
  processWindow(experiment);
  signalFftDone();
  stopCapture();
}

void runAllExperiments() {
  for (const ExperimentCase &experiment : kCases) {
    runCase(experiment);
    delay(250);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }

  configureAdc();
  pinMode(kControlPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kControlPin, HIGH);
  sampleTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(sampleTimer, &onSampleTimer, true);

  Serial.println("FFT average receiver starting");
  runAllExperiments();
  Serial.println("FFT average receiver completed");
}

void loop() {
  delay(1000);
}
