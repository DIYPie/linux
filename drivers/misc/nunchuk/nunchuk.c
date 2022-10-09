// SPDX-License-Identifier: GPL-2.0
/*
 * 
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#define NUNCHUK_DRV_NAME 			"nunchuk-joystick"
#define NUNCHUK_POLL_INTERVAL		16
#define NUNCHUK_POLL_INTERVAL_MIN	10
#define NUNCHUK_POLL_INTERVAL_MAX	32

#define NUNCHUK_BUTTON_PRESSED(d, i, b)	(((d)[i] & (1 << (b))) ? 0 : 1)

static void nunchuk_poll(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);
	u8 data[6];
	int err;

	/* Get data */
	err = i2c_master_recv(client, data, 6);
	if (err < 0) {
		dev_err(&client->dev, "failed to get device data: %d\n", err);
		return;
	}

	/* Reset register offset */
	err = i2c_master_send(client, (const u8[]){0x00}, 1);
	if (err < 0) {
		dev_err(&client->dev, "failed to set data address: %d\n", err);
		return;
	}

	input_report_key(input, BTN_DPAD_UP, NUNCHUK_BUTTON_PRESSED(data, 5, 0));
	input_report_key(input, BTN_DPAD_RIGHT, NUNCHUK_BUTTON_PRESSED(data, 4, 7));
	input_report_key(input, BTN_DPAD_DOWN, NUNCHUK_BUTTON_PRESSED(data, 4, 6));
	input_report_key(input, BTN_DPAD_LEFT, NUNCHUK_BUTTON_PRESSED(data, 5, 1));

	input_report_key(input, BTN_EAST, NUNCHUK_BUTTON_PRESSED(data, 5, 4));
	input_report_key(input, BTN_SOUTH, NUNCHUK_BUTTON_PRESSED(data, 5, 6));
	input_report_key(input, BTN_NORTH, NUNCHUK_BUTTON_PRESSED(data, 5, 3));
	input_report_key(input, BTN_WEST, NUNCHUK_BUTTON_PRESSED(data, 5, 5));

	input_report_key(input, BTN_TL, NUNCHUK_BUTTON_PRESSED(data, 4, 5));
	input_report_key(input, BTN_TR, NUNCHUK_BUTTON_PRESSED(data, 4, 1));

	input_report_key(input, BTN_START, NUNCHUK_BUTTON_PRESSED(data, 4, 2));
	input_report_key(input, BTN_SELECT, NUNCHUK_BUTTON_PRESSED(data, 4, 4));

	input_sync(input);
}

static int nunchuk_open(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);
	u8 ident[6];
	int err;

	/* Initialize (and disable encryption) */
	err = i2c_master_send(client, (const u8[]){0xf0, 0x55}, 2);
	if (err < 0) {
		dev_err(&client->dev, "failed to write register 0xf0: %d\n", err);
		return err;
	}

	msleep(10);

	err = i2c_master_send(client, (const u8[]){0xfb, 0x00}, 2);
	if (err < 0) {
		dev_err(&client->dev, "failed to write register 0xfb: %d\n", err);
		return err;
	}

	msleep(10);

	/* Read device identifier */
	err = i2c_master_send(client, (const u8[]){0xfa}, 1);
	if (err < 0) {
		dev_err(&client->dev, "failed to set ident address: %d\n", err);
		return err;
	}

	err = i2c_master_recv(client, ident, 6);
	if (err < 0) {
		dev_err(&client->dev, "failed to get ident data: %d\n", err);
		return err;
	}

	msleep(10);

	dev_info(&client->dev, "device identifier: [%02x, %02x, %02x, %02x, %02x, %02x]\n", 
							ident[0], ident[1], ident[2],
							ident[3], ident[4], ident[5]);

	/* Reset register offset */
	err = i2c_master_send(client, (const u8[]){0x00}, 1);
	if (err < 0) {
		dev_err(&client->dev, "failed to set data address: %d\n", err);
		return err;
	}

	msleep(10);

	return 0;
}

static void nunchuk_close(struct input_dev *input)
{
	struct i2c_client *client = input_get_drvdata(input);

	dev_info(&client->dev, "device closed\n");
}

static int nunchuk_probe(struct i2c_client *client)
{
	struct input_dev *input;
	int err;

	input = devm_input_allocate_device(&client->dev);
	if (!input) {
		return -ENOMEM;
	}

	input_set_drvdata(input, client);

	input->name = NUNCHUK_DRV_NAME;
	input->id.bustype = BUS_I2C;

	input->open = nunchuk_open;
	input->close = nunchuk_close;

	input_set_capability(input, EV_KEY, BTN_DPAD_UP);
	input_set_capability(input, EV_KEY, BTN_DPAD_DOWN);
	input_set_capability(input, EV_KEY, BTN_DPAD_LEFT);
	input_set_capability(input, EV_KEY, BTN_DPAD_RIGHT);

	input_set_capability(input, EV_KEY, BTN_SOUTH);
	input_set_capability(input, EV_KEY, BTN_NORTH);
	input_set_capability(input, EV_KEY, BTN_EAST);
	input_set_capability(input, EV_KEY, BTN_WEST);

	input_set_capability(input, EV_KEY, BTN_SELECT);
	input_set_capability(input, EV_KEY, BTN_START);

	input_set_capability(input, EV_KEY, BTN_TL);
	input_set_capability(input, EV_KEY, BTN_TR);

	err = input_setup_polling(input, nunchuk_poll);
	if (err) {
		dev_err(&client->dev, "failed to set up polling\n");
		return err;
	}

	input_set_poll_interval(input, NUNCHUK_POLL_INTERVAL);
	input_set_min_poll_interval(input, NUNCHUK_POLL_INTERVAL_MIN);
	input_set_max_poll_interval(input, NUNCHUK_POLL_INTERVAL_MAX);

	err = input_register_device(input);
	if (err) {
		dev_err(&client->dev, "failed to register input device\n");
		return err;
	}

	return 0;
}

static const struct i2c_device_id nunchuk_id[] = {
	{ NUNCHUK_DRV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, nunchuk_id);

static const struct of_device_id of_nunchuk_match[] = {
	{ .compatible = "nintendo,nunchuk-joystick", },
	{ },
};
MODULE_DEVICE_TABLE(of, of_nunchuk_match);

static struct i2c_driver nunchuk_driver = {
	.driver = {
		.name		= NUNCHUK_DRV_NAME,
		.of_match_table	= of_match_ptr(of_nunchuk_match),
	},
	.probe_new	= nunchuk_probe,
	.id_table = nunchuk_id
};
module_i2c_driver(nunchuk_driver);

MODULE_AUTHOR("Ionut Catalin Pavel <iocapa@iocapa.com>");
MODULE_DESCRIPTION("Nintendo Nunchuck Driver");
MODULE_LICENSE("GPL v2");
