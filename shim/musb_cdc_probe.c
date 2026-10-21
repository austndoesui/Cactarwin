/*
 * Temporary, polling MT6765 MUSB USB2 CDC-ACM enumeration experiment.
 *
 * This lives ONLY in the isolated TWRP-return Stage 8 probe. It does not
 * run XNU or promise XNU-side USB logging. Register layout is from
 * cactus-4.9/drivers/misc/mediatek/usb20/mtk_musb_reg.h.
 *
 * The MUSB controller must already have working clocks/PHY. Reject any
 * unrecognised HWVERS rather than blindly attempting PHY/clock writes.
 * Only 8- and 16-bit PIO is used; there are no DMA/TRBs or interrupts.
 */
#include <stdint.h>
#include <stddef.h>

#ifndef USB_PROBE_LEVEL
#define USB_PROBE_LEVEL 2
#endif
#if USB_PROBE_LEVEL < 0 || USB_PROBE_LEVEL > 5
#error USB_PROBE_LEVEL must be 0 through 5
#endif
#define BASE   ((uintptr_t)0x11200000u)
#define PHY    ((uintptr_t)0x11cc0800u)
#define POWER  0x01u
#define FADDR  0x00u
#define INTRTX 0x02u
#define INTRUSB 0x0au
#define INDEX  0x0eu
#define CSR0   0x12u
#define COUNT0 0x18u
#define DEVCTL 0x60u
#define TXFIFOSZ  0x62u
#define RXFIFOSZ  0x63u
#define TXFIFOADD 0x64u
#define RXFIFOADD 0x66u
#define HWVERS 0x6cu

#define RXPKTRDY  0x0001u
#define TXPKTRDY  0x0002u
#define DATAEND   0x0008u
#define SETUPEND  0x0010u
#define STALL     0x0020u
#define SVDRXPKTRDY 0x0040u
#define SVDSETUPEND 0x0080u
#define SOFTCONN 0x40u

static inline uint8_t r8(uintptr_t b, unsigned off)
{
    return *(volatile uint8_t *)(b + off);
}
static inline uint16_t r16(uintptr_t b, unsigned off)
{
    return *(volatile uint16_t *)(b + off);
}
static inline uint32_t r32(uintptr_t b, unsigned off)
{
    return *(volatile uint32_t *)(b + off);
}
static inline void w8(unsigned off, uint8_t v)
{
    *(volatile uint8_t *)(BASE + off) = v;
}
static inline void w16(unsigned off, uint16_t v)
{
    *(volatile uint16_t *)(BASE + off) = v;
}
static inline uint64_t counter(void)
{
    uint64_t n;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(n));
    return n;
}
static inline void fence(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}
static void write_fifo(unsigned ep, const uint8_t *p, unsigned n)
{
    volatile uint8_t *fifo = (volatile uint8_t *)(BASE + 0x20u + 4u * ep);
    for (unsigned i = 0; i < n; ++i)
        *fifo = p[i];
}
static void read_fifo(unsigned ep, uint8_t *p, unsigned n)
{
    volatile uint8_t *fifo = (volatile uint8_t *)(BASE + 0x20u + 4u * ep);
    for (unsigned i = 0; i < n; ++i)
        p[i] = *fifo;
}

