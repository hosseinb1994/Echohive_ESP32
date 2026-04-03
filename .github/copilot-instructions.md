# ESP32 Ecohive SPI Slave Project

## Architecture Overview
This ESP32 project implements an SPI slave that receives sensor data from an STM32 (Nucleo) master in the Ecohive system. The ESP32 acts as a data sink, validating incoming `SensorData_t` structures containing MCU temperature, MQ9 PPM, AM2302 temperature/humidity, timestamp, and XOR checksum.

- **Main Component**: `main/app_main.c` - Single task handling SPI slave operations
- **Data Flow**: STM32 master initiates full-duplex SPI transactions; ESP32 validates checksum and logs data
- **Response**: ESP32 echoes back a marker response (MCU temp = 99.9) for acknowledgment

## Key Patterns
- **SPI Configuration**: Uses HSPI (SPI2_HOST) with GPIO pins 13 (MOSI), 12 (MISO), 14 (SCLK), 15 (CS). Mode 0 (CPOL=0, CPHA=0) matching STM32.
- **Data Structure**: `SensorData_t` must match STM32 exactly - 21 bytes total, packed floats/uint32_t/uint8_t.
- **Checksum**: Simple XOR over data bytes (excluding checksum field), validated on receive.
- **GPIO Pull-ups**: Inputs (MOSI, SCLK, CS) use `GPIO_PULLUP_ONLY`; MISO floats.
- **Error Handling**: Logs failures but continues; SPI init failure aborts task.

## Build & Development
- **Environment**: Requires ESP-IDF v5.5.1+ with `idf.py` in PATH.
- **Build Commands**:
  - `idf.py build` - Compile project
  - `idf.py flash` - Flash to ESP32
  - `idf.py monitor` - Serial monitor for logs
- **Configuration**: `sdkconfig` auto-generated; modify via `idf.py menuconfig` if needed.
- **Minimal Build**: Root `CMakeLists.txt` enables `MINIMAL_BUILD ON` for faster compiles.

## Component Registration
- `main/CMakeLists.txt` registers `app_main.c` with `esp_driver_spi` and `esp_driver_gpio` dependencies.
- No custom components; relies on ESP-IDF drivers.

## Debugging
- Use ESP_LOGI/LOGE for output visible in `idf.py monitor`.
- SPI transactions are blocking (`portMAX_DELAY`); master controls timing.
- Checksum failures logged as warnings; inspect data integrity issues.</content>
<parameter name="filePath">/home/hossein/Private/ESP32_Ecohive/Ecohive_ESP32/.github/copilot-instructions.md