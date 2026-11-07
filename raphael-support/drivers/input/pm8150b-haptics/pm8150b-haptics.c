// SPDX-License-Identifier: GPL-2.0
/*
 * PM8150B haptics driver for the Xiaomi Redmi K20 Pro (raphael)
 *
 * The PM8150B PMIC exposes a Haptics peripheral at SPMI USID 0x13 that can
 * drive an ERM or LRA actuator.  The vendor kernel drives it with
 * "qti-haptics"; the mainline kernel has no driver for this block yet.
 *
 * The register window is reached through the parent PMIC regmap using the
 * Qualcomm 16-bit SPMI extended address layout (USID << 8 | offset), which is
 * how the vendor device tree describes it as well (reg = <0x1300>).
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>

/* Haptics peripheral registers (offset inside USID 0x13) */
#define REG_HAP_STATUS1		0x0A
#define REG_HAP_EN_CTL1		0x46
#define REG_HAP_EN_CTL2		0x48
#define REG_HAP_EN_CTL3		0x4A
#define REG_HAP_AUTO_RES_CTRL	0x4B
#define REG_HAP_CFG1		0x4C
#define REG_HAP_CFG2		0x4D
#define REG_HAP_SEL		0x4E
#define REG_HAP_AUTO_RES_CFG	0x4F
#define REG_HAP_SLEW_CFG	0x50
#define REG_HAP_VMAX_CFG	0x51
#define REG_HAP_ILIM_CFG	0x52
#define REG_HAP_SC_DEB_CFG	0x53
#define REG_HAP_RATE_CFG1	0x54
#define REG_HAP_RATE_CFG2	0x55
#define REG_HAP_ZX_CFG		0x5A
#define REG_HAP_BRAKE		0x5C
#define REG_HAP_PLAY		0x70
#define REG_HAP_SEC_ACCESS	0xD0
#define REG_HAP_PERPH_RESET_CTL3 0xDA

#define HAP_EN_BIT		BIT(7)
#define HAP_PLAY_BIT		BIT(7)

#define HAP_HBRIDGE_EN_BIT	BIT(7)
#define HAP_PWM_SIGNAL_EN_BIT	BIT(6)
#define HAP_ILIM_EN_BIT		BIT(5)
#define HAP_ILIM_CC_EN_BIT	BIT(4)
#define HAP_AUTO_RES_RBIAS_EN_BIT BIT(3)
#define HAP_DAC_EN_BIT		BIT(2)
#define HAP_PWM_CTL_EN_BIT	BIT(0)

#define HAP_AUTO_RES_EN_BIT	BIT(7)

#define HAP_VMAX_MV_MASK	GENMASK(5, 1)
#define HAP_VMAX_MV_SHIFT	1
#define HAP_VMAX_MV_LSB		116

#define HAP_ZX_DET_DEB_MASK	GENMASK(2, 0)
#define ZX_DET_DEB_80US		3

#define HAP_ILIM_SEL_1000MA	BIT(1)
#define HAP_WF_SOURCE_VMAX	0
#define HAP_WF_TRIGGER_BIT	BIT(0)

#define HAP_PLAY_RATE_US_LSB	5
#define HAP_PLAY_RATE_US_DEFAULT 5715
#define HAP_VMAX_MV_DEFAULT	1800
#define HAP_VMAX_MV_MAX		3596

#define HAP_SEC_ACCESS_MAGIC	0xA5
#define HAP_MAX_STRENGTH	0xFFFF

struct pm8150b_hap {
	struct device *dev;
	struct regmap *regmap;
	struct input_dev *input;
	struct mutex lock;		/* serializes the worker */
	struct work_struct work;
	u16 reg_base;
	u16 vmax_mv;
	u16 strength;			/* requested rumble strength (0..0xffff) */
	bool playing;
};

static inline bool is_secure(u8 addr)
{
	return addr > 0xD0;
}

static int hap_write(struct pm8150b_hap *hap, u8 addr, u8 val)
{
	if (is_secure(addr)) {
		int ret = regmap_write(hap->regmap,
				       hap->reg_base + REG_HAP_SEC_ACCESS,
				       HAP_SEC_ACCESS_MAGIC);
		if (ret)
			return ret;
	}
	return regmap_write(hap->regmap, hap->reg_base + addr, val);
}

