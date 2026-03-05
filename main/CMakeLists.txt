idf_component_register(SRCS "app_main.c" "core1.c"
                    PRIV_REQUIRES esp_driver_gpio esp_timer mqtt nvs_flash esp_netif esp_wifi esp_driver_rmt
                    INCLUDE_DIRS ".")
