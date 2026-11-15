# Bclimate Sedona для ESP32-S3

Sedona VM, kits и SOX на ESP32-S3 (ESP-IDF v6.0.3, FreeRTOS). Исходники VM и
kits берутся из `runtime/` без изменений; здесь только платформенный слой.

## Что внутри

| Путь | Что это |
|---|---|
| `platform/esp32-sedona.xml` | Манифест платформы: нативные kits и пути к исходникам |
| `platform/sys_esp32/` | Замена `src/sys/native/unix`: часы (`esp_timer`), сон (FreeRTOS), `PlatformService`, `sedona-local.h`, связка с ESP-IDF (`svm_esp32.c`) |
| `platform/bclimateEsp32/` | Kit `bclimateEsp32`: `Esp32PlatformService` (перезагрузка, минимум кучи, время работы, причина сброса) |
| `scode/esp32-kits.xml` | Состав `kits.scode`: sys, sox, inet, datetime, func, hvac, logic, math, timing, types, pricomp, basicSchedule, bclimateLib, bclimateMath |
| `apps/esp32-demo.sax` | Демо-приложение: платформа, SOX на udp/1876, две рампы и сумматор |
| `firmware/` | Проект ESP-IDF: `app_main`, сеть, компонент `sedona`, Kconfig, разделы |
| `build.py` | Сборка: `sedona`, `fw`, `qemu`, `all` |

## Симулятор на ПК (без ESP-IDF)

Тот же код, что идёт в прошивку (VM, нативы, `platform/sys_esp32`,
`platform/bclimateEsp32`), собирается как Linux-программа; вызовы ESP-IDF
заменены заглушками из `platform/sim`.

```sh
python3 build.py sim                     # build/sim/esp32sim и данные для «флеша»
python3 build.py simrun --erase          # «прошить» build/sim/flash и запустить
python3 tests/sim_test.py                # автоматические проверки
```

Подключение редактором или по SOX: `127.0.0.1:1876`, `admin` / пароль сборки.

| Как на чипе | Как в симуляторе |
|---|---|
| LittleFS в `/sedona` | папка `--flash` (по умолчанию `build/sim/flash`), переживает перезагрузки |
| перезагрузка (`esp_restart`, выход VM, watchdog) | процесс перезапускает сам себя; `resetReason` в `Esp32PlatformService` показывает причину |
| task watchdog | поток-сторож; `kill -USR2 <pid>` вешает цикл сканирования для проверки |
| ~320 КБ кучи | `--heap` байт; сверх лимита `malloc` возвращает NULL |

Опции `esp32sim`: `--heap`, `--wdt` (с), `--restart-delay` (мс), `--max-resets`,
`--no-restart`, `--tests`. Не эмулируются: компилятор Xtensa и выравнивание, IRAM,
Wi-Fi, скорость ядра 240 МГц. Симулятор 64-битный, поэтому `kits.scode` для него
собирается с `refSize=8` (для чипа `refSize=4`).

`tests/sim_test.py`: юнит-тесты Sedona, модули (web, в том числе медленный и молчащий
клиент; Modbus внешним клиентом; формулы; MQTT через `tests/mini_mqtt_broker.py`),
старт платформы и SOX (чтение, запись, добавление компонента, сохранение, перезагрузка),
watchdog, safeboot, лимит кучи.

## Модули

Набор kits настраивается: `--modules default|all|none|имя,имя` для `sim` и
`sedona` (по умолчанию: все, что поддерживает цель). Описание модулей в
`modules.py`, кусок демо-приложения каждого модуля в `apps/modules/<имя>.xml`.

| Модуль | Что даёт | Симулятор | ESP32 |
|---|---|---|---|
| `web` | веб-сервер Sedona, tcp/8080 (страница сводки и `/spy`) | да | да |
| `pstore`, `logmanager`, `inout` | служба pstore, уровни журналов, точки ввода-вывода Bclimate | да | да |
| `libnative` | `bclimateLibNative`: преобразования, лицензия, сетевые настройки | да | да* |
| `expression` | формулы `ExpressionFloat/Bool` (`in1*2+sqrt(in2)`) | да | да |
| `uiconnect` | `UIConnectService`, tcp/8088 | да | да |
| `mqtt` | клиент MQTT (`communityMQTT`) | да | нет: будит поток через `pthread_kill(SIGUSR1)`, в ESP-IDF сигналов нет |
| `modbus` | Modbus TCP/RTU master и slave (libmodbus), slave на tcp/1502 | да | нет: нужен libmodbus как компонент ESP-IDF и UART |

