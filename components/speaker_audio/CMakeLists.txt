idf_component_register(
    SRCS "audio_output.cpp" "bluetooth_audio.cpp" "tests/pairing_policy_test.cpp"
         "tests/control_policy_test.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES esp32_a2dp bt esp_driver_i2s freertos log nvs_flash esp_timer
)
