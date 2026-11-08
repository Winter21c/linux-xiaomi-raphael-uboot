// cpas_peek: raw register peek at SM8150 camera blocks (no DT lookup)
#include <linux/module.h>
#include <linux/io.h>

struct blk { const char *name; unsigned long addr; unsigned len; unsigned dump; };

static struct blk blocks[] = {
	{ "vfe0",    0x0acaf000, 0x4000, 0x20 },
	{ "csid0",   0x0acb3000, 0x1000, 0x20 },
	{ "csiphy2", 0x0ac67000, 0x1000, 0x20 },
};

static int __init cpas_peek_init(void)
{
	int b, i;

	for (b = 0; b < ARRAY_SIZE(blocks); b++) {
		void __iomem *m = ioremap(blocks[b].addr, blocks[b].len);
		unsigned nz = 0;

		if (!m) {
			pr_err("cpas_peek: ioremap 0x%lx failed\n", blocks[b].addr);
			continue;
		}
		pr_info("cpas_peek: === %s @0x%lx\n", blocks[b].name, blocks[b].addr);
		for (i = 0; i < blocks[b].dump; i += 4) {
			u32 v = readl_relaxed(m + i);
			if (v) {
				pr_info("cpas_peek: %s[0x%02x] = 0x%08x\n",
					blocks[b].name, i, v);
				nz++;
			}
		}
		pr_info("cpas_peek: %s: %u non-zero of %u words\n",
			blocks[b].name, nz, blocks[b].dump / 4);
		iounmap(m);
	}
	return -ENODEV; /* probe only, do not stay loaded */
}
module_init(cpas_peek_init);
MODULE_LICENSE("GPL");
