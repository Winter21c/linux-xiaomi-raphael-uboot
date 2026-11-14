// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy-2ph-1-1.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module, 2-phase v1.1
 * (SM8150 / Xiaomi Redmi K20 Pro "raphael")
 *
 * Register layout and programming sequence recovered from the device's own
 * vendor kernel (see csiphy_v1_1_tables.h for how it was extracted):
 *
 *   cam_csiphy_config_dev (vendor 0x390ff88):
 *       lane_mask = build_mask(lane_mask_field)
 *       write the reset table, dispatching on record->type (field +12) and
 *       honouring record->delay_ms (field +8)
 *       for each enabled lane i, for each record of lane_block[i]:
 *           type 0 -> record->value
 *           type 1 -> lane_mask
 *           type 2 -> low byte  of (rate / 88)
 *           type 3 -> next byte of (rate / 88)
 *           delay  -> usleep_range(delay * 1000, ...)
 *   cam_csiphy_cphy_data_rate_config (vendor 0x390fd2c):
 *       for i < 11: cam_io_w(base + rec->offset, rec->value)
 *
 * Register map (all inside the 0x1000 window the device tree maps):
 *   0x000 data lane 0, 0x200 data lane 1, 0x400 data lane 2,
 *   0x700 clock lane, 0x800 control, 0x814/0x818/0x81c clock,
 *   0x82c..0x854 interrupt masks.
 *
 * The "rate / 88" derivation comes from the vendor table itself: the type-2
 * record for the data lanes writes 0x0e = 14, and 14 * 88 = 1232, which is the
 * OV8856 link rate in Mbit/s for its 2-lane modes.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "camss-csiphy.h"
#include "camss-csiphy-2ph-1-1.h"
#include "csiphy_v1_1_tables.h"

#define CSIPHY_1_1_MAX_DATA_LANES	3

/*
 * Register bases of the four combo blocks, in the order the vendor stores them.
 * Note that the clock lane is the second block and sits at 0x700, which is
 * exactly the 0x80 bit of the enable mask.
 */
static const struct csiphy_reg_1_1 *const csiphy_1_1_lane_tbl[4] = {
	csiphy_1_1_lane0,	/* 0x000, data lane 0 */
	csiphy_1_1_lane1,	/* 0x700, clock lane  */
	csiphy_1_1_lane2,	/* 0x200, data lane 1 */
	csiphy_1_1_lane3,	/* 0x400, data lane 2 */
};

/*
 * csiphy_1_1_get_lane_mask - build the enable mask the way the vendor does
 * @lane_cfg: CSI-2 lane configuration
 *
 * The vendor lane mask uses bit 1 for the clock lane and remaps it to bit 7;
 * the data lanes are packed into bits 0..3.
 */
static u8 csiphy_1_1_get_lane_mask(struct csiphy_lanes_cfg *lane_cfg)
{
	u8 in = 0, out = 0;
	int i, bit;

	for (i = 0; i < lane_cfg->num_data; i++)
		in |= 1 << lane_cfg->data[i].pos;
	in |= 1 << 1;			/* clock lane sits on bit 1 */

	if (in & 0x2)
		out = 0x80;

	bit = 0;
	for (i = 0; i < 4; i++) {
		if (i == 1)
			continue;	/* clock */
		if (in & (1 << i))
			out |= 1 << bit;
		if (bit < 3)
			bit++;
	}
	return out;
}

/*
 * The vendor works in Mbit/s divided by 88: its own table carries 0x0e = 14 for
 * the data lanes, and 14 * 88 = 1232, the OV8856 8 MP two-lane link rate.
 * Our modes are slower, so the computed code will differ from the vendor's
 * table value; the knob lets a test force the vendor value if that turns out to
 * matter.
 */
static int camss_phy_rate_code = -1;
module_param(camss_phy_rate_code, int, 0644);
MODULE_PARM_DESC(camss_phy_rate_code, "override the type-2 register value (-1 = derive from link_freq)");

static u32 csiphy_1_1_rate_code(s64 link_freq)
{
	if (camss_phy_rate_code >= 0)
		return camss_phy_rate_code;
	if (link_freq <= 0)
		return 0;

	/* link_freq is in Hz; the vendor works in Mbit/s divided by 88 */
	return (u32)div_s64(link_freq, 1000000) / 88;
}

static void csiphy_1_1_write_rec(struct csiphy_device *csiphy,
				 const struct csiphy_reg_1_1 *r,
				 u8 lane_mask, u32 rate)
{
	u32 val;

	switch (r->type) {
	case 1:  val = lane_mask; break;
	case 2:  val = rate & 0xff; break;
	case 3:  val = (rate >> 8) & 0xff; break;
	default: val = r->value; break;
	}

	writel_relaxed(val, csiphy->base + r->offset);

	if (r->delay_ms)
		usleep_range(r->delay_ms * 1000, r->delay_ms * 1000 + 10);
}

