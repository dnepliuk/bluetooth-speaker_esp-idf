# Native build adapter for the pinned, unmodified ESP32-A2DP sources.
idf_component_register(
    SRCS
        "upstream/src/BluetoothA2DPCommon.cpp"
        "upstream/src/BluetoothA2DPSink.cpp"
        "upstream/src/BluetoothA2DPOutput.cpp"
    INCLUDE_DIRS "upstream/src"
    REQUIRES bt esp_common esp_system esp_timer esp_ringbuf freertos log nvs_flash
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    A2DP_LEGACY_I2S_SUPPORT=0
    A2DP_I2S_AUDIOTOOLS=0
    A2DP_SPP_SUPPORT=0
)
# v1.8.11 has one Arduino-named delay in end(); use its native delay_ms method.
target_compile_definitions(${COMPONENT_LIB} PRIVATE delay=delay_ms)
# The upstream headers use CONFIG_IDF_TARGET_* before including sdkconfig.
target_compile_options(${COMPONENT_LIB} PUBLIC
    "-include" "${CMAKE_BINARY_DIR}/config/sdkconfig.h")
# IDF 6 adds zero-initialized fields to structs used by the pinned headers.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
target_include_directories(${COMPONENT_LIB} SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src")
