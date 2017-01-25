
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#define PROC_GPIO

#if defined(CONFIG_OF)
static const struct of_device_id mx6_benson_dt_ids[] = {
	{ .compatible = "fsl,mx6_benson", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx6_benson_dt_ids);
#endif

#ifdef PROC_GPIO
static int GPIO_HELP_FLAGE = 0;
static char GPIO_HELP[] =
	"1. echo help > /proc/gpio\n"
	"2. echo SIM1 (IMEI code+) > /proc/MTKIMEI\n"
	"\t(can flash imei1, example: echo SIM1 555555555666666+ >  /proc/MTKIMEI\n"
	"3. echo SIM2 (IMEI code+) > /proc/MTKIMEI\n"
	"\t(can flash imei2, example: echo SIM2 777777777777777+ >  /proc/MTKIMEI\n";

static ssize_t write_procfs(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	char gpio_data[50];
	//printk("[Benson] %s() \n", __func__);

	if (count == 0)
		return -EINVAL;
	if (copy_from_user(gpio_data, buf, count))
		return -EFAULT;
	if (gpio_data[0] == 'h' || gpio_data[0] == 'H') { // help
		GPIO_HELP_FLAGE = 1;
		printk("[Benson] %s Help\n", __func__);

	}else if (strncmp(gpio_data, "SIM1", 4) == 0){
		printk("[Benson] %s SIM1\n", __func__);
	}else{
		int gpio, level, mode=-1, dir=-1;
#if 0
		if (sscanf (gpio_data, "%d %d %d %d", &gpio, &level, &mode, &dir) != 4 &&
				sscanf (gpio_data, "%d %d %d", &gpio, &level, &mode) !=3 &&
				sscanf (gpio_data, "%d %d", &gpio, &level) != 2){
			printk("usage: echo gpio level [mode [dir]] > /proc/gpio\n");
#endif
		if (sscanf (gpio_data, "%d %d", &gpio, &level) != 2){ 
			printk("[Benson] format error\n");
			return -EINVAL;
		}else{
			printk("[Benson] gpio=%d level=%d\n", gpio, level);
			gpio_free(gpio);
			if (gpio_is_valid(gpio)) {
				int rc;
				char *str = "gpio";
				char *tmp = "gpio";
				sprintf(tmp, "%d", gpio);
				strcpy(str, tmp);
				rc = gpio_request_one(gpio, GPIOF_OUT_INIT_HIGH, str);
				if (rc) {
					printk("[Benson] register ng\n");
					return -EINVAL;
				}
				mdelay(500);
				// gpio set hi or low
				gpio_set_value(gpio, level);

			}else{
				printk("[Benson] ng\n");
			}
		}
	}

	//printk("[Benson] %s() count=%d\n", __func__, count);
	//printk("[Benson] %s() buf=%s\n", __func__, buf);
	return count;
}

static ssize_t read_procfs(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	printk("[Benson] %s() \n", __func__);
	if(GPIO_HELP_FLAGE == 1){
		printk("%s\n", GPIO_HELP);
	}
	return 0; 
}

static const struct file_operations gpio_proc_fops = {
	.read		= read_procfs,
	.write		= write_procfs,
};
#endif

static int mx6_benson_probe(struct platform_device *pdev)
{
	int rc, gpio;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	printk("[Benson] %s() start\n", __func__);
#ifdef PROC_GPIO
	// for /proc/gpio
	proc_create("gpio", 0, NULL, &gpio_proc_fops);
#endif


	gpio = of_get_named_gpio(np, "test-gpio", 0);
	if (gpio_is_valid(gpio)) {
		printk(KERN_INFO "[Benson] test gpio is:%d\n", gpio);
		rc = devm_gpio_request_one(&pdev->dev,
				gpio,
				//GPIOF_OUT_INIT_HIGH,
				GPIOF_OUT_INIT_LOW,
				"test gpio");
		if (rc) {
			dev_err(&pdev->dev, "unable to get test-gpio\n");
			goto error_request_gpio;
		}

		mdelay(500);
		// gpio set hi or low
		gpio_set_value(gpio, 1);
		
		// setting gpio input 
		//int fault;
		//fault = gpio_get_value(gpio);
		//printk("[Benson] %s() fault=%s\n", __func__, fault);
	}

	printk("[Benson] %s() End\n", __func__);
	return 0;

error_request_gpio:
error_check_func:
	return rc;
}


static int  mx6_benson_remove(struct platform_device *pdev)
{
	printk("[Benson] %s()\n", __func__);
	return 0;
}

static struct platform_driver mx6_benson_driver = {
	.probe	= mx6_benson_probe,
	.remove	= mx6_benson_remove,
	.driver = {
		.name	= "mx6_benson",
		.owner	= THIS_MODULE,
		.of_match_table = mx6_benson_dt_ids,
	},
};

module_platform_driver(mx6_benson_driver)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benson");
MODULE_DESCRIPTION("Benson GPIO test");
