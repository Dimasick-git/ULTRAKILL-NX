# Звук r41

Исходный лог: uk-build/r40-audio-feedback/debug.log, запуск r40.
Пользователь подтвердил: звук пропал после перехода сцены. Один FMOD OpenSL
player: 24000 Hz stereo, SDL 48000 Hz. Причина конкретного обрыва пока не доказана.

Воспроизведены и исправлены ошибки:
- SetMute(false) не восстанавливал звук после SetMute(true). Теперь mute
  хранится отдельно от volume. Громкость меняется под mute и восстанавливается
  при unmute; намеренно нулевая громкость остаётся нулевой.
- GetMute/GetVolumeLevel возвращают действительное состояние.
- AudioManager: согласованные music volume/max = 15/15 вместо 0;
  requestAudioFocus = GRANTED. Физическую громкость применяет Horizon/SDL.

Тест tools/test_audio_output.py компилирует настоящие mixer/volume функции:
PCM до/после mute, смена громкости под mute, 100 циклов unmute, ручной ноль,
completion callback заглушённого звука; реальные JNI ветви volume/focus.

Диагностика [audio] каждые 300 кадров: callbacks, audible, peak с предыдущего
снимка, play/mute/volume_mb/queued. Счётчики атомарные; основной поток не ждёт
mixer mutex. audible включает видео: важны приращения после его окончания.
Нулевой peak допустим в тишине; автоматически снимать mute нельзя.

Аппаратный тест r41 ещё нужен: меню → 0-5 → 1-1, музыка/стрельба,
пауза/продолжение, повторный переход, намеренное выключение громкости.

Сверено с NDK SLES/OpenSLES.h: SLmillibel — int16_t, а не int32_t.
Исправлена ширина GetVolume/GetMaxVolume, тест проверяет соседние canary.
SL_RESULT_PARAMETER_INVALID исправлен с INTERNAL_ERROR (13) на 2.
