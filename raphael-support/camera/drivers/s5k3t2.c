// SPDX-License-Identifier: GPL-2.0
/*
 * Bring-up driver for the Samsung S5K3T2 (front) and S5K3L6 (ultrawide) cameras
 * of the Redmi K20 Pro (raphael, SM8150).
 *
 * The sensor is the only camera on this board for which no register table is
 * publicly available (see camera/drivers/README.md), so this driver deliberately
 * does NOT claim to stream.  What it does is the part that can be verified:
 *
 *   - run the module's power sequence (viodd 1.8 V, BOB 3.008-4.0 V, S8 1.352 V
 *     through the CAMF_VDIG_EN pad gpio95, plus the PM8150 LDO17 3.1 V rail),
 *   - drive MCLK2 (cam_cc_mclk2_clk) and the reset pad (gpio12),
 *   - read the Samsung chip-id register (16-bit reg 0x0000, the same convention
 *     S5KJN1/S5K3M5 use) and report it,
 *   - register a V4L2 subdevice so the sensor can be linked into the media graph
 *     and identified from userspace.
 *
 * `expect_id` (module parameter) turns the identification into a hard check once
 * the real value has been measured; `keep_power` leaves the rails and MCLK up
 * after probe so the CCI bus can be scanned.
 *
 * Pixel modes will be added once a register table exists - see
 * analysis/camera-s5k3t2-plan.md.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define S5K3T2_REG_CHIP_ID	0x0000
#define S5K3T2_CHIP_ID		0x0000	/* to be measured on hardware */

static int expect_id = -1;
module_param(expect_id, int, 0644);
MODULE_PARM_DESC(expect_id, "Require this chip id at register 0x0000 (default: log only)");

static bool keep_power;
module_param(keep_power, bool, 0644);
MODULE_PARM_DESC(keep_power, "Leave the rails and MCLK enabled after probe (for CCI scans)");

static const char * const s5k3t2_supply_names[] = {
	"dovdd",	/* 1.8 V  I/O  (camera_viodd_ldo) */
	"avdd",		/* 3.008-4.0 V (vreg_bob)         */
	"dvdd",		/* 1.352 V through CAMF_VDIG_EN   */
};

static const char * const s5k3t2_extra_supply_names[] = {
	"vl17",		/* PM8150 LDO17 3.1 V, module private rail */
};

struct s5k3t2 {
	struct regmap *regmap;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(s5k3t2_supply_names)];
	struct regulator *extra_rails[ARRAY_SIZE(s5k3t2_extra_supply_names)];
	struct v4l2_subdev sd;
	struct media_pad pad;
	const char *model;
	bool powered;
	u32 chip_id;
};

static inline struct s5k3t2 *to_s5k3t2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k3t2, sd);
}

static int s5k3t2_power_on(struct s5k3t2 *priv)
{
	int ret, i;

	if (priv->powered)
		return 0;

	ret = clk_prepare_enable(priv->xvclk);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(priv->extra_rails); i++) {
		if (IS_ERR_OR_NULL(priv->extra_rails[i]))
			continue;
		ret = regulator_enable(priv->extra_rails[i]);
		if (ret)
			dev_warn(priv->sd.dev, "extra rail %d: %d\n", i, ret);
	}

	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);	/* assert */

	ret = regulator_bulk_enable(ARRAY_SIZE(s5k3t2_supply_names),
				    priv->supplies);
	if (ret) {
		dev_err(priv->sd.dev, "failed to enable regulators: %d\n", ret);
		goto err_clk;
	}

	usleep_range(1000, 2000);
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 0);	/* release */
	usleep_range(5000, 6000);	/* t2: sensor ready after reset */

	priv->powered = true;
	return 0;

err_clk:
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	for (i = 0; i < ARRAY_SIZE(priv->extra_rails); i++)
		if (!IS_ERR_OR_NULL(priv->extra_rails[i]))
			regulator_disable(priv->extra_rails[i]);
	clk_disable_unprepare(priv->xvclk);
	return ret;
}

static void s5k3t2_power_off(struct s5k3t2 *priv)
{
	int i;

	if (!priv->powered)
		return;
	if (priv->reset_gpio)
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(s5k3t2_supply_names), priv->supplies);
	for (i = 0; i < ARRAY_SIZE(priv->extra_rails); i++)
		if (!IS_ERR_OR_NULL(priv->extra_rails[i]))
			regulator_disable(priv->extra_rails[i]);
	clk_disable_unprepare(priv->xvclk);
	priv->powered = false;
}

