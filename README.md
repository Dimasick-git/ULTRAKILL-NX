<p align="center"><img src="docs/images/icon.png" width="168" alt="ULTRAKILL - NX"></p>
<h1 align="center">ULTRAKILL — NX</h1>
<p align="center">ULTRAKILL на Nintendo Switch.<br>Неофициальный homebrew-порт от <a href="https://github.com/Dimasick-git">Dimasick-git</a>.</p>
<p align="center"><a href="https://github.com/Dimasick-git/ULTRAKILL-NX/releases/tag/r1">Скачать r1</a> · <a href="#установка">Установка</a> · <a href="https://github.com/Dimasick-git/ULTRAKILL-NX/issues/new/choose">Сообщить о баге</a> · <a href="docs/BUILD.md">Сборка из исходников</a></p>

![0-2 — THE MEATGRINDER на Nintendo Switch](docs/images/meatgrinder.jpg)

Работают управление с контроллера, прицеливание гироскопом, звук и видеоролики.
Проверены запуск, обучение и переход к **0-2: THE MEATGRINDER**. Порт ещё тестируется;
прохождение всей игры пока не проверено.

Скриншоты сняты на настоящем Switch с разгоном. Частоты видны в оверлее.
Цель — 60 FPS, но в тяжёлых сценах бывают просадки. Стабильные 60 на всех уровнях не обещаются.

## Скачать

**[Релиз r1](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/tag/r1)** — актуальная сборка **r23**.

Скачай **оба обычных ZIP**:

- [ULTRAKILL_NX_r1_1of2.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL_NX_r1_1of2.zip) — **1,67 ГБ**
- [ULTRAKILL_NX_r1_2of2.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL_NX_r1_2of2.zip) — **1,68 ГБ**

Распакуй содержимое обоих в **одну папку**, согласившись на объединение папок
`switch`. Порядок не важен. Склейка и дополнительные скрипты не нужны.
Внутри — сборка, HOME-форвардер и инструкция. Размеры и SHA-256 указаны в релизе.
Кнопка GitHub **Source code** скачивает только исходники порта.

Если r1 уже установлен, достаточно [обновления r23 — 29,2 МБ](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL_NX_r23_update.zip).
Полные ZIP выше также содержат r23. В ней возвращён автоматический GC с остановкой
потоков Horizon. Исправление вылета при перезапуске ещё требует проверки на консоли;
[подробности и порядок проверки](docs/GC-r23.md).

## Установка

Нужен Nintendo Switch с Atmosphère и DBI.

1. Распакуй оба ZIP на ПК в одну папку с объединением содержимого.
2. Скопируй папку `switch` в корень microSD с объединением папок.
3. Установи `ULTRAKILL_NX_forwarder.nsp` через DBI.
4. Безопасно извлеки карту и запускай **ULTRAKILL - NX** с HOME.

```text
microSD/
└── switch/
    └── ultrakill/
        ├── ultrakill_nx.nro
        ├── config.txt
        ├── offsets.ini
        ├── libil2cpp.so
        ├── libmain.so
        ├── libunity.so
        └── assets/
```

Первый запуск и загрузка уровней могут занять время. Запускай через HOME-форвардер:
игре нужна память полноценного приложения.

**Обновление:** сохрани папки `Saves` и `Preferences`, замени файлы сборки и верни
свои сохранения и настройки. Удали служебную папку `switch/ultrakill/il2cpp`,
чтобы движок создал кэш метаданных заново. Если форвардер уже установлен,
переустанавливать его не требуется.

## Управление

| Switch | Действие |
| :-- | :-- |
| Левый / правый стик | Движение / обзор |
| ZR / ZL | Основной / дополнительный огонь |
| B — нижняя кнопка | Прыжок |
| + | Пауза |
| Гироскоп | Точная доводка прицела |

Остальные действия и привязки доступны в настройках игры. Кнопки передаются
позиционно, как на Xbox: нижняя кнопка Switch B соответствует South.

Гироскоп включён по умолчанию. Параметры — в `switch/ultrakill/config.txt`:

| Параметр | Значение по умолчанию | Назначение |
| :-- | :-- | :-- |
| `gyro` | `1` | `0` — выключить гироскоп |
| `gyro_sens` | `2` | Чувствительность |
| `gyro_invert_x` / `gyro_invert_y` | `1` / `1` | Инверсия осей; меняется отдельно |
| `render_height` | `540` | Внутренняя высота рендера |

## Кадры с консоли

| Терминал | Оружие |
| :--: | :--: |
| ![Терминал в игре](docs/images/terminal.jpg) | ![Меню оружия](docs/images/terminal-close.jpg) |

## Что вошло в r1

- Гироскоп с исправленными направлениями обеих осей.
- Потоковое воспроизведение видео и звука.
- Исправление лишних нажатий крестовины при удержании курков.
- Исправление вылета при диагностическом снимке потоков.
- Автоматический GC с сохранением стеков и полного регистрового контекста потоков.
- Профиль рендера 540p, ограниченный кэш шейдеров и уменьшенный gore.

Подробности — в [списке изменений](CHANGELOG.md).

## Если что-то сломалось

[Создай issue](https://github.com/Dimasick-git/ULTRAKILL-NX/issues/new/choose): укажи
модель Switch, уровень, действие перед ошибкой и настройки разгона. Приложи
`switch/ultrakill/debug.log` и, если есть, свежий отчёт из `atmosphere/crash_reports`.

## Авторы

Порт, интеграция и тестирование — **Dimasick-git**.

ULTRAKILL — **Arsi “Hakita” Patala / New Blood Interactive**. Порт не связан с
Nintendo и авторами оригинальной игры. Поддержите оригинальную игру.

Загрузчик основан на работе **ChanseyIsTheBest**, **fgsfds** и **TheOfficialFloW**;
форвардер — **nx-hbloader**. Используются devkitPro/libnx, Mesa/nouveau,
SDL2 и libjpeg-turbo. Лицензии: [загрузчик](LICENSE.loader),
[форвардер](forwarder/LICENSE.md). Подробнее — [CREDITS](CREDITS.md).

<sub>Unofficial ULTRAKILL homebrew port for Nintendo Switch. Work in progress.</sub>
