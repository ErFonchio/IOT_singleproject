# mqtt_wifi

Local MQTT workflow for the ESP32 experiments.

## Structure

- `server/` - Dockerized Mosquitto broker for local testing
- `sender/` - Analog signal generator and energy measurement
- `receiver/` - Signal acquisition, processing, and MQTT publish

## Local broker

Start Mosquitto with:

```bash
cd mqtt_wifi/server
docker compose up -d
```

The broker exposes:
- MQTT TCP on `1883`
- MQTT over WebSocket on `9001`

## Firmware notes

- Update the receiver Wi-Fi SSID/password and broker IP before flashing.
- The receiver publishes the aggregated value to the local broker.
- The sender keeps the INA219-based energy measurement around the experiment window.

## Measurement targets

- Energy on the sender
- End-to-end latency from capture start to publish
- Payload size in bytes and message count per window
