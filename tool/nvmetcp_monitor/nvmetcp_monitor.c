#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/tracepoint.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <trace/events/block.h>
#include <linux/vmalloc.h>
#include "nvmetcp_monitor.h"

#define BUFFER_SIZE PAGE_SIZE

static struct nvmetcp_tr *tr_data;
static int record_enabled = 0; // 控制记录开关

void nvmetcp_monitor_trace_func(void *data, struct request *rq)
{
    if (record_enabled) {
        atomic64_inc(&tr_data->io_request_count);
        pr_info("I/O request captured. Count: %llu\n", atomic64_read(&tr_data->io_request_count));
    }
}

static ssize_t nvmetcp_monitor_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
    char buf[4];
    if (count > sizeof(buf) - 1)
        return -EINVAL;
    if (copy_from_user(buf, buffer, count))
        return -EFAULT;
    buf[count] = '\0';

    if (buf[0] == '1')
        record_enabled = 1;
    else if (buf[0] == '0')
        record_enabled = 0;
    else
        return -EINVAL;

    return count;
}

static int nvmetcp_monitor_mmap(struct file *filp, struct vm_area_struct *vma)
{
    if (remap_pfn_range(vma, vma->vm_start, vmalloc_to_pfn(tr_data),
                        vma->vm_end - vma->vm_start, vma->vm_page_prot))
        return -EAGAIN;
    return 0;
}

static const struct proc_ops nvmetcp_monitor_ops = {
    .proc_write = nvmetcp_monitor_write,
    .proc_mmap = nvmetcp_monitor_mmap,
};

static int __init nvmetcp_monitor_init(void)
{
    int ret;
    struct proc_dir_entry *entry;

    tr_data = vmalloc(sizeof(*tr_data));
    if (!tr_data)
        return -ENOMEM;

    atomic64_set(&tr_data->io_request_count, 0);

    ret = tracepoint_probe_register(&__tracepoint_block_rq_insert, nvmetcp_monitor_trace_func, NULL);
    if (ret) {
        pr_err("Failed to register tracepoint\n");
        vfree(tr_data);
        return ret;
    }

    entry = proc_create("nvmetcp_monitor", 0666, NULL, &nvmetcp_monitor_ops);
    if (!entry) {
        pr_err("Failed to create proc entry\n");
        tracepoint_probe_unregister(&__tracepoint_block_rq_insert, nvmetcp_monitor_trace_func, NULL);
        vfree(tr_data);
        return -ENOMEM;
    }

    pr_info("nvmetcp_monitor module loaded\n");
    return 0;
}

static void __exit nvmetcp_monitor_exit(void)
{
    remove_proc_entry("nvmetcp_monitor", NULL);
    tracepoint_probe_unregister(&__tracepoint_block_rq_insert, nvmetcp_monitor_trace_func, NULL);
    vfree(tr_data);
    pr_info("nvmetcp_monitor module unloaded\n");
}

module_init(nvmetcp_monitor_init);
module_exit(nvmetcp_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");
