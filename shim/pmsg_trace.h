#pragma once

#include <stdint.h>

/* MT6765 ramoops pmsg region; only used on the known cactus layout. */
void pmsg_trace_mark(const char *event, uint64_t a, uint64_t b, uint64_t c);
