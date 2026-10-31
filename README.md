# Switch Lite Rumble — ветка `emulation`

Системный модуль Atmosphere для Switch Lite. У Lite нет вибромоторов, поэтому
модуль перехватывает команды вибрации игры (HID) и проигрывает их тоном
через динамики консоли.

## Что делает

- Перехватывает HID-команды вибрации (200–206, 211) у игр и homebrew,
  только на Switch Lite. Команды внешних контроллеров уходят в систему как есть.
- Сигнал: 20-225 Гц, сила 1000, левый и правый каналы в противофазе,
  сглаживание фронтов 5 мс. Амплитуда берётся из команд игры.
- Звук идёт отдельным аудиопотоком модуля, громкость потока — 1.0.
- Если игра перестала обновлять вибрацию, канал глохнет через `watchdog_ms`.
- Качелька громкости в игре (`game_volume_buttons=1`): динамик и мастер-громкость
  держатся на максимуме, чтобы вибрация не тихла, а кнопки + и − меняют
  громкость самой игры через RyazhTune (15 шагов). При подключении наушников,
  выходе из игры или выключении режима прежняя громкость возвращается.
- Сервис `ryz:rum` отдаёт статус тестовому приложению.

## Связь с Ryzhand

Пока модуль зависит от Ryzhand:

- выключается вместе с виброоткликом Ryzhand: `haptic_feedback=false`
  в `config/ryazhahand/config.ini` глушит и модуль;
- качелька меняет громкость игры через RyazhTune (сервис `tune`, команды 12/13);
- полный комплект для SD собирается вместе с Ryzhand.

Ryzhand подключён сабмодулем `Ryzhand/`: это
[Ryazhahand-Overlay](https://github.com/Dimasick-git/Ryazhahand-Overlay), ветка `emulation`.
Там лежат правки Lite: звуковой отклик меню и пункт «Громкость игры и музыки».
Его библиотека `libryazhahand-deko` тоже на ветке `emulation`.

## Сборка

Нужны devkitPro (devkitA64) и g++ с C++20 для тестов. Atmosphere-RYZ v8.1.0
требует свежую libnx: в образе `devkitpro/devkita64:20260219` её надо обновить
(`dkp-pacman -Syu` или сборка switchbrew/libnx, проверено на `b156b47`) —
так делает CI в `.github/workflows/build.yml`.

```sh
git clone -b emulation https://github.com/Dimasick-git/Switch-lite-Rumble
cd Switch-lite-Rumble
git submodule update --init Atmosphere-RYZ

make          # модуль, libstratosphere собирается из Atmosphere-RYZ
make test     # хост-тесты
make dist     # готовая структура SD в sd/
```

С Ryzhand (нужен доступ к приватной `libryazhahand-deko`):

```sh
git submodule update --init --recursive Ryzhand
make full     # Ryzhand + модуль в sd/
```

Свой Atmosphere с уже собранной libstratosphere: `make AMS_ROOT=/путь AMS_CHECKED=1`.

## Установка

Скопировать содержимое `sd/` в корень карты и полностью перезагрузить консоль.

| Что | Где |
| --- | --- |
| Модуль | `atmosphere/contents/4200000000525652/` (`exefs.nsp`, `flags/boot2.flag`, `mitm.lst`) |
| Настройки | `config/ryz-lite-rumble/config.ini` |
| Тестовое приложение | `switch/ryz-rumble-test/ryz-rumble-test.nro` |

Если консоль перестала загружаться, удалите на ПК
`atmosphere/contents/4200000000525652/flags/boot2.flag`.

## Настройки

`config/ryz-lite-rumble/config.ini`, перечитывается раз в секунду:

```ini
[rumble]
enabled=1              ; 0 — выключить
gain_percent=200       ; 0..400
watchdog_ms=1000       ; 0 или 100..10000
game_volume_buttons=1  ; качелька управляет громкостью игры
```

Лог: `config/ryz-lite-rumble/runtime.log` (не больше ~64 КБ).

## Ограничения

- Только Switch Lite.
- Игры, которые сами проверяют модель консоли и не шлют вибрацию, не озвучиваются.
- Вибрация и звук игры выходят через одни динамики: пока включена качелька,
  системные звуки идут на максимуме динамика.
- Штатный индикатор громкости в игре может не совпадать с реальным уровнем игры.

## Лицензия

GPLv2, см. `LICENSE`. Модуль построен на Atmosphere (libstratosphere).
