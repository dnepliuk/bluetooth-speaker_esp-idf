idf_component_register(
    SRCS "main.cpp" "status_led.cpp" "button_input.cpp" "rotary_input.cpp" "status_strip.cpp"
         "tests/rotary_steps_test.cpp" "tests/strip_animation_test.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        speaker_audio
        speaker_battery
        espressif__button
        espressif__led_strip
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
