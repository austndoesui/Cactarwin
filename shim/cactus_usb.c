/* Reusable Stage 14 polled MT6765 MUSB USB2 CDC-ACM transport.
 * EP0 logic is copied from the *hardware-positive* Stage 12 source.
 * Isolated bare-metal MMU-off use only; no XNU integration yet. */
#include "cactus_usb.h"
#include <stdint.h>
#include <stddef.h>
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
/* EP0 CSR0 uses bit 1 for TXPKTRDY; non-control TXCSR uses bit 0. */
#define EP_TXPKTRDY 0x0001u
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
static unsigned serial_state_sent, active;
static uint32_t phy_old18, phy_old1c, phy_old20, phy_old68, phy_old6c;

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

/* Handle OUT traffic and the optional CDC serial-state notification.
 * Data transmission is deliberately owned by usb_send_budgeted(). */
static void bulk_poll(void)
{
    if (!current_cfg) return;
    w8(INDEX, 1);
    uint16_t rx = r16(BASE, 0x16);
    if (rx & 1u) {
        unsigned n = r16(BASE, 0x18);
        if (n > 512u) {
            trace[11]++;
            w16(0x16, 0x0090);
        } else {
            for (unsigned i = 0; i < n; ++i) {
                uint8_t unused;
                read_fifo(1, &unused, 1);
            }
            w16(0x16, rx & (uint16_t)~1u);
            trace[12] += n;
        }
    }
    if ((trace[8] & 1u) && !serial_state_sent) {
        static const uint8_t notice[] = {0xa1,0x20,0,0,0,0,2,0,3,0};
        w8(INDEX, 2);
        uint16_t csr = r16(BASE, 0x12);
        if (!(csr & EP_TXPKTRDY)) {
            write_fifo(2, notice, sizeof(notice));
            w16(0x12, csr | EP_TXPKTRDY);
            serial_state_sent = 1;
        }
    }
    w8(INDEX, 0);
}

int usb_init(uint32_t *diagnostics)
{
    if (!diagnostics) return 0;
    trace = diagnostics;
    for (unsigned i = 0; i < 16u; ++i) trace[i] = 0;
    trace[0] = 0x38425355u;
    trace[1] = r16(BASE, HWVERS);
    trace[2] = r8(BASE, POWER);
    trace[3] = r8(BASE, DEVCTL);
    trace[15] = 0x38444e45u;
    active = 0;
    if (trace[1] != 0xe503u || trace[2] == 0xffu) {
        trace[13] = 1u;
        return 0;
    }

    ep0 = IDLE;
    pending_addr = 0;
    current_cfg = 0;
    addr_pending = 0;
    serial_state_sent = 0;
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));

    /* Exact PHY device-mode sequence that enumerated on real cactus.
     * Preserve originals and restore after the bounded probe. */
    phy_old18 = r32(PHY, 0x18);
    phy_old1c = r32(PHY, 0x1c);
    phy_old20 = r32(PHY, 0x20);
    phy_old68 = r32(PHY, 0x68);
    phy_old6c = r32(PHY, 0x6c);
    *(volatile uint32_t *)(PHY + 0x1c) = phy_old1c & ~(1u << 12);
    *(volatile uint32_t *)(PHY + 0x20) = phy_old20 & ~((1u << 9) | (1u << 8));
    const uint32_t clear68 =
        (1u << 26) | (1u << 18) | (3u << 6) | (3u << 4) |
        (1u << 2) | (15u << 10) | (1u << 20) | (1u << 21) |
        (1u << 19) | (1u << 17) | (1u << 23);
    *(volatile uint32_t *)(PHY + 0x68) = phy_old68 & ~clear68;
    uint32_t mode18 = (phy_old18 & ~(1u << 23)) | (1u << 20);
    mode18 = (mode18 & ~(0xffu << 24)) | (0x40u << 24);
    *(volatile uint32_t *)(PHY + 0x18) = mode18;
    *(volatile uint32_t *)(PHY + 0x6c) = phy_old6c & ~(1u << 16);
    fence();
    uint64_t hz;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
    if (!hz) hz = 13000000u;
    const uint64_t since = counter();
    while (counter() - since < hz / 1000u) { }
    uint32_t mode6c = r32(PHY, 0x6c);
    mode6c = (mode6c & ~0x10u) | 0x2fu | (0x3fu << 8);
    *(volatile uint32_t *)(PHY + 0x6c) = mode6c;
    fence();

    w8(FADDR, 0);
    w8(INDEX, 0);
    w8(POWER, (uint8_t)((r8(BASE, POWER) & ~SOFTCONN) | 0x20u));
    fence();
    w8(POWER, (uint8_t)(r8(BASE, POWER) | SOFTCONN));
    active = 1;
    trace[13] = 14u;
    return 1;
}

void usb_poll(void)
{
    if (!active) return;
    uint8_t events = r8(BASE, INTRUSB);
    if (events) w8(INTRUSB, events); /* MT6765 status: write-one-clear */
    if (events & 0x04u) {
        trace[4]++;
        w8(FADDR, 0);
        current_cfg = 0;
        ep0 = IDLE;
        addr_pending = 0;
        serial_state_sent = 0;
        trace[8] = 0;
    }
    uint16_t tx = r16(BASE, INTRTX);
    if (tx) w16(INTRTX, tx);
    ep0_poll(tx & 1u);
    bulk_poll();
}

unsigned usb_online(void)
{
    /* A configured bulk IN endpoint can be queued before CDC DTR arrives.
     * The host may discard early bytes; later periodic records remain.
     * Stage 14 logs trace[8] so host-open timing can be diagnosed. */
    return active && current_cfg;
}

/* One 56-byte record fits a single full-speed maxpacket; never blocks.
 * Returning 1 means accepted into the FIFO, not acknowledged by host. */
int usb_send_budgeted(const uint8_t *data, unsigned length)
{
    if (!data || !length || length > 64u) return -1;
    if (!usb_online()) return 0;
    w8(INDEX, 1);
    uint16_t csr = r16(BASE, 0x12);
    if (csr & EP_TXPKTRDY) {
        w8(INDEX, 0);
        return 0;
    }
    write_fifo(1, data, length);
    w16(0x12, csr | EP_TXPKTRDY);
    trace[10]++;
    w8(INDEX, 0);
    return 1;
}

void usb_shutdown(void)
{
    if (!active) return;
    w8(POWER, (uint8_t)(r8(BASE, POWER) & ~SOFTCONN));
    fence();
    *(volatile uint32_t *)(PHY + 0x6c) = phy_old6c;
    *(volatile uint32_t *)(PHY + 0x68) = phy_old68;
    *(volatile uint32_t *)(PHY + 0x20) = phy_old20;
    *(volatile uint32_t *)(PHY + 0x1c) = phy_old1c;
    *(volatile uint32_t *)(PHY + 0x18) = phy_old18;
    fence();
    active = 0;
}
