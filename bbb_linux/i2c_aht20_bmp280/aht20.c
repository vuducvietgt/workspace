#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include "aht20.h"

static const struct of_device_id aht20_of_match[] = {
    { .compatible = "viet, aht20" },
    { }
};

MODULE_DEVICE_TABLE(of, aht20_of_match);

static const struct i2c_device_id aht20_id[] = {
    { "aht20", 0 },
    { }
};

static int aht20_read_status(struct i2c_client *client)
{
    u8 cmd = AHT_CMD_STATUS, st;
    int ret;

    ret = i2c_master_send(client, &cmd, 1);
    if(ret != 1)
        return ret < 0 ? ret : -EIO;
    ret = i2c_master_recv(client, &st, 1);
    if(ret != 1)
        return ret < 0 ? ret : -EIO;   
    return st;
}

static int aht20_hw_init(struct i2c_client *client)
{
    static const u8 reset = AHT_CMD_RESET;
    static const u8 init_cmds = {AHT_CMD_INIT, 0x08, 0x00};

    int ret;

    msleep(40); // Wait for sensor to power up

    ret = i2c_master_send(client, &reset, 1);
    if(ret != 1)
        return ret < 0 ? ret : -EIO;

    msleep(20); // Wait for reset to complete

    ret = aht20_read_status(client);
    if(ret < 0)
        return -ENODEV;

    if(!(ret & AHT_STATUS_CAL)) {
        ret = i2c_master_send(client, &init_cmds, 3);
        if(ret != 3)
            return ret < 0 ? ret : -EIO;
        msleep(10);

        ret = aht20_read_status(client);
        if(ret < 0)
            return ret;
        if(!(ret & AHT_STATUS_CAL))
            return -EIO;
    }
    return 0;
}

static int aht20_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    // Initialization code for the AHT20 sensor
    int ret;
    struct aht20_dev *dev;

    dev = dev_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
    if(!dev)
        return -ENOMEM;

    dev->client = client;

    i2c_set_clientdata(&client, &dev);

    ret = aht20_hw_init(client);
    if(ret < 0)
        return ret;
    
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

MODULE_DEVICE_TABLE(i2c, aht20_id);

module_i2c_driver(aht20_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Somebody");
MODULE_DESCRIPTION("AHT20 Temperature and Humidity Sensor Driver");