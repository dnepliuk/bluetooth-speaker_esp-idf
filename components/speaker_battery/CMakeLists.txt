idf_component_register(
    SRCS "battery_monitor.cpp" "battery_display.cpp" "tests/battery_filter_test.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_i2c esp_timer freertos log
                  chill-sam__ssd1306
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)

# The pinned driver hardcodes an infinite I2C wait. Redirect its transport calls
# only, keeping Registry source and the SDK untouched. Our adapter caps to 20 ms.
idf_component_get_property(oled_library chill-sam__ssd1306 COMPONENT_LIB)
target_compile_definitions(${oled_library} PRIVATE
    i2c_master_transmit=speaker_oled_i2c_transmit
)
