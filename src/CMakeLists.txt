idf_component_register(
    SRCS "main.cpp" "status_led.cpp" "button_input.cpp" "rotary_input.cpp"
         "tests/rotary_steps_test.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        speaker_audio
        espressif__button
        esp_app_format
        esp_common
        esp_driver_gpio
        esp_driver_pcnt
        esp_hw_support
        esp_partition
        esp_system
        esp_timer
        freertos
        log
        spi_flash
)
