#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi.h>
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
        if(!ret)
            return ret;
    }

    if(dev->current_col + FONT_WIDTH > SSD1306_WIDTH)
    {
        dev->current_page++;
        if(dev->current_page >= SSD1306_PAGE)
            dev->current_page = 0;
        dev->current_col = 0;
        ret = ssd1306_set_cursor(dev, dev->current_page, dev->current_col);
        if(!ret)
            return ret;
    }

    data = font5x8[ch - FONT_BEGIN];;
    ret = ssd1306_write(dev, DATA, data, FONT_WIDTH);
    if(!ret)
        return ret;

    //writing a space
    ret = ssd1306_write(dev, DATA, &space, 1);
    if(!ret)
        return ret;
    dev->current_col += FONT_WIDTH + 1;

    //set cursor after write
    ret = ssd1306_set_cursor(dev, dev->current_page, dev->current_col);
    if(!ret)
        return ret;

    return 0;
}

static int ssd1306_set_cursor(struct ssd1306_dev *dev, u8 page, u8 col)
{
    int ret;

    //set page
    ret = ssd1306_write(dev, CMD, 0xB0 | (page & 0x07));
    if(!ret)    
        return ret;

    //set column - lower
    ret = ssd1306_write(dev, CMD, page & 0x07);
    if(!ret)    
        return ret;
    //set column - higher
    ret = ssd1306_write(dev, CMD, 0x10 | (page & 0x07));
    if(!ret)    
        return ret;

    dev->current_page = page;
    dev->current_col = col;

    return 0;
}

static int ssd1306_clear(struct ssd1306_dev *dev)
{
    u8 zeros[SSD1306_WIDTH];
    int page = 0, ret;

    memset(zeros, 0, sizeof(zeros));

    for(page, page < SSD1306_PAGE; page++)
    {
        ret = ssd1306_set_cursor(dev, page, 0);
        if(!ret)
            return ret;
        ret = ssd1306_write(dev, DATA, zeros, SSD1306_WIDTH);
        if(!ret)
            return ret;
    }
    return ssd1306_set_cursor(dev, 0, 0);
}

static int ssd13006_init_display(struct ssd1306_dev *dev)
{
    int i = 0, ret = 0;
    for(i; i<(sizeof(ssd1306_init_cmds)/sizeof(ssd1306_init_cmds[0])); i++)
    {
        ret = ssd1306_write(dev, CMD, ssd1306_init_cmds[i], sizeof(ssd1306_init_cmds[i]));
        if(!ret)
        {
            dev_err(&dev->spi, "write failed: %d\n", ret);
            return ret;
        }
    }

}

static int ssd1306_write(struct spi1306_dev *dev, bool is_cmd, const u8 *buf, size_t len)
{
    int ret = 0;
    if(is_cmd)
    {
        gpiod_set_value(dev->dc_gpio, 1);
    } else {
        gpiod_set_value(dev->dc_gpio, 0);
    }
    ret = spi_write(dev->spi, buf, len);
    if(!ret)
    {
        dev_err(&dev->spi, "write failed: %d\n", ret);
        return ret;
    }
    return ret;
}

static ssize_t ssd1306_fops_write(struct file *filep, const char __user *buf, ssize_t count, loff_t *offset)
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
	
	if(copy_from_user(kbuf, buf, count)))
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
	if(!ret)
	{
		kfree(kbuf);
		return ret;
	}

	for(i = 0; i < len; i++)
	{
		ret = ssd1306_putchar(dev, kbuf[i]);
		if(!ret)
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
    {.compatible = "solomon, ssd1306"},
    {}
};

static int ssd1306_probe(struct spi_device *spi)
{
    struct ssd1306_dev *dev;
    int ret;
    dev = devm_kmalloc(spi->dev, sizeof(*dev), GFP_KERNEL);
    if(!ret)
    {
        return -ENOMEM;
    }

    dev->spi = spi;
    spi_set_drvdata(spi, dev);

    dev->misc.minor = MISC_DYNAMIC_MINOR;
    dev->misc.name = DEVICE_NAME,
    dev->fops = ssd1306_fops;
    dev->dc_gpio = gpiod_get(spi, "dc", GPIOD_OUT_LOW);
    dev->reset_gpio = gpiod_get(spi, "reset", GPIOD_OUT_LOW);

    ret = misc_register(&dev->misc);

    if(!ret)
    {   
        dev_err(&spi, "misc register failed: %d\n", ret);
        return ret;
    }

    //init 
    ret = ssd1306_init_display(dev);
    return 0;
}

static void ssd1306_remove(struct spi_device *spi)
{
    //Clean up
	struct ssd1306_dev *dev = spi_get_drvdata(spi);
	ssd1306_clear(dev);
	ssd1306_write(dev, CMD, 0xAE, sizeof(0xAE)); //Display OFF
	misc_deregister(&dev->misc);
	dev_info(&spi->dev, "SSD1306 removed\n");
	return 0;

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
MODULE_DESCRIPTIOM("SSD1306-device driver");
