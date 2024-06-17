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
	{ 0x953dead6, "remove_proc_entry" },
	{ 0x8a33fe02, "tracepoint_probe_register" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x999e8297, "vfree" },
	{ 0xef2141dc, "proc_create" },
	{ 0xa47006d6, "proc_mkdir" },
	{ 0xf177ba0e, "__tracepoint_nvmet_tcp_try_recv_data" },
	{ 0xfcad8b2b, "__tracepoint_nvmet_tcp_handle_h2c_data_pdu" },
	{ 0x75f14906, "__tracepoint_nvmet_tcp_try_send_data" },
	{ 0x74189ffa, "__tracepoint_nvmet_tcp_try_send_response" },
	{ 0x8ed30368, "__tracepoint_nvmet_tcp_try_send_r2t" },
	{ 0x60e97ac5, "__tracepoint_nvmet_tcp_try_send_data_pdu" },
	{ 0x3f1f73f8, "__tracepoint_nvmet_tcp_setup_response_pdu" },
	{ 0x795aa4e2, "__tracepoint_nvmet_tcp_setup_r2t_pdu" },
	{ 0x28af541f, "__tracepoint_nvmet_tcp_setup_c2h_data_pdu" },
	{ 0xb725d8e9, "__tracepoint_nvmet_tcp_queue_response" },
	{ 0xf753aae1, "__tracepoint_nvmet_tcp_exec_write_req" },
	{ 0xa0252915, "__tracepoint_nvmet_tcp_exec_read_req" },
	{ 0x7b5fd3ad, "__tracepoint_nvmet_tcp_done_recv_pdu" },
	{ 0x23928884, "tracepoint_probe_unregister" },
	{ 0x6d427eb3, "__tracepoint_nvmet_tcp_try_recv_pdu" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xb3f7646e, "kthread_should_stop" },
	{ 0xf9a482f9, "msleep" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x37a0cba, "kfree" },
	{ 0xe51688c1, "pv_ops" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x5f8ff9eb, "kmem_cache_alloc_trace" },
	{ 0xc1f15dee, "kmalloc_caches" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x41ed3709, "get_random_bytes" },
	{ 0x56470118, "__warn_printk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x92997ed8, "_printk" },
	{ 0x23130f8f, "wake_up_process" },
	{ 0x4d38f1af, "kthread_create_on_node" },
	{ 0x31157ed9, "kthread_stop" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x7261cd16, "remap_pfn_range" },
	{ 0x3744cf36, "vmalloc_to_pfn" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "nvmet-tcp");


MODULE_INFO(srcversion, "91A2BA6ECE53180E9E93E9F");
