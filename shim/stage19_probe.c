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
extern char _start[], image_end[];

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
    cactus_trace_reset(0xca190001u);
    mark(S19_ENTRY,dtb,el,frequency);
    online=usb_init(usb_diag);
    mark(S19_USB,online,usb_diag[1],usb_diag[2]);
    if(!online) return;
    unsigned status=perform(dtb);
    mark(S19_DONE,status,cactus_trace_pending(),cactus_trace_dropped());
    drain(5500);
    usb_shutdown();
    online=0;
}
void stage19_fault(uint64_t esr,uint64_t elr,uint64_t far)
{
    if(faulted) for(;;) __asm__ volatile("wfe");
    faulted=1;
    mark(S19_FAULT,esr,elr,far);
    if(online) drain(2500);
    for(;;) __asm__ volatile("wfe");
}
