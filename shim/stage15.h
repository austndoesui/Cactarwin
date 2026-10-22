#pragma once
#include <stdint.h>

/* Shim-owned, MMU-off diagnostic USB transport. XNU is not its owner. */
void stage15_begin(uintptr_t dtb);
void stage15_mark(uint32_t event, uint64_t a, uint64_t b, uint64_t c);
void stage15_fault(uint64_t slot, uint64_t esr, uint64_t elr,
                   uint64_t far, uint64_t spsr);
void stage15_end(uint32_t reason);
void stage15_el1_ready(uintptr_t entry, uintptr_t boot_args);
unsigned stage15_preflight_ok(void);

#define S15_ENTRY         0x1500u
#define S15_USB_READY     0x1501u
#define S15_DTB_OK        0x1502u
#define S15_PAYLOAD_OK    0x1503u
#define S15_LAYOUT_OK     0x1504u
#define S15_SEGMENT       0x1505u
#define S15_BOOTARGS      0x1506u
#define S15_EL1_READY     0x1507u
#define S15_XNU_BRANCH    0x1508u
#define S15_DIAG_STOP     0x15ffu
#define S15_ERROR         0x15e0u
#define S15_FAULT         0x15efu
