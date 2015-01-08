#include <linux/kernel.h> /* pr_info所需 include 檔案*/
#include <linux/init.h>
#include <linux/module.h> /* 所有 module 需要檔案*/
#include <linux/version.h>
 
MODULE_DESCRIPTION("Hello World !!");
MODULE_AUTHOR("Bo-Yi Wu <appleboy.tw AT gmail.com>");
MODULE_LICENSE("GPL");
 
static int __init po_lin_init(void)
{
	    printk(KERN_INFO "[Po add] Hello, world appleboy\n");
		    return 0;
}
 
static void __exit po_lin_exit(void)
{
	    printk(KERN_INFO "[Po add] Goodbye\n");
}
 
module_init(po_lin_init);
module_exit(po_lin_exit);
