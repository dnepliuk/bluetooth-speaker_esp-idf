"""Keep PlatformIO's generated ESP-IDF memory layout in sync with Kconfig."""

from pathlib import Path

Import("env")

# espressif32 7.1.3 preprocesses memory.ld.in but does not track its includes.
# Reusing the non-Bluetooth layout after enabling BT overlaps application DRAM
# with controller memory and panics in heap initialization, before app_main.
framework_dir = Path(env.PioPlatform().get_package_dir("framework-espidf"))
env.Depends(
    "$BUILD_DIR/memory.ld",
    [
        "$BUILD_DIR/config/sdkconfig.h",
        str(framework_dir / "components" / "esp_system" / "ld" / "ld.common"),
    ],
)
