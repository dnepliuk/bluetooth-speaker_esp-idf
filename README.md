# Faital Bluetooth Speaker — ESP-IDF

Прошивка Bluetooth-колонки на ESP32 із зовнішнім ЦАП PCM5102A,
фізичним керуванням відтворенням, індикацією стану та контролем батареї.

## Цільові можливості

- Приймання стереоаудіо з телефона через Bluetooth Classic A2DP.
- Виведення звуку через I2S на PCM5102A.
- Керування гучністю енкодером і синхронізація з телефоном через AVRCP.
- Play/Pause, Next/Previous та окрема кнопка підключення Bluetooth.
- Автоматичне перепідключення до останнього пристрою.
- Вимірювання напруги батареї 3S2P, оцінка заряду та попередження LOW BATTERY.
- Відображення батареї на OLED SSD1306 і стану колонки на WS2812.

Цільовий аудіотракт:

```text
Телефон → Bluetooth A2DP → PCM → I2S → PCM5102A → підсилювач
```

## Апаратна конфігурація

Плата — ESP32 DOIT DevKit V1, target `esp32`, налаштування flash — 4 MiB.
Призначення GPIO збережені з робочої Arduino-прошивки.

| Функція | GPIO |
| --- | --- |
| I2S BCK / WS / DATA | 26 / 25 / 19 |
| EC11 A / B / кнопка | 32 / 33 / 18 |
| Previous / Next / Bluetooth | 13 / 14 / 23 |
| Батарея, ADC1 | 34 |
| OLED SDA / SCL | 21 / 22 |
| WS2812, 8 світлодіодів | 27 |
| Status LED | 2 |

OLED: SSD1306 128×64, I²C-адреса `0x3C`.
Кнопки й енкодер використовують внутрішні підтягування та активний LOW.
Параметри еталонного вимірювання батареї: дільник 150/30 кОм, коефіцієнт
калібрування 0,9880. GPIO2 показує роботу головного циклу коротким спалахом щосекунди.

## Середовище та компоненти

| Налаштування | Значення |
| --- | --- |
| Система збірки | PlatformIO |
| Environment | `esp32doit-devkit-v1` |
| Платформа | `espressif32@7.1.3` |
| Framework | ESP-IDF 6.1.0 |
| Пакет framework | `platformio/framework-espidf@4.60100.0` |
| Мова застосунку | C++ |
| UART | 115200 baud |

Поточна основа використовує штатні компоненти ESP-IDF: GPIO, FreeRTOS,
esp_timer, log, esp_system, esp_hw_support, esp_app_format, spi_flash та esp_partition.

## Структура та конфігурація

| Файл | Призначення |
| --- | --- |
| [platformio.ini](platformio.ini) | Плата, версії пакетів і параметри PlatformIO |
| [sdkconfig.defaults](sdkconfig.defaults) | Навмисні налаштування ESP-IDF |
| [partitions.csv](partitions.csv) | Таблиця розділів flash |
| [src/app_config.h](src/app_config.h) | GPIO та параметри застосунку |
| [src/main.cpp](src/main.cpp) | Запуск, діагностика та цикл обслуговування |
| [src/status_led.cpp](src/status_led.cpp) | Heartbeat головного циклу |

Згенерований `sdkconfig.esp32doit-devkit-v1` відстежується Git;
його оновлюють інструменти конфігурації та збірки.

Flash містить NVS 20 KiB з адреси `0x9000`, PHY 4 KiB з `0xF000`
та factory app 3 MiB з `0x10000`. Решта простору не розмічена.
Поточна таблиця не передбачає OTA чи файлової системи.

## Збірка та запуск

Відкрити корінь проєкту у VS Code з PlatformIO і вибрати
`Project Tasks → esp32doit-devkit-v1`.
Доступні стандартні дії `Build`, `Upload` та `Monitor`.

Еквівалентні команди в терміналі PlatformIO з кореня проєкту:

```powershell
pio run -e esp32doit-devkit-v1
pio run -e esp32doit-devkit-v1 -t upload --upload-port COMx
pio device monitor -e esp32doit-devkit-v1 --port COMx --baud 115200
```

`COMx` потрібно замінити фактичним портом плати.

## Документація

- [Перевірки на платі](docs/testing.md) — сценарії, очікувані логи та результати тестування.
- [Правила роботи з проєктом](AGENTS.md) — вимоги до реалізації та міграції.
- [Arduino-еталон](../a2dp-arduino-minimal/README.md) — вихідний проєкт, який залишається незмінним.
