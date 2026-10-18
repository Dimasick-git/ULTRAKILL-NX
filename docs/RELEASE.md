## ULTRAKILL — NX · r1 / {{VERSION}}

Порт от **Dimasick-git**. Главный тестер, скриншоты и видео — **Ryazhenka-helper-01 / Zeropaxy**.

Скачай **оба** файла и распакуй в одну папку, объединяя `switch`.
Это обычные ZIP с разным содержимым; склейка не нужна, порядок не важен.
В комплекте все изменения, отдельно старые патчи скачивать не требуется.

| Архив | Байт |
| :-- | --: |
| [ULTRAKILL-NX_PART_1.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_1.zip) | {{BYTES1}} |
| [ULTRAKILL-NX_PART_2.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_2.zip) | {{BYTES2}} |

Скопируй `switch` в корень microSD, установи `ULTRAKILL_NX_forwarder.nsp`
через DBI и запускай с HOME. Если форвардер уже установлен, переустановка не нужна.
Перед обновлением сохрани `Saves`, `Preferences`, `LocalPrefs.json`, `config.txt`.
После замены верни личные настройки; удали runtime-кэш `switch/ultrakill/il2cpp`.
Полная инструкция и манифест файлов находятся внутри обоих архивов.

### Что изменилось

- Добавлена проверка NULL при загрузке Umbra Tome: исправляется найденная точка
  вылета на переходе Лимбо 1-1 → 1-2. Валидные данные обрабатываются как прежде.
- Добавлены редкие образцы стека до выброса NullReferenceException для поиска источника.

- FSR1 EASU + RCAS: 960×540 → 1280×720, включая HUD; активный вывод проверен на Switch.
- GC фактически включён с r25: исправлено ошибочное значение режима в r23/r24.
- Реальные CPU affinity и профиль потоков; ограничен вывод чтений PlayerPrefs.
- В r27 одинаковые исключения записываются образцами и сводками, счётчик сохраняется.
- A подтверждает, B отменяет в меню. Сохранены исправления гироскопа, курков,
  времени движка, потоковое видео и защита обходчика стеков.

В config включены `fsr 1`, `render_height 540`, `fsr_sharpness 0.5`.
При возврате старого config добавь эти строки для включения FSR.

### Ограничения

Полное прохождение ещё не проверено. Цель — 60 FPS; в последнем прогоне r26
встречались 40–54 FPS и паузы до 0,4 с, загрузки могут занимать десятки секунд.
Повторяющийся NullReferenceException остаётся без установленной причины.
Новая агрегация журнала не исправляет само исключение. r28 собрана; guard и агрегация проверены host-тестами.
Повтор перехода Лимбо на устройстве после исправления ещё нужен.

[Изменения](https://github.com/Dimasick-git/ULTRAKILL-NX/blob/main/CHANGELOG.md) ·
[Проверки](https://github.com/Dimasick-git/ULTRAKILL-NX/blob/main/docs/TESTING.md) ·
[Сообщить о баге](https://github.com/Dimasick-git/ULTRAKILL-NX/issues/new/choose)

### SHA-256

```text
{{SHA1}}  ULTRAKILL-NX_PART_1.zip
{{SHA2}}  ULTRAKILL-NX_PART_2.zip
```

ULTRAKILL — Arsi “Hakita” Patala / New Blood Interactive.
Неофициальный homebrew-порт, не связан с Nintendo или авторами оригинальной игры.