/* USB 2.0, IAD + CDC ACM control interface and one bulk data interface. */
static const uint8_t device[] = {
    18,1,0x00,0x02, 0xef,0x02,0x01,64, 0x09,0x12,0x01,0x00,
    0x00,0x01,1,2,3,1
};
static const uint8_t qualifier[] = {10,6,0,2,0xef,2,1,64,1,0};
/* Total config length: 9+8+9+5+5+4+5+7+9+7+7 = 75 bytes. */
static const uint8_t config[] = {
    9,2,75,0, 2,1,0,0x80,50,
    8,11,0,2,0x02,0x02,0x01,0,
    9,4,0,0,1,0x02,0x02,0x01,0,
    5,0x24,0x00,0x10,0x01, /* CDC Header */
    5,0x24,0x01,0x00,1,    /* Call management */
    4,0x24,0x02,0x02,      /* ACM */
    5,0x24,0x06,0,1,       /* Union */
    7,5,0x82,3,16,0,16,   /* EP2 IN interrupt */
    9,4,1,0,2,0x0a,0,0,0,
    7,5,0x01,2,64,0,0,    /* EP1 OUT bulk FS */
    7,5,0x81,2,64,0,0     /* EP1 IN bulk FS */
};
static const uint8_t lang[] = {4,3,0x09,0x04};
static const uint8_t vendor[] = {
    22,3,'C',0,'a',0,'c',0,'t',0,'u',0,'s',0,' ',0,'L',0,'a',0,'b',0
};
static const uint8_t product[] = {
    32,3,'C',0,'a',0,'c',0,'t',0,'u',0,'s',0,' ',0,'S',0,'h',0,'i',0,'m',0,
    ' ',0,'U',0,'S',0,'B',0
};
static const uint8_t serial[] = {
    10,3,'0',0,'0',0,'0',0,'1',0
};
static uint8_t line_coding[7] = {0x00,0xc2,0x01,0,0,0,8}; /* 115200 8N1 */
static uint8_t ctrl_data[256];

enum ep0_state { IDLE, IN_DATA, STATUS_OUT, STATUS_IN, OUT_LINE_CODING };
static enum ep0_state ep0;
static unsigned data_len, data_off, requested_len, want_zlp;
static uint8_t pending_addr, current_cfg;
static unsigned addr_pending;
static const uint8_t *data_ptr;
static uint32_t *trace;
static unsigned hello_sent, serial_state_sent;

static void begin_tx(void)
{
    unsigned remaining = data_len - data_off;
    unsigned count = remaining > 64 ? 64 : remaining;
    if (count) {
        write_fifo(0, data_ptr + data_off, count);
        data_off += count;
    }
    /* Short packet / exactly requested length terminates the DATA stage. */
    unsigned final = (data_off == data_len && !want_zlp);
    w16(CSR0, TXPKTRDY | (final ? DATAEND : 0));
    ep0 = final ? STATUS_OUT : IN_DATA;
    if (data_off == data_len && want_zlp)
        want_zlp = 0;
}

static void ack_status(void)
{
    w16(CSR0, SVDRXPKTRDY | DATAEND);
    ep0 = STATUS_IN;
}

static void reject_setup(void)
{
    w16(CSR0, SVDRXPKTRDY | STALL);
    ep0 = IDLE;
    trace[11]++;
}

static void endpoints_configure(void)
{
    /* MUSB dynamic FIFOs; offsets are in 8-byte units. */
    w8(INDEX, 1);
    w8(TXFIFOSZ, 6);   /* 512 bytes */
    w16(TXFIFOADD, 8); /* 64-byte EP0 */
    w8(RXFIFOSZ, 6);
    w16(RXFIFOADD, 72);
    uint16_t packet = (r8(BASE, POWER) & 0x10u) ? 512 : 64;
    w16(0x10, packet);
    w16(0x14, packet);
    w16(0x12, 0x0048); /* flush TX, clear data toggle */
    w16(0x16, 0x0090); /* flush RX, clear data toggle */
    w8(INDEX, 2);
    w8(TXFIFOSZ, 4);  /* 128-byte interrupt FIFO */
    w16(TXFIFOADD, 136);
    w16(0x10, 16);
    w16(0x12, 0x0048);
    w8(INDEX, 0);
}

