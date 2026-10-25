# deko probe v3: найден неверный pitch

Аппаратный лог: uk-build/fsr31-deko-feedback-v3/probe.log.
Subgroup и packed mip проверки снова прошли. Тайм-аут DkResult=2,
queue_error=0 возникает на RGBA8 buffer-to-image upload. До dispatch AMD
prepare_luma выполнение не дошло.

В тесте DkCopyBuf.rowLength ошибочно был задан в пикселях (64) вместо
байтов (256 для RGBA8). imageHeight тоже ошибочно задавался как высота
в пикселях, хотя поле используется как stride слоя в байтах.
Исходник devkitPro/deko3d, revision 373bfd43d8b2d805fa2eb7d406dbb823032df958,
source/dk_image.cpp подтверждает:

- srcInfo.m_horizontal = rowLength либо width * bytesPerBlock;
- srcInfo.m_layerStride = imageHeight либо height * m_horizontal;
- аналогичная логика в copyImageToBuffer.

v4 задаёт оба поля 0, чтобы deko сам вывел tightly packed byte strides
из формата. Исправлены upload RGBA8, RGBA8 roundtrip и download R8.
Остальные GPU проверки сохранены. Сборка успешна; аппаратного результата v4
пока нет. Это исправление диагностического теста, не рендера самой игры.

На карту записан E:/switch/fsr31-deko-probe.nro:
576632 bytes, SHA256 edf2b8016b541c5a09de8f0423f06fc9f25552ea030e2473d684d3bef43e0c6e.
Хеш проверен чтением файла обратно с карты. Игра не заменялась.
