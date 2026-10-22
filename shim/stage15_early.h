#pragma once
#include <stdint.h>

/* Minimal real-shim entry probe, only used by STAGE15_EARLY=1..4 builds. */
void stage15_early_main(uintptr_t dtb);
void stage15_early_fault(uint64_t vector, uint64_t esr, uint64_t elr,
                         uint64_t far, uint64_t spsr);
