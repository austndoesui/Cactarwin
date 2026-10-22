/*
 * Just enough flattened-device-tree parsing to find what the bootloader put
 * in /chosen (initrd location), plus a diagnostic dump of the nodes we need
 * when the device and QEMU disagree.
 *
 * Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"
#include "console.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static uint64_t be64(uintptr_t a)
{
	return (uint64_t)be32(a) << 32 | be32(a + 4);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p, uintptr_t end)
{
	while (p < end && *(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_check(uintptr_t fdt, uint32_t *total_out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;

	uint32_t total = be32(fdt + 4);
	uint32_t off_struct = be32(fdt + 8);
	uint32_t off_strings = be32(fdt + 12);
	uint32_t off_rsv = be32(fdt + 16);
	uint32_t version = be32(fdt + 20);

	if (total < 40 || off_struct >= total || off_strings >= total ||
	    off_rsv >= total || version < 16)
		return 0;
	if (total_out)
		*total_out = total;
	return 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	uint32_t total;

	if (!fdt_check(fdt, &total))
		return 0;

	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	uintptr_t end = fdt + total;
	int depth = 0, in_chosen = 0;

	while (p + 4 <= end) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p, end));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			if (depth)
				depth--;
			break;
		case FDT_PROP: {
			if (p + 8 > end)
				return 0;
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (val + len > end || strings + nameoff >= end)
				return 0;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = be64(val);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
	return 0;
}

static void dump_string(const char *s, uint32_t len)
{
	char tmp[33];
	uint32_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;

	for (uint32_t i = 0; i < n; i++)
		tmp[i] = s[i] ? s[i] : ' ';
	tmp[n] = 0;
	con_str(" STR", tmp);
}

void fdt_dump(uintptr_t fdt)
{
	uint32_t total;

	if (!fdt_check(fdt, &total)) {
		con_hex("FDT", fdt);
		con_str("FDT", "BAD MAGIC/HEADER");
		return;
	}

	con_hex2("FDT", "size=", total, "ver=", be32(fdt + 20));
	con_hex("STRUCT", be32(fdt + 8));
	con_hex("STRINGS", be32(fdt + 12));
	con_hex("RSVMAP", be32(fdt + 16));
	con_skip();

	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	uintptr_t end = fdt + total;
	int depth = 0, show = 0;

	while (p + 4 <= end) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			if (depth == 2) {
				show = streq(name, "chosen") ||
				       streq(name, "memory") ||
				       streq(name, "reserved-memory");
				if (show)
					con_str("NODE", name);
			} else if (show && depth == 3) {
				con_str(" CHILD", name);
			}
			p = align4(skip_str(p, end));
			break;
		}
		case FDT_END_NODE:
			if (depth == 2)
				show = 0;
			if (depth)
				depth--;
			break;
		case FDT_PROP: {
			if (p + 8 > end)
				goto bad;
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (val + len > end || strings + nameoff >= end)
				goto bad;
			const char *pn = (const char *)(strings + nameoff);
			if (show) {
				if (len == 4) {
					con_hex2(" PROP", pn, be32(val), 0, 0);
				} else if (len == 8) {
					con_str(" PROP", pn);
					con_hex("  VAL", be64(val));
				} else if (len && pn[0] == 'b' && pn[1] == 'o') {
					con_str(" PROP", pn);
					dump_string((const char *)val, len);
				} else if (streq(pn, "reg") && len >= 8) {
					con_hex2("  REG", "a=", be32(val), "b=", be32(val + 4));
				} else if (len) {
					con_str(" PROP", pn);
					con_hex("  LEN", len);
				}
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		case FDT_END:
			return;
		default:
			goto bad;
		}
	}
	return;

bad:
	con_str("FDT", "BAD STRUCTURE");
}
