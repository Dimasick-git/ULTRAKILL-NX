/* CPU presentation timing; swap wait includes GPU/compositor waits, not GPU time. */
#include <switch.h>
#include <stdlib.h>
#include "nx_perf.h"
#include "nx_salty.h"
#include "util.h"

static int compare_float(const void *a, const void *b) {
  const float x = *(const float *)a, y = *(const float *)b;
  return (x > y) - (x < y);
}

void nx_perf_present(uint64_t begin_tick, uint64_t end_tick, unsigned long draws) {
  nx_salty_present(end_tick);
  static uint64_t previous, start;
  static unsigned count, samples;
  static double waits, draw_sum;
  static float periods[512], maximum;
  if (!previous) { previous = start = end_tick; return; }
  const float ms = (float)((double)armTicksToNs(end_tick - previous) / 1e6);
  previous = end_tick;
  if (samples < 512) periods[samples++] = ms;
  if (ms > maximum) maximum = ms;
  waits += (double)armTicksToNs(end_tick - begin_tick) / 1e6;
  draw_sum += draws;
  ++count;
  const double elapsed_ms = (double)armTicksToNs(end_tick - start) / 1e6;
  if (elapsed_ms < 5000.0) return;
  qsort(periods, samples, sizeof periods[0], compare_float);
  const unsigned index95 = (samples * 95 + 99) / 100 - 1;
  debugPrintf("[perf] present=%.1f FPS frame=%.2fms p95=%.2fms max=%.2fms swap_wait=%.2fms draws=%.0f n=%u\n",
              count * 1000.0 / elapsed_ms, elapsed_ms / count,
              (double)periods[index95], (double)maximum, waits / count,
              draw_sum / count, count);
  count = samples = 0; waits = draw_sum = 0; maximum = 0; start = end_tick;
}
