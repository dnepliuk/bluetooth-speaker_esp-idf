# Перевірка прошивки на платі

Тут зберігаються сценарії перевірки окремих етапів міграції.
Загальний опис проєкту — у [README](../README.md).

## Етап 1: запуск і heartbeat

Мета — перевірити базовий запуск ESP-IDF, UART, конфігурацію flash та GPIO2 heartbeat.
Bluetooth і звук у цій версії ще не запускаються.
GPIO27 утримується в LOW; WS2812 може зберігати попередній колір до вимкнення живлення.

## Збірка і перевірка на платі

Відкрити цю папку у VS Code / PlatformIO. Вибрати
`Project Tasks → esp32doit-devkit-v1 → General → Build`.
Еквівалент у терміналі PlatformIO з кореня проєкту:

```powershell
pio run -e esp32doit-devkit-v1
```

Після успішної збірки користувач виконує `Upload` для потрібної ESP32,
відкриває `Monitor` на 115200 і натискає EN/RESET, щоб побачити повний запуск.
За потреби явного вибору порту (замінити COMx фактичним портом):

```powershell
pio run -e esp32doit-devkit-v1 -t upload --upload-port COMx
pio device monitor -e esp32doit-devkit-v1 --port COMx --baud 115200
```

Не запускати `erase_flash` для звичайного тесту. Якщо перевіряється плата,
на якій працювала Arduino-прошивка, Upload замінить прошивку на цій платі;
файли Arduino-проєкту залишаються незмінними.

Очікувані фрагменти логу (префікси часу, reset reason і heap можуть відрізнятися):

```text
FAITAL_SPEAKER firmware=esp-idf version=0.1.0-stage1 stage=1
ESP-IDF=6.1.0 target=esp32 ...
Reset reason: ...
Flash: configured=4194304 bytes detected=4194304 bytes
App partition: factory offset=0x10000 size=3145728 bytes
STATUS LED: GPIO=2 period=1000 ms pulse=50 ms
Stage 1 ready: application loop=10 ms; Bluetooth/audio pending
Loop alive: uptime=... s free_heap=... minimum_heap=...
```

Критерії перевірки:

1. UART читабельний; flash і app partition відповідають рядкам вище.
2. GPIO2 дає короткий спалах щосекунди.
3. 5–10 хвилин роботи: немає panic, watchdog, brownout, повторного startup banner
   або зависання; uptime зростає. Невелика початкова зміна heap допустима;
   послідовне зниження free heap потребує аналізу.
4. Після EN/RESET запуск повторюється. Окремо перевірити вимкнення/ввімкнення
   живлення без фантомного живлення через USB.
5. Надіслати стартовий лог і кілька `Loop alive`, повідомити про heartbeat.

Фізичний PASS ще не отримано. Після успішного тесту користувач робить коміт,
і лише потім починається етап 2: A2DP → I2S → PCM5102A та гучність із телефона.
