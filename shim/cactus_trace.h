#pragma once
#include <stdint.h>

/* CLOG v2: 56 bytes, little-endian, CRC32/IEEE over the first 52 bytes.
 * 0 magic[4], 4 version, 5 length, 6 stage, 7 CurrentEL
 * 8 seq32, 12 session32, 16 event32, 20 ticks64, 28 three u64 args,
 * 52 crc32. Single-core, single-producer ring, no heap/IRQ dependence. */
#define CACTUS_TRACE_BYTES 56u
#define CACTUS_TRACE_SLOTS 32u
void cactus_trace_reset(uint32_t session);
int cactus_trace_emit(uint32_t event, uint64_t a, uint64_t b, uint64_t c);
const uint8_t *cactus_trace_peek(void);
void cactus_trace_pop(void);
uint32_t cactus_trace_dropped(void);
uint32_t cactus_trace_pending(void);
