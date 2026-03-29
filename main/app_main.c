#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"

// --- Updated Constants for VSPI (Standard Pins) ---
#define GPIO_MOSI 23    // Connects to Nucleo PC3
#define GPIO_MISO 19    // Connects to Nucleo PC2
#define GPIO_SCLK 18    // Connects to Nucleo PC10
#define GPIO_CS   5     // Connects to Nucleo PC0
#define SPI_HOST  SPI3_HOST // VSPI is typically SPI3_HOST on ESP32
#define GPIO_LED  2     // LED pin for status indication (optional)

// Structure must match STM32 exactly
// Added __attribute__((packed)) to ensure no hidden padding bytes
typedef struct __attribute__((packed)) {
    float mcu_temperature;
    float mq9_ppm;
    float am2302_temperature;
    float am2302_humidity;
    uint32_t timestamp;
    uint8_t checksum;
} SensorData_t;

#define DATA_LENGTH_BYTES (sizeof(SensorData_t))

static const char *TAG = "ESP32_SPI_SLAVE";

// --- LED Blinking Task ---
void blink_task(void* arg)
{
    gpio_reset_pin(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
    while (1) {
        gpio_set_level(GPIO_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(500)); // LED ON for 500ms
        gpio_set_level(GPIO_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(500)); // LED OFF for 500ms
    }
}

uint8_t calculate_checksum(uint8_t *data, uint32_t size)
{
    uint8_t checksum = 0;
    for(uint32_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void spi_slave_task(void* arg)
{
    // Buffers must be DMA-capable (WORD_ALIGNED)
    WORD_ALIGNED_ATTR uint8_t rx_buffer[DATA_LENGTH_BYTES];
    WORD_ALIGNED_ATTR uint8_t tx_buffer[DATA_LENGTH_BYTES];

    // 1. Configuration for SPI Bus (VSPI)
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    // 2. Configuration for SPI Slave Interface
    spi_slave_interface_config_t slavetrans_config = {
        .spics_io_num = GPIO_CS,
        .flags = 0,
        .queue_size = 3,
        .mode = 0 // Mode 0 (CPOL=0, CPHA=0) to match Nucleo
    };

    // 3. Initialize SPI Slave
    esp_err_t ret = spi_slave_initialize(SPI_HOST, &buscfg, &slavetrans_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Slave Initialization Failed");
        return;
    }

    ESP_LOGI(TAG, "VSPI Slave initialized. Waiting for Master on Pins 18, 19, 23, 5...");

    // 4. Prepare initial dummy data for transmission
    memset(tx_buffer, 0, DATA_LENGTH_BYTES);
    
    while (1) {
        // Clear RX buffer before every transaction
        memset(rx_buffer, 0, DATA_LENGTH_BYTES);

        spi_slave_transaction_t t = {
            .length = DATA_LENGTH_BYTES * 8, // length in bits
            .tx_buffer = tx_buffer,
            .rx_buffer = rx_buffer,
        };

        // Blocking wait for master to clock data
        esp_err_t ret = spi_slave_transmit(SPI_HOST, &t, portMAX_DELAY);

        if (ret == ESP_OK) {
            // 1. Print RAW hex to see if anything is coming through
            printf("Raw: ");
              for(int i=0; i<DATA_LENGTH_BYTES; i++) {
                    printf("%02X ", rx_buffer[i]);
                }
            printf("\n");
            // Use a local struct to hold the data (prevents pointer alignment issues)
            SensorData_t received;
            memcpy(&received, rx_buffer, sizeof(SensorData_t));

            // Calculate checksum on everything EXCEPT the last byte (the checksum byte)
            uint8_t calc = calculate_checksum(rx_buffer, DATA_LENGTH_BYTES - 1);
            
            if (received.checksum == calc) {
                // LOG all four sensor values now that the structure is synced
                ESP_LOGI(TAG, "MCU: %.1fC | MQ9: %.1fppm | DHT: %.1fC | Hum: %.1f%%", 
                         received.mcu_temperature, 
                         received.mq9_ppm, 
                         received.am2302_temperature,
                         received.am2302_humidity);
            } else {
                // Helpful debug to see what is actually coming over the wire
                ESP_LOGW(TAG, "Checksum Error! Expected: 0x%02X, Received: 0x%02X", 
                         calc, received.checksum);
            }
        }
    }
}

void app_main(void)
{
    xTaskCreate(spi_slave_task, "spi_slave_task", 4096, NULL, 10, NULL);
    //xTaskCreate(blink_task, "blink_task", 1024, NULL, 5, NULL);
    
    // Add this loop to keep app_main alive
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}