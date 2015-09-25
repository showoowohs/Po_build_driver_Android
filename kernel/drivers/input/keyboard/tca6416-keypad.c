/*
 * Driver for keys on TCA6416 I2C IO expander
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * Author : Sriramakrishnan.A.G. <srk@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/tca6416_keypad.h>

//Po add
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
//Po End

#define TCA6416_INPUT          0
#define TCA6416_OUTPUT         1
#define TCA6416_INVERT         2
#define TCA6416_DIRECTION      3

#define Po_Proc 1

static const struct i2c_device_id tca6416_id[] = {
	{ "tca6416-keys", 16, },
	{ "tca6408-keys", 8, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tca6416_id);

struct tca6416_drv_data {
	struct input_dev *input;
	struct tca6416_button data[0];
};

struct tca6416_keypad_chip {
	uint16_t reg_output;
	uint16_t reg_direction;
	uint16_t reg_input;

	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work dwork;
	int io_size;
	int irqnum;
	u16 pinmask;
	bool use_polling;
	struct tca6416_button buttons[0];
};

struct i2c_client *the_client        = NULL;
struct tca6416_keypad_chip *the_chip = NULL;

static int tca6416_write_reg(int reg, u16 val)
{
	int error;

	// TODO:  i2c_smbus_write_word_data(the_client, reg << 1, val) :
	error=i2c_smbus_write_word_data(the_client, reg, val); 
	if (error < 0) {
		dev_err(&the_client->dev,
			"%s failed, reg: %d, val: %d, error: %d\n",
			__func__, reg, val, error);
		return error;
	}

	return 0;
}

// TODO: call this to get T-Key status
int tca6416_read_reg(int reg, u16 *val)
{
	int retval;

	// TODO: i2c_smbus_read_word_data(chip->client, reg << 1) :
	retval=i2c_smbus_read_word_data(the_client, reg); 
	if (retval < 0) {
		dev_err(&the_client->dev, "%s failed, reg: %d, error: %d\n",
			__func__, reg, retval);
		return retval;
	}

	*val = (u16)retval;
	return 0;
}

static void tca6416_keys_scan(struct tca6416_keypad_chip *chip)
{
#ifdef WADE
	struct input_dev *input = chip->input;
	u16 reg_val, val;
	int error, i, pin_index;

	error = tca6416_read_reg(chip, TCA6416_INPUT, &reg_val);
	if (error)
		return;

	reg_val &= chip->pinmask;

	/* Figure out which lines have changed */
	val = reg_val ^ chip->reg_input;
	chip->reg_input = reg_val;

	for (i = 0, pin_index = 0; i < 16; i++) {
		if (val & (1 << i)) {
			struct tca6416_button *button = &chip->buttons[pin_index];
			unsigned int type = button->type ?: EV_KEY;
			int state = ((reg_val & (1 << i)) ? 1 : 0)
						^ button->active_low;

			input_event(input, type, button->code, !!state);
			input_sync(input);
		}

		if (chip->pinmask & (1 << i))
			pin_index++;
	}
#endif
}

/*
 * This is threaded IRQ handler and this can (and will) sleep.
 */
static irqreturn_t tca6416_keys_isr(int irq, void *dev_id)
{
#ifdef WADE
	struct tca6416_keypad_chip *chip = dev_id;

	tca6416_keys_scan(chip);

	return IRQ_HANDLED;
#endif
}

static void tca6416_keys_work_func(struct work_struct *work)
{
#ifdef WADE
	struct tca6416_keypad_chip *chip =
		container_of(work, struct tca6416_keypad_chip, dwork.work);

	tca6416_keys_scan(chip);
	schedule_delayed_work(&chip->dwork, msecs_to_jiffies(100));
#endif
}

static int tca6416_keys_open(struct input_dev *dev)
{
#ifdef WADE
	struct tca6416_keypad_chip *chip = input_get_drvdata(dev);

	/* Get initial device state in case it has switches */
	tca6416_keys_scan(chip);

	if (chip->use_polling)
		schedule_delayed_work(&chip->dwork, msecs_to_jiffies(100));
	else
		enable_irq(chip->irqnum);

#endif
	return 0;
}

static void tca6416_keys_close(struct input_dev *dev)
{
#ifdef WADE
	struct tca6416_keypad_chip *chip = input_get_drvdata(dev);

	if (chip->use_polling)
		cancel_delayed_work_sync(&chip->dwork);
	else
		disable_irq(chip->irqnum);
#endif
}

