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
	{ 0x6a5cb5ee, "__get_free_pages" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x36e58bcd, "pv_ops" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x8c3253ec, "_raw_spin_trylock" },
	{ 0xdbf17652, "_raw_spin_lock" },
	{ 0x56470118, "__warn_printk" },
	{ 0xc5850110, "printk" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "82B776ED3C8663130A2C99E");
