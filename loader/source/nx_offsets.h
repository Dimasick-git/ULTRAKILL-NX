/* nx_offsets.h -- смещения внутри libil2cpp.so, зависящие от конкретной сборки игры.
 *
 * В порте Papers, Please они были зашиты в код (pp_il2cpp.h) и годились только
 * для его libil2cpp. У нас Android-сборку делаем мы сами, поэтому смещения
 * выводятся из символов НАШЕЙ сборки (tools/derive_offsets.py читает
 * libil2cpp.sym.so из symbols.zip) и лежат на карте рядом с .nro в offsets.ini.
 *
 * Нет файла или нет ключа -> 0. Ноль везде означает «функция выключена»:
 * лучше не поставить заплатку, чем записать по чужому адресу. */
#ifndef NX_OFFSETS_H
#define NX_OFFSETS_H

#include <stdint.h>

#define NXO_GC_SUSPEND_SIG  "il2cpp.gc_suspend_sig"   /* int: GC_sig_suspend          */
#define NXO_GC_RESTART_SIG  "il2cpp.gc_restart_sig"   /* int: GC_sig_thr_restart      */
#define NXO_GC_START_ACK    "il2cpp.gc_start_ack"     /* int: GC_retry_signals         */
#define NXO_GC_ACK_SEM      "il2cpp.gc_ack_sem"       /* sem_t: GC_suspend_ack_sem     */
#define NXO_JAVAVM          "il2cpp.javavm"           /* JavaVM*, пишет JNI_OnLoad     */
#define NXO_TIME_DELTA      "il2cpp.time_get_deltaTime"
#define NXO_TIME_FRAMECOUNT "il2cpp.time_get_frameCount"
#define NXO_TIME_REALTIME_D "il2cpp.time_get_realtimeSinceStartupAsDouble"
#define NXO_TIME_REALTIME_F "il2cpp.time_get_realtimeSinceStartup"

/* Читает <корень игры>/offsets.ini. Повторный вызов ничего не делает. */
void nxo_load(void);

/* Смещение от начала модуля или 0, если ключа нет. */
uint32_t nxo_get(const char *key);

/* Обход ключей с префиксом и ровно nvals числами (patch.* -- 3, poke8.* -- 2). */
typedef void (*nxo_cb)(void *ctx, const char *key, const uint32_t *v);
int nxo_each(const char *prefix, int nvals, nxo_cb cb, void *ctx);

#endif
