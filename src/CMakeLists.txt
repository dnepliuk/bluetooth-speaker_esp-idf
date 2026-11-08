idf_component_register(
    SRCS "main.cpp" "status_led.cpp" "pairing_button.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        speaker_audio
        espressif__button
        esp_app_format
        esp_common
        esp_driver_gpio
        esp_hw_support
        esp_partition
        esp_system
        esp_timer
        freertos
        log
        spi_flash
)
