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

static struct _blk_tr *_tr_data;
static struct blk_tr *tr_data;

static int record_enabled = 0; 

void nvmetcp_monitor_trace_func(void *data, struct request *rq)
{
    if (record_enabled) {

        atomic64_t* arr = NULL;
        if (rq_data_dir(rq) == READ) {
            arr = _tr_data->read_io;
            atomic64_inc(&_tr_data->read_count);
        } else {
            arr = _tr_data->write_io;
            atomic64_inc(&_tr_data->write_count);
        }
        if(!arr){
            pr_info("io is neither read nor write.");
            return;
        }

        unsigned int size = blk_rq_bytes(rq);

        switch (size) {
        case 4096:
            atomic64_inc(&arr[_4K]);
            break;
        case 8192:
            atomic64_inc(&arr[_8K]);
            break;
        case 16384:
            atomic64_inc(&arr[_16K]);
            break;
        case 32768:
            atomic64_inc(&arr[_32K]);
            break;
        case 65536:
            atomic64_inc(&arr[_64K]);
            break;
        case 131072:
            atomic64_inc(&arr[_128K]);
            break;
        default:
            if(size < 4096){
                atomic64_inc(&arr[_LT_4K]);
            } else if(size > 131072){
                atomic64_inc(&arr[_GT_128K]);
            } else {
                atomic64_inc(&arr[_OTHERS]);
            }
        }

        copy_blk_tr(tr_data, _tr_data);

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

    _tr_data = vmalloc(sizeof(*_tr_data));
    if (!_tr_data)
        return -ENOMEM;
    _init_blk_tr(_tr_data);

    tr_data = vmalloc(sizeof(*tr_data));
    if (!tr_data)
        return -ENOMEM;

    init_blk_tr(tr_data);

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
    vfree(_tr_data);
    pr_info("nvmetcp_monitor module unloaded\n");
}

module_init(nvmetcp_monitor_init);
module_exit(nvmetcp_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yuyuan");
MODULE_DESCRIPTION("Lightweight NVMeTCP Monitoring Module");