#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace {

constexpr uint8_t kAdcPin = 7;
constexpr uint8_t kControlPin = 5;
constexpr uint32_t kWindowSeconds = 5;
constexpr uint32_t kWifiReconnectPeriodMs = 5000;
constexpr char kWifiSsid[] = "Vodafone-mango";
constexpr char kWifiPass[] = "Mangoblu2020";
constexpr char kMqttHost[] = "192.168.1.13";
constexpr uint16_t kMqttPort = 1883;
constexpr char kClientId[] = "mqtt_wifi_receiver";
constexpr char kTelemetryTopic[] = "mqtt_wifi/telemetry";
constexpr char kLatencyRequestTopic[] = "mqtt_wifi/latency/request";
constexpr char kLatencyResponseTopic[] = "mqtt_wifi/latency/response";
constexpr uint32_t kLatencyTimeoutMs = 5000;

struct ExperimentCase {
  const char *label;
  uint32_t targetHz;
};

constexpr ExperimentCase kCases[] = {
    {"10k", 10000},
    {"256", 256},
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
hw_timer_t *sampleTimer = nullptr;

volatile bool captureRunning = false;
volatile bool captureComplete = false;
volatile uint32_t expectedSampleCount = 0;
volatile uint32_t capturedSamples = 0;
volatile uint64_t sampleSum = 0;

struct LatencyEcho {
  bool pending = false;
  String payload;
};

LatencyEcho latencyEcho;

struct LatencyProbe {
  bool active = false;
  bool responseReceived = false;
  uint32_t sentUs = 0;
  uint32_t responseUs = 0;
};

LatencyProbe latencyProbe;

void ensureNetwork();
void handleLatencyEcho();
void runLatencyProbe(const ExperimentCase &experiment);

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

  sampleSum += static_cast<uint32_t>(analogRead(kAdcPin));
  ++capturedSamples;
  if (capturedSamples >= expectedSampleCount) {
    captureComplete = true;
    captureRunning = false;
  }
}

void startCapture(uint32_t targetHz, uint32_t sampleCount) {
  expectedSampleCount = sampleCount;
  capturedSamples = 0;
  sampleSum = 0;
  captureComplete = false;
  captureRunning = true;
  configureTimer(targetHz);
  timerWrite(sampleTimer, 0);
  timerAlarmEnable(sampleTimer);
}

void stopCapture() {
  timerAlarmDisable(sampleTimer);
  captureRunning = false;
}

void waitForStartPulse() {
  Serial.println("Waiting for START pulse on GPIO5...");
  while (digitalRead(kControlPin) == LOW) {
    ensureNetwork();
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    delayMicroseconds(50);
  }
  while (digitalRead(kControlPin) == HIGH) {
    ensureNetwork();
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    delayMicroseconds(50);
  }
  Serial.println("START pulse received");
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPass);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

bool connectMqtt() {
  mqttClient.setServer(kMqttHost, kMqttPort);
  mqttClient.setBufferSize(256);
  mqttClient.setKeepAlive(60);
  if (!mqttClient.connected()) {
    if (!mqttClient.connect(kClientId)) {
      return false;
    }
  }

  const bool requestSubscribed = mqttClient.subscribe(kLatencyRequestTopic);
  const bool responseSubscribed = mqttClient.subscribe(kLatencyResponseTopic);
  return requestSubscribed && responseSubscribed;
}

void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, kLatencyRequestTopic) == 0) {
    latencyEcho.payload = String(reinterpret_cast<char *>(payload), length);
    latencyEcho.pending = true;
    return;
  }

  if (strcmp(topic, kLatencyResponseTopic) == 0 && latencyProbe.active) {
    latencyProbe.responseUs = micros();
    latencyProbe.responseReceived = true;
  }
}

String buildLatencyResponse(const String &requestPayload) {
  String response = requestPayload;
  response += ";ack_us=";
  response += String(micros());
  return response;
}

void handleLatencyEcho() {
  if (!latencyEcho.pending) {
    return;
  }

  latencyEcho.pending = false;

  if (!mqttClient.connected() && !connectMqtt()) {
    Serial.println("[MQTT] latency echo skipped, not connected");
    return;
  }

  const String response = buildLatencyResponse(latencyEcho.payload);
  const bool published = mqttClient.publish(kLatencyResponseTopic, response.c_str());
  Serial.printf("mqtt_wifi_receiver_latency,response_published=%s,response=%s\n",
                published ? "yes" : "no",
                response.c_str());
}