static int hap_write2(struct pm8150b_hap *hap, u8 addr, u8 v0, u8 v1)
{
	if (is_secure(addr) || is_secure(addr + 1)) {
		int ret = hap_write(hap, addr, v0);
		if (ret)
			return ret;
		return hap_write(hap, addr + 1, v1);
	}
	return regmap_bulk_write(hap->regmap, hap->reg_base + addr,
				 (const u8[]){ v0, v1 }, 2);
}

static int hap_masked_write(struct pm8150b_hap *hap, u8 addr, u8 mask, u8 val)
{
	if (is_secure(addr)) {
		int ret = regmap_write(hap->regmap,
				       hap->reg_base + REG_HAP_SEC_ACCESS,
				       HAP_SEC_ACCESS_MAGIC);
		if (ret)
			return ret;
	}
	return regmap_update_bits(hap->regmap, hap->reg_base + addr, mask, val);
}

static void hap_dump_all(struct pm8150b_hap *hap, const char *tag)
{
	static const u8 regs[] = { 0x0a, 0x46, 0x48, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e,
				   0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
				   0x56, 0x57, 0x58, 0x5a, 0x5c, 0x5e, 0x60, 0x70 };
	u8 i;

	dev_info(hap->dev, "%s register dump:\n", tag);
	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		unsigned int v = 0xdead;

		regmap_read(hap->regmap, hap->reg_base + regs[i], &v);
		pr_info("  hap[%#04x] = %#04x\n", hap->reg_base + regs[i], v);
	}
}

static void hap_dump_state(struct pm8150b_hap *hap, const char *tag)
{
	unsigned int en = 0xdead, play = 0xdead, status = 0xdead;

	regmap_read(hap->regmap, hap->reg_base + REG_HAP_EN_CTL1, &en);
	regmap_read(hap->regmap, hap->reg_base + REG_HAP_PLAY, &play);
	regmap_read(hap->regmap, hap->reg_base + REG_HAP_STATUS1, &status);
	dev_info(hap->dev, "%s: EN_CTL1=%#x PLAY=%#x STATUS1=%#x\n",
		 tag, en, play, status);
}

static int hap_module_enable(struct pm8150b_hap *hap, bool en)
{
	return hap_write(hap, REG_HAP_EN_CTL1, en ? HAP_EN_BIT : 0);
}

static int hap_play(struct pm8150b_hap *hap, bool play)
{
	return hap_write(hap, REG_HAP_PLAY, play ? HAP_PLAY_BIT : 0);
}

static int hap_set_vmax(struct pm8150b_hap *hap, u16 vmax_mv)
{
	u8 val;

	if (vmax_mv > HAP_VMAX_MV_MAX)
		vmax_mv = HAP_VMAX_MV_MAX;
	val = (vmax_mv / HAP_VMAX_MV_LSB) << HAP_VMAX_MV_SHIFT;
	return hap_masked_write(hap, REG_HAP_VMAX_CFG, HAP_VMAX_MV_MASK, val);
}

static int hap_hw_init(struct pm8150b_hap *hap)
{
	int ret;
	u16 rate = hap->vmax_mv ? HAP_PLAY_RATE_US_DEFAULT : HAP_PLAY_RATE_US_DEFAULT;
	u16 rate_val;

	/* ERM actuator (the K20 Pro uses a coin/ERM motor) */
	ret = hap_write(hap, REG_HAP_CFG1, 1);
	if (ret)
		return ret;

	/* Current limit: 1000 mA */
	ret = hap_write(hap, REG_HAP_ILIM_CFG, HAP_ILIM_SEL_1000MA);
	if (ret)
		return ret;

	/* H-bridge, PWM signal, current limit, DAC, PWM control */
	ret = hap_write(hap, REG_HAP_EN_CTL3,
			HAP_HBRIDGE_EN_BIT | HAP_PWM_SIGNAL_EN_BIT |
			HAP_ILIM_EN_BIT | HAP_ILIM_CC_EN_BIT |
			HAP_AUTO_RES_RBIAS_EN_BIT | HAP_DAC_EN_BIT |
			HAP_PWM_CTL_EN_BIT);
	if (ret)
		return ret;

	/* Zero-cross detection debounce */
	ret = hap_masked_write(hap, REG_HAP_ZX_CFG, HAP_ZX_DET_DEB_MASK,
			       ZX_DET_DEB_80US);
	if (ret)
		return ret;

	/* Waveform play rate (sample period) */
	rate_val = rate / HAP_PLAY_RATE_US_LSB;
	ret = hap_write2(hap, REG_HAP_RATE_CFG1, rate_val & 0xff,
			 (rate_val >> 8) & 0x0f);
	if (ret)
		return ret;

	/* ERM: auto-resonance off */
	ret = hap_masked_write(hap, REG_HAP_AUTO_RES_CTRL,
				HAP_AUTO_RES_EN_BIT, 0);
	if (!ret)
		hap_dump_all(hap, "after init");
	return ret;
}

