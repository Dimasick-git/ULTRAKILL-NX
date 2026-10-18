/* Caller serializes access. Keep initial samples and periodic identical errors;
 * counts remain available even when duplicate SD output is suppressed. */
#ifndef NX_LOG_REPEAT_H
#define NX_LOG_REPEAT_H
#include <stdint.h>
#include <string.h>
typedef struct {
  char text[4096];
  uint64_t since;
  unsigned samples, suppressed;
} NxLogRepeat;
static int nx_log_repeat(NxLogRepeat *s, const char *text, uint64_t now,
                         uint64_t interval, unsigned *repeated) {
  *repeated = 0;
  /* Do not collapse truncated messages: their suffixes may differ. */
  if (strlen(text) >= sizeof(s->text)) return 1;
  if (strcmp(s->text, text)) {
    *repeated = s->suppressed;
    strcpy(s->text, text); s->since = now; s->samples = 1; s->suppressed = 0;
    return 1;
  }
  if (s->samples < 4) { ++s->samples; return 1; }
  if (now - s->since >= interval) {
    *repeated = s->suppressed;
    s->suppressed = 0; s->since = now;
    return 1;
  }
  ++s->suppressed;
  return 0;
}
#endif
