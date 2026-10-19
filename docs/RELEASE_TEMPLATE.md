## ULTRAKILL — NX · r1 / {{VERSION}}

Порт: **Dimasick-git**. Тестирование, скриншоты и видео: **Ryazhenka-helper-01 / Zeropaxy**.

Скачай оба ZIP и распакуй их в одну папку с объединением каталогов. Склеивать файлы не нужно.

| Архив | Размер, байт |
| :-- | --: |
| [ULTRAKILL-NX_PART_1.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_1.zip) | {{BYTES1}} |
| [ULTRAKILL-NX_PART_2.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_2.zip) | {{BYTES2}} |

Скопируй `switch`, `atmosphere` и `SaltySD` в корень microSD. Установи `ULTRAKILL_NX_forwarder.nsp` через DBI и запускай игру с HOME. После установки или обновления SaltyNX перезагрузи Switch в Atmosphère.

Перед обновлением сохрани `Saves`, `Preferences`, `LocalPrefs.json` и `config.txt`. После копирования удали кэш `switch/ultrakill/il2cpp`, затем верни свои настройки.

### Что изменилось в r32

- FSR1 EASU/RCAS и управление масштабом через FSR-NX/Ryazha-clk.
- Прямой мост SaltyNX/NX-FPS с FPS и frametime от EGL.
- Чтение частоты дисплея и FastLoad для запуска и Single-загрузок сцен.
- Исправления гироскопа, кнопок Switch, курков, GC и перехода Лимбо.

FSR-NX не входит в архив. Без оверлея масштаб задаётся через `config.txt`. Lite, NVN, HDR, depth и FSRE не поддерживаются.

### Проверено

Host-тесты, сборка r32 и GPU-тест EASU/RCAS пройдены. Новые live-масштаб, refresh и FastLoad требуют проверки на Switch. Полного прохождения нет; в тяжёлых сценах возможны низкий FPS и паузы. Стабильные 60 FPS не заявляются.

[Изменения](https://github.com/Dimasick-git/ULTRAKILL-NX/blob/main/CHANGELOG.md) · [Проверки](https://github.com/Dimasick-git/ULTRAKILL-NX/blob/main/docs/TESTING.md) · [Сообщить о баге](https://github.com/Dimasick-git/ULTRAKILL-NX/issues/new/choose)

### SHA-256

```text
{{SHA1}}  ULTRAKILL-NX_PART_1.zip
{{SHA2}}  ULTRAKILL-NX_PART_2.zip
```

ULTRAKILL создан Arsi “Hakita” Patala и New Blood Interactive. Это неофициальный порт, не связанный с Nintendo или авторами оригинальной игры.
