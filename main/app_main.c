#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
// ─── WiFi credentials ────────────────────────────────────────────────────────
#define WIFI_SSID      "Wifi_SSID"
#define WIFI_PASSWORD  "Wifi_Password"

// ─── AWS IoT endpoint ────────────────────────────────────────────────────────
#define AWS_IOT_ENDPOINT  "a2use3vi2ks08t-ats.iot.eu-north-1.amazonaws.com"
#define AWS_IOT_PORT       8883
#define MQTT_TOPIC         "stm32/sensors"
#define MQTT_CLIENT_ID     "stm32-nucleo-bridge"

// ─── SPI pins ────────────────────────────────────────────────────
#define GPIO_MOSI  23
#define GPIO_MISO  19
#define GPIO_SCLK  18
#define GPIO_CS     5
#define MY_SPI_HOST   SPI3_HOST

static const char *TAG = "ESP32_AWS";

// ─── Event group bits ────────────────────────────────────────────────────────
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MQTT_CONNECTED_BIT  BIT2

static EventGroupHandle_t s_event_group;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

// ─── Sensor data struct ───────────────────────────
typedef struct __attribute__((packed)) {
    float    mcu_temperature;
    float    mq9_ppm;
    float    am2302_temperature;
    float    am2302_humidity;
    uint32_t timestamp;
    uint8_t  checksum;
} SensorData_t;

#define DATA_LENGTH_BYTES (sizeof(SensorData_t))

// ─── AWS certificates ────────────────────
extern const uint8_t aws_root_ca_pem_start[]   asm("_binary_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[]     asm("_binary_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[]   asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[]   asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[]     asm("_binary_private_pem_key_end");

// ─── Checksum ────────────────────────────────────────────────────────────────
static uint8_t calculate_checksum(uint8_t *data, uint32_t size)
{
    uint8_t cs = 0;
    for (uint32_t i = 0; i < size; i++) cs ^= data[i];
    return cs;
}

// ─── WiFi event handler ──────────────────────────────────────────────────────
static int s_retry_count = 0;
#define WIFI_MAX_RETRY 5

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi... (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    s_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected!");
    } else {
        ESP_LOGE(TAG, "WiFi connection FAILED");
    }
}

// ─── MQTT event handler ──────────────────────────────────────────────────────
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to AWS IoT");
            mqtt_connected = true;
            xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            mqtt_connected = false;
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);
            break;
        default:
            break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname  = AWS_IOT_ENDPOINT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
                .port      = AWS_IOT_PORT,
            },
            .verification = {
                .certificate     = (const char *)aws_root_ca_pem_start,
                //.certificate_len = aws_root_ca_pem_end - aws_root_ca_pem_start,
                .certificate_len = 0,
            },
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
            .authentication = {
                .certificate     = (const char *)certificate_pem_crt_start,
                //.certificate_len = certificate_pem_crt_end - certificate_pem_crt_start,
                .certificate_len = 0,
                .key             = (const char *)private_pem_key_start,
                //.key_len         = private_pem_key_end - private_pem_key_start,
                .key_len         = 0,
            },
        },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "Waiting for MQTT connection...");
    xEventGroupWaitBits(s_event_group, MQTT_CONNECTED_BIT,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
}

// ─── SPI slave task ──────────────────────────────────────────────────────────
void spi_slave_task(void *arg)
{
    WORD_ALIGNED_ATTR uint8_t rx_buffer[DATA_LENGTH_BYTES];
    WORD_ALIGNED_ATTR uint8_t tx_buffer[DATA_LENGTH_BYTES];
    memset(tx_buffer, 0, DATA_LENGTH_BYTES);

    spi_bus_config_t buscfg = {
        .mosi_io_num  = GPIO_MOSI,
        .miso_io_num  = GPIO_MISO,
        .sclk_io_num  = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_slave_interface_config_t slavecfg = {
        .spics_io_num = GPIO_CS,
        .flags        = 0,
        .queue_size   = 3,
        .mode         = 0,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(MY_SPI_HOST, &buscfg, &slavecfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI slave ready, waiting for STM32...");

    char json_buf[256];

    while (1) {
        memset(rx_buffer, 0, DATA_LENGTH_BYTES);
        spi_slave_transaction_t t = {
            .length    = DATA_LENGTH_BYTES * 8,
            .tx_buffer = tx_buffer,
            .rx_buffer = rx_buffer,
        };

        esp_err_t ret = spi_slave_transmit(MY_SPI_HOST, &t, portMAX_DELAY);
        if (ret != ESP_OK) continue;

        SensorData_t received;
        memcpy(&received, rx_buffer, sizeof(SensorData_t));

        uint8_t calc = calculate_checksum(rx_buffer, DATA_LENGTH_BYTES - 1);
        if (received.checksum != calc) {
            ESP_LOGW(TAG, "Checksum error — skipping frame");
            continue;
        }

        ESP_LOGI(TAG, "MCU: %.1fC | MQ9: %.1fppm | DHT: %.1fC | Hum: %.1f%%",
                 received.mcu_temperature, received.mq9_ppm,
                 received.am2302_temperature, received.am2302_humidity);

        // ── Build JSON payload ──────────────────────────────────────────────
        int len = snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"device\":\"stm32-nucleo\","
            "\"mcu_temp\":%.2f,"
            "\"mq9_ppm\":%.2f,"
            "\"dht_temp\":%.2f,"
            "\"dht_humidity\":%.2f,"
            "\"timestamp\":%lu"
            "}",
            received.mcu_temperature,
            received.mq9_ppm,
            received.am2302_temperature,
            received.am2302_humidity,
            (unsigned long)received.timestamp
        );

        // ── Publish to AWS IoT via MQTT ─────────────────────────────────────
        if (mqtt_connected && len > 0) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC,
                                                  json_buf, len, 1, 0);
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published to %s (msg_id=%d)", MQTT_TOPIC, msg_id);
            } else {
                ESP_LOGW(TAG, "Publish failed — MQTT not ready");
            }
        }
    }
}

// ─── Entry point ─────────────────────────────────────────────────────────────
void app_main(void)
{
    // NVS is required by WiFi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    mqtt_init();

    xTaskCreate(spi_slave_task, "spi_slave", 4096, NULL, 10, NULL);

    while (1) vTaskDelay(pdMS_TO_TICKS(5000));
}