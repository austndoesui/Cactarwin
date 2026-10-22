/* Hardware-derived Stage17 admission regression, using actual SWEETXNU bytes.
 * Host-native build: gcc test_stage18_layout.c payload_layout.c fdt_ranges.c
 * This test does not execute XNU and never writes physical device RAM. */
#include "payload_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct fdt_range live_reserved[] = {
    {0x78000000,0x01600000}, {0x7a000000,0x00170000},
    {0xae000000,0x10000000}, {0xbff60000,0x00010000},
    {0x9fe00000,0x00180000}, {0x77ff0000,0x00010000},
    {0xbff70000,0x00080000}, {0x7f0d0000,0x00e20000},
    {0x70000000,0x03c40000}, {0x7fef0000,0x000c0000},
    {0x47d80000,0x00050000}, {0x7ffbf000,0x00040000},
    {0xbffff000,0x00001000}, {0x7ffff000,0x00001000},
    {0x47c80000,0x00010000}, {0x47c90000,0x000e0000},
    {0x47d70000,0x00010000},
};

static unsigned char *load(const char *path, size_t *bytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fseek(f, 0, SEEK_END)) { perror("seek"); exit(2); }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET)) {
        perror("seek"); exit(2);
    }
    *bytes = (size_t)length;
    unsigned char *raw = malloc(*bytes);
    if (!raw || fread(raw, 1, *bytes, f) != *bytes) {
        perror("read"); exit(2);
    }
    fclose(f);
    return raw;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_stage18_layout original.bin candidate.bin\n");
        return 2;
    }
    struct fdt_info live = {0};
    live.memory_count = 1;
    live.memory[0] = (struct fdt_range){0x40000000, 0x80000000};
    live.reserved_count = sizeof(live_reserved) / sizeof(live_reserved[0]);
    memcpy(live.reserved, live_reserved, sizeof(live_reserved));
    /* Stage18 aligned-payload ramdisk ends at 0x53b923ec. */
    live.initrd_start = 0x51b00000;
    live.initrd_end = 0x53b923ec;

    size_t old_bytes, candidate_bytes;
    unsigned char *old = load(argv[1], &old_bytes);
    unsigned char *candidate = load(argv[2], &candidate_bytes);
    struct xnu_layout result = {0};
    enum xnu_validation old_result = xnu_validate_payload(
        old, old_bytes, &live, 0x47880000, 0x28074,
        0x40080000, 0x02000000, &result);
    enum xnu_validation candidate_result = xnu_validate_payload(
        candidate, candidate_bytes, &live, 0x47880000, 0x28074,
        0x40080000, 0x02000000, &result);
    printf("original_status=%u relocated_status=%u entry=%#llx boot_args=%#llx\n",
        (unsigned)old_result, (unsigned)candidate_result,
        (unsigned long long)result.entry_pa,
        (unsigned long long)result.boot_args_pa);
    if (old_result != XNU_VALIDATE_UNSAFE_RAM ||
        candidate_result != XNU_VALIDATE_OK || result.segments != 10 ||
        result.phys_base != 0x80000000 || result.mem_size != 0x1e000000)
        return 1;

    struct xnu_payload *p = (struct xnu_payload *)candidate;
    uint64_t old_base = p->phys_base;
    p->phys_base = 0x9fe00000; /* Directly inside hardware-reserved MM area */
    if (xnu_validate_payload(candidate, candidate_bytes, &live,
        0x47880000, 0x28074, 0x40080000, 0x02000000,
        &result) != XNU_VALIDATE_UNSAFE_RAM) return 1;
    p->phys_base = old_base;
    p->segs[0].file_off = UINT64_MAX;
    if (xnu_validate_payload(candidate, candidate_bytes, &live,
        0x47880000, 0x28074, 0x40080000, 0x02000000,
        &result) != XNU_VALIDATE_SEGMENT) return 1;
    puts("Stage18 actual-payload admission and corruption regressions PASS");
    free(old);
    free(candidate);
    return 0;
}
