/*
 * Reference http://nano-chicken.blogspot.tw/2010/01/linux-modules9-kthread.html
*
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>

MODULE_LICENSE("GPL");

static struct task_struct *brook_tsk;
static int data;
static int kbrook(void *arg);

static int kbrook(void *arg)
{
	unsigned int timeout;
	int *d = (int *) arg;
	printk(KERN_INFO "[Po add] %s\n", __func__);

	for(;;) {
		if (kthread_should_stop()) break;
		printk(KERN_INFO "[Po add] %s(): %d\n", __FUNCTION__, (*d)++);
		do {
			set_current_state(TASK_INTERRUPTIBLE);
			timeout = schedule_timeout(10 * HZ);
		} while(timeout);
	}
	printk(KERN_INFO "[Po add] break\n");

	return 0;
}

static int __init init_modules(void)
{
	int ret;
	printk(KERN_INFO "[Po add] %s\n", __func__);
	brook_tsk = kthread_create(kbrook, &data, "brook");
	if (IS_ERR(brook_tsk)) {
		ret = PTR_ERR(brook_tsk);
		brook_tsk = NULL;
		goto out;
	}
	wake_up_process(brook_tsk);

	return 0;

out:
	return ret;
}

static void __exit exit_modules(void)
{
	printk(KERN_INFO "[Po add] %s\n", __func__);
	kthread_stop(brook_tsk);
}

module_init(init_modules);
module_exit(exit_modules);