/*
 * Raw replay, no type dispatch.  The vendor uses this shape in
 * cam_csiphy_reset (0x390f888), which walks the IRQ table with a plain
 * {offset, value, delay} loop and runs before csiphy->cfg.csi2 exists.
 */
static void csiphy_1_1_write_raw(struct csiphy_device *csiphy,
				 const struct csiphy_reg_1_1 *tbl, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		writel_relaxed(tbl[i].value, csiphy->base + tbl[i].offset);
		if (tbl[i].delay_ms)
			usleep_range(tbl[i].delay_ms * 1000,
				     tbl[i].delay_ms * 1000 + 10);
	}
}

/*
 * csiphy_1_1_hw_reset - power-on.  The vendor's ->reset() replays the *IRQ*
 * table (res + 88, i.e. csiphy_irq_reg_1_1), not the reset table:
 *   0x814 = 0, 0x818 = 0, 0x81c = 0, 0x800 = 1, 1 ms, 0x800 = 0
 */
static void csiphy_1_1_hw_reset(struct csiphy_device *csiphy)
{
	csiphy_1_1_write_raw(csiphy, csiphy_1_1_irq,
			     ARRAY_SIZE(csiphy_1_1_irq));
}

static void csiphy_1_1_lanes_enable(struct csiphy_device *csiphy,
				    struct csiphy_config *cfg,
				    s64 link_freq, u8 lane_mask)
{
	struct csiphy_lanes_cfg *lc = &cfg->csi2->lane_cfg;
	u32 rate = csiphy_1_1_rate_code(link_freq);
	int i, j;

	dev_dbg(csiphy->camss->dev,
		"csiphy%d v1.1: %d data lanes, mask 0x%02x, rate code 0x%x\n",
		csiphy->id, lc->num_data, lane_mask, rate);

	/* config path: the reset table (0xd5 / 1 / 2 / 1 / 0), not the IRQ one */
	csiphy_1_1_write_raw(csiphy, csiphy_1_1_reset,
			     ARRAY_SIZE(csiphy_1_1_reset));

	/* data lanes: 0x000, then 0x200 / 0x400 */
	for (i = 0; i < lc->num_data && i < CSIPHY_1_1_MAX_DATA_LANES; i++) {
		int which = (i == 0) ? 0 : i + 1;

		for (j = 0; j < ARRAY_SIZE(csiphy_1_1_lane0); j++)
			csiphy_1_1_write_rec(csiphy,
					     &csiphy_1_1_lane_tbl[which][j],
					     lane_mask, rate);
	}

	/* clock lane block (0x700) */
	for (j = 0; j < ARRAY_SIZE(csiphy_1_1_lane1); j++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_lane_tbl[1][j],
				     lane_mask, rate);

	/* 0x82c..0x854 interrupt masks */
	for (i = 0; i < ARRAY_SIZE(csiphy_1_1_2ph); i++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_2ph[i],
				     lane_mask, rate);
}

static void csiphy_1_1_lanes_disable(struct csiphy_device *csiphy,
				     struct csiphy_config *cfg)
{
	writel_relaxed(0x00, csiphy->base + 0x800);
}

static irqreturn_t csiphy_1_1_isr(int irq, void *dev)
{
	struct csiphy_device *csiphy = dev;
	u32 status = readl_relaxed(csiphy->base + 0x82c);

	if (!status)
		return IRQ_NONE;

	writel_relaxed(status, csiphy->base + 0x82c);
	return IRQ_HANDLED;
}

/*
 * csiphy_1_1_init - called before the ioremap, so it must not touch registers.
 */
static int csiphy_1_1_init(struct csiphy_device *csiphy)
{
	return 0;
}

/*
 * csiphy_1_1_hw_version_read - informational only.  0x828 is the lowest control
 * register this PHY is known to implement (the vendor common table writes it),
 * and the whole block is powered and clocked when this runs.
 */
static void csiphy_1_1_hw_version_read(struct csiphy_device *csiphy,
				       struct device *dev)
{
	u32 hw = readl_relaxed(csiphy->base + 0x828);

	dev_dbg(dev, "CSIPHY%d v1.1 status 0x828 = 0x%08x\n",
		csiphy->id, hw);
}

const struct csiphy_hw_ops csiphy_ops_2ph_1_1 = {
	.get_lane_mask = csiphy_1_1_get_lane_mask,
	.hw_version_read = csiphy_1_1_hw_version_read,
	.init = csiphy_1_1_init,
	.reset = csiphy_1_1_hw_reset,
	.lanes_enable = csiphy_1_1_lanes_enable,
	.lanes_disable = csiphy_1_1_lanes_disable,
	.isr = csiphy_1_1_isr,
};
