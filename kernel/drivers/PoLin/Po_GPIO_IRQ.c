#include <linux/init.h>

#include <linux/module.h>

#include <linux/proc_fs.h>

#include <linux/uaccess.h>

#include <linux/kthread.h>

#include <linux/interrupt.h>

#include <linux/gpio.h>

#include <linux/timer.h>

MODULE_LICENSE("Dual BSD/GPL");

//Po add
#define GTP_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define Po_AC_INT_PORT    16
#define Po_AC_INT_IRQ     MSM_GPIO_TO_INT(Po_AC_INT_PORT) //gpio_to_irq(Po_AC_INT_PORT)

static struct task_struct *brook_tsk;
static int data;
static int kbrook(void *arg);

struct proc_dir_entry *Po_value=NULL;

int po_val=0;

static int Po_AC_status =0;
unsigned long Po_timeout = 0;

static irqreturn_t Po_AC_st(int irq, void *dev)
{
	Po_AC_status = !gpio_get_value(16);
	printk ("[Po add] %s gpio_get_value(16)=%d \n", __func__, Po_AC_status);
	return IRQ_HANDLED;
}

static int kbrook(void *arg)
{
	//s32 ret2 = -1;
	unsigned int timeout;
	int *d = (int *) arg;
	printk(KERN_INFO "[Po add] %s\n", __func__);

	for(;;) {
		if (kthread_should_stop()) break;
		//printk(KERN_INFO "[Po add] %s(): %d\n", __FUNCTION__, (*d)++);
		(*d)++;
		//ret2 = GTP_GPIO_REQUEST(Po_AC_INT_PORT, "Po_AC_INT_IRQ");
		//ret2 = request_irq(Po_AC_INT_IRQ, Po_AC_st, IRQF_TRIGGER_MASK, "PoHsun_IRQ", NULL);
		printk(KERN_INFO "[Po add] %s Po_AC_status=%d\n", __func__, Po_AC_status);

		do {
			set_current_state(TASK_INTERRUPTIBLE);
			timeout = schedule_timeout(1 * HZ);
		} while(timeout);
	}
	printk(KERN_INFO "[Po add] break\n");

	return 0;
}

static int READ_PROC(char *page, char **start, off_t off, int count, int *eof, void *data)

{

	int len;

	len = sprintf(page, "%d\n", po_val);

	if (len <= off+count) *eof = 1;

	*start = page + off;

	len -= off;

	if (len>count) len = count;

	if (len<0) len = 0;

	return len;



}



static int WRITE_PROC( struct file *filp, const char *buff,unsigned long len1, void *data )

{

	char tmpbuf[64];



	if (buff && !copy_from_user(tmpbuf, buff, len1)) {

		tmpbuf[len1-1] = '\0';

		if ( tmpbuf[0] == '1' ) {

			printk(KERN_INFO "Po_value is HIGH!\n");

			po_val = 1;

		}else {

			printk(KERN_INFO "Po_value is LOW!\n");

			po_val = 0;       

		}

	}        



	return len1;

}







static int __init_po_write_proc(void)

{


        int ret;
        int ret2;


	Po_value = create_proc_entry("Po_value", 0, NULL);

	if (Po_value) {

		printk(KERN_INFO "Create successful!!!\n");

		Po_value->read_proc = READ_PROC;

		Po_value->write_proc = WRITE_PROC;

	}               

	printk(KERN_INFO "[Po add] %s\n", __func__);
	
	// init Po_AC_status
	Po_AC_status = !gpio_get_value(16);
	
	brook_tsk = kthread_create(kbrook, &data, "brook");
	if (IS_ERR(brook_tsk)) {
		ret = PTR_ERR(brook_tsk);
		brook_tsk = NULL;
		goto out;
	}
	wake_up_process(brook_tsk);

	
	ret2 = GTP_GPIO_REQUEST(Po_AC_INT_PORT, "Po_AC_INT_IRQ");
	ret2 = request_irq(Po_AC_INT_IRQ, Po_AC_st, IRQF_TRIGGER_MASK, "PoHsun_IRQ", NULL);

	return 0;
out:
	return ret;

}



static void __exit_po_write_proc(void)

{

	printk(KERN_INFO "[Po add] %s Goodbye, cruel world\n", __func__);

}



module_init(__init_po_write_proc);

module_exit(__exit_po_write_proc);
