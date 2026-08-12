#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "ssd1306.h"

static int ssd1306_putchar(struct ssd1306_dev *dev, char ch)
{
    const u8 *data;
    u8 space = 0x00;
    int ret = 0;

    if(ch < FONT_BEGIN || ch > FONT_LAST)
        ch = '?';

    if(ch == '\n')
    {
        dev->current_page++;
        if(dev->current_page > SSD1306_PAGE)
            dev->current_page = 0;
        dev->current_col = 0;
        ret = ssd1306_set_cursor(dev, dev->current_page, dev->current_col);
        if(ret < 0)
            return ret;
    }

    if(dev->current_col + FONT_WIDTH + 1 > SSD1306_WIDTH)
    {
        dev->current_page++;
        if(dev->current_page >= SSD1306_PAGE)
            dev->current_page = 0;
        dev->current_col = 0;
        ret = ssd1306_set_cursor(dev, dev->current_page, dev->current_col);
        if(ret < 0)
            return ret;
    }

    data = font5x8[ch - FONT_BEGIN];;
    ret = ssd1306_write(dev, DATA, data, FONT_WIDTH);
    if(ret < 0)
        return ret;

    //writing a space
    ret = ssd1306_write(dev, DATA, &space, 1);
    if(ret < 0)
        return ret;
    dev->current_col += FONT_WIDTH + 1;

    //set cursor after write
    ret = ssd1306_set_cursor(dev, dev->current_page, dev->current_col);
    if(ret < 0)
        return ret;

    return 0;
}

static int ssd1306_set_cursor(struct ssd1306_dev *dev, u8 page, u8 col)
{
    int ret;
    u8 tmp;

    //set page
    tmp = 0xB0 | (page & 0x07);
    ret = ssd1306_write(dev, CMD, &tmp, 1);
    if(ret < 0)
        return ret;

    //set column - lower
    tmp = col & 0x0F;
    ret = ssd1306_write(dev, CMD, &tmp, 1);
    if(ret < 0)
        return ret;
    //set column - higher
    tmp = 0x10 | ((col >> 4) & 0x0F);
    ret = ssd1306_write(dev, CMD, &tmp, 1);
    if(ret < 0)
        return ret;

    dev->current_page = page;
    dev->current_col = col;

    return 0;
}

static int ssd1306_clear(struct ssd1306_dev *dev)
{
    u8 zeros[SSD1306_WIDTH];
    int page, ret;

    memset(zeros, 0, sizeof(zeros));

    for(page = 0; page < SSD1306_PAGE; page++)
    {
        ret = ssd1306_set_cursor(dev, page, 0);
        if(ret < 0)
            return ret;
        ret = ssd1306_write(dev, DATA, zeros, SSD1306_WIDTH);
        if(ret < 0)
            return ret;
    }
    return ssd1306_set_cursor(dev, 0, 0);
}

static int ssd1306_init_display(struct ssd1306_dev *dev)
{
    int i, ret = 0;

    //Make HW reset
    gpiod_set_value_cansleep(dev->reset_gpio, 1);
    msleep(100);
    gpiod_set_value_cansleep(dev->reset_gpio, 0);
    msleep(100);

    for(i = 0; i<(sizeof(ssd1306_init_cmds)/sizeof(ssd1306_init_cmds[0])); i++)
    {
        ret = ssd1306_write(dev, CMD, &ssd1306_init_cmds[i], 1);
        if(ret < 0)
        {
            dev_err(&dev->spi->dev, "write failed: %d\n", ret);
            return ret;
        }
    }

    ret = ssd1306_clear(dev);
    return ret;
}

static int ssd1306_write(struct ssd1306_dev *dev, bool is_cmd, const u8 *buf, size_t len)
{
    int ret = 0;
    if(is_cmd)
    {
        gpiod_set_value_cansleep(dev->dc_gpio, 0);
    } else {
        gpiod_set_value_cansleep(dev->dc_gpio, 1);
    }
    ret = spi_write(dev->spi, buf, len);
    if(ret < 0)
    {
        dev_err(&dev->spi->dev, "write failed: %d\n", ret);
        return ret;
    }
    return ret;
}

