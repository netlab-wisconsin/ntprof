#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xdc658e53, "module_layout" },
	{ 0x8a33fe02, "tracepoint_probe_register" },
	{ 0x23928884, "tracepoint_probe_unregister" },
	{ 0x849fce57, "__tracepoint_block_bio_queue" },
	{ 0xef2141dc, "proc_create" },
	{ 0xa47006d6, "proc_mkdir" },
	{ 0x999e8297, "vfree" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xf9a482f9, "msleep" },
	{ 0x37a0cba, "kfree" },
	{ 0xb3f7646e, "kthread_should_stop" },
	{ 0x282174ed, "blkdev_put" },
	{ 0x2ae0273b, "blkdev_get_by_path" },
	{ 0x56470118, "__warn_printk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xe51688c1, "pv_ops" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x5f8ff9eb, "kmem_cache_alloc_trace" },
	{ 0xc1f15dee, "kmalloc_caches" },
	{ 0x41ed3709, "get_random_bytes" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x23130f8f, "wake_up_process" },
	{ 0x4d38f1af, "kthread_create_on_node" },
	{ 0x31157ed9, "kthread_stop" },
	{ 0x92997ed8, "_printk" },
	{ 0x9166fada, "strncpy" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x7261cd16, "remap_pfn_range" },
	{ 0x3744cf36, "vmalloc_to_pfn" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x953dead6, "remove_proc_entry" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "82DDB4F596D7B5DB315FE04");
