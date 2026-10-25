## ULTRAKILL — NX · r1 / {{VERSION}}

Порт от **Dimasick-git**. Главный тестер, скриншоты и видео — **Ryazhenka-helper-01 / Zeropaxy**.

Скачай **оба** файла и распакуй в одну папку, объединяя папки.
Это обычные ZIP с разным содержимым; склейка не нужна, порядок не важен.
В комплекте все изменения, отдельно старые патчи скачивать не требуется.

| Архив | Байт |
| :-- | --: |
| [ULTRAKILL-NX_PART_1.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_1.zip) | {{BYTES1}} |
| [ULTRAKILL-NX_PART_2.zip](https://github.com/Dimasick-git/ULTRAKILL-NX/releases/download/r1/ULTRAKILL-NX_PART_2.zip) | {{BYTES2}} |

Скопируй `switch`, `atmosphere`, `SaltySD` в корень microSD, установи `ULTRAKILL_NX_forwarder.nsp`
через DBI и запускай с HOME. Если форвардер уже установлен, переустановка не нужна.
Перед обновлением сохрани `Saves`, `Preferences`, `LocalPrefs.json`, `config.txt`.
После замены верни личные настройки; удали runtime-кэш `switch/ultrakill/il2cpp`.
Полная инструкция и манифест файлов находятся внутри обоих архивов.

### В комплекте r32

- Исправлено обращение GunControl.Update к отсутствующей мыши; шаг физики штатный.
- SaltyNX 2.1.3 и прямой мост NX-FPS: реальные EGL FPS/frametime, исправление
  исчерпанного бюджета IPC-сессий и ограниченные повторные подключения.
- Совместимое управление через FSR-NX/Ryazha-clk: масштаб 25–100%, резкость и
  EASU+RCAS в Unity/GLES. 50%=360p, 75%=540p, выход 720p. Lite/NVN/HDR/FSRE не реализованы.
- Чтение частоты дисплея, запрос 60 Гц при reported>60 в handheld и проверка ответа.
- Штатный CPU FastLoad на старте и Single-загрузках, снятие перед игрой,
  timeout 60 с, HOME/focus/exit и повтор неудачного сброса. Additive не бустит.
- Сохранены гироскоп, Nintendo A/B, курки, видеоролики, включённый GC и защита Umbra
  на переходе Лимбо. Старые патчи отдельно не нужны.

Включены FSR 540p → 720p, SaltyNX bridge и loading_boost. Для новой установки
maxGore=64; личные настройки при обновлении сохрани. FSR-NX оверлей не включён;
без него высоту рендера можно менять через config.txt.

Скопируй также `atmosphere` и `SaltySD`. После обновления системного модуля
**полностью перезагрузи Switch в Atmosphere**.

### Проверено и что остаётся

Сборки r32, GPU-тест EASU/RCAS и host/CI проверки пройдены. Подключение SaltyNX
подтверждено аппаратным журналом r31, найденный поток NRE исчез в r30/r31.
Переход Лимбо 1-1 → 1-2 пройден на r28 без закрытия программы.
Новые live-масштаб, refresh и FastLoad r32 ещё требуют проверки на Switch.
Полное прохождение не завершено; в тяжёлых сценах остаются низкий FPS и паузы.
Стабильные 60 FPS не заявляются.

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