\* лицензия Bclimate читает MAC из `/sys/class/net`; на ESP32 нужен `esp_read_mac`.

Демо-приложение со всеми модулями: Modbus slave отдаёт рампу в holding-регистре 0
(float, ABCD), Modbus master опрашивает его же по 127.0.0.1:1502 (`/mbMastr/dev1/reg0`),
формула `/play/formula`, MQTT-публикация `/play/adder.out` в `bclimate/esp32/sum`
(`/service/mqtt/pub`: задайте `host`, затем действие `connect` или `autoStart=true`).

## Доступ по сети

Симулятор слушает на всех интерфейсах: SOX udp/1876, web tcp/8080, UI tcp/8088,
Modbus TCP tcp/1502. С других машин: `<controller-host>:<порт>`.

Чтобы он работал постоянно (и после перезагрузки сервера), а не пока открыт терминал:

```sh
bash tools/sim_service.sh install     # systemd --user служба esp32sim + linger
bash tools/sim_service.sh log         # журнал; status, restart, uninstall
```

Если `loginctl enable-linger` откажет без прав, нужен
`sudo loginctl enable-linger $USER`, иначе служба остановится при выходе из сеанса.

## Манифесты китов по SOX

Каждая сборка (симулятор и прошивка) кладёт в раздел данных манифесты ровно тех
китов, что вошли в `kits.scode` (имена и checksum читаются из самого образа;
если какого-то манифеста нет, сборка падает):

```
manifests/m.zip                              все манифесты одним архивом
manifests/<kit>/<kit>-<checksum>.xml         по одному
```

