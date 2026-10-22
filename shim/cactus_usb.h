#pragma once
#include <stdint.h>

/* Early-stage, polled MUSB gadget. MMIO addresses are physical (MMU off).
 * A separate XNU post-MMU driver/mapping will be required. */
int usb_init(uint32_t *diagnostics);
void usb_poll(void);
int usb_send_budgeted(const uint8_t *data, unsigned length);
unsigned usb_online(void);
void usb_shutdown(void);
