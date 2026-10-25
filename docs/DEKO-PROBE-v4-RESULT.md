# deko probe v4 — все аппаратные проверки прошли

Результат с карты от 20 октября 2026, 18:52:48 по времени файла.
Лог сохранён в uk-build/fsr31-deko-feedback-v4/probe.log,
машиночитаемый результат — hardware-result.json в той же папке.

Проверено на консоли:

- subgroup full/partial active masks, integer/float reductions;
- 13 mip ranges RGBA16F, half packing, bounds, read-after-write;
- RGBA8 upload/download: 16384 байта совпали;
- texture sampling: 16384 компонента совпали с CPU в пределах допуска;
- R8 image store/download, защита границ;
- настоящий AMD prepare_luma: 4096 пикселей совпали с CPU в пределах допуска.

Все именованные GPU fences: result=0, queue_error=0.
Итог: ALL GPU CHECKS PASSED.
Исправление byte strides в v4 устранило воспроизводившийся в v2/v3 тайм-аут.

Проверен конкретный probe SHA256
edf2b8016b541c5a09de8f0423f06fc9f25552ea030e2473d684d3bef43e0c6e.
Это не аппаратная проверка остальных 17 AMD passes, всех 8 профилей,
полного optical flow/frame interpolation или интеграции с Unity.
Генерации кадров в самой игре пока нет; игра на карте не заменялась.
