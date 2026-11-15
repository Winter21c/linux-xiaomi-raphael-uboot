// SPDX-License-Identifier: GPL-2.0
/*
 * cam-mclk-hold - hold one CAMCC clock enabled from user space.
 *
 * Bring-up helper: a camera module only answers on I2C while its MCLK runs, and
 * nothing requests that clock until a sensor driver binds.  This module enables a
 * CAMCC clock by its DT clock id (the second cell of <&camcc ID>, e.g. 85 is the
 * MCLK the tele module uses).
 *
 *   insmod cam-mclk-hold.ko clk_id=86
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static int clk_id = -1;
module_param(clk_id, int, 0444);
MODULE_PARM_DESC(clk_id, "camcc clock id to hold enabled (e.g. 86)");

static struct clk *held;

static int __init cam_mclk_hold_init(void)
{
	struct of_phandle_args spec = { };
	struct device_node *np;
	long rate;

	if (clk_id < 0) {
		pr_info("cam-mclk-hold: pass clk_id=<n>\n");
		return -EINVAL;
	}

	np = of_find_compatible_node(NULL, NULL, "qcom,sm8150-camcc");
	if (!np) {
		pr_err("cam-mclk-hold: no camcc node\n");
		return -ENODEV;
	}

	spec.np = np;
	spec.args_count = 1;
	spec.args[0] = clk_id;

	held = of_clk_get_from_provider(&spec);
	of_node_put(np);
	if (IS_ERR(held)) {
		pr_err("cam-mclk-hold: id %d not found: %ld\n", clk_id,
		       PTR_ERR(held));
		return PTR_ERR(held);
	}

	if (clk_prepare_enable(held)) {
		pr_err("cam-mclk-hold: enable failed\n");
		clk_put(held);
		return -EIO;
	}

	rate = clk_get_rate(held);
	pr_info("cam-mclk-hold: holding camcc id %d at %ld Hz\n", clk_id, rate);
	return 0;
}

static void __exit cam_mclk_hold_exit(void)
{
	if (!IS_ERR_OR_NULL(held)) {
		clk_disable_unprepare(held);
		clk_put(held);
		pr_info("cam-mclk-hold: released\n");
	}
}

module_init(cam_mclk_hold_init);
module_exit(cam_mclk_hold_exit);
MODULE_DESCRIPTION("Hold one CAMCC clock enabled for bring-up");
MODULE_LICENSE("GPL");
