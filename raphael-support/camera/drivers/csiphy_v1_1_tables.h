/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CSIPHY v1.1 register tables for the Qualcomm SM8150 CSIPHY as used on the
 * Xiaomi Redmi K20 Pro (raphael).
 *
 * Extracted from the device's own vendor kernel (/dev/block/by-name/boot ->
 * UNCOMPRESSED_IMG -> arm64 Image -> vmlinux-to-elf -> symbols
 * csiphy_{common,reset,irq}_reg_1_1 and csiphy_2ph_v1_1_{,combo_mode_}reg).
 *
 * IMPORTANT: the control block of this PHY lives at base + 0x800, not at
 * base + 0x1000 as on the v2.x PHYs.  The tables on the Mi 9 (dada) ROM, which
 * were used in the earlier rounds of this port, belong to a different
 * generation and do not apply to raphael.
 */
#ifndef __CAMSS_CSIPHY_V1_1_TABLES_H
#define __CAMSS_CSIPHY_V1_1_TABLES_H

#include <linux/types.h>

struct csiphy_reg_1_1 {
	u32 offset;
	u32 value;
	u32 mask;
	u32 delay_us;
};

/* common_1_1: 4 records, symbol @0x5513028.
 * CAUTION: the decoded values for this table do not look like write pairs
 * (offset 0x828 with value 0x8b0), so either the field order differs from the
 * other tables or the symbol points into a different structure.  Verify against
 * cam_csiphy_config_dev before using it. */
static const struct csiphy_reg_1_1 common_1_1[] = {
	{ 0x0828, 0x000008b0, 0x00000000, 0 },
	{ 0x0000, 0x00000858, 0x00000000, 5 },
	{ 0x0005, 0x0000000e, 0x0000002b, 0 },
	{ 0x0000, 0x00000000, 0x00000000, 1 },
};

/* reset_1_1: 5 records, symbol @0x551306c */
static const struct csiphy_reg_1_1 reset_1_1[] = {
	{ 0x0814, 0x000000d5, 0x00000000, 1 },
	{ 0x0818, 0x00000001, 0x00000000, 0 },
	{ 0x081c, 0x00000002, 0x00000000, 0 },
	{ 0x0800, 0x00000001, 0x00000001, 0 },
	{ 0x0800, 0x00000000, 0x00000000, 0 },
};

/* irq_1_1: 5 records, symbol @0x55130bc */
static const struct csiphy_reg_1_1 irq_1_1[] = {
	{ 0x0814, 0x00000000, 0x00000005, 1 },
	{ 0x0818, 0x00000000, 0x00000000, 0 },
	{ 0x081c, 0x00000000, 0x00000000, 0 },
	{ 0x0800, 0x00000001, 0x00000001, 0 },
	{ 0x0800, 0x00000000, 0x00000000, 0 },
};

/* 2ph_v1_1: 11 records, symbol @0x551310c */
static const struct csiphy_reg_1_1 2ph_v1_1[] = {
	{ 0x082c, 0x000000ff, 0x00000000, 0 },
	{ 0x0830, 0x000000ff, 0x00000000, 0 },
	{ 0x0834, 0x000000fb, 0x00000000, 0 },
	{ 0x0838, 0x000000ff, 0x00000000, 0 },
	{ 0x083c, 0x0000007f, 0x00000000, 0 },
	{ 0x0840, 0x000000ff, 0x00000000, 0 },
	{ 0x0844, 0x000000ff, 0x00000000, 0 },
	{ 0x0848, 0x000000ef, 0x00000000, 0 },
	{ 0x084c, 0x000000ff, 0x00000000, 0 },
	{ 0x0850, 0x000000ff, 0x00000000, 0 },
	{ 0x0854, 0x000000ff, 0x00000000, 0 },
};

/* combo_1_1: 11 records, symbol @0x55131bc */
static const struct csiphy_reg_1_1 combo_1_1[] = {
	{ 0x0004, 0x0000000c, 0x00000000, 0 },
	{ 0x002c, 0x00000001, 0x00000000, 0 },
	{ 0x0034, 0x0000000f, 0x00000000, 0 },
	{ 0x001c, 0x0000000a, 0x00000000, 0 },
	{ 0x0014, 0x00000060, 0x00000000, 0 },
	{ 0x0028, 0x00000000, 0x00000000, 4 },
	{ 0x003c, 0x000000b8, 0x00000000, 0 },
	{ 0x0000, 0x00000090, 0x00000000, 0 },
	{ 0x0008, 0x0000000e, 0x00000000, 2 },
	{ 0x000c, 0x00000000, 0x00000000, 4 },
	{ 0x0010, 0x00000052, 0x00000000, 0 },
};

#endif /* __CAMSS_CSIPHY_V1_1_TABLES_H */