static void handle_setup(void)
{
    uint8_t setup[8];
    read_fifo(0, setup, sizeof(setup));
    uint8_t type = setup[0], req = setup[1];
    unsigned value = (unsigned)setup[2] | ((unsigned)setup[3] << 8);
    unsigned length = (unsigned)setup[6] | ((unsigned)setup[7] << 8);
    trace[6]++;
    trace[9] = (type << 8) | req;
    data_ptr = 0;
    data_len = data_off = want_zlp = 0;
    requested_len = length;

    if (type == 0x80 && req == 6) { /* GET_DESCRIPTOR */
        switch (value >> 8) {
        case 1: data_ptr = device; data_len = sizeof(device); break;
        case 2:
        case 7:
            for (unsigned i = 0; i < sizeof(config); ++i)
                ctrl_data[i] = config[i];
            /* USB high-speed bulk endpoints must advertise 512 bytes. */
            if (((r8(BASE, POWER) & 0x10u) != 0) !=
                ((value >> 8) == 7)) {
                ctrl_data[65] = 0;
                ctrl_data[66] = 2;
                ctrl_data[72] = 0;
                ctrl_data[73] = 2;
            }
            if ((value >> 8) == 7)
                ctrl_data[1] = 7;
            data_ptr = ctrl_data;
            data_len = sizeof(config);
            break;
        case 6: data_ptr = qualifier; data_len = sizeof(qualifier); break;
        case 3:
            switch (value & 255) {
            case 0: data_ptr = lang; data_len = sizeof(lang); break;
            case 1: data_ptr = vendor; data_len = sizeof(vendor); break;
            case 2: data_ptr = product; data_len = sizeof(product); break;
            case 3: data_ptr = serial; data_len = sizeof(serial); break;
            }
            break;
        }
    } else if (type == 0x00 && req == 5 && length == 0) {
        pending_addr = (uint8_t)(value & 127u);
        addr_pending = 1;
        ack_status();
        return;
    } else if (type == 0x00 && req == 9 && length == 0) {
        current_cfg = value == 1 ? 1 : 0;
        if (current_cfg)
            endpoints_configure();
        trace[7] = current_cfg;
        ack_status();
        return;
    } else if ((type == 0x80 && req == 8) ||
               (type == 0x81 && req == 10)) {
        ctrl_data[0] = type == 0x80 ? current_cfg : 0;
        data_ptr = ctrl_data;
        data_len = 1;
    } else if ((type == 0x80 || type == 0x81 ||
                type == 0x82) && req == 0) {
        ctrl_data[0] = 0;
        ctrl_data[1] = 0;
        data_ptr = ctrl_data;
        data_len = 2;
    } else if (type == 0x21 && req == 0x20 && length == 7) {
        w16(CSR0, SVDRXPKTRDY);
        ep0 = OUT_LINE_CODING;
        return;
    } else if (type == 0xa1 && req == 0x21) {
        data_ptr = line_coding;
        data_len = sizeof(line_coding);
    } else if (type == 0x21 && (req == 0x22 || req == 0x23) &&
               length == 0) {
        trace[8] = value; /* DTR/RTS */
        ack_status();
        return;
    } else if (type == 0x01 && req == 11 && length == 0) {
        ack_status();
        return;
    }
    if (data_ptr && (type & 0x80)) {
        if (data_len > requested_len)
            data_len = requested_len;
        /* ZLP only if descriptor length is a multiple of 64 and the host
         * requested more bytes than that descriptor contains. */
        want_zlp = data_len && ((data_len % 64) == 0) &&
                   (requested_len > data_len);
        w16(CSR0, SVDRXPKTRDY);
        /* The MTK gadget driver waits for RXPKTRDY to actually drop.
         * A fixed 4096-read spin is far shorter than its allowed delay
         * on a slow MMIO bus, and silently abandons descriptors.
         * Use a bounded counter timeout so recovery is still reachable. */
        uint64_t f;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
        if (!f) f = 13000000;
        const uint64_t ack_start = counter();
        while ((r16(BASE, CSR0) & RXPKTRDY) &&
               counter() - ack_start < f / 20u) { }
        if (r16(BASE, CSR0) & RXPKTRDY) {
            trace[11]++;
            return;
        }
        begin_tx();
    } else {
        reject_setup();
    }
}

