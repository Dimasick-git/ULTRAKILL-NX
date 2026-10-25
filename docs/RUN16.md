# Исправление запуска форвардера r16 — 18.10.2026

## Найденный системный отчёт

`E:/atmosphere/erpt_reports/cb81ee9c-4e89-d14d-5564-350c887fd677`
декодирован как MessagePack. Он относится к ApplicationID
`01005ce3b1a40000` и содержит ErrorCode `2001-0104`.
Копия: `~/uk-build/backup-run15/launch-error.erpt`.

2001-0104 соответствует KernelError_OutOfMemory (104). В NPDM r15 остался
`application_type=2` из исходного nx-hbloader: это Applet. Для него загрузчик
выбирает пул Applet; служебный защищённый резерв этого пула ограничен 4 МБ.
r15 запросила 32 МБ и получала отказ ещё при создании процесса.
Поэтому нового запуска в debug.log игры нет.

Первичные исходники:

- [Выбор пула по ApplicationType](https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/loader/source/ldr_process_creation.cpp).
- [Ограничение Applet secure memory](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/board/nintendo/nx/kern_k_system_control.cpp).

Также в упакованной r15 ID ACI был заменён hacBrewPack на ID игры, но диапазон
ACID оставался ID исходного hbloader. Это отдельная несогласованность;
именно ошибку 2001-0104 объясняет ошибочный пул Applet, а не диапазон ID.

## Исправление

В forwarder/hbl.json теперь:

- `application_type=1` (Application), пул Application.
- `program_id` и обе границы `program_id_range_*` — `01005CE3B1A40000`.
- имя процесса `ULTRAKILL_NX`.
- Сохранены 39-битное пространство, 32 МБ SystemResource и права Map/Unmap.

Версия NACP — r16. NRO остаётся demand-memory-r15: код игры не менялся.

## Проверка готового пакета

Создан `~/uk-build/nsp-r16/hacbrewpack_nsp/01005ce3b1a40000.nsp`.
NSP разобран как PFS0; границы файлов и SHA-256 всех трёх NCA проверены.
Program NCA извлечена hactool с локальными ключами, из её ExeFS извлечён
main.npdm. К нему применён новый `tools/validate_forwarder.py`.

Проверка подтверждает одновременно тип Application в ACI/ACID, пул Application,
попадание ID в разрешённый диапазон, 39-битное пространство, 32-МБ резерв
и разрешения физических отображений. Та же проверка отклоняет исходную r15
по диапазону ID и, после его исправления в тестовой копии, по типу Applet.

Пакет r15 с карты сохранён в `uk-build/backup-run15/card-forwarder.nsp`.
Для проверки на Switch требуется переустановить новый NSP через DBI с заменой
ранее установленного форвардера и запустить ULTRAKILL NX с HOME.
Успешный запуск r16 и работоспособность игры на консоли ещё не подтверждены.