Отдаёт их `bclimateEsp32::Esp32ManifestStore` по той же схеме, что `platUnix` на
стенде: `m:m.zip` и `m:<kit>-<checksum>.xml` (только чтение; имена с `/`, `\`,
`..` отклоняются). MCS/Workbench с пустым кэшем сам скачивает недостающие
манифесты при подключении. Опция `--manifests all|zip|xml|none` (по умолчанию
`all`, около 130–170 КБ; `zip` около 26 КБ).

Проверка (пустой `sedona.home`, как у нового MCS): `python3 tests/sim_test.py -k manifests`.

## Сборка прошивки

```sh
bash tools/install_idf.sh                 # один раз: ESP-IDF + QEMU в ~/esp
. ~/esp/esp-idf-v6.0.3/export.sh          # в каждой новой оболочке
python3 build.py sedona                   # kits.scode, app.sab, нативы -> build/
python3 build.py fw                       # прошивка, профиль qemu
python3 build.py qemu                     # запуск в QEMU, SOX на localhost:1876
```

Профили прошивки (`--profile`): `qemu` (Ethernet эмулятора), `hw` (Wi-Fi, SSID и
пароль в `idf.py menuconfig` → Sedona VM), `tests` (юнит-тесты Sedona в QEMU).

Пароль `admin` берётся из `SEDONA_ADMIN_PASSWORD`, по умолчанию `esp32-dev-only`
(только для разработки).

## Как устроено

- **Файлы.** Раздел `storage` (LittleFS) монтируется в `/sedona`. В ESP-IDF нет
  текущего каталога, поэтому `fopen/rename/remove/stat/open/mkdir/chdir`
  обёрнуты компоновщиком (`-Wl,--wrap`): относительные имена (`app.sab`,
  `kits.scode`) получают префикс `/sedona`.
- **`svm.conf`** работает как на Linux: stage-проверка и откат, атомарное
  сохранение, `save.keep`, safeboot, статистика. `rt.*` и `crash.handler`
  на ESP32 ничего не делают.
- **Watchdog.** После первого цикла сканирования задача VM подписывается на task
  watchdog ESP-IDF и кормит его из цикла (`watchdog=auto`). Зависший цикл
  перезагружает чип (Kconfig `SEDONA_WATCHDOG_TIMEOUT`, по умолчанию 15 с).
- **Выход VM** перезагружает чип через 3 с, как `Restart=` у systemd; safeboot
  и откат обновления считают эти перезапуски через файлы на LittleFS.
- **Скорость** (Kconfig, всё включено по умолчанию): интерпретатор `vm.c` в
  IRAM, computed goto, `-O3` для `vm.c`, 240 МГц, тик FreeRTOS 1 кГц. VM
  работает на ядре 1, Wi-Fi/lwIP на ядре 0.


## ESP32-P4 / Waveshare ESP32-P4-ETH

Добавлены профили `p4` (приложение) и `p4-tests` (встроенные тесты).
Цель — ESP32-P4 ревизии >=3.0, flash 32 МБ, CPU 400 МГц; PSRAM пока не используется.
Профили S3 сохранены. Сетевые типы и API времени/синхронизации файлов
адаптируются в копиях `build/native`; общий `runtime/` не изменяется.

Установка: `bash tools/install_idf.sh esp32p4`.
Тесты: `python3 build.py sedona --profile p4-tests`, затем
`bash tools/build_p4_bringup.sh`. Обычное приложение:
`bash tools/build_p4_platform.sh` (создаёт случайный пароль admin в
`build/p4-admin-password.txt`, доступ только владельцу).
Не собирать разные профили одновременно: staging и раздел данных общие.

Перед записью сохранить весь flash через esptool read-flash 0 ALL и проверить SHA-256.
Прошивать по `flasher_args.json`/`flash_args` соответствующего профиля;
у P4 bootloader начинается с 0x2000. Полная запись заменяет раздел storage.
Тестовый профиль следит watchdog за всей задачей тестов (120 секунд),
а не за IDLE1, поскольку тесты рамп намеренно занимают ядро.
Рабочий профиль использует штатный scan watchdog.

GPIO-компоненты и их ограничения: [GPIO_RU.md](GPIO_RU.md).
Демо `/gpio/di1` и `/gpio/do1` выключены, список разрешённых GPIO пуст.
Профиль p4: IP101/RMII по разводке Waveshare, статический IPv4 <controller-host>,
шлюз <controller-host>. p4-tests остаётся без физической сети.
Настройки SEDONA_ETH_STATIC_IP/GATEWAY/NETMASK доступны в menuconfig;
пустой SEDONA_ETH_STATIC_IP включает DHCP. При изменении дефолтов также
изменить уже созданный build/fw-p4/sdkconfig через menuconfig.

Проверка 15.11.2026: ESP32-P4 v3.2, flash 32 МБ, MAC e8:f6:0a:eb:85:1e,
Proxmox <controller-host>, /dev/ttyACM0 (USB serial <device-serial>).
На устройстве: 10835 успешных assert, 0 ошибок, SEDONA TESTS EXIT 0.
На ПК: 10834 успешных assert, 0 ошибок. Нативные GPIO прошли тест
с инструментированным драйвером (`tests/gpio_native_test.py`).
Электрический тест GPIO ещё не выполнен. Ethernet проверен: ping, HTTP 200,
SOX admin (info, изменяющиеся значения, схема/манифесты с платы), GPIO-компоненты
выключены pin=-1, fault=-6. Статический IP сохраняется после reset.
Исходный flash: /home/admin1/esp32-backups/p4-original-20261115.bin;
SHA-256: 20847106dd7db746dfd57a93255f1aa7fc366fc12d76d634b97d9c99b14195d8.


Исправлен сбой старта: LogMgr.start() добавляет компоненты и может перевыделить
App.comps. Старый foreach в App.startApp() сохранял адрес уже освобождённого
массива (use-after-free). В build/home/src/sys создаётся отдельная копия sys:
loaded/start обходят компоненты по индексу, перечитывая поле comps каждый раз.
Kit sys пересобирается перед платформенным китом. Общий runtime не изменяется.
На P4 после исправления исчезли Unknown opcode и stack imbalance при запуске,
UIConnect снова включён: порт TCP/8088 принимает соединения. Проверка протокола
UIConnect с реальным HMI пока не выполнена.
Профиль p4 пока использует switch / -O2. Тестовый профиль ранее проверен
с computed goto / -O3. Диагностических printf в итоговой VM нет.
Источники разводки/драйвера:
https://github.com/waveshareteam/ESP32-P4-Platform/tree/main/examples/esp-idf/11_ethernetbasic


## Расширенная прошивка контроллера ESP32-P4

Компоненты IO, MQTT, Modbus RTU, диагностика и HTTPS OTA описаны в [CONTROLLER_IO_RU.md](CONTROLLER_IO_RU.md).