static void ep0_poll(unsigned tx_irq)
{
    w8(INDEX, 0);
    uint16_t csr = r16(BASE, CSR0);
    if (csr & SETUPEND) {
        w16(CSR0, SVDSETUPEND);
        ep0 = IDLE;
        return;
    }
    if (csr & 0x0004u) { /* SENTSTALL: write-0-clear, per vendor driver */
        w16(CSR0, csr & (uint16_t)~0x0004u);
        ep0 = IDLE;
        return;
    }
    /* SET_ADDRESS takes effect only after host acknowledges status IN.
     * MUSB raises EP0 TX IRQ on that handshake (Linux gadget does this). */
    if (ep0 == STATUS_IN && tx_irq &&
        !(csr & (RXPKTRDY | TXPKTRDY))) {
        if (addr_pending) {
            w8(FADDR, pending_addr);
            addr_pending = 0;
            trace[5]++;
        }
        ep0 = IDLE;
    }
    /* The next control-IN packet is valid only after MUSB reports the
     * previous EP0 packet completed. Seeing TXPKTRDY==0 alone during
     * tight polling can schedule a second packet prematurely. */
    if (ep0 == IN_DATA && tx_irq && !(csr & TXPKTRDY)) {
        begin_tx();
        return;
    }
    if (!(csr & RXPKTRDY))
        return;
    if (ep0 == OUT_LINE_CODING) {
        unsigned n = r16(BASE, COUNT0);
        if (n >= 7) {
            read_fifo(0, line_coding, 7);
            for (unsigned i = 7; i < n; ++i) {
                uint8_t discarded;
                read_fifo(0, &discarded, 1);
            }
            ack_status();
        } else {
            reject_setup();
        }
    } else if (ep0 == STATUS_OUT) {
        /* DATAEND can complete the IN control transfer in hardware.
         * The next RXPKTRDY may therefore already be the NEXT 8-byte
         * SETUP, not an OUT status packet. The vendor EP0 IRQ handler
         * explicitly checks that case (STATUSOUT -> setup). Old shim
         * code unconditionally discarded that setup, breaking Windows
         * address/configuration descriptor requests. */
        unsigned count = r8(BASE, COUNT0);
        ep0 = IDLE;
        if (count == 8u)
            handle_setup();
        else if (count == 0u)
            w16(CSR0, SVDRXPKTRDY);
        else
            reject_setup();
    } else {
        ep0 = IDLE;
        handle_setup();
    }
}

