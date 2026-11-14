/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CSIPHY 2-phase v1.1 register tables - Qualcomm SM8150 as used on the
 * Xiaomi Redmi K20 Pro (raphael).
 *
 * Recovered from the device's own vendor kernel
 *   boot partition -> UNCOMPRESSED_IMG -> arm64 Image -> vmlinux-to-elf
 *
 * Record layout, proven from cam_csiphy_config_dev (0x390ff88):
 *   +0  u32 offset
 *   +4  u32 value
 *   +8  u32 delay_ms   (loaded with "ldr w8, [x, #8]", multiplied by 1000 and
 *                       passed to usleep_range at 0xaca2b8 / 0xaca3ec)
 *   +12 u32 type       (loaded with "ldr w10, [x, #12]", range-checked and used
 *                       for a jump table at 0xaca250 / 0xaca360)
 *
 * The reset table uses 7 type cases (unconditional write, write-if-flag,
 * write-if-not-flag, ...), the lane tables use 4 (literal value, lane enable
 * mask, rate low byte, rate high byte).
 *
 * Register map (all inside the 0x1000 window the device tree maps):
 *   0x000 data lane 0, 0x200 data lane 1, 0x400 data lane 2,
 *   0x700 clock lane, 0x800 reset/enable, 0x814/0x818/0x81c clock,
 *   0x82c..0x854 interrupt masks.
 */
#ifndef __CAMSS_CSIPHY_2PH_1_1_TABLES_H
#define __CAMSS_CSIPHY_2PH_1_1_TABLES_H

#include <linux/types.h>

struct csiphy_reg_1_1 {
	u32 offset;
	u32 value;
	u32 delay_ms;
	u32 type;
};

/* csiphy_1_1_reset: reset sequence, @0x551306c */
static const struct csiphy_reg_1_1 csiphy_1_1_reset[] = {
	{ 0x0814, 0x000000d5, 0, 1 },
	{ 0x0818, 0x00000001, 0, 0 },
	{ 0x081c, 0x00000002, 0, 0 },
	{ 0x0800, 0x00000001, 1, 0 },
	{ 0x0800, 0x00000000, 0, 0 },
};

/* csiphy_1_1_irq: interrupt configuration, @0x55130bc */
static const struct csiphy_reg_1_1 csiphy_1_1_irq[] = {
	{ 0x0814, 0x00000000, 5, 1 },
	{ 0x0818, 0x00000000, 0, 0 },
	{ 0x081c, 0x00000000, 0, 0 },
	{ 0x0800, 0x00000001, 1, 0 },
	{ 0x0800, 0x00000000, 0, 0 },
};

/* csiphy_1_1_2ph: 0x82c..0x854 interrupt masks, @0x551310c */
static const struct csiphy_reg_1_1 csiphy_1_1_2ph[] = {
	{ 0x082c, 0x000000ff, 0, 0 },
	{ 0x0830, 0x000000ff, 0, 0 },
	{ 0x0834, 0x000000fb, 0, 0 },
	{ 0x0838, 0x000000ff, 0, 0 },
	{ 0x083c, 0x0000007f, 0, 0 },
	{ 0x0840, 0x000000ff, 0, 0 },
	{ 0x0844, 0x000000ff, 0, 0 },
	{ 0x0848, 0x000000ef, 0, 0 },
	{ 0x084c, 0x000000ff, 0, 0 },
	{ 0x0850, 0x000000ff, 0, 0 },
	{ 0x0854, 0x000000ff, 0, 0 },
};

