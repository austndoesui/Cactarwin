/*
 * Stage19-A: copy the actual ten XNU segments, Apple ADT and boot_args
 * into the FDT-verified 480 MiB DRAM window. Verify every copy while
 * retaining the Stage18 USB logger and working TWRP return.
 * No EL change, MMU enable, kernel jump or persistent storage writes.
 */
#include <stdint.h>
#include "cactus_trace.h"
#include "cactus_usb.h"
#include "fdt.h"
#include "fdt_ranges.h"
#include "payload_layout.h"
#include "console.h"

#ifndef PAYLOAD_ADDR
#error Stage19 requires the physical pointer to the embedded XNU payload
#endif
#ifndef PAYLOAD_LENGTH
#error Stage19 requires the embedded XNU payload length
#endif

enum {
    S19_ENTRY=0x1900, S19_USB=0x1901, S19_BEAT=0x1902,
    S19_FDT=0x1910, S19_VALID=0x1911, S19_PAYLOAD=0x1912,
    S19_BEFORE_COPY=0x1930,
    S19_SEG_START=0x1940, S19_SEG_DONE=0x1941,
    S19_ADT=0x1950, S19_BOOTARGS=0x1951, S19_COPY_DONE=0x1952,
    S19_DONE=0x19ff, S19_FAULT=0x19ee
};

struct boot_video {
    uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};
struct boot_args {
    uint16_t Revision, Version;
    uint64_t virtBase, physBase, memSize, topOfKernelData;
    struct boot_video Video;
    uint32_t machineType;
    uint64_t deviceTreeP;
    uint32_t deviceTreeLength;
    char CommandLine[608];
    uint64_t bootFlags, memSizeActual;
};
_Static_assert(__builtin_offsetof(struct boot_args, Video)==40,
               "boot_args Video offset");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine)==108,
               "boot_args cmdline offset");
_Static_assert(sizeof(struct boot_args)==736, "boot_args ABI");

static uint32_t usb_diag[16];
static uint64_t frequency;
static unsigned online, faulted, heartbeat_count;
static void mark(unsigned event, uint64_t a, uint64_t b, uint64_t c);
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
static uint64_t loaded_entry_pa, loaded_boot_args_pa;
#endif
#if defined(CACTUS_STAGE20B) && CACTUS_STAGE20B
#define STAGE20B_ENTRY  0x8142d488ull
#define STAGE20B_ENTRY_TARGET 0x8142f0ecull
#if defined(CACTUS_STAGE20C) && CACTUS_STAGE20C
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
#if defined(CACTUS_STAGE22) && CACTUS_STAGE22
#define STAGE20B_TARGET 0x81ac0c0cull
#define STAGE20B_ORIG   0xb0ffacc1u /* adrp x1, early kernel data */
#define STAGE20B_BRK    0xd421c480u /* brk #0xe24 */
#define STAGE20B_IMM    0xe24u
#define STAGE20B_ARM_EVENT 0x2204u
#define STAGE20B_TRAP_EVENT 0x2206u
#define STAGE20B_RESTORE_EVENT 0x220bu
#define STAGE22_TARGET_VA 0xfffffff007ac0c0cull
#elif defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
#define STAGE20B_TARGET 0x81ac061cull
#define STAGE20B_ORIG   0xa903ffffu /* stp xzr,xzr,[sp,#48] */
#define STAGE20B_BRK    0xd421c460u /* brk #0xe23 */
#define STAGE20B_IMM    0xe23u
#define STAGE20B_ARM_EVENT 0x21e0u
#define STAGE20B_TRAP_EVENT 0x21e1u
#define STAGE20B_RESTORE_EVENT 0x21e8u
#define STAGE21D_TARGET_VA 0xfffffff007ac061cull
#elif defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
#define STAGE20B_TARGET 0x8142f490ull
#define STAGE20B_ORIG   0xd2a00600u /* movz x0, #0x30, lsl #16 */
#define STAGE20B_BRK    0xd421c440u /* brk #0xe22 */
#define STAGE20B_IMM    0xe22u
#define STAGE20B_ARM_EVENT 0x21d0u
#define STAGE20B_TRAP_EVENT 0x21d1u
#define STAGE20B_RESTORE_EVENT 0x21d7u
#elif defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
#define STAGE20B_TARGET 0x8142f474ull
#define STAGE20B_ORIG   0xd518c000u /* msr vbar_el1, x0 */
#define STAGE20B_BRK    0xd421c420u /* brk #0xe21 */
#define STAGE20B_IMM    0xe21u
#define STAGE20B_ARM_EVENT 0x21c0u
#define STAGE20B_TRAP_EVENT 0x21c1u
#define STAGE20B_RESTORE_EVENT 0x21c5u
#else
#define STAGE20B_TARGET 0x8142f154ull
#define STAGE20B_ORIG   0xf0ffea59u /* adrp x25, 0x8117a000 */
#define STAGE20B_BRK    0xd421a400u /* brk #0xd20 */
#define STAGE20B_IMM    0xd20u
#define STAGE20B_ARM_EVENT 0x20d0u
#define STAGE20B_TRAP_EVENT 0x20d1u
#define STAGE20B_RESTORE_EVENT 0x20d5u
#endif
#if defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
#define STAGE20D_VECTOR_BASE 0x81429000ull
#define STAGE21C2_VBAR_VA 0xfffffff007429000ull
#else
#define STAGE20D_VECTOR_BASE 0x8142e000ull
#endif
#if defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
#define STAGE20D_SYNC_SLOT STAGE20D_VECTOR_BASE /* EL1t */
#else
#define STAGE20D_SYNC_SLOT (STAGE20D_VECTOR_BASE+0x200ull)
#endif
static const uint32_t stage20d_after_vbar[4] = {
    0xf9400696u, 0xf9400a97u, 0xf9400e98u, 0xf0ffea59u
};
/* The ELF is linked at zero but loaded at a physical runtime address.
 * Resolve the handler with ADR at runtime, never an absolute link address. */
#if !defined(CACTUS_STAGE21C1) || !CACTUS_STAGE21C1
extern uintptr_t stage20d_vector_handler_address(void);
#endif
static uint32_t stage20d_vector_saved[
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    8
#else
    4
#endif
];
static unsigned stage20d_vector_armed;
#else
#define STAGE20B_TARGET 0x8142f144ull
#define STAGE20B_ORIG   0xd518c000u /* msr vbar_el1, x0 */
#define STAGE20B_BRK    0xd4218400u /* brk #0xc20 */
#define STAGE20B_IMM    0xc20u
#define STAGE20B_ARM_EVENT 0x20c0u
#define STAGE20B_TRAP_EVENT 0x20c1u
#define STAGE20B_RESTORE_EVENT 0x20c3u
#endif
#define STAGE20C_FB_FIRST 0x5e9e3000ull
#define STAGE20C_FB_LAST  (STAGE20C_FB_FIRST+31u*2880u+63u*4u)
#define STAGE20C_FB_BASE 0x5e605000ull
#define STAGE20C_FB_SIZE 0x17bb000ull
/* Exact 23 A64 instructions from the verified relocated SWEETXNU. */
static const uint32_t stage20c_early[23] = {
    0xd510109f, 0xd5034fdf, 0xaa0003f4, 0xd2800015,
    0xf2e00000, 0xf2c00000, 0xf2abd3c0, 0xf2860000,
    0x12800001, 0xd2800402, 0xd2816804, 0xd2800803,
    0xaa0003e5, 0xb80044a1, 0xf1000463, 0x54ffffc1,
    0x8b040000, 0xf1000442, 0x54ffff21, 0xd5033f9f,
    0xf0ffffe0, 0x91000000, 0xd518c000
};
#else
#define STAGE20B_TARGET 0x8142f0ecull
#define STAGE20B_ORIG   0xd510109fu
#define STAGE20B_BRK    0xd4216400u  /* brk #0xb20 */
#define STAGE20B_IMM    0xb20u
#define STAGE20B_ARM_EVENT 0x20b0u
#define STAGE20B_TRAP_EVENT 0x20b2u
#define STAGE20B_RESTORE_EVENT 0x20b4u
#endif
#define STAGE20B_BRANCH 0x14000719u
static uint32_t stage20b_saved;
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
/* BL _pmap_map_bd.cold.1 at the null-PTE failure join. On cactus the
 * Stage24 hardware panic reached this path. At the join x19 = failing
 * virtual address, x20 = requested VA, x21 = physical end, and x22 =
 * physical start, all retained from pmap_map_bd's function prologue. */
