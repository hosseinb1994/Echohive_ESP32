# EcoHive — ESP32 IoT Gateway Firmware

ESP32 firmware built with **ESP-IDF v5.5** that acts as the IoT gateway in the EcoHive environmental monitoring pipeline. Receives sensor data from an STM32F401RE over SPI, validates it, and publishes a JSON payload to **AWS IoT Core** via MQTT over TLS 1.2.

> Part of the **EcoHive IoT pipeline**: [STM32 firmware](https://github.com/YOUR_USERNAME/ecohive-stm32) → SPI → ESP32 (this repo) → MQTT/TLS → AWS IoT Core

---

## System architecture

![ESP32 firmware architecture](esp32_diagram.svg)

---

## Hardware

| Component | GPIO | Purpose |
|---|---|---|
| SPI MOSI | GPIO 23 | Data from STM32 (PB15) |
| SPI MISO | GPIO 19 | Data to STM32 (PB14) |
| SPI SCK | GPIO 18 | Clock from STM32 (PB10) |
| SPI CS | GPIO 5 | Chip select from STM32 (PC0) |

SPI configuration: **Mode 0, DMA, 21 bytes per frame**.

---

## Software architecture

### Data pipeline per frame

```
SPI slave receive (DMA)
        ↓
XOR checksum verify — discard frame on mismatch
        ↓
memcpy to SensorData_t struct
        ↓
snprintf → JSON payload
        ↓
esp_mqtt_client_publish (QoS 1, topic: stm32/sensors)
        ↓
AWS IoT Core
```

### SPI frame format

Must exactly match the STM32 packed struct:

```c
typedef struct __attribute__((packed)) {
    float    mcu_temperature;
    float    mq9_ppm;
    float    am2302_temperature;
    float    am2302_humidity;
    uint32_t timestamp;
    uint8_t  checksum;          // XOR of all previous 20 bytes
} SensorData_t;                 // 21 bytes total
```

### JSON payload published to AWS

```json
{
  "device": "stm32-nucleo",
  "mcu_temp": 28.91,
  "mq9_ppm": 5.7,
  "dht_temp": 25.7,
  "dht_humidity": 58.1,
  "timestamp": 12400
}
```

Topic: `stm32/sensors` — published every 2 seconds.

### WiFi

- Mode: Station (STA)
- Auth: WPA2-PSK
- Auto-reconnect: up to 5 retries with exponential backoff via FreeRTOS event group

### TLS / MQTT security

- Transport: MQTT over TLS 1.2, port **8883**
- Authentication: **X.509 mutual authentication**
  - Device certificate (`certificate.pem.crt`)
  - Device private key (`private.pem.key`)
  - Amazon Root CA 1 (`root_ca.pem`)
- Certificates embedded as binary blobs via ESP-IDF `EMBED_FILES` — zero runtime file I/O
- AWS IoT policy grants: `iot:Connect`, `iot:Publish`

---

## AWS IoT Core setup

1. Create a **Thing** named `stm32-nucleo` in AWS IoT Core
2. Auto-generate a certificate and download all three files into `main/certs/`
3. Attach the following policy to your certificate:

```json
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Action": ["iot:Connect", "iot:Publish", "iot:Subscribe", "iot:Receive"],
    "Resource": "*"
  }]
}
```

4. Copy your **Device data endpoint** from AWS IoT Core → Settings and paste it into `app_main.c`:

```c
#define AWS_IOT_ENDPOINT  "your-endpoint-ats.iot.eu-north-1.amazonaws.com"
```

---

## Build and flash

### Requirements

- ESP-IDF v5.5 ([installation guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/))
- ESP32 DevKit board

### Setup

```bash
git clone https://github.com/YOUR_USERNAME/ecohive-esp32
cd ecohive-esp32

# Place your AWS certificates here:
# main/certs/root_ca.pem
# main/certs/certificate.pem.crt
# main/certs/private.pem.key

# Set WiFi credentials and AWS endpoint in main/app_main.c
```

### Build and flash

```bash
. $HOME/esp/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

### Expected monitor output

```
I (7006) ESP32_AWS: WiFi connected!
I (7016) ESP32_AWS: MQTT connected to AWS IoT
I (7026) ESP32_AWS: SPI slave ready, waiting for STM32...
I (9200) ESP32_AWS: MCU: 28.9C | MQ9: 5.7ppm | DHT: 25.7C | Hum: 58.1%
I (9210) ESP32_AWS: Published to stm32/sensors (msg_id=1)
```

### Verify in AWS console

AWS IoT Core → **MQTT test client** → Subscribe to `stm32/sensors` → live JSON appears every 2 seconds.

---

## Key implementation notes

**Checksum validation** — every received SPI frame is XOR-verified before processing. Frames with checksum mismatch are silently discarded and logged with `ESP_LOGW`.

**Certificate embedding** — the three PEM files are embedded at link time via `EMBED_FILES` in `CMakeLists.txt`. The linker generates `_binary_*_start` and `_binary_*_end` symbols pointing to the certificate data in flash.

**MQTT outbox** — ensure `CONFIG_MQTT_CUSTOM_OUTBOX` is **disabled** in `menuconfig` (Component config → ESP-MQTT → uncheck "Enable custom outbox implementation"). Leaving it enabled silently excludes the outbox from linking.

---

## Project context

This is the **gateway and cloud connectivity** half of the EcoHive pipeline. The companion STM32 firmware handles bare-metal sensor acquisition and SPI transmission.

**Full pipeline:**
```
AM2302 + MQ-9 + internal ADC
        ↓  bare-metal FreeRTOS (STM32)
   STM32F401RE  ──SPI 1MHz──►  ESP32  ──MQTT/TLS──►  AWS IoT Core
```

---

## Author

Hossein Baghaei — embedded systems engineer  
Built: 2025–2026
