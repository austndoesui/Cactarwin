// SPDX-License-Identifier: GPL-2.0
/* Built-in early snapshot of the cactus shim trace before ramoops probes. */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define CACTUS_TRACE_PHYS 0x47c90000ULL
#define CACTUS_TRACE_LEN 0x5000U
#define CACTUS_TRACE_MAGIC 0x31424443U

static void *trace_copy;
static bool trace_valid;

static int __init cactus_snapshot_early(void)
{
	void *mapped;
	u32 magic;

	trace_copy = kmalloc(CACTUS_TRACE_LEN, GFP_KERNEL);
	if (!trace_copy)
		return 0; /* Never prevent recovery from booting. */

	/* Copy only: this must not erase or initialize the ramoops region. */
	mapped = memremap(CACTUS_TRACE_PHYS, CACTUS_TRACE_LEN, MEMREMAP_WB);
	if (!mapped) {
		kfree(trace_copy);
		trace_copy = NULL;
		pr_warn("cactus_trace: early mapping failed\n");
		return 0;
	}
	memcpy(trace_copy, mapped, CACTUS_TRACE_LEN);
	memunmap(mapped);
	magic = ((u8 *)trace_copy)[0] | ((u32)((u8 *)trace_copy)[1] << 8) |
		((u32)((u8 *)trace_copy)[2] << 16) |
		((u32)((u8 *)trace_copy)[3] << 24);
	trace_valid = (magic == CACTUS_TRACE_MAGIC);
	pr_info("cactus_trace: early copy completed; magic=%08x\n", magic);
	return 0;
}
early_initcall(cactus_snapshot_early);

static ssize_t cactus_snapshot_read(struct file *f, char __user *dst,
				    size_t size, loff_t *off)
{
	if (!trace_copy)
		return -ENODATA;
	return simple_read_from_buffer(dst, size, off, trace_copy,
				       CACTUS_TRACE_LEN);
}

static const struct file_operations cactus_snapshot_fops = {
	.owner = THIS_MODULE,
	.read = cactus_snapshot_read,
	.llseek = default_llseek,
};

static int __init cactus_snapshot_publish(void)
{
	if (!trace_copy)
		return 0;
	if (!proc_create("cactus_trace", 0400, NULL, &cactus_snapshot_fops))
		pr_warn("cactus_trace: proc entry unavailable\n");
	else if (!trace_valid)
		pr_info("cactus_trace: no valid CDB1 trace (raw snapshot available)\n");
	return 0;
}
late_initcall(cactus_snapshot_publish);
