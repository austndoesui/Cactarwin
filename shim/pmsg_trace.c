/*
 * Mirror early cactus milestones into Linux 4.9 ramoops pmsg.
 *
 * Recovery reports 0xe0000 bytes at 0x47c90000, pmsg_size=0x10000,
 * ecc=0, and uses a 12-byte persistent_ram_buffer header. The pmsg zone
 * is at the END of the reserved range. This writes only that zone, so a
 * subsequent warm boot of TWRP can expose pmsg-ramoops-0 without /dev/mem.
 *
 * The writer is synchronous and intended for one early boot CPU with the
 * MMU off. It is diagnostic, and does not make DRAM survive a power loss.
 */
#include "pmsg_trace.h"

#if defined(BOARD_CACTUS)

#define PMSG_BASE 0x47d60000UL
#define PMSG_ZONE_SIZE 0x10000U
#define PMSG_HEADER_SIZE 12U
#define PMSG_DATA_SIZE (PMSG_ZONE_SIZE - PMSG_HEADER_SIZE)
#define PMSG_SIGNATURE 0x43474244U /* persistent_ram_core: 'DBGC' */

static void clean_range(uintptr_t addr, unsigned len)
{
    uintptr_t p = addr & ~(uintptr_t)63;
    uintptr_t end = addr + len;
    for (; p < end; p += 64)
        __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

static void append_char(char *out, unsigned *n, char c)
{
    if (*n < 127)
        out[(*n)++] = c;
}

static void append_text(char *out, unsigned *n, const char *text, unsigned max)
{
    while (max-- && *text)
        append_char(out, n, *text++);
}

static void append_hex(char *out, unsigned *n, uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    append_text(out, n, "0x", 2);
    for (int shift = 60; shift >= 0; shift -= 4)
        append_char(out, n, hex[(value >> shift) & 15]);
}

void pmsg_trace_mark(const char *event, uint64_t a, uint64_t b, uint64_t c)
{
    volatile uint32_t *header = (volatile uint32_t *)PMSG_BASE;
    volatile char *data = (volatile char *)(PMSG_BASE + PMSG_HEADER_SIZE);
    uint32_t start = header[1];
    uint32_t size = header[2];
    char line[128];
    unsigned len = 0;

    append_text(line, &len, "CACTUS/SHIM ", 12);
    append_text(line, &len, event, 31);
    append_text(line, &len, " a=", 3);
    append_hex(line, &len, a);
    append_text(line, &len, " b=", 3);
    append_hex(line, &len, b);
    append_text(line, &len, " c=", 3);
    append_hex(line, &len, c);
    append_char(line, &len, '\n');

    if (header[0] != PMSG_SIGNATURE || size > PMSG_DATA_SIZE || start > size) {
        start = 0;
        size = 0;
        header[0] = PMSG_SIGNATURE;
        header[1] = 0;
        header[2] = 0;
    }

    /* Exactly the ring format read by persistent_ram_save_old(). */
    uint32_t old_start = start;
    unsigned first = len;
    if (first > PMSG_DATA_SIZE - start)
        first = PMSG_DATA_SIZE - start;
    for (unsigned i = 0; i < first; i++)
        data[start + i] = line[i];
    for (unsigned i = first; i < len; i++)
        data[i - first] = line[i];

    clean_range((uintptr_t)(data + old_start), first);
    if (first < len)
        clean_range((uintptr_t)data, len - first);

    start += len;
    if (start >= PMSG_DATA_SIZE)
        start -= PMSG_DATA_SIZE;
    if (size + len < PMSG_DATA_SIZE)
        size += len;
    else
        size = PMSG_DATA_SIZE;
    header[1] = start;
    header[2] = size;
    clean_range(PMSG_BASE, PMSG_HEADER_SIZE);
}

#else

void pmsg_trace_mark(const char *event, uint64_t a, uint64_t b, uint64_t c)
{
    (void)event;
    (void)a;
    (void)b;
    (void)c;
}

#endif
