#pragma once
#include <stdint.h>
#include <stddef.h>

/* Subtraction avoids overflow for malformed frame/return pointers. */
static inline int diag_span_contains(uint64_t base, uint64_t size,
                                     uint64_t address, size_t bytes) {
  return bytes && address >= base && (uint64_t)bytes <= size &&
         address-base <= size-(uint64_t)bytes;
}

/* A saved return address may point at the end of a segment, but its preceding
 * instruction must be entirely inside that executable segment. In particular,
 * a module/segment base found on the stack is NOT a readable call site. */
static inline int diag_return_site(uint64_t base, uint64_t size,
                                   uint64_t return_address, uint64_t *site) {
  if ((return_address & 3) || return_address < 4 ||
      !diag_span_contains(base, size, return_address-4, 4)) return 0;
  *site = return_address-4;
  return 1;
}
