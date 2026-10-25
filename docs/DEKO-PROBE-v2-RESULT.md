# Первый аппаратный результат deko

Копия лога: uk-build/fsr31-deko-feedback-v2/probe.log.
Результат с карты 20 октября 2026:

- PASS: subgroup, полные и частичные активные маски, float/integer reductions.
- PASS: 13 mip ranges RGBA16F, half packing, границы, read-after-write.
- FAIL: двухсекундный GPU fence в объединённом тесте prepare_luma.

Тест не различал upload/dispatch/download. Это не подтверждённый сбой
математики AMD и не доказательство работоспособности всей frame generation.
Свежий crash report относится к qlaunch/LibraryAppletThread, а не ULTRAKILL.
Обработка ошибки v2 вызывала svcExitProcess напрямую; v3 использует std::exit,
чтобы выполнить стандартные callbacks завершения libnx.

v3 разделяет GPU submissions с именованными fences, пишет числовой DkResult
и состояние очереди, добавляет независимые texture-fetch и image-store kernels
с проверкой результата на CPU. Сборка проходит. Требуется повтор на консоли.
