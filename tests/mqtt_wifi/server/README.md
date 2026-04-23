# MQTT Server

Local Mosquitto broker for the `mqtt_wifi` experiments.

## Run with Docker

From this directory:

```bash
docker compose up -d
```

The broker listens on:
- MQTT: `1883`
- WebSocket: `9001`

## Files

- `docker-compose.yml` - Mosquitto service and persistent volumes
- `mosquitto.conf` - broker configuration

## Notes

- The broker allows anonymous connections for local development.
- Update the ESP32 Wi-Fi credentials and the broker IP in the firmware before running the experiment.
- If you need to inspect traffic, you can use `docker logs -f mqtt_wifi_mosquitto`.
