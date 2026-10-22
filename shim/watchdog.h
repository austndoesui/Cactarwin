#pragma once

#include <stdint.h>

void wdt_early_init(void);
void wdt_init(unsigned seconds);
void wdt_ping(void);
void wdt_check(void);
void wdt_disable(void);
void wdt_sw_reset(void) __attribute__((noreturn));
uint32_t wdt_mode(void);
unsigned wdt_timeout(void);
