// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy-2ph-1-1.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module, 2-phase v1.1
 * (SM8150 / Xiaomi Redmi K20 Pro "raphael")
 *
 * Register layout and programming sequence recovered from the device's own
 * vendor kernel (see csiphy_v1_1_tables.h for the extraction):
 *
 *   cam_csiphy_config_dev (vendor 0x390ff88):
 *       lane_mask = build_mask(lane_mask_field)
 *       write reset table, dispatching on record->type
 *       for each enabled lane i:
 *           for each record of lane_block[i]:
 *               type 0 -> record->value
 *               type 1 -> lane_mask
 *               type 2 -> low byte of the rate field
 *               type 3 -> next byte of the rate field
 *               delay  -> usleep_range(delay * 1000, ...)
 *   cam_csiphy_cphy_data_rate_config (vendor 0x390fd2c):
 *       for i < 11: write 2ph_table[i] to base + offset
 *
 * Unlike the older 2ph v1.0 PHY:
 *   - data lanes live at 0x000 / 0x200 / 0x400 and the clock lane at 0x700
 *   - the control block is at 0x800, not 0x140
 *   - interrupt masks are at 0x82c..0x854
 *   - everything fits in the 0x1000 window the device tree maps, so there is no
 *     second register page (the Mi 9 PHY's base+0x1000 block does not exist here)
 */

#include <linux/delay.h>
#include <linux/io.h>

#include "camss-csiphy.h"
#include "camss-csiphy-2ph-1-1.h"
#include "csiphy_v1_1_tables.h"

#define CSIPHY_1_1_MAX_DATA_LANES	3

/* register base of each combo block, in the order the vendor tables store them */
static const struct csiphy_reg_1_1 *const csiphy_1_1_lane_tbl[4] = {
	csiphy_1_1_lane0,	/* 0x000, data lane 0 */
	csiphy_1_1_lane1,	/* 0x700, clock lane  */
	csiphy_1_1_lane2,	/* 0x200, data lane 1 */
	csiphy_1_1_lane3,	/* 0x400, data lane 2 */
};

/*
 * Build the lane enable mask exactly like the vendor: the input lane mask uses
 * bit 1 for the clock lane (remapped to bit 7), the data lanes are packed into
 * bits 0..3.
 */
static u8 csiphy_1_1_lane_mask(struct csiphy_device *csiphy)
{
	struct csiphy_lanes_cfg *cfg = &csiphy->cfg.csi2->lane_cfg;
	u8 in = 0;
	u8 out = 0;
	int i, bit;

	for (i = 0; i < cfg->num_data; i++)
		in |= 1 << cfg->data[i].pos;
	in |= 1 << 1;			/* clock lane on input bit 1 */

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

static void csiphy_1_1_write_rec(struct csiphy_device *csiphy,
				 const struct csiphy_reg_1_1 *r, u8 lane_mask)
{
	u32 val;

	switch (r->type) {
	case 1:  val = lane_mask; break;
	case 2:  val = 0; break;
	case 3:  val = 0; break;
	default: val = r->value; break;
	}

	writel_relaxed(val, csiphy->base + r->offset);

	if (r->delay_ms)
		usleep_range(r->delay_ms * 1000, r->delay_ms * 1000 + 10);
}

/*
 * csiphy_1_1_reset - the vendor reset sequence:
 *   0x814 = 0xd5, 0x818 = 1, 0x81c = 2, then 0x800 pulsed 1 -> 0
 */
static void csiphy_1_1_reset(struct csiphy_device *csiphy)
{
	writel_relaxed(0xd5, csiphy->base + 0x814);
	writel_relaxed(0x01, csiphy->base + 0x818);
	writel_relaxed(0x02, csiphy->base + 0x81c);
	writel_relaxed(0x01, csiphy->base + 0x800);
	usleep_range(1000, 1010);
	writel_relaxed(0x00, csiphy->base + 0x800);
	usleep_range(1000, 1010);
}

static void csiphy_1_1_regs(struct csiphy_device *csiphy)
{
	u8 mask = csiphy_1_1_lane_mask(csiphy);
	int i;

	for (i = 0; i < ARRAY_SIZE(csiphy_1_1_reset); i++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_reset[i], mask);

	for (i = 0; i < ARRAY_SIZE(csiphy_1_1_irq); i++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_irq[i], mask);
}

static void csiphy_1_1_lanes_enable(struct csiphy_device *csiphy,
				    struct csiphy_config *cfg)
{
	struct csiphy_lanes_cfg *lc = &cfg->csi2->lane_cfg;
	u8 lane_mask = csiphy_1_1_lane_mask(csiphy);
	int i, j;

	dev_dbg(csiphy->camss->dev,
		"csiphy%d v1.1: %d data lanes, enable mask 0x%02x\n",
		csiphy->id, lc->num_data, lane_mask);

	csiphy_1_1_reset(csiphy);
	writel_relaxed(0x01, csiphy->base + 0x800);

	/* data lanes: 0x000, then 0x200 / 0x400 */
	for (i = 0; i < lc->num_data && i < CSIPHY_1_1_MAX_DATA_LANES; i++) {
		int which = (i == 0) ? 0 : i + 1;

		for (j = 0; j < ARRAY_SIZE(csiphy_1_1_lane0); j++)
			csiphy_1_1_write_rec(csiphy,
					     &csiphy_1_1_lane_tbl[which][j],
					     lane_mask);
	}

	/* clock lane block (0x700) */
	for (j = 0; j < ARRAY_SIZE(csiphy_1_1_lane1); j++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_lane_tbl[1][j],
				     lane_mask);

	/* 0x82c..0x854 interrupt masks */
	for (i = 0; i < ARRAY_SIZE(csiphy_1_1_2ph); i++)
		csiphy_1_1_write_rec(csiphy, &csiphy_1_1_2ph[i], lane_mask);

	writel_relaxed(0x01, csiphy->base + 0x800);
}

static void csiphy_1_1_lanes_disable(struct csiphy_device *csiphy,
				     struct csiphy_config *cfg)
{
	writel_relaxed(0x00, csiphy->base + 0x800);
}

static irqreturn_t csiphy_1_1_isr(int irq, void *data)
{
	struct csiphy_device *csiphy = data;
	u32 status = readl_relaxed(csiphy->base + 0x82c);

	if (!status)
		return IRQ_NONE;

	writel_relaxed(status, csiphy->base + 0x82c);
	return IRQ_HANDLED;
}

const struct csiphy_hw_ops csiphy_ops_2ph_1_1 = {
	.regs = csiphy_1_1_regs,
	.lanes_enable = csiphy_1_1_lanes_enable,
	.lanes_disable = csiphy_1_1_lanes_disable,
	.isr = csiphy_1_1_isr,
};