#define STAGE25_PMAP_PA  0x81405218ull
#define STAGE25_PMAP_VA  0xfffffff007405218ull
#define STAGE25_PMAP_ORIG 0x941ae25eu
#define STAGE25_PMAP_BRK  0xd421c4a0u /* brk #0xe25 */
static uint32_t stage25_pmap_saved;
static unsigned stage25_pmap_armed;
#endif
static volatile unsigned stage20b_trap_ok, stage20b_hvc_ok;
static void stage20b_sync_instruction(uintptr_t pa)
{
    /* Publish patched/restored instruction from D-cache to I-cache. */
    __asm__ volatile("dc cvau, %0; dsb sy; ic ivau, %0; dsb sy; isb"
                     : : "r"(pa) : "memory");
}
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
static void stage20d_restore_vector(void)
{
    if(!stage20d_vector_armed) return;
    volatile uint32_t *const slot=(void *)(uintptr_t)STAGE20D_SYNC_SLOT;
    for(unsigned i=0;i<4u;i++) slot[i]=stage20d_vector_saved[i];
    stage20b_sync_instruction((uintptr_t)STAGE20D_SYNC_SLOT);
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    volatile uint32_t *const el1h=(void *)(uintptr_t)(STAGE20D_VECTOR_BASE+0x200u);
    for(unsigned i=0;i<4u;i++) el1h[i]=stage20d_vector_saved[i+4];
    stage20b_sync_instruction((uintptr_t)(STAGE20D_VECTOR_BASE+0x200u));
#endif
    stage20d_vector_armed=0;
}
static unsigned stage20d_vector_is_original(void)
{
    const volatile uint32_t *const slot=(const void *)(uintptr_t)STAGE20D_SYNC_SLOT;
#if defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
    const unsigned el1t_original=slot[0]==0x14000400u && slot[1]==0xd503201fu &&
           slot[2]==0xd503201fu && slot[3]==0xd503201fu;
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    const volatile uint32_t *const el1h=(const void *)(uintptr_t)(STAGE20D_VECTOR_BASE+0x200u);
    return el1t_original && el1h[0]==0x140003f6u &&
           el1h[1]==0xd503201fu && el1h[2]==0xd503201fu &&
           el1h[3]==0xd503201fu;
#else
    return el1t_original;
#endif
#else
    return slot[0]==0x14000000u && slot[1]==0xd503201fu &&
           slot[2]==0xd503201fu && slot[3]==0xd503201fu;
#endif
}
static int stage20d_install_vector(void)
{
    if((STAGE20D_VECTOR_BASE&0x7ffu) ||
       !stage20d_vector_is_original()) return 0;
    volatile uint32_t *const slot=(void *)(uintptr_t)STAGE20D_SYNC_SLOT;
#if defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
    /* XNU has virtual SP_EL0 and SP_EL1 but its MMU remains disabled.
     * The EL1t vector uses HVC directly, never touching an EL1 stack.
     * EL2 receives this exception using its working physical stack. */
    const uint64_t handler=0xd4000422u; /* HVC #0x21 */
    const uint32_t stub[4] = {
        (uint32_t)handler, 0x14000000u, 0xd503201fu, 0xd503201fu
    };
#else
    const uint64_t handler=(uint64_t)stage20d_vector_handler_address();
    if(!handler || (handler&3u)) return 0;
    const uint32_t stub[4] = {
        0x58000050u, /* ldr x16, #8 */
        0xd61f0200u, /* br x16 */
        (uint32_t)handler, (uint32_t)(handler>>32)
    };
#endif
    for(unsigned i=0;i<4u;i++) stage20d_vector_saved[i]=slot[i];
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    volatile uint32_t *const el1h=(void *)(uintptr_t)(STAGE20D_VECTOR_BASE+0x200u);
    for(unsigned i=0;i<4u;i++) stage20d_vector_saved[i+4]=el1h[i];
#endif
    stage20d_vector_armed=1;
#if defined(CACTUS_STAGE22) && CACTUS_STAGE22
    mark(0x2203,STAGE20D_VECTOR_BASE,STAGE20D_VECTOR_BASE+0x200u,STAGE21C2_VBAR_VA);
#elif defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    mark(0x21e6,STAGE20D_VECTOR_BASE,STAGE20D_VECTOR_BASE+0x200u,STAGE21C2_VBAR_VA);
#elif defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
    mark(0x21d6,STAGE20D_VECTOR_BASE,STAGE21C2_VBAR_VA,handler);
#elif defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
    mark(0x21c6,STAGE20D_VECTOR_BASE,STAGE20D_SYNC_SLOT,handler);
#else
    mark(0x20d6,STAGE20D_VECTOR_BASE,STAGE20D_SYNC_SLOT,handler);
#endif
    for(unsigned i=0;i<4u;i++) slot[i]=stub[i];
    stage20b_sync_instruction((uintptr_t)STAGE20D_SYNC_SLOT);
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    for(unsigned i=0;i<4u;i++) el1h[i]=stub[i];
    stage20b_sync_instruction((uintptr_t)(STAGE20D_VECTOR_BASE+0x200u));
#endif
    for(unsigned i=0;i<4u;i++) if(slot[i]!=stub[i]) {
        stage20d_restore_vector();
        return 0;
    }
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    for(unsigned i=0;i<4u;i++) if(el1h[i]!=stub[i]) {
        stage20d_restore_vector();
        return 0;
    }
#endif
    return 1;
}
#endif
static int stage20b_arm(void)
{
    if(loaded_entry_pa!=STAGE20B_ENTRY ||
       loaded_boot_args_pa!=0x824bc000ull)
        return 0;
    const uint32_t branch=*(volatile const uint32_t *)(uintptr_t)loaded_entry_pa;
    if(branch!=STAGE20B_BRANCH ||
       loaded_entry_pa + ((uint64_t)(branch & 0x03ffffffu)<<2) !=
           STAGE20B_ENTRY_TARGET)
        return 0;
#if defined(CACTUS_STAGE20C) && CACTUS_STAGE20C
    /* Verify the exact XNU path up to its VBAR handoff before patching. */
    for(unsigned i=0;i<23u;i++)
        if(*(volatile const uint32_t *)(uintptr_t)
           (STAGE20B_ENTRY_TARGET+4u*i) != stage20c_early[i])
            return 0;
    const volatile struct boot_args *ba=(const void *)(uintptr_t)loaded_boot_args_pa;
    if(ba->Video.v_baseAddr!=STAGE20C_FB_BASE ||
       ba->Video.v_rowBytes!=2880u || ba->Video.v_width!=720u ||
       ba->Video.v_height!=1440u || ba->Video.v_depth!=32u ||
       STAGE20C_FB_FIRST<STAGE20C_FB_BASE ||
       STAGE20C_FB_LAST+4u>STAGE20C_FB_BASE+STAGE20C_FB_SIZE)
        return 0;
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
    for(unsigned i=0;i<4u;i++)
        if(*(volatile const uint32_t *)(uintptr_t)(0x8142f148ull+4u*i)
           !=stage20d_after_vbar[i]) return 0;
#if defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
    /* Pin the exact opcode sequence surrounding MMU enable and ISB. */
    if(*(volatile const uint32_t *)(uintptr_t)0x8142f474ull!=0xd518c000u ||
       *(volatile const uint32_t *)(uintptr_t)0x8142f488ull!=0xd5181000u ||
       *(volatile const uint32_t *)(uintptr_t)0x8142f48cull!=0xd5033fdfu)
        return 0;
#endif
#if defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    /* Guard the first C routine's exact stack prologue and memory ops
     * before permitting a high-VA breakpoint in copied XNU code. */
    static const uint32_t c_entry[]={
        0xd10343ffu,0xa9076ffcu,0xa90867fau,0xa9095ff8u,
        0xa90a57f6u,0xa90b4ff4u,0xa90c7bfdu,0x910303fdu,
        0xd0000a68u,0xaa0003f3u,0x90ffae01u,0x91353021u,
        0x52800020u,0xd503201fu,0xf9459108u,0xf81a83a8u
    };
    for(unsigned i=0;i<sizeof(c_entry)/sizeof(c_entry[0]);i++)
        if(*(volatile const uint32_t *)(uintptr_t)
           (0x81ac05dcull+4u*i)!=c_entry[i]) return 0;
#endif
#if defined(CACTUS_STAGE22) && CACTUS_STAGE22
    /* The branch into this later C checkpoint follows two early calls,
     * a relocation/header path and several additional startup calls.
     * Refuse the image if any pinned instruction or join moved. */
    static const uintptr_t pcs[]={
        0x81ac0624ull,0x81ac0628ull,0x81ac0638ull,
        0x81ac0668ull,0x81ac06ecull,0x81ac0b9cull,
        0x81ac0ba0ull,0x81ac0bf8ull,0x81ac0bfcull,
        0x81ac0c00ull,0x81ac0c04ull,0x81ac0c08ull
    };
    static const uint32_t stage22_words[]={
        0x97e59363u,0x97e58a6au,0x37f805c9u,
        0x540029c0u,0x1400012du,0x97e58399u,
        0x90ffb628u,0x97e5cee6u,0x97e5cba9u,
        0x97e5cb90u,0x97e5cedbu,0x97e5cedeu
    };
    for(unsigned i=0;i<sizeof(pcs)/sizeof(pcs[0]);i++)
        if(*(volatile const uint32_t *)(uintptr_t)pcs[i]!=stage22_words[i])
            return 0;
#endif
#if defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
    static const uintptr_t pins[8]={
        0x8142f174ull,0x8142f18cull,0x8142f220ull,0x8142f3ccull,
        0x8142f3e8ull,0x8142f430ull,0x8142f450ull,0x8142f488ull
    };
    static const uint32_t words[8]={
        0xd50041bfu,0xd50040bfu,0xd355ff05u,0xd5182041u,
        0xd5182020u,0xd518a200u,0xb40000b5u,0xd5181000u
    };
    for(unsigned i=0;i<8u;i++)
        if(*(volatile const uint32_t *)(uintptr_t)pins[i]!=words[i])
            return 0;
    if(*(volatile const uint64_t *)(uintptr_t)0x81178a18ull!=
       0x22a5192519ull) return 0;
#endif
    if(!stage20d_install_vector()) return 0;
#endif
#endif
    const uint32_t original=
        *(volatile const uint32_t *)(uintptr_t)STAGE20B_TARGET;
    if(original!=STAGE20B_ORIG) {
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
        stage20d_restore_vector();
#endif
        return 0;
    }
    stage20b_saved=original;
    stage20b_trap_ok=0;
    stage20b_hvc_ok=0;
    mark(STAGE20B_ARM_EVENT,loaded_entry_pa,STAGE20B_TARGET,branch);
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
    /* Stage23 runs native XNU past the Stage22 stop. The target is
     * checked above but NEVER replaced with a synthetic BRK. Only the
     * two synchronous vectors remain instrumented for genuine faults. */
    mark(0x2300,STAGE20B_TARGET,STAGE20B_ORIG,
         *(volatile const uint32_t *)(uintptr_t)STAGE20B_TARGET);
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
    /* Vector is installed, so the first real missing-PTE call will
     * stop before XNU enters panic(). No speculative table writes. */
    stage25_pmap_armed=0;
    stage25_pmap_saved=*(volatile const uint32_t *)(uintptr_t)STAGE25_PMAP_PA;
    if(stage25_pmap_saved!=STAGE25_PMAP_ORIG) {
        stage20d_restore_vector();
        return 0;
    }
    *(volatile uint32_t *)(uintptr_t)STAGE25_PMAP_PA=STAGE25_PMAP_BRK;
    stage20b_sync_instruction(STAGE25_PMAP_PA);
    if(*(volatile const uint32_t *)(uintptr_t)STAGE25_PMAP_PA!=STAGE25_PMAP_BRK) {
        *(volatile uint32_t *)(uintptr_t)STAGE25_PMAP_PA=stage25_pmap_saved;
        stage20b_sync_instruction(STAGE25_PMAP_PA);
        stage20d_restore_vector();
        return 0;
    }
    stage25_pmap_armed=1;
    mark(0x2500,STAGE25_PMAP_PA,STAGE25_PMAP_VA,stage25_pmap_saved);
#endif
#else
    *(volatile uint32_t *)(uintptr_t)STAGE20B_TARGET=STAGE20B_BRK;
    stage20b_sync_instruction((uintptr_t)STAGE20B_TARGET);
    if(*(volatile const uint32_t *)(uintptr_t)STAGE20B_TARGET!=
       STAGE20B_BRK) {
        *(volatile uint32_t *)(uintptr_t)STAGE20B_TARGET=stage20b_saved;
        stage20b_sync_instruction((uintptr_t)STAGE20B_TARGET);
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
        stage20d_restore_vector();
#endif
        return 0;
    }
#endif
    return 1;
}
static int stage20b_restore(void)
{
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
    if(stage25_pmap_armed) {
        *(volatile uint32_t *)(uintptr_t)STAGE25_PMAP_PA=stage25_pmap_saved;
        stage20b_sync_instruction(STAGE25_PMAP_PA);
        stage25_pmap_armed=0;
    }
    const unsigned pmap_restored=
        *(volatile const uint32_t *)(uintptr_t)STAGE25_PMAP_PA==STAGE25_PMAP_ORIG;
#endif
#if !defined(CACTUS_STAGE23) || !CACTUS_STAGE23
    *(volatile uint32_t *)(uintptr_t)STAGE20B_TARGET=stage20b_saved;
    stage20b_sync_instruction((uintptr_t)STAGE20B_TARGET);
#endif
    unsigned restored=
        *(volatile const uint32_t *)(uintptr_t)STAGE20B_TARGET ==
        STAGE20B_ORIG;
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
    restored=restored && pmap_restored;
    mark(0x2508,pmap_restored,STAGE25_PMAP_PA,stage25_pmap_saved);
#endif
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
    stage20d_restore_vector();
    restored=restored && stage20d_vector_is_original();
#endif
    mark(STAGE20B_RESTORE_EVENT,restored,stage20b_trap_ok,stage20b_hvc_ok);
    return restored && stage20b_trap_ok && stage20b_hvc_ok;
}
#endif
extern char _start[], image_end[];
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
extern void stage20_el1_transition(uintptr_t entry, uintptr_t boot_args);
#endif

