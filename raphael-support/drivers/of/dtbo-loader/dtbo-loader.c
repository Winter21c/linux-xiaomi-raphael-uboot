// SPDX-License-Identifier: GPL-2.0
/*
 * dtbo-loader - apply a device tree overlay to the live tree at module load
 * time, and optionally instantiate platform devices for nodes the overlay
 * enabled (the kernel only creates devices from DT nodes during early boot).
 *
 * Used on the Xiaomi Redmi K20 Pro (raphael) to bring up peripherals that are
 * missing from the upstream device tree without touching the boot DTB.
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/string.h>

static char *overlay = "raphael/raphael-nfc.dtbo";
module_param(overlay, charp, 0444);
MODULE_PARM_DESC(overlay, "Firmware path of the DT overlay to apply");

static char *create_nodes;
module_param(create_nodes, charp, 0444);
MODULE_PARM_DESC(create_nodes,
		 "Comma separated DT paths to instantiate as platform devices");

static int ovcs_id = -1;

static void dtbo_create_platform_devices(void)
{
	char *buf, *path, *save = NULL;

	if (!create_nodes)
		return;

	buf = kstrdup(create_nodes, GFP_KERNEL);
	if (!buf)
		return;

	for (path = strsep(&buf, ","); path; path = strsep(&buf, ",")) {
		struct device_node *np;
		struct platform_device *pdev;

		path = strim(path);
		if (!*path)
			continue;

		np = of_find_node_by_path(path);
		if (!np) {
			pr_warn("dtbo-loader: node %s not found\n", path);
			continue;
		}
		pdev = of_platform_device_create(np, NULL, NULL);
		of_node_put(np);
		if (!pdev)
			pr_warn("dtbo-loader: no platform device for %s\n", path);
		else
			pr_info("dtbo-loader: created platform device for %s\n",
				pdev->name);
	}
	kfree(save);
	kfree(buf);
}

static int __init dtbo_loader_init(void)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, overlay, NULL);
	if (ret) {
		pr_err("dtbo-loader: cannot read %s (%d)\n", overlay, ret);
		return ret;
	}

	ret = of_overlay_fdt_apply(fw->data, fw->size, &ovcs_id, NULL);
	release_firmware(fw);
	if (ret < 0) {
		pr_err("dtbo-loader: overlay %s failed (%d)\n", overlay, ret);
		return ret;
	}
	if (ret > 0)
		ovcs_id = ret;

	pr_info("dtbo-loader: applied %s as overlay id %d\n", overlay, ovcs_id);
	dtbo_create_platform_devices();
	return 0;
}

static void __exit dtbo_loader_exit(void)
{
	if (ovcs_id >= 0) {
		int ret = of_overlay_remove(&ovcs_id);

		if (ret)
			pr_err("dtbo-loader: remove failed (%d)\n", ret);
		else
			pr_info("dtbo-loader: overlay %d removed\n", ovcs_id);
		ovcs_id = -1;
	}
}

module_init(dtbo_loader_init);
module_exit(dtbo_loader_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Apply a device tree overlay from firmware at load time");
