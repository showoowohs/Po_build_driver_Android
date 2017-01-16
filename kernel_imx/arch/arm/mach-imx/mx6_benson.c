
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/gpio.h>

#if defined(CONFIG_OF)
static const struct of_device_id mx6_benson_dt_ids[] = {
	{ .compatible = "fsl,mx6_benson", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mx6_benson_dt_ids);
#endif
static int mx6_benson_probe(struct platform_device *pdev)
{
	int rc, gpio;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	printk("[Benson] %s() start\n", __func__);


	gpio = of_get_named_gpio(np, "test-gpio", 0);
	if (gpio_is_valid(gpio)) {
		printk(KERN_INFO "[Benson] test gpio is:%d\n", gpio);
		rc = devm_gpio_request_one(&pdev->dev,
				gpio,
				GPIOF_OUT_INIT_HIGH,
				"test gpio");
		if (rc) {
			dev_err(&pdev->dev, "unable to get test-gpio\n");
			goto error_request_gpio;
		}

		mdelay(500);
		// gpio set hi or low
		//gpio_set_value(gpio, 1);
		
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