static uint64_t ticks(void)
{
    uint64_t x;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(x));
    return x;
}
static void mark(unsigned event, uint64_t a, uint64_t b, uint64_t c)
{
    (void)cactus_trace_emit(event,a,b,c);
}
static void drain(unsigned milliseconds)
{
    const uint64_t start=ticks();
    const uint64_t limit=(frequency/1000u)*milliseconds;
    uint64_t beat=start;
    while(ticks()-start<limit) {
        if(online) {
            usb_poll();
            if(usb_online() && (usb_diag[8]&1u)) {
                const uint8_t *f=cactus_trace_peek();
                if(f && usb_send_budgeted(f,CACTUS_TRACE_BYTES)==1)
                    cactus_trace_pop();
            }
        }
        uint64_t now=ticks();
        if(now-beat>=frequency) {
            beat=now;
            mark(S19_BEAT,heartbeat_count++,usb_diag[8],
                 ((uint64_t)cactus_trace_dropped()<<32)|
                 cactus_trace_pending());
        }
    }
}

#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
/* MI logo remains visible during COM25 on the physical panel, as reported
 * by the user. Test whether the *specific* LK splash RAM is being scanned
 * out, using three bright bars at x=48..335, y=80..159. The probe writes
 * a bounded 288x80 rectangle and leaves all other pixels unchanged.
 * Readback demonstrates RAM writes only; visibility is a separate test. */