static ssize_t ssd1306_fops_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset)
{
	char *kbuf;
	int ret, i;
	ssize_t len;

	struct ssd1306_dev *dev = container_of(filep->private_data, struct ssd1306_dev, misc);
	if(count <= 0)
		return 0;
	
	kbuf = kmalloc(count+1, GFP_KERNEL);
	if(!kbuf)
		return -ENOMEM;
	
	if(copy_from_user(kbuf, buf, count))
	{
		kfree(kbuf);
		return -EFAULT;
	}

	kbuf[count] = '\0';
	len = count;

	/*Strip trailing newline from echo*/
	while(len > 0 && kbuf[len-1] == '\n')
		len--;

	/*Emptystring (or only newline) -> clear screen*/
	if(len == 0)
	{
		ret = ssd1306_clear(dev);
		kfree(kbuf);
		return ret < 0 ? ret : count;
	}

	/*Clear screen from top-left*/
	ret = ssd1306_set_cursor(dev, 0, 0);
	if(ret < 0)
	{
		kfree(kbuf);
		return ret;
	}

	for(i = 0; i < len; i++)
	{
		ret = ssd1306_putchar(dev, kbuf[i]);
		if(ret < 0)
		{
			kfree(kbuf);
			return ret;
		}
	}
	kfree(kbuf);
	return count;
	
}


static const struct file_operations ssd1306_fops = {
    .owner = THIS_MODULE,
    .write = ssd1306_fops_write,
};

static const struct of_device_id ssd1306_of_match[] = {
    {.compatible = "solomon,ssd1306"},
    {}
};

static int ssd1306_probe(struct spi_device *spi)
{
    struct ssd1306_dev *dev;
    int ret = 0;
    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if(!dev)
    {
        return -ENOMEM;
    }

    dev->spi = spi;
    spi_set_drvdata(spi, dev);

    dev->misc.minor = MISC_DYNAMIC_MINOR;
    dev->misc.name = DEVICE_NAME;
    dev->misc.fops = &ssd1306_fops;
    dev->dc_gpio = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);

    if(IS_ERR(dev->dc_gpio)) {
        ret = PTR_ERR(dev->dc_gpio);
        dev_err(&spi->dev, "failed to get dc gpio: %d\n", ret);
        return ret;
    }

    dev->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);

    if(IS_ERR(dev->reset_gpio)) {
        ret = PTR_ERR(dev->reset_gpio);
        dev_err(&spi->dev, "failed to get reset gpio: %d\n", ret);
        return ret;
    }

    ret = misc_register(&dev->misc);

    if(ret < 0)
    {   
        dev_err(&spi->dev, "misc register failed: %d\n", ret);
        return ret;
    }

    //init 
    ret = ssd1306_init_display(dev);
    if(ret < 0)
    {
        misc_deregister(&dev->misc);
        return ret;
    }
    return ret;
}

static void ssd1306_remove(struct spi_device *spi)
{
    //Clean up
    u8 tmp = 0xAE;
	struct ssd1306_dev *dev = spi_get_drvdata(spi);
    if(!dev)
    {
        return;
    }
	ssd1306_clear(dev);
	ssd1306_write(dev, CMD, &tmp, 1); //Display OFF
	misc_deregister(&dev->misc);
	dev_info(&spi->dev, "SSD1306 removed\n");

}

MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static struct spi_driver ssd1306_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .of_match_table = ssd1306_of_match,
    },
    .probe = ssd1306_probe,
    .remove = ssd1306_remove,
};

module_spi_driver(ssd1306_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Somebody");
MODULE_DESCRIPTION("SSD1306-device driver");