static int __devinit tca6416_setup_registers(struct tca6416_keypad_chip *chip)
{
#ifdef WADE
	int error;

	error = tca6416_read_reg(chip, TCA6416_OUTPUT, &chip->reg_output);
	if (error)
		return error;

	error = tca6416_read_reg(chip, TCA6416_DIRECTION, &chip->reg_direction);
	if (error)
		return error;

	/* ensure that keypad pins are set to output */
	error = tca6416_write_reg(chip, TCA6416_DIRECTION, 0xff00);
	if (error)
		return error;

	error = tca6416_read_reg(chip, TCA6416_DIRECTION, &chip->reg_direction);
	if (error)
		return error;

	error = tca6416_read_reg(chip, TCA6416_INPUT, &chip->reg_input);
	if (error)
		return error;

	chip->reg_input &= chip->pinmask;

	return 0;
#endif
}
/*
read_proc()
{
  uint16 cmd;

  if (cmd == 0)
	error = tca6416_write_reg(the_chip, 0x02, 0x0000);
  else
	error = tca6416_write_reg(the_chip, 0x02, 0xffff);
}
*/

static int __devinit tca6416_keypad_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
#ifdef WADE
	struct tca6416_keys_platform_data *pdata;
	struct tca6416_keypad_chip *chip;
	struct input_dev *input;
	int error;
	int i;

	/* Check functionality */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE)) {
		dev_err(&client->dev, "%s adapter not supported\n",
			dev_driver_string(&client->adapter->dev));
		return -ENODEV;
	}

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_dbg(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(struct tca6416_keypad_chip) +
		       pdata->nbuttons * sizeof(struct tca6416_button),
		       GFP_KERNEL);
	input = input_allocate_device();
	if (!chip || !input) {
		error = -ENOMEM;
		goto fail1;
	}

	chip->client = client;
	chip->input = input;
	chip->io_size = id->driver_data;
	chip->pinmask = pdata->pinmask;
	chip->use_polling = pdata->use_polling;
        the_chip = chip;

	INIT_DELAYED_WORK(&chip->dwork, tca6416_keys_work_func);

	input->phys = "tca6416-keys/input0";
	input->name = client->name;
	input->dev.parent = &client->dev;

	input->open = tca6416_keys_open;
	input->close = tca6416_keys_close;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < pdata->nbuttons; i++) {
		unsigned int type;

		chip->buttons[i] = pdata->buttons[i];
		type = (pdata->buttons[i].type) ?: EV_KEY;
		input_set_capability(input, type, pdata->buttons[i].code);
	}

	input_set_drvdata(input, chip);

	/*
	 * Initialize cached registers from their original values.
	 * we can't share this chip with another i2c master.
	 */
	error = tca6416_setup_registers(chip);
	if (error)
		goto fail1;
#ifdef WADE
	if (!chip->use_polling) {
		if (pdata->irq_is_gpio)
			chip->irqnum = gpio_to_irq(client->irq);
		else
			chip->irqnum = client->irq;

		error = request_threaded_irq(chip->irqnum, NULL,
					     tca6416_keys_isr,
					     IRQF_TRIGGER_FALLING,
					     "tca6416-keypad", chip);
		if (error) {
			dev_dbg(&client->dev,
				"Unable to claim irq %d; error %d\n",
				chip->irqnum, error);
			goto fail1;
		}
		disable_irq(chip->irqnum);
	}
#endif

	error = input_register_device(input);
	if (error) {
		dev_dbg(&client->dev,
			"Unable to register input device, error: %d\n", error);
		goto fail2;
	}

	i2c_set_clientdata(client, chip);
	device_init_wakeup(&client->dev, 1);

	return 0;

fail2:
	if (!chip->use_polling) {
		free_irq(chip->irqnum, chip);
		enable_irq(chip->irqnum);
	}
fail1:
	input_free_device(input);
	kfree(chip);
	return error;
#endif
}

static int __devexit tca6416_keypad_remove(struct i2c_client *client)
{
	struct tca6416_keypad_chip *chip = i2c_get_clientdata(client);

	if (!chip->use_polling) {
		free_irq(chip->irqnum, chip);
		enable_irq(chip->irqnum);
	}

	input_unregister_device(chip->input);
	kfree(chip);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tca6416_keypad_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tca6416_keypad_chip *chip = i2c_get_clientdata(client);

	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irqnum);

	return 0;
}