static int stage23_splash_probe(void)
{
    const volatile struct boot_args *const ba=
        (const void *)(uintptr_t)loaded_boot_args_pa;
    if(ba->Video.v_baseAddr!=FB_BASE ||
       ba->Video.v_rowBytes!=FB_STRIDE ||
       ba->Video.v_width!=FB_W || ba->Video.v_height!=FB_H ||
       ba->Video.v_depth!=32 || FB_W<336u || FB_H<160u) {
        mark(0x23ee,ba->Video.v_baseAddr,ba->Video.v_rowBytes,ba->Video.v_depth);
        return 0;
    }
    volatile uint32_t *const fb=(void *)(uintptr_t)FB_BASE;
    const uint32_t colors[3]={0x00ff00ffu,0x0000ff00u,0x0000ffffu};
    for(unsigned y=80;y<160;y++) {
        for(unsigned x=48;x<336;x++)
            fb[y*FB_W+x]=colors[(x-48u)/96u];
        /* Clean dirty pixel data for display DMA (if LK really scans
         * out this buffer). Pixels outside the marker are untouched. */
        for(unsigned x=48;x<336;x+=16) {
            uintptr_t p=FB_BASE + (uintptr_t)y*FB_STRIDE + x*4u;
            __asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
        }
    }
    __asm__ volatile("dsb sy" ::: "memory");
    const uint32_t a=fb[80u*FB_W+48u];
    const uint32_t b=fb[80u*FB_W+144u];
    const uint32_t c=fb[159u*FB_W+335u];
    mark(0x2301,FB_BASE + 80u*FB_STRIDE + 48u*4u,
         ((uint64_t)a<<32)|b,c);
    mark(0x2302,((uint64_t)48u<<32)|80u,((uint64_t)288u<<32)|80u,
         (a==colors[0] && b==colors[1] && c==colors[2]));
    return a==colors[0] && b==colors[1] && c==colors[2];
}
#endif

static void store_copy(uintptr_t destination, uintptr_t source, uint64_t bytes)
{
    uint64_t n=0;
    volatile uint64_t *d=(volatile uint64_t *)destination;
    volatile const uint64_t *s=(volatile const uint64_t *)source;
    for(;n+8<=bytes;n+=8)
        d[n/8]=s[n/8];
    for(;n<bytes;n++)
        ((volatile uint8_t *)destination)[n]=
            ((volatile const uint8_t *)source)[n];
}
static void store_zero(uintptr_t destination, uint64_t bytes)
{
    uint64_t n=0;
    while(n<bytes && ((destination+n)&7u)) {
        ((volatile uint8_t *)destination)[n++]=0;
    }
    for(;n+8<=bytes;n+=8)
        *(volatile uint64_t *)(destination+n)=0;
    for(;n<bytes;n++) ((volatile uint8_t *)destination)[n]=0;
}
/* Sample copied segment endpoints and BSS; full byte-by-byte verification
 * would double the uncached boot-time traffic and can trip the watchdog. */
static int verify_copy(uintptr_t dst, uintptr_t src, uint64_t file, uint64_t vm)
{
    const uint64_t offsets[3]={0,file>32 ? file/2-16 : 0,
                                file>32 ? file-32 : 0};
    for(unsigned window=0;window<3;window++) {
        for(uint64_t i=offsets[window],end=i+32;i<end && i<file;i++)
            if(((volatile const uint8_t *)dst)[i]!=
               ((volatile const uint8_t *)src)[i]) return 0;
    }
    uint64_t tail=vm-file;
    const uint64_t zero_offsets[2]={0,tail>32 ? tail-32 : 0};
    for(unsigned window=0;window<2;window++) {
        for(uint64_t i=zero_offsets[window],end=i+32;i<end && i<tail;i++)
            if(((volatile const uint8_t *)(dst+file))[i]!=0) return 0;
    }
    return 1;
}

