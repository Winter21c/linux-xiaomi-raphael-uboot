// SPDX-License-Identifier: GPL-2.0
/*
 * cam-power-hold - enable camera regulators by name, without a sensor driver.
 *
 * The per-camera vdig/vio rails are ordinary regulator-fixed nodes, so they only
 * turn on when something requests them.  Before a sensor driver exists there is no
 * consumer, which means the module stays unpowered and never answers on I2C.
 * This helper takes regulators by their global names and holds them enabled:
 *
 *   insmod cam-power-hold.ko supplies=camera_viodd,camera_vdig_front
 *
 * It is a bring-up tool only; rmmod releases every rail.
 */
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MAX_SUPPLIES 8

static char *supplies = "camera_vdig_front";
module_param(supplies, charp, 0444);
MODULE_PARM_DESC(supplies, "comma separated regulator names to hold enabled");

static struct regulator *held[MAX_SUPPLIES];
static int held_count;

static int __init cam_power_hold_init(void)
{
	char *list, *name, *pos;
	int ret;

	list = kstrdup(supplies, GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	pos = list;
	while ((name = strsep(&pos, ",")) && held_count < MAX_SUPPLIES) {
		if (!*name)
			continue;

		held[held_count] = regulator_get(NULL, name);
		if (IS_ERR(held[held_count])) {
			pr_err("cam-power-hold: %s not found: %ld\n", name,
			       PTR_ERR(held[held_count]));
			held[held_count] = NULL;
			kfree(list);
			return -ENODEV;
		}

		ret = regulator_enable(held[held_count]);
		if (ret) {
			pr_err("cam-power-hold: enable %s failed: %d\n", name, ret);
			regulator_put(held[held_count]);
			held[held_count] = NULL;
			kfree(list);
			return ret;
		}

		pr_info("cam-power-hold: holding %s at %d uV\n", name,
			regulator_get_voltage(held[held_count]));
		held_count++;
	}

	kfree(list);
	return 0;
}

static void __exit cam_power_hold_exit(void)
{
	int i;

	for (i = 0; i < held_count; i++) {
		if (!IS_ERR_OR_NULL(held[i])) {
			regulator_disable(held[i]);
			regulator_put(held[i]);
		}
	}
	pr_info("cam-power-hold: released %d rail(s)\n", held_count);
}

module_init(cam_power_hold_init);
module_exit(cam_power_hold_exit);
MODULE_DESCRIPTION("Hold camera regulators enabled for bring-up");
MODULE_LICENSE("GPL");