static int hap_start(struct pm8150b_hap *hap, u16 strength)
{
	u16 vmax_mv = (u16)((u32)hap->vmax_mv * strength / HAP_MAX_STRENGTH);
	int ret;

	if (vmax_mv < 3 * HAP_VMAX_MV_LSB)
		vmax_mv = 3 * HAP_VMAX_MV_LSB;

	ret = hap_set_vmax(hap, vmax_mv);
	if (ret)
		return ret;

	/* Drive the actuator from the VMAX setting (continuous waveform) */
	ret = hap_masked_write(hap, REG_HAP_SEL, GENMASK(5, 4),
			       HAP_WF_SOURCE_VMAX << 4);
	if (ret)
		return ret;

	ret = hap_module_enable(hap, true);
	if (ret)
		return ret;

	ret = hap_play(hap, true);
	if (!ret)
		hap_dump_state(hap, "playing");
	return ret;
}

static void hap_stop(struct pm8150b_hap *hap)
{
	hap_play(hap, false);
	hap_dump_state(hap, "stopped");
	/* Let the H-bridge brake before powering the block down */
	msleep(2);
	hap_module_enable(hap, false);
}

static void hap_work(struct work_struct *work)
{
	struct pm8150b_hap *hap = container_of(work, struct pm8150b_hap, work);
	u16 strength = READ_ONCE(hap->strength);

	mutex_lock(&hap->lock);

	if (strength && !hap->playing) {
		if (!hap_start(hap, strength))
			hap->playing = true;
	} else if (strength && hap->playing) {
		/* strength changed while running: retune the amplitude */
		u16 vmax = (u16)((u32)hap->vmax_mv * strength / HAP_MAX_STRENGTH);

		if (vmax >= 3 * HAP_VMAX_MV_LSB)
			hap_set_vmax(hap, vmax);
	} else if (!strength && hap->playing) {
		hap_stop(hap);
		hap->playing = false;
	}

	mutex_unlock(&hap->lock);
}

/*
 * Called from the input core, possibly with the event spinlock held, so it
 * must not sleep: hand the actual register access to a work item.
 */
static int hap_play_effect(struct input_dev *dev, void *data,
			   struct ff_effect *effect)
{
	struct pm8150b_hap *hap = data;
	u16 strong = effect->u.rumble.strong_magnitude;
	u16 weak = effect->u.rumble.weak_magnitude;

	WRITE_ONCE(hap->strength, max(strong, weak));
	schedule_work(&hap->work);
	return 0;
}

struct hap_regmap_lookup {
	struct device_node *np;
	struct regmap *map;
};

static int hap_find_child_regmap(struct device *dev, void *data)
{
	struct hap_regmap_lookup *l = data;

	if (dev->of_node != l->np)
		return 0;
	l->map = dev_get_regmap(dev, NULL);
	return l->map ? 1 : 0;
}

/*
 * The SPMI devices hang off the SPMI controller device ("spmi-0"), which is
 * itself a child of the SPMI controller's platform device, so walk two levels.
 */
static int hap_walk_spmi_controller(struct device *dev, void *data)
{
	struct hap_regmap_lookup *l = data;

	device_for_each_child(dev, l, hap_find_child_regmap);
	return l->map ? 1 : 0;
}

/*
 * Locate the regmap of the SPMI slave that hosts the haptics peripheral
 * (the PM8150B secondary die, SPMI USID 3).  spmi-pmic creates one regmap
 * per slave on top of the spmi_device; the SPMI devices are children of the
 * SPMI controller platform device.
 */
static struct regmap *hap_get_pmic_regmap(struct device *dev,
					  struct device_node *pmic_np)
{
	struct device_node *ctrl_np;
	struct platform_device *ctrl;
	struct hap_regmap_lookup lookup = { .np = pmic_np, .map = NULL };

