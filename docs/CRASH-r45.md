# TEST8 crash → r45 / TEST9

Отчёт: `uk-build/r44-crash-feedback/crash.log`, исходное имя на карте
01792526198_01005ce3b1a40000.log. Последний boot — r44. Native build-id
fdb9ca6cd527b5fe104f1f4f7c62cdfa461608e5.

Это User Break / 0x767. Символизация стека:
diagAbortWithResult → dk::detail::RaiseError → Device::allocMem →
MemBlock::initialize → dkMemBlockCreate → GpuBackend::allocate/Create →
GpuBackend::Shared → Session::initialize/render. Вылет во время создания
ресурсов FG для новой сцены; это другой сбой, чем старый null memcpy в
ArchiveStorageReader. Старый menu crash этим отчётом не объясняется.

В release deko ошибочный allocation callback вызывает RaiseError до того,
как наш caller получает nullptr. r45 выделяет большие backing buffers
самостоятельно и проверяет результат ДО dkMemBlockCreate. Storage передаётся
в DkMemBlockMaker; память освобождается после unmap/teardown GPU backend.
При карантине не освобождается. Это обход известного аварийного пути
выделения backing memory, не обещание обработки всех ошибок драйвера deko.

Проверка с инъекцией отказов: allocation node, aligned backing, overflow и
ошибочное выравнивание; explicit release и отсутствие вызова deko без storage.
Все проходят. Также перед уничтожением сессии проверяется последний GLES
consumer fence; при ошибке сохраняется весь backend.

В этом запуске GPU marker и полный native graph заработали. Однако reset
кадр стоил 66.35 ms, следующие два — 122.72 и 120.92 ms. Зафиксированное
окно вывода — 14.8 FPS. FastLoad к этому моменту уже восстановил Normal.
Продолжать такую генерацию ради ON в настройках нельзя считать ускорением.

r45 игнорирует стоимость первого кадра, затем при двух подряд затратах FG
свыше 16.667 ms отключает её до явного переключения OFF → ON. Камерные
сбросы истории не обходят защиту. Новая сцена не запускает дорогую FG заново.
Запрошенный параметр frame_generation остаётся 1 по умолчанию; это запрос,
а не подтверждение активного вывода. Об отключении пишется automatic fallback.

Официальные шейдеры/математика AMD не менялись. Время 120 ms требует отдельной
оптимизации GPU-графа; этот патч устраняет известный allocation crash path
и ограничивает вред производительности, а не превращает FG в готовое ускорение.
Игровой аппаратный тест r45 ещё нужен. IL2CPP оставлен от r44:
ff699cbb468d1f8038d4451fdad35ce182a33816; физика не менялась.
