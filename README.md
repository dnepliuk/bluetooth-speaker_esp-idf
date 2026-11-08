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
| CPU | 240 MHz |

Bluetooth A2DP та AVRCP реалізовані бібліотекою **ESP32-A2DP v1.8.11** —
тією самою, що в Arduino-еталоні, зібраною як native ESP-IDF component.
Вбудований SBC-декодер Bluedroid видає PCM; невеликий адаптер передає його
штатному драйверу `esp_driver_i2s`. Arduino framework і AudioTools не потрібні.
Нелінійна крива гучності з ESP32-A2DP збережена.

Копія вихідних файлів бібліотеки зафіксована в репозиторії разом із ліцензією.
Версія, походження та параметри інтеграції описані в
[components/esp32_a2dp/README.md](components/esp32_a2dp/README.md).
Інші основні компоненти — GPIO, FreeRTOS, NVS, esp_timer, log, esp_system,
esp_hw_support, esp_app_format, spi_flash та esp_partition.

## Bluetooth і аудіовихід

Назва пристрою — **Faital Bluetooth Speaker**. Підключення обирають у Bluetooth
налаштуваннях телефона. Підтримується A2DP з кодеком SBC; гучність телефона
передається через AVRCP Absolute Volume та застосовується до PCM.
Діапазон — 0..127, початкове значення до команди телефона — 64.

I2S працює як master у форматі Philips, 16 біт, стерео, з початковою частотою
44,1 кГц. Частота змінюється відповідно до узгодженого SBC-потоку;
моно подається однаково в обидва канали. Під час паузи на виході — тиша.
MCLK не виводиться на GPIO.

Поточна реалізація містить аудіоприймання, керування гучністю з телефона
та heartbeat. Автоматичне перепідключення, фізичні органи керування,
вимірювання батареї, OLED і WS2812 додаються поступово.
Bluetooth-зв'язки зберігаються в NVS; автоматичного очищення NVS немає.

## Структура та конфігурація

| Файл | Призначення |
| --- | --- |
| [platformio.ini](platformio.ini) | Плата, версії пакетів і параметри PlatformIO |
| [scripts/idf_memory_layout.py](scripts/idf_memory_layout.py) | Оновлення карти пам'яті при змінах конфігурації ESP-IDF |
| [sdkconfig.defaults](sdkconfig.defaults) | Навмисні налаштування ESP-IDF |
| [partitions.csv](partitions.csv) | Таблиця розділів flash |
| [src/app_config.h](src/app_config.h) | GPIO та параметри застосунку |
| [src/main.cpp](src/main.cpp) | Запуск, діагностика та цикл обслуговування |
| [src/status_led.cpp](src/status_led.cpp) | Heartbeat головного циклу |
| [components/speaker_audio](components/speaker_audio) | Інтеграція Bluetooth, PCM-гучності та native I2S |
| [components/esp32_a2dp](components/esp32_a2dp) | Зафіксована ESP32-A2DP та її CMake-адаптер |

Згенерований `sdkconfig.esp32doit-devkit-v1` відстежується Git;
його оновлюють інструменти конфігурації та збірки.
Скрипт PlatformIO додає залежності `memory.ld` від `sdkconfig.h` та `ld.common`,
щоб резервування пам'яті Bluetooth відповідало поточній конфігурації.

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