void runLatencyProbe(const ExperimentCase &experiment) {
  if (!mqttClient.connected() && !connectMqtt()) {
    Serial.printf("latency_probe,label=%s,rtt_us=0,response_received=no\n", experiment.label);
    return;
  }

  latencyProbe = LatencyProbe{};
  latencyProbe.active = true;

  const uint32_t sentUs = micros();
  const String requestPayload = String("sent_us=") + String(sentUs);
  latencyProbe.sentUs = sentUs;

  if (!mqttClient.publish(kLatencyRequestTopic, requestPayload.c_str())) {
    latencyProbe.active = false;
    Serial.printf("latency_probe,label=%s,rtt_us=0,response_received=no\n", experiment.label);
    return;
  }

  const uint32_t waitStartMs = millis();
  while (!latencyProbe.responseReceived && (millis() - waitStartMs) < kLatencyTimeoutMs) {
    mqttClient.loop();
    handleLatencyEcho();
    delay(1);
  }

  const bool received = latencyProbe.responseReceived;
  const uint32_t rttUs = received ? (latencyProbe.responseUs - latencyProbe.sentUs) : 0;
  Serial.printf("latency_probe,label=%s,rtt_us=%lu,response_received=%s\n",
                experiment.label,
                static_cast<unsigned long>(rttUs),
                received ? "yes" : "no");

  latencyProbe.active = false;
}

void ensureNetwork() {
  static uint32_t lastWifiAttemptMs = 0;
  static uint32_t lastMqttAttemptMs = 0;

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttemptMs >= kWifiReconnectPeriodMs) {
    lastWifiAttemptMs = millis();
    connectWifi();
  }

  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected() && millis() - lastMqttAttemptMs >= kWifiReconnectPeriodMs) {
    lastMqttAttemptMs = millis();
    connectMqtt();
  }
}

String buildPayload(const ExperimentCase &experiment, uint32_t sampleCount, double average, uint32_t captureElapsedUs) {
  String payload;
  payload.reserve(192);
  payload += "label=analog_average";
  payload += ";case=";
  payload += experiment.label;
  payload += ";target_hz=";
  payload += String(experiment.targetHz);
  payload += ";window_s=";
  payload += String(kWindowSeconds);
  payload += ";sample_count=";
  payload += String(sampleCount);
  payload += ";average=";
  payload += String(average, 4);
  payload += ";capture_us=";
  payload += String(captureElapsedUs);
  return payload;
}

void processWindow(const ExperimentCase &experiment, uint32_t captureElapsedUs) {
  const uint32_t sampleCount = expectedSampleCount;
  const double average = sampleCount > 0 ? static_cast<double>(sampleSum) / static_cast<double>(sampleCount) : 0.0;
  const String payload = buildPayload(experiment, sampleCount, average, captureElapsedUs);
  const size_t payloadBytes = payload.length();

  if (!mqttClient.connected() && !connectMqtt()) {
    Serial.println("[MQTT] publish skipped, not connected");
    return;
  }

  const bool published = mqttClient.publish(kTelemetryTopic, payload.c_str());
  Serial.printf(
      "mqtt_wifi_receiver,label=%s,target_hz=%lu,window_s=%lu,sample_count=%lu,average=%.4f,capture_us=%lu,payload_bytes=%lu,published=%s,payload=%s\n",
      experiment.label,
      static_cast<unsigned long>(experiment.targetHz),
      static_cast<unsigned long>(kWindowSeconds),
      static_cast<unsigned long>(sampleCount),
      average,
      static_cast<unsigned long>(captureElapsedUs),
      static_cast<unsigned long>(payloadBytes),
      published ? "yes" : "no",
      payload.c_str());
}

void runCase(const ExperimentCase &experiment) {
  expectedSampleCount = experiment.targetHz * kWindowSeconds;
  if (expectedSampleCount == 0) {
    Serial.printf("mqtt_wifi_receiver,label=%s,status=invalid_sample_count\n", experiment.label);
    return;
  }

  configureTimer(experiment.targetHz);
  ensureNetwork();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  waitForStartPulse();
  startCapture(experiment.targetHz, expectedSampleCount);

  const uint32_t captureStartUs = micros();
  while (!captureComplete) {
    ensureNetwork();
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    yield();
  }

  const uint32_t captureElapsedUs = micros() - captureStartUs;
  stopCapture();
  processWindow(experiment, captureElapsedUs);
  runLatencyProbe(experiment);
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
  pinMode(kControlPin, INPUT_PULLUP);
  sampleTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(sampleTimer, &onSampleTimer, true);

  mqttClient.setCallback(onMqttMessage);
  connectWifi();
  connectMqtt();

  Serial.println("MQTT WiFi receiver starting");
  runAllExperiments();
  Serial.println("MQTT WiFi receiver completed");
}

void loop() {
  ensureNetwork();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  handleLatencyEcho();
  delay(10);
}
