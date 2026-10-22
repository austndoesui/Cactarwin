#pragma once

#include <stdint.h>

#define BOOTMODE_OFF		0
#define BOOTMODE_FASTBOOT	1
#define BOOTMODE_RECOVERY	2

#if defined(BOARD_CACTUS)
#define BOOTMODE_DEFAULT_TARGET	BOOTMODE_FASTBOOT
#else
#define BOOTMODE_DEFAULT_TARGET	BOOTMODE_OFF
#endif

#ifndef AUTO_BOOT_TARGET
#define AUTO_BOOT_TARGET	BOOTMODE_DEFAULT_TARGET
#endif

int bootmode_probe(uint16_t *pdn1);
void bootmode_prepare_reset(void);
