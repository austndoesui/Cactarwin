// SPDX-License-Identifier: GPL-2.0
/* Snapshot the cactus shim's retained DRAM trace, without writing to it. */
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define TRACE_PHYS 0x47c90000ULL
#define TRACE_BYTES 0x5000U
#define TRACE_MAGIC 0x31424443U

static u8 *snapshot;
static struct proc_dir_entry *entry;

static ssize_t trace_read(struct file *file, char __user *buf,
                          size_t size, loff_t *pos)
{
    return simple_read_from_buffer(buf, size, pos, snapshot, TRACE_BYTES);
}

static const struct file_operations trace_fops = {
    .owner = THIS_MODULE,
    .read = trace_read,
    .llseek = default_llseek,
};

static int __init cactus_trace_init(void)
{
    void __iomem *mapped;
    u32 magic;

    snapshot = kmalloc(TRACE_BYTES, GFP_KERNEL);
    if (!snapshot)
        return -ENOMEM;

    /* ioremap is read-only in practice here: no writes are issued. */
    mapped = ioremap(TRACE_PHYS, TRACE_BYTES);
    if (!mapped) {
        kfree(snapshot);
        snapshot = NULL;
        return -ENXIO;
    }
    memcpy_fromio(snapshot, mapped, TRACE_BYTES);
    iounmap(mapped);
    magic = (u32)snapshot[0] | ((u32)snapshot[1] << 8) |
            ((u32)snapshot[2] << 16) | ((u32)snapshot[3] << 24);
    entry = proc_create("cactus_trace", 0400, NULL, &trace_fops);
    if (!entry) {
        kfree(snapshot);
        snapshot = NULL;
        return -ENOMEM;
    }
    pr_info("cactus_trace: snapshot %u bytes, magic=%08x (%s)\n",
            TRACE_BYTES, magic, magic == TRACE_MAGIC ? "shim" : "unknown");
    return 0;
}

static void __exit cactus_trace_exit(void)
{
    if (entry)
        proc_remove(entry);
    kfree(snapshot);
}

module_init(cactus_trace_init);
module_exit(cactus_trace_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Redmi 6A shim trace snapshot");