static int tca6416_keypad_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tca6416_keypad_chip *chip = i2c_get_clientdata(client);

	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irqnum);

	return 0;
}
#endif

#ifdef Po_Proc
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

u16 port_now=0xFFFF;

/*
 * Port 0
 * echo 0 --> Thermal printer on     (df)
 * echo 1 --> Thermal printer off    (20)
 * echo 2 --> Finger printer on      (fd)
 * echo 3 --> Finger printer off     (02)
 * echo 4 --> NFC/SCR on             (fe)
 * echo 5 --> NFC/SCR off            (01)
 * echo 6 --> Barcode DC 3.3v on     (fb)
 * echo 7 --> Barcode DC 3.3v off    (04)
 * echo 8 --> USB GSM module on      (ef)
 * echo 9 --> USB GSM module off     (10)
 * echo A --> USB Multi-Touch on     (bf)
 * echo B --> USB Multi-Touch off    (40)
 * echo C --> USB GPS on             (7f)
 * echo D --> USB GPS off            (80)
 * echo E --> USB Barcode on         (f7)
 * echo F --> USB Barcode off        (08)
 */
static int WRITE_PROC( struct file *filp, const char *buff,unsigned long len1, void *data )
{
	char tmpbuf[64];
	u16 port_val;

	if (buff && !copy_from_user(tmpbuf, buff, len1)) {
		tmpbuf[len1-1] = '\0';
		if ( tmpbuf[0] == '0' ) {
			//echo 0 --> Thermal/MSR on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFDF;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  0 is [%x]!\n", __func__,port_val);
			po_val = 0;
		}else if ( tmpbuf[0] == '1' ) {
			//echo 1 --> Thermal/MSR off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x20;
			tca6416_write_reg(0x02, port_val);
			port_now = port_val;
			IMT8_printk("[Po add] %s tca6416  1 is [%x]!\n", __func__,port_val);
			po_val = 1;
		}else if ( tmpbuf[0] == '2' ) {
			//echo 2 --> Finger on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFFD;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  2 is [%x]!\n", __func__,port_val);
			po_val = 2;
		}else if ( tmpbuf[0] == '3' ){
			//echo 3 --> Finger off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x02;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  3 is [%x]!\n", __func__,port_val);
			po_val = 3;
		}else if ( tmpbuf[0] == '4' ) {
			//echo 4 --> NFC/SCR on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFFE;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  4 is [%x]!\n", __func__,port_val);
			po_val = 4;
		}else if ( tmpbuf[0] == '5' ){
			//echo 5 --> NFC/SCR off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x01;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  5 is [%x]!\n", __func__,port_val);
			po_val = 5;
		}else if ( tmpbuf[0] == '6' ) {
			//echo 6 --> Barcode DC 3.3v on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFFB;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  6 is [%x]!\n", __func__,port_val);
			po_val = 6;
		}else if ( tmpbuf[0] == '7' ){
			//echo 7 --> Barcode DC 3.3v off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x04;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  7 is [%x]!\n", __func__,port_val);
			po_val = 7;
		}else if ( tmpbuf[0] == '8' ) {
			//echo 8 --> USB GSM module on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFEF;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  8 is [%x]!\n", __func__,port_val);
			po_val = 8;
		}else if ( tmpbuf[0] == '9' ){
			//echo 9 --> USB GSM module off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x10;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  9 is [%x]!\n", __func__,port_val);
			po_val = 9;
		}else if ( tmpbuf[0] == 'A' ) {
			//echo A --> USB Multi-Touch on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFBF;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  A is [%x]!\n", __func__,port_val);
			po_val = 10;//A
		}else if ( tmpbuf[0] == 'B' ){
			//echo B --> USB Multi-Touch off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x40;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  B is [%x]!\n", __func__,port_val);
			po_val = 11;//B
		}else if ( tmpbuf[0] == 'C' ) {
			//echo C --> USB GPS on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFF7F;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  C is [%x]!\n", __func__,port_val);
			po_val = 12;//C
		}else if ( tmpbuf[0] == 'D' ){
			//echo D --> USB GPS off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x80;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  D is [%x]!\n", __func__,port_val);
			po_val = 13;//D
		}else if ( tmpbuf[0] == 'E' ) {
			//echo E --> USB Barcode(5v) on
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val & 0xFFF7;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  E is [%x]!\n", __func__,port_val);
			po_val = 14;//E
		}else if ( tmpbuf[0] == 'F' ){
			//echo F --> USB Barcode(5v) off
			tca6416_read_reg(0x00, &port_val);
			port_val = port_val | 0x08;
			tca6416_write_reg(0x02, port_val);
			tca6416_read_reg(0x00, &port_val);
			IMT8_printk("[Po add] %s tca6416  F is [%x]!\n", __func__,port_val);
			po_val = 15;//F
		}
	}

	return len1;
}

