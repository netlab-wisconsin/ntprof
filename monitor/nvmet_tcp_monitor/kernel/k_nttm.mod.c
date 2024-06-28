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
	{ 0x602c1205, "module_layout" },
	{ 0x4b30cf95, "__tracepoint_pkt_tcp_send_syn_data" },
	{ 0xb9eb3acd, "__tracepoint_pkt_tcp_adjust_pcount" },
	{ 0xe7e27837, "__tracepoint_pkt_tcp_clean_rtx_queue" },
	{ 0xb7e0b5c5, "__tracepoint_pkt_tcp_connect_queue_skb" },
	{ 0xdaa9bbb2, "__tracepoint_pkt_tcp_event_new_data_sent" },
	{ 0x6c78cbb2, "__tracepoint_cwnd_tcp_mtu_probe" },
	{ 0xc62c154, "__tracepoint_cwnd_tcp_cwnd_application_limited" },
	{ 0xb72582e7, "__tracepoint_cwnd_tcp_cwnd_restart" },
	{ 0xeb24d9e8, "__tracepoint_cwnd_tcp_init_transfer" },
	{ 0xbaba360d, "__tracepoint_cwnd_tcp_mtup_probe_failed" },
	{ 0x9b998189, "__tracepoint_cwnd_tcp_mtup_probe_success" },
	{ 0x8d277eee, "__tracepoint_cwnd_tcp_end_cwnd_reduction" },
	{ 0x5874668e, "__tracepoint_cwnd_tcp_cwnd_reduction" },
	{ 0x9fad9ed2, "__tracepoint_cwnd_tcp_undo_cwnd_reduction" },
	{ 0x550cba25, "__tracepoint_cwnd_tcp_enter_loss" },
	{ 0x79f24c6d, "__tracepoint_cwnd_tcp_cong_avoid_ai" },
	{ 0x277944fb, "__tracepoint_cwnd_tcp_slow_start" },
	{ 0xf9a482f9, "msleep" },
	{ 0xb3f7646e, "kthread_should_stop" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xa916b694, "strnlen" },
	{ 0xe1cdb19e, "__tracepoint_block_rq_complete" },
	{ 0xb9df43ce, "__tracepoint_block_bio_complete" },
	{ 0xeb29403a, "__tracepoint_block_bio_queue" },
	{ 0x363f205f, "remove_proc_entry" },
	{ 0x1800aed6, "tracepoint_probe_register" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x999e8297, "vfree" },
	{ 0x4334b731, "proc_create" },
	{ 0x9e261506, "proc_mkdir" },
	{ 0xef318b16, "__tracepoint_nvmet_tcp_try_recv_data" },
	{ 0x27ae1370, "__tracepoint_nvmet_tcp_handle_h2c_data_pdu" },
	{ 0x6bb7781e, "__tracepoint_nvmet_tcp_try_send_data" },
	{ 0x9830bacd, "__tracepoint_nvmet_tcp_try_send_response" },
	{ 0xc91073fa, "__tracepoint_nvmet_tcp_try_send_r2t" },
	{ 0x8cc15ff2, "__tracepoint_nvmet_tcp_try_send_data_pdu" },
	{ 0x874efed2, "__tracepoint_nvmet_tcp_setup_response_pdu" },
	{ 0x671c95fa, "__tracepoint_nvmet_tcp_setup_r2t_pdu" },
	{ 0x90fed935, "__tracepoint_nvmet_tcp_setup_c2h_data_pdu" },
	{ 0xa457068e, "__tracepoint_nvmet_tcp_queue_response" },
	{ 0xe4217486, "__tracepoint_nvmet_tcp_exec_write_req" },
	{ 0xbe63180d, "__tracepoint_nvmet_tcp_exec_read_req" },
	{ 0x6519e2b5, "__tracepoint_nvmet_tcp_done_recv_pdu" },
	{ 0x10c3d561, "tracepoint_probe_unregister" },
	{ 0x2a810e21, "__tracepoint_nvmet_tcp_try_recv_pdu" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x37a0cba, "kfree" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x5bb7b46f, "kmem_cache_alloc_trace" },
	{ 0x41964dfe, "kmalloc_caches" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x56470118, "__warn_printk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0x5a921311, "strncmp" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x92997ed8, "_printk" },
	{ 0x481ead8c, "wake_up_process" },
	{ 0x683eadc2, "kthread_create_on_node" },
	{ 0x2aee72c, "kthread_stop" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xff1a2262, "remap_pfn_range" },
	{ 0x3744cf36, "vmalloc_to_pfn" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x84f243a6, "pv_ops" },
};

MODULE_INFO(depends, "nvmet-tcp");


MODULE_INFO(srcversion, "6D394CE10E0724102BCA5FE");