static void bulk_poll(void)
{
    if (!current_cfg)
        return;
#if USB_PROBE_LEVEL == 4
    /* Fixed 32-byte raw diagnostic frame, intentionally includes binary
     * values that cannot be validated by a text-only Windows COM console.
     * CLOG | version=1 | sequence=1 | 24 known payload bytes. */
    static const uint8_t hello[] = {
        0x43,0x4c,0x4f,0x47, 0x01,0x00,0x00,0x00,
        0x01,0x00,0x00,0x00, 0x00,0xff,0x7f,0x80,
        0x55,0xaa,0x0d,0x0a, 0x10,0x20,0x30,0x40,
        0x50,0x60,0x70,0x90, 0xa0,0xb0,0xc0,0xd0
    };
#else
    static const uint8_t hello[] =
        "CACTUS MUSB CDC: enumerated; XNU not running\r\n";
#endif
    w8(INDEX, 1);
    uint16_t tx = r16(BASE, 0x12);
#if USB_PROBE_LEVEL == 4
    /* Windows may discard bulk data sent before usbser.sys opens the COM
     * port. Wait for CDC SET_CONTROL_LINE_STATE(DTR), then emit several
     * identical frames as the host drains endpoint 1. */
    if ((trace[8] & 1u) && hello_sent < 8u && !(tx & 1u)) {
        write_fifo(1, hello, sizeof(hello)); /* all 32 raw bytes */
#else
    if (!hello_sent && !(tx & 1u)) {
        write_fifo(1, hello, sizeof(hello) - 1);
#endif
        w16(0x12, tx | 1u);
        hello_sent++;
        trace[10]++;
    }
    uint16_t rx = r16(BASE, 0x16);
    if (rx & 1u) {
        unsigned count = r16(BASE, 0x18);
        if (count > 512) {
            trace[11]++; /* suspicious RXCOUNT, do not spin on MMIO */
            w16(0x16, 0x0090); /* clear toggle and flush endpoint */
            w8(INDEX, 0);
            return;
        }
        for (unsigned i = 0; i < count; ++i) {
            uint8_t discard;
            read_fifo(1, &discard, 1);
        }
        w16(0x16, rx & (uint16_t)~1u);
        trace[12] += count;
    }
    if ((trace[8] & 1u) && !serial_state_sent) {
        static const uint8_t notification[] =
            {0xa1,0x20,0,0,0,0,2,0,3,0};
        w8(INDEX, 2);
        uint16_t notify_csr = r16(BASE, 0x12);
        if (!(notify_csr & 1u)) {
            write_fifo(2, notification, sizeof(notification));
            w16(0x12, notify_csr | 1u);
            serial_state_sent = 1;
        }
    }
    w8(INDEX, 0);
}

/* Returns to Stage 8 / TWRP. Trace is 16 words at initramfs +64. */
void musb_cdc_probe(uint32_t *result)
{
    trace = result;
    for (unsigned i = 0; i < 16; ++i)
        trace[i] = 0;
    trace[0] = 0x38425355u; /* USB8 */
    trace[1] = r16(BASE, HWVERS);
    trace[2] = r8(BASE, POWER);
    trace[3] = r8(BASE, DEVCTL);
    trace[4] = 0; /* USB reset count */
    trace[15] = 0x38444e45u; /* END8 */
    fence();

    /* The exact MUSB revision has been read under a working TWRP 4.9
     * kernel. If bootloader clocks/PHY are not accessible, don't connect. */
    if (trace[1] != 0xe503u || (trace[2] == 0xffu)) {
        trace[13] = 1; /* rejected controller precondition */
        return;
    }

#if USB_PROBE_LEVEL == 0
    /* Stage 9: exercise C ABI, local timer and MUSB reads only.
     * A passing return-to-TWRP excludes these operations as the hang. */
    trace[13] = 9;
    return;
#endif

    ep0 = IDLE;
    pending_addr = 0;
    current_cfg = 0;
    addr_pending = 0;
    hello_sent = 0;
    serial_state_sent = 0;
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
#if USB_PROBE_LEVEL == 4
    /* Stage 13 established that LK leaves the PHY in an inactive
     * device mode (PHY +0x6c == 0), so SOFTCONN alone cannot enumerate.
     * Apply the same *hardware-tested* PHY activation before Stage 12
     * starts EP0/CDC and restore the original values on handoff. */
    const uint32_t phy_old18 = r32(PHY, 0x18);
    const uint32_t phy_old1c = r32(PHY, 0x1c);
    const uint32_t phy_old20 = r32(PHY, 0x20);
    const uint32_t phy_old68 = r32(PHY, 0x68);
    const uint32_t phy_old6c = r32(PHY, 0x6c);
    *(volatile uint32_t *)(PHY + 0x1c) = phy_old1c & ~(1u << 12);
    *(volatile uint32_t *)(PHY + 0x20) =
        phy_old20 & ~((1u << 9) | (1u << 8));
    const uint32_t phy_clrmask68 =
        (1u << 26) | (1u << 18) | (3u << 6) | (3u << 4) |
        (1u << 2) | (15u << 10) | (1u << 20) | (1u << 21) |
        (1u << 19) | (1u << 17) | (1u << 23);
    *(volatile uint32_t *)(PHY + 0x68) = phy_old68 & ~phy_clrmask68;
    uint32_t phy_active18 = (phy_old18 & ~(1u << 23)) | (1u << 20);
    phy_active18 =
        (phy_active18 & ~(0xffu << 24)) | (0x40u << 24);
    *(volatile uint32_t *)(PHY + 0x18) = phy_active18;
    *(volatile uint32_t *)(PHY + 0x6c) = phy_old6c & ~(1u << 16);
    fence();
    uint64_t phy_freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(phy_freq));
    if (!phy_freq) phy_freq = 13000000;
    uint64_t phy_delay = counter();
    while (counter() - phy_delay < (phy_freq / 1000u)) { }
    uint32_t phy_active6c = r32(PHY, 0x6c);
    phy_active6c &= ~0x10u;
    phy_active6c |= 0x2fu | (0x3fu << 8);
    *(volatile uint32_t *)(PHY + 0x6c) = phy_active6c;
    fence();
#endif
    w8(FADDR, 0);
    w8(INDEX, 0);
    w8(POWER, (uint8_t)((r8(BASE, POWER) & ~SOFTCONN) | 0x20u));
    fence();
    w8(POWER, (uint8_t)(r8(BASE, POWER) | SOFTCONN));
    trace[13] = 2; /* connected */

#if USB_PROBE_LEVEL == 1
    /* Stage 10: USB soft-connect only, no EP0/FIFO polling or CDC code.
     * This bounds exposure and helps isolate MMIO writes. */
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    if (!f) f = 13000000;
    uint64_t begin = counter();
    while (counter() - begin < f) { }
    trace[13] = 10;
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
    fence();
    return;
#endif

#if USB_PROBE_LEVEL == 3
    /* Stage 11: sample MUSB interrupt and endpoint-zero status for 2s.
     * No FIFO or CSR0 servicing, so the host will NOT enumerate.
     * Always deassert SOFTCONN before chainloading recovery. */
    uint64_t f11;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f11));
    if (!f11) f11 = 13000000;
    uint64_t begin11 = counter();
    trace[13] = 11;
    while (counter() - begin11 < f11 * 2u) {
        uint8_t usb_irq = r8(BASE, INTRUSB);
        uint16_t tx_irq = r16(BASE, INTRTX);
        w8(INDEX, 0);
        uint16_t csr = r16(BASE, CSR0);
        if (usb_irq & 0x04u) trace[4]++;  /* USB RESET */
        if (tx_irq & 1u) trace[5]++;      /* EP0 TX IRQ */
        if (csr & RXPKTRDY) trace[6]++;  /* host setup pending */
        trace[8] = usb_irq;
        trace[9] = csr;
        trace[10] = r8(BASE, DEVCTL);
        trace[11]++;
    }
    trace[14] = r8(BASE, POWER);
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
    fence();
    return;
