#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
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
__used __section(__versions) = {
	{ 0xb3753869, "module_layout" },
	{ 0x6bc3fbc0, "__unregister_chrdev" },
	{ 0xe6278ff5, "__register_chrdev" },
	{ 0x4302d0eb, "free_pages" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x6a5cb5ee, "__get_free_pages" },
	{ 0xdf9208c0, "alloc_workqueue" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x977f511b, "__mutex_init" },
	{ 0x37a0cba, "kfree" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x8ddd8aad, "schedule_timeout" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0xf21017d9, "mutex_trylock" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x3eeb2322, "__wake_up" },
	{ 0x409bcb62, "mutex_unlock" },
	{ 0xa1c76e0a, "_cond_resched" },
	{ 0x2db3d320, "mutex_lock_interruptible" },
	{ 0xf5cb25c8, "kmem_cache_alloc_trace" },
	{ 0x35216b26, "kmalloc_caches" },
	{ 0x56470118, "__warn_printk" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "9979E2618BA9CC11C489292");