static int s5k3t2_identify(struct s5k3t2 *priv)
{
	u32 id = 0;
	int ret;

	ret = regmap_read(priv->regmap, S5K3T2_REG_CHIP_ID, &id);
	if (ret) {
		dev_err(priv->sd.dev, "chip id read failed: %d\n", ret);
		return ret;
	}
	priv->chip_id = id;

	dev_info(priv->sd.dev, "chip id at 0x0000 = 0x%04x\n", id);

	if (expect_id >= 0 && id != (u32)expect_id) {
		dev_err(priv->sd.dev, "expected 0x%04x, got 0x%04x\n",
			expect_id, id);
		return -ENXIO;
	}
	return 0;
}

static int s5k3t2_set_stream(struct v4l2_subdev *sd, int enable)
{
	/*
	 * No pixel mode yet: refuse to pretend.  A stream attempt returns -EINVAL
	 * so userspace sees an honest failure instead of a silent black frame.
	 */
	return enable ? -EINVAL : 0;
}

static const struct v4l2_subdev_video_ops s5k3t2_video_ops = {
	.s_stream = s5k3t2_set_stream,
};

static const struct v4l2_subdev_ops s5k3t2_subdev_ops = {
	.video = &s5k3t2_video_ops,
};

static int s5k3t2_probe(struct i2c_client *client)
{
	struct s5k3t2 *priv;
	int ret, i;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->model = (const char *)device_get_match_data(&client->dev);
	if (!priv->model)
		priv->model = "Samsung S5K";

	priv->regmap = devm_regmap_init_i2c(client, &(struct regmap_config){
		.reg_bits = 16,
		.val_bits = 16,
	});
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	for (i = 0; i < ARRAY_SIZE(s5k3t2_supply_names); i++)
		priv->supplies[i].supply = s5k3t2_supply_names[i];
	ret = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(s5k3t2_supply_names),
				      priv->supplies);
	if (ret)
		return dev_err_probe(&client->dev, ret, "regulators\n");

	for (i = 0; i < ARRAY_SIZE(priv->extra_rails); i++)
		priv->extra_rails[i] =
			devm_regulator_get_optional(&client->dev,
						    s5k3t2_extra_supply_names[i]);

	priv->xvclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(priv->xvclk))
		return dev_err_probe(&client->dev, PTR_ERR(priv->xvclk), "xvclk\n");

	priv->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	v4l2_i2c_subdev_init(&priv->sd, client, &s5k3t2_subdev_ops);
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret)
		return ret;

	ret = s5k3t2_power_on(priv);
	if (ret)
		goto err_entity;

	ret = s5k3t2_identify(priv);
	if (ret)
		goto err_power;

	if (!keep_power)
		s5k3t2_power_off(priv);

	ret = v4l2_async_register_subdev_sensor(&priv->sd);
	if (ret) {
		dev_err(&client->dev, "async register failed: %d\n", ret);
		goto err_power;
	}

	dev_info(&client->dev, "%s bring-up driver bound (id 0x%04x, keep_power=%d)\n",
		 priv->model, priv->chip_id, keep_power);
	return 0;

err_power:
	s5k3t2_power_off(priv);
err_entity:
	media_entity_cleanup(&priv->sd.entity);
	return ret;
}

static void s5k3t2_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k3t2 *priv = to_s5k3t2(sd);

	v4l2_async_unregister_subdev(sd);
	if (!keep_power)
		s5k3t2_power_off(priv);
	media_entity_cleanup(&sd->entity);
}

static const struct of_device_id s5k3t2_dt_ids[] = {
	{ .compatible = "samsung,s5k3t2", .data = "S5K3T2 (front)" },
	{ .compatible = "samsung,s5k3l6", .data = "S5K3L6 (ultrawide)" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5k3t2_dt_ids);

static struct i2c_driver s5k3t2_i2c_driver = {
	.driver = {
		.name = "s5k3t2",
		.of_match_table = s5k3t2_dt_ids,
	},
	.probe = s5k3t2_probe,
	.remove = s5k3t2_remove,
};

module_i2c_driver(s5k3t2_i2c_driver);

MODULE_DESCRIPTION("Bring-up driver for the Samsung S5K3T2/S5K3L6 cameras (id probe, no streaming)");
MODULE_LICENSE("GPL");