	ctrl_np = of_find_node_by_path("/soc@0/spmi@c440000");
	if (!ctrl_np)
		return NULL;

	ctrl = of_find_device_by_node(ctrl_np);
	of_node_put(ctrl_np);
	if (!ctrl)
		return NULL;

	device_for_each_child(&ctrl->dev, &lookup, hap_walk_spmi_controller);
	put_device(&ctrl->dev);

	if (!lookup.map)
		dev_err(dev, "no regmap for %pOF\n", pmic_np);

	return lookup.map;
}

static int pm8150b_hap_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pm8150b_hap *hap;
	struct device_node *np = dev->of_node;
	struct device_node *pmic;
	u32 usid, reg_base, val32;
	int ret;

	hap = devm_kzalloc(dev, sizeof(*hap), GFP_KERNEL);
	if (!hap)
		return -ENOMEM;
	hap->dev = dev;
	mutex_init(&hap->lock);

	reg_base = 0xc000;	/* PM8150B secondary die, Haptics peripheral */
	usid = 3;
	of_property_read_u32(np, "reg-base", &reg_base);
	of_property_read_u32(np, "spmi-usid", &usid);
	hap->reg_base = (u16)reg_base;
	dev_info(dev, "haptics register base 0x%04x\n", hap->reg_base);

	INIT_WORK(&hap->work, hap_work);

	hap->vmax_mv = HAP_VMAX_MV_DEFAULT;
	if (!of_property_read_u32(np, "vmax-mv", &val32))
		hap->vmax_mv = (u16)min_t(u32, val32, HAP_VMAX_MV_MAX);

	/*
	 * The haptics block sits on the PM8150B secondary die (SPMI USID 3)
	 * at address 0xc000.  The spmi-pmic MFD registers the children of
	 * pmic@3 as platform devices whose parent owns the per-slave regmap,
	 * so borrow it from any of them.
	 */
	pmic = of_find_node_by_path("/soc@0/spmi@c440000/pmic@3");
	if (!pmic)
		pmic = of_find_node_by_path("/soc@0/spmi@c440000/pmic@2");
	if (!pmic) {
		dev_err(dev, "PM8150B node not found\n");
		return -ENODEV;
	}
	hap->regmap = hap_get_pmic_regmap(dev, pmic);
	of_node_put(pmic);
	if (!hap->regmap) {
		dev_err(dev, "PMIC regmap not found\n");
		return -ENODEV;
	}

	ret = hap_hw_init(hap);
	if (ret) {
		dev_err(dev, "hardware init failed: %d\n", ret);
		return ret;
	}

	hap->input = input_allocate_device();
	if (!hap->input)
		return -ENOMEM;

	hap->input->name = "pm8150b_haptics";
	hap->input->id.version = 1;

	input_set_capability(hap->input, EV_FF, FF_RUMBLE);

	ret = input_ff_create_memless(hap->input, hap, hap_play_effect);
	if (ret)
		goto err_free_input;

	ret = input_register_device(hap->input);
	if (ret)
		goto err_free_input;

	platform_set_drvdata(pdev, hap);
	dev_info(dev, "PM8150B haptics ready (vmax %u mV)\n", hap->vmax_mv);
	return 0;

err_free_input:
	input_free_device(hap->input);
	return ret;
}

static void pm8150b_hap_remove(struct platform_device *pdev)
{
	struct pm8150b_hap *hap = platform_get_drvdata(pdev);

	cancel_work_sync(&hap->work);
	mutex_lock(&hap->lock);
	if (hap->playing)
		hap_stop(hap);
	mutex_unlock(&hap->lock);
	input_unregister_device(hap->input);
}

static const struct of_device_id pm8150b_hap_of_match[] = {
	{ .compatible = "xiaomi,pm8150b-haptics" },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8150b_hap_of_match);

static struct platform_driver pm8150b_hap_driver = {
	.probe = pm8150b_hap_probe,
	.remove = pm8150b_hap_remove,
	.driver = {
		.name = "pm8150b-haptics",
		.of_match_table = pm8150b_hap_of_match,
	},
};
module_platform_driver(pm8150b_hap_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PM8150B haptics driver (Xiaomi Redmi K20 Pro)");
MODULE_AUTHOR("raphael-mainline bring-up");