static unsigned perform(uintptr_t dtb)
{
    uint32_t bytes=0;
    struct fdt_info live;
    int valid=fdt_check(dtb,&bytes) && bytes<=0x100000u &&
              fdt_inspect((const void *)dtb,bytes,&live)==0;
    mark(S19_FDT,valid,valid?live.memory_count:0,valid?live.reserved_count:0);
    if(!valid) return 1;

    const struct xnu_payload *p=(const void *)(uintptr_t)PAYLOAD_ADDR;
    if((PAYLOAD_ADDR&15u) || PAYLOAD_LENGTH<XNU_PAYLOAD_HEADER_BYTES)
        return 2;
    mark(S19_PAYLOAD,p->phys_base,p->mem_size,p->nsegs);
    struct xnu_layout region;
    enum xnu_validation v=xnu_validate_payload(p,PAYLOAD_LENGTH,&live,
            dtb,bytes,(uintptr_t)_start,
            (uintptr_t)image_end-(uintptr_t)_start,&region);
    mark(S19_VALID,v,PAYLOAD_ADDR,PAYLOAD_LENGTH);
    if(v!=XNU_VALIDATE_OK) return 0x100u+(unsigned)v;
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
    loaded_entry_pa=region.entry_pa;
    loaded_boot_args_pa=region.boot_args_pa;
#endif

    mark(S19_BEFORE_COPY,region.entry_pa,region.adt_pa,
         region.boot_args_pa);
    for(uint32_t i=0;i<region.segments;i++) {
        const struct xnu_payload_segment *s=&p->segs[i];
        uintptr_t dst=(uintptr_t)(p->phys_base+
                       (s->vmaddr-p->virt_base));
        uintptr_t src=(uintptr_t)p+s->file_off;
        /* This is an EL2, MMU-off loop: all 64-bit operands aligned. */
        if((dst|src)&7u) return 0x200u+i;
        mark(S19_SEG_START,i,dst,s->file_size);
        store_copy(dst,src,s->file_size);
        store_zero(dst+s->file_size,s->vmsize-s->file_size);
        __asm__ volatile("dsb sy" ::: "memory");
        if(!verify_copy(dst,src,s->file_size,s->vmsize))
            return 0x300u+i;
        mark(S19_SEG_DONE,i,dst,s->vmsize);
        /* A bounded transmission window avoids filling the 32-slot
         * CLOG ring while copying many segments. */
        drain(40);
    }

    if((region.adt_pa|((uintptr_t)p+p->adt_off))&7u) return 0x400;
    store_copy(region.adt_pa,(uintptr_t)p+p->adt_off,p->adt_size);
    __asm__ volatile("dsb sy" ::: "memory");
    if(!verify_copy(region.adt_pa,(uintptr_t)p+p->adt_off,
                    p->adt_size,p->adt_size)) return 0x401;
    mark(S19_ADT,region.adt_pa,p->adt_size,0);
    store_zero(region.boot_args_pa,XNU_PAGE_BYTES);
    volatile struct boot_args *ba=(void *)(uintptr_t)region.boot_args_pa;
    ba->Revision=2;
    ba->Version=2;
    ba->virtBase=p->virt_base;
    ba->physBase=p->phys_base;
    ba->memSize=p->mem_size;
    ba->topOfKernelData=region.top;
    ba->Video.v_baseAddr=FB_BASE;
    ba->Video.v_display=1;
    ba->Video.v_rowBytes=FB_STRIDE;
    ba->Video.v_width=FB_W;
    ba->Video.v_height=FB_H;
    ba->Video.v_depth=32;
    ba->deviceTreeP=region.adt_pa+(p->virt_base-p->phys_base);
    ba->deviceTreeLength=(uint32_t)p->adt_size;
    for(unsigned i=0;i<608u;i++) ba->CommandLine[i]=p->cmdline[i];
    ba->CommandLine[607]=0;
    ba->bootFlags=0;
    ba->memSizeActual=p->mem_size;
    __asm__ volatile("dsb sy" ::: "memory");
    if(ba->Revision!=2 || ba->physBase!=p->phys_base ||
       ba->deviceTreeLength!=p->adt_size) return 0x402;
    mark(S19_BOOTARGS,region.boot_args_pa,ba->deviceTreeP,region.top);
    /* Flush instructions for future XNU entry, but Stage19-A itself
     * runs no XNU code and keeps Linux recovery as fallback. */
    __asm__ volatile("ic iallu; dsb sy; isb" ::: "memory");
    mark(S19_COPY_DONE,region.entry_pa,region.boot_args_pa,region.top);
    return 0;
}

void stage19_probe(uintptr_t dtb)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL":"=r"(el));
    __asm__ volatile("mrs %0, cntfrq_el0":"=r"(frequency));
    if(!frequency) frequency=13000000u;
    online=faulted=heartbeat_count=0;
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
    cactus_trace_reset(0xca200001u);
#else
    cactus_trace_reset(0xca190001u);
#endif
    mark(S19_ENTRY,dtb,el,frequency);
    online=usb_init(usb_diag);
    mark(S19_USB,online,usb_diag[1],usb_diag[2]);
    if(!online) return;
    unsigned status=perform(dtb);
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
    if(status==0) {
#if defined(CACTUS_STAGE20B) && CACTUS_STAGE20B
        if(!stage20b_arm()) {
            status=0x510;
        } else {
#endif
        mark(0x20a0,loaded_entry_pa,loaded_boot_args_pa,8u);
#if defined(CACTUS_STAGE22) && CACTUS_STAGE22
        /* Read-only EL2 CPU/timer census and the actual loaded boot_args.
         * No speculative GIC accesses or timer programming. */
        uint64_t midr, mpidr, cntfrq, cntpct, cntvct;
        __asm__ volatile("mrs %0, MIDR_EL1" : "=r"(midr));
        __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
        __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(cntfrq));
        __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct));
        __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(cntvct));
        const volatile struct boot_args *const ba=
            (const void *)(uintptr_t)loaded_boot_args_pa;
        mark(0x2200,midr,mpidr,8u);
        mark(0x2201,cntfrq,cntpct,cntvct);
        mark(0x2202,ba->physBase,ba->memSize,ba->topOfKernelData);
        mark(0x2205,STAGE20B_TARGET,STAGE22_TARGET_VA,usb_diag[8]);
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
        /* Keep a visible color sample on the MI splash for the full
         * COM25 preflight enumeration interval. */
        if(!stage23_splash_probe()) {
            status=0x523;
            (void)stage20b_restore();
        } else {
#endif
        drain(5500);
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
        }
#endif
#elif defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
        mark(0x21ea,STAGE20B_TARGET,STAGE21D_TARGET_VA,usb_diag[8]);
        drain(5500);
#elif defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
        mark(0x21da,STAGE20B_TARGET,STAGE21C2_VBAR_VA,usb_diag[8]);
        drain(5500);
#elif defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
        /* Allow Windows to enumerate CDC and receive the pre-XNU markers
         * even if the later EL1t page-table path never reaches the HVC. */
        mark(0x21ca,STAGE20B_TARGET,STAGE20D_SYNC_SLOT,usb_diag[8]);
        drain(5500);
#endif
        /* ERET enters shim EL1; Stage20-B/C then branch to real XNU. */
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
        if(status==0) {
            mark(0x2303,loaded_entry_pa,loaded_boot_args_pa,
                 STAGE22_TARGET_VA);
            stage20_el1_transition(loaded_entry_pa,loaded_boot_args_pa);
        }
#else
        stage20_el1_transition(loaded_entry_pa,loaded_boot_args_pa);
#endif
        uint64_t now_el;
        __asm__ volatile("mrs %0, CurrentEL":"=r"(now_el));
        mark(0x20a3,now_el,loaded_entry_pa,loaded_boot_args_pa);
        if(now_el!=8u) status=0x500;
#if defined(CACTUS_STAGE20B) && CACTUS_STAGE20B
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
        if(status!=0x523 && !stage20b_restore()) status=0x511;
#else
        if(!stage20b_restore()) status=0x511;
#endif
#if defined(CACTUS_STAGE23) && CACTUS_STAGE23
        if(status!=0x523) {
            /* A genuine synchronous exception has returned control to
             * EL2. Keep the COM25 endpoint polled and the fault readable;
             * never intentionally restart the boot/recovery chain. */
            mark(0x2308,status,stage20b_trap_ok,stage20b_hvc_ok);
            mark(0x20ff,status,cactus_trace_pending(),cactus_trace_dropped());
            mark(S19_DONE,status,cactus_trace_pending(),cactus_trace_dropped());
            for(;;) drain(1000);
        }