#endif

#if USB_PROBE_LEVEL == 5
    /* Stage 13: isolate MT6765 USB2 PHY recovery (from the cactus
     * Linux 4.9 vendor driver) and two-second link/EP0 IRQ sampling.
     * Do not touch endpoint FIFOs or attempt descriptor responses.
     * Return original PHY control registers before TWRP handoff. */
    uint64_t f13;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f13));
    if (!f13) f13 = 13000000;
    const uint32_t old18 = r32(PHY, 0x18);
    const uint32_t old1c = r32(PHY, 0x1c);
    const uint32_t old20 = r32(PHY, 0x20);
    const uint32_t old68 = r32(PHY, 0x68);
    const uint32_t old6c = r32(PHY, 0x6c);
    trace[7] = old6c;
    trace[12] = old18;
    trace[13] = 13;
    /* Keep original efuse and analog trim. Touch only PHY mode bits
     * present in usb_phy_recover() / set_usb_phy_mode(PHY_DEV_ACTIVE). */
    *(volatile uint32_t *)(PHY + 0x1c) = old1c & ~(1u << 12);
    *(volatile uint32_t *)(PHY + 0x20) = old20 & ~((1u << 9) | (1u << 8));
    const uint32_t clrmask68 =
        (1u << 26) | (1u << 18) | (3u << 6) | (3u << 4) |
        (1u << 2) | (15u << 10) | (1u << 20) | (1u << 21) |
        (1u << 19) | (1u << 17) | (1u << 23);
    *(volatile uint32_t *)(PHY + 0x68) = old68 & ~clrmask68;
    uint32_t phy18 = old18 & ~(1u << 23);
    phy18 |= (1u << 20);
    phy18 = (phy18 & ~(0xffu << 24)) | (0x40u << 24);
    *(volatile uint32_t *)(PHY + 0x18) = phy18;
    *(volatile uint32_t *)(PHY + 0x6c) = old6c & ~(1u << 16);
    fence();
    const uint64_t delay = counter();
    while (counter() - delay < (f13 / 1000u)) { }
    uint32_t phy6c = r32(PHY, 0x6c);
    phy6c &= ~(0x10u);
    phy6c |= 0x2fu | (0x3fu << 8);
    *(volatile uint32_t *)(PHY + 0x6c) = phy6c;
    fence();
    trace[8] = r32(PHY, 0x6c);
    trace[14] = r32(PHY, 0x18);
    w8(POWER, (uint8_t)(r8(BASE, POWER) | SOFTCONN | 0x20u));
    const uint64_t begin13 = counter();
    while (counter() - begin13 < f13 * 2u) {
        uint8_t usb_irq = r8(BASE, INTRUSB);
        uint16_t tx_irq = r16(BASE, INTRTX);
        w8(INDEX, 0);
        uint16_t csr = r16(BASE, CSR0);
        if (usb_irq & 0x04u) trace[4]++;
        if (tx_irq & 1u) trace[5]++;
        if (csr & RXPKTRDY) trace[6]++;
        trace[9] = usb_irq;
        trace[10] = r8(BASE, DEVCTL);
        trace[11]++;
    }
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
    fence();
    *(volatile uint32_t *)(PHY + 0x6c) = old6c;
    *(volatile uint32_t *)(PHY + 0x68) = old68;
    *(volatile uint32_t *)(PHY + 0x20) = old20;
    *(volatile uint32_t *)(PHY + 0x1c) = old1c;
    *(volatile uint32_t *)(PHY + 0x18) = old18;
    fence();
    return;
