# r43 — работа после обратной связи по r42

Не опубликован и не упакован. Текущий native NRO в рабочей папке ещё содержит
boot marker r42; его нельзя выдавать за проверенный установщик r43.

## Изображение

r42 восстанавливал MRT сразу после SetupRTs. Дальше тот же callback вызывает
RenderSkyboxes/вложенные Camera.Render. Восстановление перенесено в финальный
IL_048e, после вложенных вызовов, только для mainCam. HUD callback не меняется.
Host test сбрасывает привязку внутри вложенной отрисовки: 100 восстановлений,
без выделения памяти; проверки невалидных целей и несовпадения ABI проходят.
Incremental IL2CPP: il2cpp-r43-mrt.log, 9 successful nodes / 0 failed.
Аппаратного подтверждения исправления изображения пока нет.

eglGetProcAddress теперь возвращает те же обёртки, что ELF imports/dlsym.
Раньше этот путь обходил часть диагностик. Неизвестные функции по-прежнему
разрешаются штатным eglGetProcAddress.

## Возврат в Main Menu

Лог: uk-build/r42-transition-feedback/debug.log.
Crash: 01792510146_01005ce3b1a40000.log (копия crash.log в той же папке).
Module ID native AB99DD00411C3B8CEC190625112BFF660A885A5E.
libunity virtbase 0x1bf9efc000; отчёт Atmosphere показывает неверную базу
анонимного модуля — для символизации она непригодна.

PC memcpy с dst=0, bytes=0x18d89. LR libunity+0x8d1c40:
ArchiveStorageReader::ReadBlock. Следующий подтверждённый адрес возврата из
stack dump: 0x1bfa7c85e0 => +0x8cc5e0 (ArchiveStorageReader::Read),
затем 0x1bfa7c83c0 => +0x8cc3c0 (ArchiveReadFile::Read).
Это НЕ ReadHeader; прежнее предположение со смещением +0x8d15e0 неверно.
Нулевой буфер поступает сверху; причина пока не доказана. Нет глобального
обхода memcpy или фиктивного успешного чтения. Добавлены прозрачные обёртки
malloc/calloc/realloc/memalign: первые 16 ошибок сохраняются без аллокаций и
логирования в пути отказа; периодический diagnostic печатает malloc arena,
занято/свободно и адреса вызывавших функций. Native build проходит.

## deko / FG

На Switch прошли v6 (общий PBO GLES → deko → GLES), v8 (полный граф AMD:
сброс + два кадра с историей) и v9 (движущийся объект). Для v9 три
промежуточных кадра проверены по центру объекта: между двумя исходными
положениями, а не копия текущего кадра. Лог заканчивается ALL GPU CHECKS PASSED.
Копия: uk-build/fsr31-deko-feedback-v9/probe.log.

На карте только отдельный probe v9, SHA256
6212323b47da4f04c318c13009a7a6265256a5d56c52d4656ce8b1be8728c501.
Игра/NSP на карту не копировались.

Игровая интеграция добавлена в исходники: Unity передаёт цвет, глубину и
векторы движения; native callback запускает официальные CPU-планировщики AMD
и 18 deko compute passes. Глубина float32 и motion half2 передаются побитно
через RGBA8 PBO. GPU-проверка упаковки Unity проходит. EGL presenter выводит
промежуточный и текущий кадры, не вызывает второй Update/FixedUpdate.
В настройках отдельный frame_generation, по умолчанию 1. Сброс истории при
смене сцены/камеры, разрыве frame ID, телепорте и изменении FOV.

Ограничения: профиль SDR/FP32/standard depth/low-resolution motion vectors;
HUD пока включён в интерполяцию. Реальная игра, вывод двух кадров и его
стоимость на 720p ещё требуют аппаратной проверки. 60 FPS не подтверждены.
Native r43 собран; Android export: Succeeded, errors 0. IL2CPP: 406 successful
nodes, 0 failed. Готов ULTRAKILL-NX_2.0.0_TEST7_FULL.nsp, 3 806 683 352 байт,
SHA256 d15220fd66d7616fd8098e63dcc4892493951d208c97aa92f042b330e8c81dc5.
Повторная проверка: 221 payload files, NCA hashes / RomFS IVFC / payload hashes
успешны. Сборка на Switch ещё не запускалась.

Native build-id a88771b0b3b96b6b7f06e6774a9448ea7bce315f.
IL2CPP build-id 8e9f930e9bb12b3028567a03e33a8192bfef8deb.
Символы и исходники сохранены в uk-build/symbols/r43-<native build-id>:
debug-symbols.7z, generated-cpp.7z, source.7z и отдельно изменённый runtime.

Коммитов, push и публикаций нет.
