/* util.c -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "config.h"
#include "nx_gameroot.h"

// Thread-safe, file-only logger. We open+flush+close on every call so the last
// lines survive a hard crash, and serialise with a mutex because the engine
// logs from several worker threads. No nxlink/socket: this must work on bare
// hardware. The log lands in the game dir (main() chdir()s there at startup).
#if DEBUG_LOG
static Mutex g_log_lock; // libnx Mutex: 0 == unlocked, no init needed
#endif

int debugPrintf(char *text, ...) {
#if DEBUG_LOG
  static FILE *f = NULL;
  va_list list;
  mutexLock(&g_log_lock);
  if (!f) f = fopen(game_log_path(), "a");   // open once, keep open (SD fopen/close per line is very slow)
                                             // path is resolved at runtime -- see nx_gameroot.c
  if (f) {
    va_start(list, text);
    vfprintf(f, text, list);
    va_end(list);
    /* Per-line SD flushes stall the main thread during bursts of Unity logs.
     * Keep at most one second buffered; fatal/watchdog diagnostics flush now. */
    static uint64_t last_flush = 0;
    const uint64_t now = armGetSystemTick();
    if (!last_flush || now - last_flush >= armNsToTicks(1000000000ULL) ||
        strstr(text, "FAIL") || strstr(text, "fatal") || strstr(text, "abort") ||
        strstr(text, "[wd]")) {
      fflush(f);
      last_flush = now;
    }
  }
  mutexUnlock(&g_log_lock);
#else
  (void)text;
#endif
  return 0;
}

// Per-thread bionic TLS. The engine reads its stack canary from tpidr_el0+0x28;
// every thread that runs engine code needs its OWN zeroed block here. A single
// shared block races: one thread's TLS writes (including the guard slot) corrupt
// another thread's in-flight canary, tripping a false __stack_chk_fail. `buf`
// must outlive the thread (TPIDR_EL0 points into it until the thread exits).
void install_bionic_tls(void *buf) {
  memset(buf, 0, BIONIC_TLS_SIZE);
  armSetTlsRw((uint8_t *)buf + BIONIC_TLS_TP_OFFSET);
}

// boost the CPU to 1785MHz while loading
void cpu_boost(int on) {
  appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
}

int ret0(void) { return 0; }

int retm1(void) { return -1; }
