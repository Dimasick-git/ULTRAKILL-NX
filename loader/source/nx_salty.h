#pragma once
#include <stdint.h>
#include <stddef.h>
/* Wire layout used by SaltyNX 2.1.x NX-FPS (NxFpsSharedBlock). Compatible
 * telemetry producer for our NRO; no injected retail-game hooks required.
 * Protocol: https://github.com/masagrator/SaltyNX/tree/2.1.1 */
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t fps; float average; uint8_t active, locked, mode, zero_sync, patch, api;
  uint32_t ticks[10];
  uint8_t buffers,set_buffers,active_buffers,set_active_buffers,display_sync;
  uint8_t render_calls[48],viewport_calls[48];
  uint8_t original_refresh,dont_force60,suspend,refresh;
  float read_speed;
  uint8_t locked_docked; uint64_t frame; int8_t expected_buffers;
  uint8_t perf_counters[136];
} NxSaltyBlock;
_Static_assert(sizeof(NxSaltyBlock)==310,"SaltyNX 2.1.x ABI");
_Static_assert(offsetof(NxSaltyBlock,average)==5,"packed average");
_Static_assert(offsetof(NxSaltyBlock,frame)==165,"packed frame");
_Static_assert(offsetof(NxSaltyBlock,refresh)==159,"Status Monitor refresh ABI");
void nx_salty_init(void);
void nx_salty_present(uint64_t tick);
