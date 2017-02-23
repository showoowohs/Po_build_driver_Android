
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/input-polldev.h>
#include <linux/input.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>


#define BENSON_DRV_NAME       "bensondts"
static const struct i2c_device_id bensondts_id[] = {
	{ BENSON_DRV_NAME, 0 },
	{}
};

static irqreturn_t benson_irq_handler(int irq, void *dev)
{

	return IRQ_HANDLED;
}

static int bensondts_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter;
	struct input_dev *idev;
	struct regulator *vdd, *vdd_io;
	u32 irq_flag;
	struct irq_data *irq_data;
	struct device_node *of_node = client->dev.of_node;
	bool shared_irq;
	int ret = 0;
	u32 pos;
	printk("[Benson irq] %s\n", __func__);
	vdd = devm_regulator_get(&client->dev, "vdd");
	if (!IS_ERR(vdd)) {
		ret = regulator_enable(vdd);
		if (ret) {
			dev_err(&client->dev, "vdd set voltage error\n");
			return ret;
		}
	}

	vdd_io = devm_regulator_get(&client->dev, "vddio");
	if (!IS_ERR(vdd_io)) {
		ret = regulator_enable(vdd_io);
		if (ret) {
			dev_err(&client->dev, "vddio set voltage error\n");
			return ret;
		}
	}
	adapter = to_i2c_adapter(client->dev.parent);
	
	printk("[Benson irq] %s client->dev.driver->name=%s\n", __func__, client->dev.driver->name);
	shared_irq = of_property_read_bool(of_node, "shared-interrupt");
	if (client->irq) {
		irq_data = irq_get_irq_data(client->irq);
		irq_flag = irqd_get_trigger_type(irq_data);
		irq_flag |= IRQF_ONESHOT;
		if (shared_irq)
				irq_flag |= IRQF_SHARED;
		ret = request_irq(client->irq, benson_irq_handler,
			irq_flag, client->dev.driver->name, NULL);
		if (ret < 0) {
			dev_err(&client->dev, "failed to register irq %d!\n",
					client->irq);
		}
	}
	return 0;
}

static int bensondts_remove(struct i2c_client *client)
{

	printk("[Benson irq] %s \n", __func__);
	return 0;
}


MODULE_DEVICE_TABLE(i2c, bensondts_id);

static struct i2c_driver bensondts_driver = {
	.driver		= {
		.name	= BENSON_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe			= bensondts_probe,
	.remove			= bensondts_remove,
	.id_table		= bensondts_id,
};


module_i2c_driver(bensondts_driver);

MODULE_AUTHOR("Benson Lin");
MODULE_DESCRIPTION("copy from mag3110 3-axis magnetometer driver");
MODULE_LICENSE("GPL");