/* csiphy_1_1_lane0: data lane 0, base 0x000, @0x55131bc */
static const struct csiphy_reg_1_1 csiphy_1_1_lane0[] = {
	{ 0x0004, 0x0000000c, 0, 0 },
	{ 0x002c, 0x00000001, 0, 0 },
	{ 0x0034, 0x0000000f, 0, 0 },
	{ 0x001c, 0x0000000a, 0, 0 },
	{ 0x0014, 0x00000060, 0, 0 },
	{ 0x0028, 0x00000000, 0, 4 },
	{ 0x003c, 0x000000b8, 0, 0 },
	{ 0x0000, 0x00000090, 0, 0 },
	{ 0x0008, 0x0000000e, 0, 2 },
	{ 0x000c, 0x00000000, 0, 4 },
	{ 0x0010, 0x00000052, 0, 0 },
	{ 0x0038, 0x000000fe, 0, 0 },
	{ 0x0060, 0x00000000, 0, 0 },
	{ 0x0064, 0x0000007f, 0, 0 },
};

/* csiphy_1_1_lane1: clock lane, base 0x700, @0x551346c */
static const struct csiphy_reg_1_1 csiphy_1_1_lane1[] = {
	{ 0x0704, 0x0000000c, 0, 0 },
	{ 0x072c, 0x00000001, 0, 0 },
	{ 0x0734, 0x0000000f, 0, 0 },
	{ 0x071c, 0x0000000a, 0, 0 },
	{ 0x0714, 0x00000060, 0, 0 },
	{ 0x0728, 0x00000004, 0, 0 },
	{ 0x073c, 0x000000b8, 0, 0 },
	{ 0x0700, 0x00000080, 0, 0 },
	{ 0x0708, 0x0000000e, 0, 2 },
	{ 0x070c, 0x000000a5, 0, 0 },
	{ 0x0710, 0x00000052, 0, 0 },
	{ 0x0738, 0x0000001f, 0, 0 },
	{ 0x0760, 0x00000000, 0, 0 },
	{ 0x0764, 0x0000007f, 0, 0 },
};

/* csiphy_1_1_lane2: data lane 1, base 0x200, @0x551371c */
static const struct csiphy_reg_1_1 csiphy_1_1_lane2[] = {
	{ 0x0204, 0x0000000c, 0, 0 },
	{ 0x022c, 0x00000001, 0, 0 },
	{ 0x0234, 0x0000000f, 0, 0 },
	{ 0x021c, 0x0000000a, 0, 0 },
	{ 0x0214, 0x00000060, 0, 0 },
	{ 0x0228, 0x00000000, 0, 4 },
	{ 0x023c, 0x000000b8, 0, 0 },
	{ 0x0200, 0x00000090, 0, 0 },
	{ 0x0208, 0x0000000e, 0, 2 },
	{ 0x020c, 0x00000000, 0, 4 },
	{ 0x0210, 0x00000052, 0, 0 },
	{ 0x0238, 0x000000fe, 0, 0 },
	{ 0x0260, 0x00000000, 0, 0 },
	{ 0x0264, 0x0000007f, 0, 0 },
};

/* csiphy_1_1_lane3: data lane 2, base 0x400, @0x55139cc */
static const struct csiphy_reg_1_1 csiphy_1_1_lane3[] = {
	{ 0x0404, 0x0000000c, 0, 0 },
	{ 0x042c, 0x00000001, 0, 0 },
	{ 0x0434, 0x0000000f, 0, 0 },
	{ 0x041c, 0x0000000a, 0, 0 },
	{ 0x0414, 0x00000060, 0, 0 },
	{ 0x0428, 0x00000000, 0, 4 },
	{ 0x043c, 0x000000b8, 0, 0 },
	{ 0x0400, 0x00000090, 0, 0 },
	{ 0x0408, 0x0000000e, 0, 2 },
	{ 0x040c, 0x00000000, 0, 4 },
	{ 0x0410, 0x00000052, 0, 0 },
	{ 0x0438, 0x000000fe, 0, 0 },
	{ 0x0460, 0x00000000, 0, 0 },
	{ 0x0464, 0x0000007f, 0, 0 },
};

#endif /* __CAMSS_CSIPHY_2PH_1_1_TABLES_H */