#endif
        }
#endif
    }
    mark(0x20ff,status,cactus_trace_pending(),cactus_trace_dropped());
#endif
    mark(S19_DONE,status,cactus_trace_pending(),cactus_trace_dropped());
    drain(5500);
    usb_shutdown();
    online=0;
}
#if defined(CACTUS_STAGE20) && CACTUS_STAGE20
/* Executed with MMU off at the new privilege level before HVC. The CLOG
 * frame's own CurrentEL field also independently reports EL1. */
void stage20_el1_signal(uint64_t entry,uint64_t boot_args)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL":"=r"(el));
    mark(0x20a1,el,entry,boot_args);
}
void stage20_hvc_signal(uint64_t esr,uint64_t elr,uint64_t spsr)
{
    mark(0x20a2,esr,elr,spsr);
}
#if defined(CACTUS_STAGE20B) && CACTUS_STAGE20B
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
void stage20b_el1_exception(uint64_t esr,uint64_t elr,uint64_t far,
                           uint64_t saved_bootargs,
                           uint64_t virtbase,uint64_t physbase,
                           uint64_t memsize,uint64_t vbar)
#else
void stage20b_el1_exception(uint64_t esr,uint64_t elr,uint64_t far,
                           uint64_t saved_bootargs)
#endif
{
    stage20b_trap_ok=((esr>>26)==0x3cu &&
                      (esr&0xffffu)==STAGE20B_IMM &&
                      elr==STAGE20B_TARGET);
    mark(STAGE20B_TRAP_EVENT,esr,elr,far);
#if defined(CACTUS_STAGE20D) && CACTUS_STAGE20D
    const volatile struct boot_args *const ba=
        (const void *)(uintptr_t)loaded_boot_args_pa;
    mark(0x20d2,vbar,saved_bootargs,virtbase);
    mark(0x20d3,physbase,memsize,ba->memSize);
    stage20b_trap_ok=stage20b_trap_ok &&
        vbar==STAGE20D_VECTOR_BASE &&
        saved_bootargs==loaded_boot_args_pa &&
        virtbase==ba->virtBase &&
        physbase==ba->physBase &&
        memsize==ba->memSize;
#endif
#if defined(CACTUS_STAGE20C) && CACTUS_STAGE20C
    uint64_t first=0,last=0;
    if(stage20b_trap_ok) {
        first=*(volatile const uint32_t *)(uintptr_t)STAGE20C_FB_FIRST;
        last=*(volatile const uint32_t *)(uintptr_t)STAGE20C_FB_LAST;
    }
    mark(0x20c2,saved_bootargs,first,last);
    stage20b_trap_ok = stage20b_trap_ok &&
                      saved_bootargs==loaded_boot_args_pa &&
                      first==0xffffffffull && last==0xffffffffull;
#else
    (void)saved_bootargs;
#endif
}
#if defined(CACTUS_STAGE24) && CACTUS_STAGE24
/* Strict 4K, 39-bit TTBR1 page walk using the EL2 physical identity map.
 * Every table and resulting address must stay in the measured cactus
 * loader RAM window. This protects COM25 capture from wild XNU stack VAs.
 * XNU may legitimately use other mappings; in that case simply log no
 * frame, rather than touching unknown physical RAM or MMIO. */
static int stage24_in_ram(uint64_t pa,uint64_t size)
{
    return pa>=0x80000000ull && size<=0x1e000000ull &&
           pa<=0x9e000000ull-size;
}
static int stage24_translate(uint64_t va,uint64_t root,uint64_t *pa_out)
{
    const uint64_t pa_mask=0x0000fffffffff000ull;
    if((va>>39)!=0x1ffffffull || (root&0xfffull)) return 0;
    uint64_t table=root&pa_mask;
    for(unsigned level=0;level<3;level++) {
        const unsigned shift=30u-9u*level;
        const uint64_t slot=table+8u*((va>>shift)&0x1ffull);
        if(!stage24_in_ram(slot,8)) return 0;
        const uint64_t desc=*(volatile const uint64_t *)(uintptr_t)slot;
        const uint64_t kind=desc&3ull;
        if(kind==3u && level!=2u) {
            table=desc&pa_mask;
            if(!stage24_in_ram(table,4096)) return 0;
        } else if((kind==1u && level!=2u) ||
                  (kind==3u && level==2u)) {
            const uint64_t offset=(1ull<<shift)-1ull;
            const uint64_t pa=((desc&pa_mask)&~offset)|(va&offset);
            if(!stage24_in_ram(pa,8)) return 0;
            *pa_out=pa;
            return 1;
        } else return 0;
    }
    return 0;
}
/* The active cactus intstack is in the loaded XNU __BOOTDATA segment.
 * Early XNU image VAs are loaded contiguously at physBase + (VA-virtBase).
 * The exact __BOOTDATA bounds are verified by the Stage22 Mach-O payload
 * guard. This second bounded lookup does not depend on XNU TTBR1: it is
 * needed while pmap init is itself panicking on a missing page-table PTE. */
static int stage24_bootdata_pa(uint64_t va,uint64_t *pa_out)
{
    const uint64_t base=0xfffffff008068000ull;
    const uint64_t end=0xfffffff0080b4000ull;
    if(va<base || va>end-8u) return 0;
    const uint64_t pa=0x80000000ull+(va-0xfffffff006000000ull);
    if(!stage24_in_ram(pa,8)) return 0;
    *pa_out=pa;
    return 1;
}
static void stage24_log_callers(uint64_t tcr,uint64_t ttbr1,uint64_t fp)
{
    /* T1SZ=25 and TG1=4KB required by this three-level walker. */
    const int walker_ok=(((tcr>>16)&0x3fu)==25u &&
       ((tcr>>30)&3u)==2u &&
       stage24_in_ram(ttbr1&0x0000fffffffff000ull,4096));
    unsigned frames=0;
    for(;frames<4;frames++) {
        uint64_t frame_pa=0,return_pa=0;
        if(fp&15u) break;
        const int walked=(walker_ok &&
            stage24_translate(fp,ttbr1,&frame_pa) &&
            stage24_translate(fp+8u,ttbr1,&return_pa));
        if(!walked && !(stage24_bootdata_pa(fp,&frame_pa) &&
                        stage24_bootdata_pa(fp+8u,&return_pa))) break;
        mark(0x241e,fp,frame_pa,(uint64_t)(walked ? 1u : 2u));
        const uint64_t prev=*(volatile const uint64_t *)(uintptr_t)frame_pa;
        const uint64_t lr=*(volatile const uint64_t *)(uintptr_t)return_pa;
        mark(0x2410u+frames,fp,prev,lr);
        if(prev<=fp || prev-fp>0x10000u) {frames++;break;}
        fp=prev;
    }
    mark(0x241f,fp,ttbr1,frames);
}
#endif
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
/* Read the actual XNU TTBR1 descriptors for the failing VA. Inspection
 * only: no mapping is made until the required PA/VA interval is known. */