void PO_Enable_USBGPS(){
	u16 port_val;
	//echo C --> USB GPS on
	tca6416_read_reg(0x00, &port_val);
	port_val = port_val & 0xFF7F;
	tca6416_write_reg(0x02, port_val);
	tca6416_read_reg(0x00, &port_val);
	IMT8_printk("[Po add] %s tca6416  C is [%x]!\n", __func__,port_val);
	po_val = 12;//C
}

void PO_Enable_USBNFCSCR(){
	u16 port_val;
	//echo 4 --> USB NFC/SCR on
	tca6416_read_reg(0x00, &port_val);
	port_val = port_val & 0xFFFE;
	tca6416_write_reg(0x02, port_val);
	tca6416_read_reg(0x00, &port_val);
	IMT8_printk("[Po add] %s tca6416  4 is [%x]!\n", __func__,port_val);
	po_val = 4;//4
}

#endif

static SIMPLE_DEV_PM_OPS(tca6416_keypad_dev_pm_ops,
		tca6416_keypad_suspend, tca6416_keypad_resume);

static struct i2c_driver tca6416_keypad_driver = {
	.driver = {
		.name	= "tca6416-keypad",
		.pm	= &tca6416_keypad_dev_pm_ops,
	},
	.probe		= tca6416_keypad_probe,
	.remove		= __devexit_p(tca6416_keypad_remove),
	.id_table	= tca6416_id,
};

static int __init tca6416_keypad_init(void)
{
	struct i2c_adapter *adapter;
	struct i2c_board_info info = {
		.type = "tca6416",
		.addr = 0x20,
	};

	IMT8_printk("[Po add] %s start!\n", __func__);

	adapter = i2c_get_adapter(0x0);
	if (!adapter){
		IMT8_printk("[Po add] %s: i2c_get_adapter(0) failed\n", __func__);
	}else{
		the_client = i2c_new_device(adapter, &info);

		if (!the_client){
			IMT8_printk("[Po add] %s: i2c_new_device() failed\n", __func__);
			i2c_put_adapter(adapter);
			i2c_add_driver(&tca6416_keypad_driver);

			if (0 != tca6416_write_reg(TCA6416_DIRECTION, 0xFF00)){
				IMT8_printk("[Po add] %s tca6416_write_reg(TCA6416_DIRECTION, 0x0000) failed!\n", __func__);
			}
		}
	}

#ifdef Po_Proc
	Po_value = create_proc_entry("tca6416", 0666, NULL);

	if (Po_value) {
		IMT8_printk("[Po add] %s Create successful!!!\n", __func__);
		Po_value->read_proc = READ_PROC;
		Po_value->write_proc = WRITE_PROC;
		IMT8_printk("[Po add] %s init tca6416\n", __func__);
		// init i2c LED
		IMT8_printk("[Po add] %s init set 0x06 0x0000\n", __func__);
		tca6416_write_reg(0x06, 0xFF00);
		msleep(20);
		
		if(po_val == 0){

			// set Low
			IMT8_printk("[Po add] %s init set 0x02 0x0000\n", __func__);
			//tca6416_write_reg(0x02, 0xffff);
		}

	}
	// OPEN USBGPS Power
	PO_Enable_USBGPS();
	// OPEN USBNFCSCR Power
	PO_Enable_USBNFCSCR();

#endif
	IMT8_printk("[Po add] %s End!\n", __func__);
}

late_initcall(tca6416_keypad_init);

static void __exit tca6416_keypad_exit(void)
{
	i2c_del_driver(&tca6416_keypad_driver);
}
module_exit(tca6416_keypad_exit);

MODULE_AUTHOR("Sriramakrishnan <srk@ti.com>");
MODULE_DESCRIPTION("Keypad driver over tca6146 IO expander");
MODULE_LICENSE("GPL");
