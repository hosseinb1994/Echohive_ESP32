idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        nvs_flash
        esp_event
        esp_netif
        mqtt
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_gpio
    EMBED_FILES
        "certs/root_ca.pem"
        "certs/certificate.pem.crt"
        "certs/private.pem.key"
)