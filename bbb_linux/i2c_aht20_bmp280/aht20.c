#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include "aht20.h"

static const struct of_device_id aht20_of_match[] = {
	{ .compatible = "viet,aht20" },
	{ }
};
MODULE_DEVICE_TABLE(of, aht20_of_match);

static const struct i2c_device_id aht20_id[] = {
	{ "aht20", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aht20_id);

static u8 aht20_crc8(const u8 *buf, size_t len)
{
	u8 crc = 0xFF;
	size_t i;
	int b;

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		for (b = 0; b < 8; b++)
			crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
	}
	return crc;
}

static int aht20_read_status(struct i2c_client *client)
{
	u8 cmd = AHT_CMD_STATUS, st;
	int ret;

	ret = i2c_master_send(client, &cmd, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;
	ret = i2c_master_recv(client, &st, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;
	return st;
}

static int aht20_measure(struct aht20_dev *dev)
{
	struct i2c_client *client = dev->client;
	static const u8 mes_cmds[3] = { AHT_CMD_MEASURE, AHT_MEASURE_DATA1,
					 AHT_MEASURE_DATA2 };
	u8 buf[AHT_DATA_LEN];
	u32 raw_humi, raw_temp;
	int ret, i;

	mutex_lock(&dev->lock);

	ret = i2c_master_send(client, mes_cmds, sizeof(mes_cmds));
	if (ret != sizeof(mes_cmds)) {
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}

	msleep(AHT_MEASURE_WAIT_MS);

	for (i = 0; i < AHT_MAX_POLL; i++) {
		ret = i2c_master_recv(client, buf, sizeof(buf));
		if (ret != sizeof(buf)) {
			ret = ret < 0 ? ret : -EIO;
			goto out;
		}
		if (!(buf[0] & AHT_STATUS_BUSY))
			break;
		msleep(10);
	}
	if (i == AHT_MAX_POLL) {
		ret = -ETIMEDOUT;
		goto out;
	}

	if (aht20_crc8(buf, 6) != buf[6]) {
		dev_dbg(&client->dev, "CRC error\n");
		ret = -EIO;
		goto out;
	}

	raw_humi = ((u32)buf[1] << 12) | ((u32)buf[2] << 4) | (buf[3] >> 4);
	raw_temp = ((u32)(buf[3] & 0x0F) << 16) | ((u32)buf[4] << 8) | buf[5];

	dev->humidity    = (int)(((u64)raw_humi * 100000) >> 20);
	dev->temperature = (int)(((u64)raw_temp * 200000) >> 20) - 50000;
	ret = 0;
out:
	mutex_unlock(&dev->lock);
	return ret;
}

static ssize_t aht20_fops_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *offp)
{
	struct miscdevice *misc = filp->private_data;
	struct aht20_dev *dev = container_of(misc, struct aht20_dev, misc);
	char out[48];
	int len, ret;

	if (*offp > 0)
		return 0;

	ret = aht20_measure(dev);
	if (ret < 0)
		return ret;

	len = scnprintf(out, sizeof(out), "temp=%d rh=%d\n",
			 dev->temperature, dev->humidity);
	return simple_read_from_buffer(ubuf, count, offp, out, len);
}

static const struct file_operations aht20_fops = {
	.owner = THIS_MODULE,
	.read  = aht20_fops_read,
};

static int aht20_hw_init(struct i2c_client *client)
{
	static const u8 reset = AHT_CMD_RESET;
	static const u8 init_cmds[3] = { AHT_CMD_INIT, 0x08, 0x00 };
	int ret;

	msleep(40);

	ret = i2c_master_send(client, &reset, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;
	msleep(20);

	ret = aht20_read_status(client);
	if (ret < 0)
		return -ENODEV;

	if (!(ret & AHT_STATUS_CAL)) {
		ret = i2c_master_send(client, init_cmds, sizeof(init_cmds));
		if (ret != sizeof(init_cmds))
			return ret < 0 ? ret : -EIO;
		msleep(10);

		ret = aht20_read_status(client);
		if (ret < 0)
			return ret;
		if (!(ret & AHT_STATUS_CAL))
			return -EIO;
	}
	return 0;
}

static int aht20_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct aht20_dev *dev;
	int ret;

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->client = client;
	mutex_init(&dev->lock);
	i2c_set_clientdata(client, dev);

	ret = aht20_hw_init(client);
	if (ret < 0)
		return ret;

	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.name  = AHT_DEVICE_NAME;
	dev->misc.fops  = &aht20_fops;

	ret = misc_register(&dev->misc);
	if (ret < 0) {
		dev_err(&client->dev, "misc register failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int aht20_remove(struct i2c_client *client)
{
	struct aht20_dev *dev = i2c_get_clientdata(client);

	misc_deregister(&dev->misc);
	return 0;
}

static struct i2c_driver aht20_driver = {
	.driver = {
		.name = "aht20",
		.of_match_table = aht20_of_match,
	},
	.probe = aht20_probe,
	.remove = aht20_remove,
	.id_table = aht20_id,
};
module_i2c_driver(aht20_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Somebody");
MODULE_DESCRIPTION("AHT20 Temperature and Humidity Sensor Driver");