#endif

    /* Time-limited proof so a failed enumerator still returns to TWRP.
     * Stage 7 must first confirm live register visibility on hardware. */
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t start = counter();
    if (freq == 0)
        freq = 13000000;
    while ((counter() - start) < freq * 10) {
        uint8_t events = r8(BASE, INTRUSB);
        /* MT6765 MUSB interrupt status is write-one-to-clear.
         * The vendor's musb_interrupt() writes the sampled status back
         * BEFORE handling each event. Reading alone leaves RESET/EP0
         * pending forever and can repeatedly cancel SET_ADDRESS. */
        if (events)
            w8(INTRUSB, events);
        if (events & 0x04u) { /* USB RESET */
            trace[4]++;
            w8(FADDR, 0);
            current_cfg = 0;
            ep0 = IDLE;
            addr_pending = 0;
            hello_sent = 0;
            serial_state_sent = 0;
            trace[8] = 0; /* CDC DTR no longer valid after a bus reset */
        }
        uint16_t tx_irq = r16(BASE, INTRTX);
        if (tx_irq)
            w16(INTRTX, tx_irq); /* W1C: allow next EP0 completion */
        ep0_poll(tx_irq & 1u);
        bulk_poll();
    }
    trace[14] = current_cfg;
#if USB_PROBE_LEVEL == 4
    trace[13] = 12;  /* reached timeout/return path of raw CDC test */
#endif
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
    fence();
#if USB_PROBE_LEVEL == 4
    *(volatile uint32_t *)(PHY + 0x6c) = phy_old6c;
    *(volatile uint32_t *)(PHY + 0x68) = phy_old68;
    *(volatile uint32_t *)(PHY + 0x20) = phy_old20;
    *(volatile uint32_t *)(PHY + 0x1c) = phy_old1c;
    *(volatile uint32_t *)(PHY + 0x18) = phy_old18;
    fence();
#endif
}
