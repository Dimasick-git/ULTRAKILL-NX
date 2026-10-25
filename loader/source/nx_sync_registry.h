#pragma once
/* Backing objects keyed by the address of 4-byte-aligned Android pthread
 * storage. Initialized lookups never lock or allocate. Publication may allocate
 * an address record, then uses CAS; it never waits for an initializing owner. */
void *nx_sync_lookup(const void *storage);
/* Returns the published object, possibly an existing winner; NULL on OOM.
 * Caller destroys its candidate whenever the result differs from candidate. */
void *nx_sync_publish(const void *storage, void *candidate);
void *nx_sync_remove(const void *storage);