static void stage25_log_missing_pte(uint64_t va,uint64_t ttbr1)
{
    const uint64_t mask=0x0000fffffffff000ull;
    uint64_t table=ttbr1&mask;
    unsigned visited=0;
    for(unsigned level=0;level<3u;level++) {
        const unsigned shift=30u-9u*level;
        const uint64_t slot=table + 8u*((va>>shift)&0x1ffull);
        if(!stage24_in_ram(slot,8u)) break;
        const uint64_t desc=*(volatile const uint64_t *)(uintptr_t)slot;
        mark(0x2504u+level,slot,desc,(uint64_t)level);
        visited++;
        if(level==2u || (desc&3u)!=3u) break;
        table=desc&mask;
        if(!stage24_in_ram(table,4096u)) break;
    }
    mark(0x2507,va,ttbr1,visited);
}
#endif
void stage20b_hvc_exception(uint64_t esr,uint64_t elr,uint64_t spsr)
{
#if defined(CACTUS_STAGE22) && CACTUS_STAGE22
    /* Observe the first synchronous exception or the late C join using
     * the proven virtual EL1t/EL1h HVC stubs and the EL2 physical stack. */
    uint64_t esr1,elr1,far1,vbar,tcr,ttbr0,ttbr1,mair,sctlr,spsr1;
    __asm__ volatile("mrs %0, ESR_EL1":"=r"(esr1));
    __asm__ volatile("mrs %0, ELR_EL1":"=r"(elr1));
    __asm__ volatile("mrs %0, FAR_EL1":"=r"(far1));
    __asm__ volatile("mrs %0, VBAR_EL1":"=r"(vbar));
    __asm__ volatile("mrs %0, TCR_EL1":"=r"(tcr));
    __asm__ volatile("mrs %0, TTBR0_EL1":"=r"(ttbr0));
    __asm__ volatile("mrs %0, TTBR1_EL1":"=r"(ttbr1));
    __asm__ volatile("mrs %0, MAIR_EL1":"=r"(mair));
    __asm__ volatile("mrs %0, SCTLR_EL1":"=r"(sctlr));
    __asm__ volatile("mrs %0, SPSR_EL1":"=r"(spsr1));
    const unsigned ec=(unsigned)(esr1>>26);
    const unsigned checkpoint=(ec==0x3cu &&
        (esr1&0xffffu)==STAGE20B_IMM && elr1==STAGE22_TARGET_VA);
    /* kind: 1=post-init calls checkpoint, 2=exception, 3=other BRK. */
    const unsigned kind=checkpoint ? 1u : ((ec==0x3cu) ? 3u : 2u);
    mark(STAGE20B_TRAP_EVENT,esr1,elr1,far1);
#if defined(CACTUS_STAGE24) && CACTUS_STAGE24
    /* The HVC vector runs with EL2 MMU disabled. Snapshot original XNU
     * registers in assembly before any C function or ABI clobber. We
     * only read the known physical shim buffer; NEVER dereference an
     * untrusted high-VA frame pointer or panic string here. */
    extern uint64_t stage24_register_snapshot[23];
    const volatile uint64_t *const r=stage24_register_snapshot;
    mark(0x2400,r[19],r[20],r[22]); /* x29, LR/x30, SP_EL1 */
    mark(0x2401,r[0],r[1],r[2]);    /* x0..x2 */
    mark(0x2402,r[3],r[4],r[5]);    /* x3..x5 */
    mark(0x2403,r[6],r[7],r[8]);    /* x6..x8 */
    mark(0x2404,r[9],r[10],r[11]); /* x19..x21 */
    mark(0x2405,r[12],r[13],r[14]);/* x22..x24 */
    mark(0x2406,r[15],r[16],r[17]);/* x25..x27 */
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
    mark(0x2407,r[18],r[21],30u);  /* x28, SP_EL0, Stage30 */
#else
    mark(0x2407,r[18],r[21],0x29u);/* x28, SP_EL0, old build ID */
#endif
#if defined(CACTUS_STAGE25) && CACTUS_STAGE25
    const unsigned pmap_hit=(esr1==0xf2000e25ull &&
                             elr1==STAGE25_PMAP_VA);
    mark(0x2501,esr1,elr1,pmap_hit);
    if(pmap_hit) {
        /* x19=current VA, x20=initial VA, x21=end PA, x22=start PA.
         * These are saved callee-saved regs inside pmap_map_bd itself. */
        mark(0x2502,r[9],r[12],r[11]);
        mark(0x2503,r[10],r[13],r[14]);
        stage25_log_missing_pte(r[9],ttbr1);
    }
#endif
    stage24_log_callers(tcr,ttbr1,r[19]);
#endif
    mark(0x2207,vbar,tcr,sctlr);
    mark(0x2208,ttbr0,ttbr1,mair);
    mark(0x2209,elr,spsr,spsr1);
    mark(0x220a,kind,elr1,ec);
    stage20b_trap_ok=(vbar==STAGE21C2_VBAR_VA &&
        (sctlr&1u)!=0u &&
        ((spsr1&0xfu)==4u || (spsr1&0xfu)==5u) &&
        (elr==STAGE21C2_VBAR_VA+4ull ||
         elr==STAGE21C2_VBAR_VA+0x204ull));
    const unsigned expected_el_mode=5u;
#elif defined(CACTUS_STAGE21D) && CACTUS_STAGE21D
    /* Both virtual synchronous exception slots contain an HVC-only
     * trampoline. The logger uses EL2's physical stack and mappings. */
    uint64_t esr1,elr1,far1,vbar,tcr,ttbr0,ttbr1,mair,sctlr,spsr1;
    __asm__ volatile("mrs %0, ESR_EL1":"=r"(esr1));
    __asm__ volatile("mrs %0, ELR_EL1":"=r"(elr1));
    __asm__ volatile("mrs %0, FAR_EL1":"=r"(far1));
    __asm__ volatile("mrs %0, VBAR_EL1":"=r"(vbar));
    __asm__ volatile("mrs %0, TCR_EL1":"=r"(tcr));
    __asm__ volatile("mrs %0, TTBR0_EL1":"=r"(ttbr0));
    __asm__ volatile("mrs %0, TTBR1_EL1":"=r"(ttbr1));
    __asm__ volatile("mrs %0, MAIR_EL1":"=r"(mair));
    __asm__ volatile("mrs %0, SCTLR_EL1":"=r"(sctlr));
    __asm__ volatile("mrs %0, SPSR_EL1":"=r"(spsr1));
    mark(STAGE20B_TRAP_EVENT,esr1,elr1,far1);
    mark(0x21e2,vbar,tcr,sctlr);
    mark(0x21e3,ttbr0,ttbr1,mair);
    mark(0x21e4,elr,spsr,spsr1);
    const unsigned ec=(unsigned)(esr1>>26);
    const unsigned checkpoint=(ec==0x3cu &&
                               (esr1&0xffffu)==STAGE20B_IMM &&
                               elr1==STAGE21D_TARGET_VA);
    /* Any earlier non-BRK exception is a separately classified fault. */
    const unsigned fault=ec!=0x3cu;
    const unsigned kind=checkpoint ? 1u : (fault ? 2u : 0u);
    mark(0x21e5,kind,elr1,(uint64_t)ec);
    stage20b_trap_ok=(kind!=0u &&
                      vbar==STAGE21C2_VBAR_VA &&
                      tcr==0x22a5192519ull &&
                      ttbr0==0x8117a000ull &&
                      ttbr1==0x8117b000ull &&
                      mair==0x0c08044f00bb44ffull &&
                      (sctlr&1u)!=0u &&
                      ((spsr1&0xfu)==4u || (spsr1&0xfu)==5u) &&
                      (elr==STAGE21C2_VBAR_VA+4ull ||
                       elr==STAGE21C2_VBAR_VA+0x204ull));
    const unsigned expected_el_mode=5u;
#elif defined(CACTUS_STAGE21C2) && CACTUS_STAGE21C2
    /* EL1 has just enabled translations. HVC enters the still-MMU-off
     * EL2 logger using its own physical stack and data addresses. */
    uint64_t esr1,elr1,far1,vbar,tcr,ttbr0,ttbr1,mair,sctlr,spsr1;
    __asm__ volatile("mrs %0, ESR_EL1":"=r"(esr1));
    __asm__ volatile("mrs %0, ELR_EL1":"=r"(elr1));
    __asm__ volatile("mrs %0, FAR_EL1":"=r"(far1));
    __asm__ volatile("mrs %0, VBAR_EL1":"=r"(vbar));
    __asm__ volatile("mrs %0, TCR_EL1":"=r"(tcr));
    __asm__ volatile("mrs %0, TTBR0_EL1":"=r"(ttbr0));
    __asm__ volatile("mrs %0, TTBR1_EL1":"=r"(ttbr1));
    __asm__ volatile("mrs %0, MAIR_EL1":"=r"(mair));
    __asm__ volatile("mrs %0, SCTLR_EL1":"=r"(sctlr));
    __asm__ volatile("mrs %0, SPSR_EL1":"=r"(spsr1));
    mark(STAGE20B_TRAP_EVENT,esr1,elr1,far1);
    mark(0x21d2,vbar,tcr,sctlr);
    mark(0x21d3,ttbr0,ttbr1,mair);
    const volatile uint64_t *const root0=(const void *)0x8117a000ull;
    const volatile uint64_t *const root1=(const void *)0x8117b000ull;
    const volatile uint64_t *const phys=(const void *)0x8117c000ull;
    const volatile uint64_t *const high=(const void *)0x8117d000ull;
    const uint64_t r0=root0[2],r1=root1[0x1c0];
    const uint64_t p8=phys[8],h38=high[0x38];
    mark(0x21d4,r0,r1,p8);
    mark(0x21d8,h38,root1[0x180],phys[0xee]);
    mark(0x21d5,elr,spsr,spsr1);
    stage20b_trap_ok=((esr1>>26)==0x3cu &&
                      (esr1&0xffffu)==STAGE20B_IMM &&
                      elr1==STAGE20B_TARGET &&
                      vbar==STAGE21C2_VBAR_VA &&
                      tcr==0x22a5192519ull &&
                      ttbr0==0x8117a000ull &&
                      ttbr1==0x8117b000ull &&
                      mair==0x0c08044f00bb44ffull &&
                      /* Cortex-A53 readback adds bit 23 to XNU's
                       * 0x3454593d SCTLR write (observed on hardware
                       * in three independent Stage25 COM captures). */
                      sctlr==0x34d4593dull &&
                      r0==0x8117c003ull &&
                      r1==0x8117d003ull &&
                      p8==0x81000601ull &&
                      h38==0x81000601ull &&
                      elr==STAGE21C2_VBAR_VA+4ull &&
                      (spsr1&0xfu)==4u);
    const unsigned expected_el_mode=5u;
#elif defined(CACTUS_STAGE21C1) && CACTUS_STAGE21C1
    /* HVC runs on the known-good EL2 physical stack. Reading EL1 state
     * here avoids both virtual XNU stack pointers while its MMU is OFF. */
    uint64_t esr1,elr1,far1,vbar,tcr,ttbr0,ttbr1,mair,sctlr,spsr1;
    __asm__ volatile("mrs %0, ESR_EL1":"=r"(esr1));
    __asm__ volatile("mrs %0, ELR_EL1":"=r"(elr1));
    __asm__ volatile("mrs %0, FAR_EL1":"=r"(far1));
    __asm__ volatile("mrs %0, VBAR_EL1":"=r"(vbar));
    __asm__ volatile("mrs %0, TCR_EL1":"=r"(tcr));
    __asm__ volatile("mrs %0, TTBR0_EL1":"=r"(ttbr0));
    __asm__ volatile("mrs %0, TTBR1_EL1":"=r"(ttbr1));
    __asm__ volatile("mrs %0, MAIR_EL1":"=r"(mair));
    __asm__ volatile("mrs %0, SCTLR_EL1":"=r"(sctlr));
    __asm__ volatile("mrs %0, SPSR_EL1":"=r"(spsr1));
    mark(STAGE20B_TRAP_EVENT,esr1,elr1,far1);
    mark(0x21c2,vbar,tcr,sctlr);
    mark(0x21c3,ttbr0,ttbr1,mair);
    const volatile uint64_t *const root0=(const void *)0x8117a000ull;
    const volatile uint64_t *const root1=(const void *)0x8117b000ull;
    const volatile uint64_t *const phys=(const void *)0x8117c000ull;
    const volatile uint64_t *const high=(const void *)0x8117d000ull;
    const volatile uint64_t *const fb=(const void *)0x8117e000ull;
    const uint64_t r0=root0[2],r1=root1[0x1c0],rf=root1[0x180];
    const uint64_t p8=phys[8],h38=high[0x38],f1=fb[1];
    const uint64_t f2=fb[2],tail=phys[0xef],last=phys[0xee];
    mark(0x21c4,r0,r1,rf);
    mark(0x21c7,p8,h38,f1);
    mark(0x21c8,f2,tail,last);
    /* EL1t BRK enters the EL1h vector, so its HVC saves EL1h in
     * SPSR_EL2. SPSR_EL1 independently retains the original EL1t. */
    mark(0x21c9,elr,spsr,spsr1);
    stage20b_trap_ok=((esr1>>26)==0x3cu &&
                      (esr1&0xffffu)==STAGE20B_IMM &&
                      elr1==STAGE20B_TARGET &&
                      vbar==STAGE20D_VECTOR_BASE &&
                      tcr==0x22a5192519ull &&
                      ttbr0==0x8117a000ull &&
                      ttbr1==0x8117b000ull &&
                      mair==0x0c08044f00bb44ffull &&
                      !(sctlr&1ull) &&
                      r0==0x8117c003ull &&
                      r1==0x8117d003ull &&
                      rf==0x8117e003ull &&
                      p8==0x81000601ull &&
                      h38==0x81000601ull &&
                      f1==0x6000005e600605ull &&
                      f2==0x6000005e800605ull &&
                      !tail && last!=0 &&
                      elr==0x8142e004ull &&
                      (spsr1&0xfu)==4u);
    const unsigned expected_el_mode=5u;
#else
    const unsigned expected_el_mode=5u;
#endif
    stage20b_hvc_ok=((esr>>26)==0x16u &&
                     (esr&0xffffu)==0x21u &&
                     (spsr&0xfu)==expected_el_mode);
    mark(0x20b3,esr,elr,spsr);
}
#endif
#endif
void stage19_fault(uint64_t esr,uint64_t elr,uint64_t far)
{
    if(faulted) for(;;) __asm__ volatile("wfe");
    faulted=1;
    mark(S19_FAULT,esr,elr,far);
    if(online) drain(2500);
    for(;;) __asm__ volatile("wfe");
}
