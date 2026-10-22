#pragma once

#include <stdint.h>

/*
 * Best-effort persistent trace for the no-UART shim.
 *
 * The on-screen console remains the authoritative debug output.  This trace
 * only exists to leave a breadcrumb in the CACTUS pstore region if the shim
 * resets before it can draw the panic screen.
 */

#define DBG_MAGIC		0x31424443U	/* "CDB1" */
#define DBG_VERSION		1U
#define DBG_BASE		0x47c90000UL
#define DBG_TRACE_OFF		0x1000UL
#define DBG_TRACE_COUNT		256U
#define DBG_NAME_LEN		16U

struct dbg_entry {
	uint32_t seq;
	uint32_t cpu;
	uint64_t ticks;
	char name[DBG_NAME_LEN];
	uint64_t a, b, c;
};

struct dbg_header {
	uint32_t magic;
	uint32_t version;
	uint32_t seq;
	uint32_t count;
	uint64_t entry_ticks;
	uint64_t last_ticks;
	uint32_t cpu;
	uint32_t flags;
	char last[DBG_NAME_LEN];
	uint64_t a, b, c;
	uint32_t reserved0;
	uint32_t reserved1;
};

void dbg_init(void);
void dbg_stage(const char *name);
void dbg_mark(const char *name, uint64_t a, uint64_t b, uint64_t c);
void dbg_flush(void);
