#pragma once

/*
 * Board-specific constants for the boot shim.
 *
 * The shim is packed as an Android boot image and entered by the device
 * bootloader with the MMU off.  Keep every board difference here so the
 * generic loader/debug code stays identical.
 */

#if defined(BOARD_CACTUS)

#define BOARD_NAME		"cactus"

/* Redmi 6A / MT6765: reserved-memory cont_splash_region, from the live FDT. */
#define FB_BASE			0x5e605000UL
#define FB_PHYS			FB_BASE
#define FB_W			720
#define FB_H			1440
#define FB_STRIDE		(FB_W * 4)	/* a8r8g8b8 */

/* 720 px wide: 8x16 font at 2x gives 45 columns and 45 rows. */
#define CON_SCALE		2
#define CON_MARGIN		12

/* MT6765 GICv3. */
#define GICD_BASE		0x0c000000UL
#define GICR_BASE		0x0c100000UL
#define GICR_SIZE		0x200000UL
#define GICR_PE_SIZE		0x20000UL
#define VTIMER_INTID		27
#define PTIMER_INTID		26
#define SGI_INTID		1

/* No UFS on cactus; storage is MSDC/eMMC. */
#define BOARD_HAS_UFS		0
/* Cortex-A53 supports 4K/64K granules, not 16K. */
#define BOARD_MMU_16K		0
/* CACTUS has the MT6765 top RGU and a pstore reserved region. */
#define BOARD_HAS_WDT		1
#define BOARD_HAS_DBG		1
#define WDT_BASE		0x10007000UL
#define PWRAP_BASE		0x1000D000UL

#else

#define BOARD_NAME		"sweet"

/* Pixel 3 / SM7150 cont_splash_region. */
#define FB_BASE			0x9c000000UL
#define FB_PHYS			FB_BASE
#define FB_W			1080
#define FB_H			2400
#define FB_STRIDE		(FB_W * 4)
#define CON_SCALE		3
#define CON_MARGIN		24

#define GICD_BASE		0x17a00000UL
#define GICR_BASE		0x17a60000UL
#define GICR_SIZE		0x100000UL
#define GICR_PE_SIZE		0x20000UL
#define VTIMER_INTID		19
#define PTIMER_INTID		18
#define SGI_INTID		1

#define BOARD_HAS_UFS		1
#define BOARD_MMU_16K		1
#define BOARD_HAS_WDT		0
#define BOARD_HAS_DBG		0

#endif

/* Common GICv3 redistributor layout. */
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14
#define GICR_SGI		0x10000
#define GICR_IGROUPR0		(GICR_SGI + 0x080)
#define GICR_ISENABLER0		(GICR_SGI + 0x100)
#define GICR_ICENABLER0		(GICR_SGI + 0x180)
#define GICR_IPRIORITYR		(GICR_SGI + 0x400)
#define GICR_ISPENDR0		(GICR_SGI + 0x200)
#define GICR_ISACTIVER0		(GICR_SGI + 0x300)
#define GICR_ICFGR1		(GICR_SGI + 0xc04)
