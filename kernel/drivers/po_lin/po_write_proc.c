#include <linux/init.h>

#include <linux/module.h>

#include <linux/proc_fs.h>

#include <linux/uaccess.h>

MODULE_LICENSE("Dual BSD/GPL");



struct proc_dir_entry *Po_value=NULL;

int po_val=0;



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



	Po_value = create_proc_entry("Po_value", 0, NULL);

	if (Po_value) {

		printk(KERN_INFO "Create successful!!!\n");

		Po_value->read_proc = READ_PROC;

		Po_value->write_proc = WRITE_PROC;

	}               

	return 0;

}



static void __exit_po_write_proc(void)

{

	printk(KERN_INFO "[Po add] %s Goodbye, cruel world\n", __func__);

}



module_init(__init_po_write_proc);

module_exit(__exit_po_write_proc);
