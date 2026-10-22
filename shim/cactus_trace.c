#include "cactus_trace.h"

/* Dedicated temporary Stage 14 probe memory. Do not transfer this BSS
 * allocation directly to XNU: Stage 15 must reserve shared DRAM in FDT. */
static uint8_t frames[CACTUS_TRACE_SLOTS][CACTUS_TRACE_BYTES];
static uint32_t rd_index, wr_index, pending, next_sequence, session_id, lost;

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}

static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)value);
    put32(p + 4, (uint32_t)(value >> 32));
}

static uint32_t crc32(const uint8_t *data, unsigned length)
{
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void cactus_trace_reset(uint32_t session)
{
    rd_index = wr_index = pending = next_sequence = lost = 0;
    session_id = session;
}

int cactus_trace_emit(uint32_t event, uint64_t a, uint64_t b, uint64_t c)
{
    const uint32_t sequence = next_sequence++;
    if (pending == CACTUS_TRACE_SLOTS) {
        ++lost;
        return 0;
    }
    uint8_t *p = frames[wr_index];
    p[0] = 'C'; p[1] = 'L'; p[2] = 'O'; p[3] = 'G';
    p[4] = 2; p[5] = CACTUS_TRACE_BYTES;
#ifndef CACTUS_TRACE_STAGE
#define CACTUS_TRACE_STAGE 14
#endif
    p[6] = CACTUS_TRACE_STAGE;
    uint64_t level, ticks;
#if defined(CACTUS_TRACE_HOST_TEST)
    level = 8u; ticks = (uint64_t)sequence * 13000u;
#else
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(level));
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
#endif
    p[7] = (uint8_t)level;
    put32(p + 8, sequence);
    put32(p + 12, session_id);
    put32(p + 16, event);
    put64(p + 20, ticks);
    put64(p + 28, a);
    put64(p + 36, b);
    put64(p + 44, c);
    put32(p + 52, crc32(p, 52));
    wr_index = (wr_index + 1u) % CACTUS_TRACE_SLOTS;
    ++pending;
    return 1;
}

const uint8_t *cactus_trace_peek(void)
{
    return pending ? frames[rd_index] : 0;
}

void cactus_trace_pop(void)
{
    if (!pending) return;
    rd_index = (rd_index + 1u) % CACTUS_TRACE_SLOTS;
    --pending;
}

uint32_t cactus_trace_dropped(void) { return lost; }
uint32_t cactus_trace_pending(void) { return pending; }
