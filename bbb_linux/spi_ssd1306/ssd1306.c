// SPDX-License-Identifier: GPL-2.0
/*
 * SSD1306 OLED SPI driver (text-only misc device)
 *
 * Exposes /dev/ssd1306. Writing text via write(2) renders it on the
 * panel using the built-in 5x8 font. Writing an empty line clears
 * the screen.
 *
 * SSD1306 is a write-only device: no MISO is used. Chip select is
 * fully hardware-managed by the SPI controller/core, so this driver
 * never touches CS directly. D/C and RESET are plain GPIOs.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>

#include "ssd1306.h"
#include "ssd1306_font.h"

/* Recommended SSD1306 power-up initialization sequence. */
static const u8 ssd1306_init_seq[] = {
	SSD1306_CMD_DISPLAY_OFF,
	0xD5, 0x80,		/* clock divide ratio / oscillator freq */
	0xA8, 0x3F,		/* multiplex ratio: 64 */
	0xD3, 0x00,		/* display offset: none */
	0x40,			/* display start line = 0 */
	0x8D, 0x14,		/* charge pump enable */
	0x20, 0x02,		/* memory addressing mode: page */
	0xA1,			/* segment re-map */
	0xC8,			/* COM output scan direction: remapped */
	0xDA, 0x12,		/* COM pins hardware config */
	0x81, 0xCF,		/* contrast */
	0xD9, 0xF1,		/* pre-charge period */
	0xDB, 0x40,		/* VCOMH deselect level */
	0xA4,			/* resume to RAM content display */
	0xA6,			/* normal (not inverted) display */
	0x2E,			/* deactivate scroll */
	SSD1306_CMD_DISPLAY_ON,
};

/*
 * Low-level byte transfer. Toggles D/C, then hands off to the SPI
 * core. Chip-select assert/deassert around the transfer, and the
 * clock itself, are entirely handled by spi_sync()/the controller
 * driver -- not this function.
 */
static int ssd1306_write(struct ssd1306_dev *dev, bool is_cmd,
			  const u8 *buf, size_t len)
{
	int ret;

	gpiod_set_value_cansleep(dev->dc_gpio, is_cmd ? 0 : 1);

	ret = spi_write(dev->spi, buf, len);
	if (ret < 0)
		dev_err(&dev->spi->dev, "spi_write failed: %d\n", ret);

	return ret;
}

static inline int ssd1306_write_cmd(struct ssd1306_dev *dev, u8 cmd)
{
	return ssd1306_write(dev, SSD1306_CMD, &cmd, 1);
}

/* Page-addressing cursor move: page register + 2 column nibbles. */
static int ssd1306_set_cursor(struct ssd1306_dev *dev, u8 page, u8 col)
{
	int ret;

	ret = ssd1306_write_cmd(dev, SSD1306_CMD_SET_PAGE_ADDR_BASE |
				      (page & 0x07));
	if (ret < 0)
		return ret;

	ret = ssd1306_write_cmd(dev, SSD1306_CMD_SET_COL_LOW_BASE |
				      (col & 0x0F));
	if (ret < 0)
		return ret;

	ret = ssd1306_write_cmd(dev, SSD1306_CMD_SET_COL_HIGH_BASE |
				      ((col >> 4) & 0x0F));
	if (ret < 0)
		return ret;

	dev->cur_page = page;
	dev->cur_col = col;
	return 0;
}

static int ssd1306_clear(struct ssd1306_dev *dev)
{
	u8 zeros[SSD1306_WIDTH];
	int page, ret;

	memset(zeros, 0, sizeof(zeros));

	for (page = 0; page < SSD1306_PAGES; page++) {
		ret = ssd1306_set_cursor(dev, page, 0);
		if (ret < 0)
			return ret;

		ret = ssd1306_write(dev, SSD1306_DATA, zeros, sizeof(zeros));
		if (ret < 0)
			return ret;
	}

	return ssd1306_set_cursor(dev, 0, 0);
}

static void ssd1306_hw_reset(struct ssd1306_dev *dev)
{
	if (!dev->reset_gpio)
		return;

	gpiod_set_value_cansleep(dev->reset_gpio, 1); /* assert reset */
	usleep_range(10000, 12000);
	gpiod_set_value_cansleep(dev->reset_gpio, 0); /* release reset */
	usleep_range(10000, 12000);
}

static int ssd1306_init_display(struct ssd1306_dev *dev)
{
	int i, ret;

	ssd1306_hw_reset(dev);

	for (i = 0; i < ARRAY_SIZE(ssd1306_init_seq); i++) {
		ret = ssd1306_write_cmd(dev, ssd1306_init_seq[i]);
		if (ret < 0)
			return ret;

		/* let the charge pump settle before continuing */
		if (ssd1306_init_seq[i] == 0x14)
			usleep_range(2000, 3000);
	}

	return ssd1306_clear(dev);
}

/*
 * Draw one glyph followed by a blank spacer column, advancing the
 * cursor. Wraps to the next page when the line is full or on '\n'.
 */
static int ssd1306_putchar(struct ssd1306_dev *dev, char ch)
{
	const u8 *glyph;
	u8 blank = 0x00;
	int ret;

	if (ch == '\n') {
		dev->cur_page = (dev->cur_page + 1) % SSD1306_PAGES;
		dev->cur_col = 0;
		return ssd1306_set_cursor(dev, dev->cur_page, dev->cur_col);
	}

	if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR)
		ch = '?';

	/* +1 accounts for the spacer column written after the glyph */
	if (dev->cur_col + FONT_WIDTH + 1 > SSD1306_WIDTH) {
		dev->cur_page = (dev->cur_page + 1) % SSD1306_PAGES;
		dev->cur_col = 0;
		ret = ssd1306_set_cursor(dev, dev->cur_page, dev->cur_col);
		if (ret < 0)
			return ret;
	}

	glyph = ssd1306_font5x8[ch - FONT_FIRST_CHAR];

	ret = ssd1306_write(dev, SSD1306_DATA, glyph, FONT_WIDTH);
	if (ret < 0)
		return ret;

	ret = ssd1306_write(dev, SSD1306_DATA, &blank, 1);
	if (ret < 0)
		return ret;

	dev->cur_col += FONT_WIDTH + 1;
	return ssd1306_set_cursor(dev, dev->cur_page, dev->cur_col);
}

static int ssd1306_put_string(struct ssd1306_dev *dev, const char *str)
{
	int ret;

	while (*str) {
		ret = ssd1306_putchar(dev, *str++);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static ssize_t ssd1306_fops_write(struct file *filp, const char __user *ubuf,
				   size_t count, loff_t *off)
{
	struct ssd1306_dev *dev = container_of(filp->private_data,
						struct ssd1306_dev, misc);
	char *kbuf;
	size_t len;
	int ret, i;

	if (count == 0)
		return 0;

	/* screen only holds a few hundred glyphs; cap absurd writes */
	if (count > 512)
		count = 512;

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, ubuf, count)) {
		kfree(kbuf);
		return -EFAULT;
	}
	kbuf[count] = '\0';

	len = count;
	while (len > 0 && kbuf[len - 1] == '\n')
		len--;

	mutex_lock(&dev->lock);

	if (len == 0) {
		ret = ssd1306_clear(dev);
		goto out_unlock;
	}

	ret = ssd1306_clear(dev);
	if (ret < 0)
		goto out_unlock;

	for (i = 0; i < len; i++) {
		ret = ssd1306_putchar(dev, kbuf[i]);
		if (ret < 0)
			goto out_unlock;
	}

out_unlock:
	mutex_unlock(&dev->lock);
	kfree(kbuf);

	return ret < 0 ? ret : count;
}

static const struct file_operations ssd1306_fops = {
	.owner	= THIS_MODULE,
	.write	= ssd1306_fops_write,
};

static int ssd1306_probe(struct spi_device *spi)
{
	struct ssd1306_dev *dev;
	int ret;

	dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->spi = spi;
	mutex_init(&dev->lock);
	spi_set_drvdata(spi, dev);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		return ret;
	}

	dev->dc_gpio = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dev->dc_gpio)) {
		ret = PTR_ERR(dev->dc_gpio);
		dev_err(&spi->dev, "failed to get dc-gpios: %d\n", ret);
		return ret;
	}

	/* reset line is optional: some boards tie RES to VCC */
	dev->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(dev->reset_gpio)) {
		ret = PTR_ERR(dev->reset_gpio);
		dev_err(&spi->dev, "failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.name = SSD1306_DEVICE_NAME;
	dev->misc.fops = &ssd1306_fops;

	ret = misc_register(&dev->misc);
	if (ret < 0) {
		dev_err(&spi->dev, "misc_register failed: %d\n", ret);
		return ret;
	}

	ret = ssd1306_init_display(dev);
	if (ret < 0) {
		dev_err(&spi->dev, "display init failed: %d\n", ret);
		misc_deregister(&dev->misc);
		return ret;
	}

	dev_info(&spi->dev, "SSD1306 ready at /dev/%s\n",
		 SSD1306_DEVICE_NAME);

	ssd1306_put_string(dev, "Hello, world!\nThis is a test of the SSD1306 text driver.\n");

	return 0;
}

static void ssd1306_remove(struct spi_device *spi)
{
	struct ssd1306_dev *dev = spi_get_drvdata(spi);

	if (!dev)
		return;

	mutex_lock(&dev->lock);
	ssd1306_clear(dev);
	ssd1306_write_cmd(dev, SSD1306_CMD_DISPLAY_OFF);
	mutex_unlock(&dev->lock);

	misc_deregister(&dev->misc);
	dev_info(&spi->dev, "SSD1306 removed\n");
}

/*
 * NOTE on the compatible string: "solomon,ssd1306" is already owned
 * by the in-tree DRM panel driver (drivers/gpu/drm/solomon/ssd130x-spi.c).
 * Using that string here lets the mainline driver win the OF match and
 * bind first, silently preventing this driver's probe() from ever
 * running. Use a distinct vendor prefix instead.
 */
static const struct of_device_id ssd1306_of_match[] = {
	{ .compatible = "solomon,ssd1306-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, ssd1306_of_match);

static const struct spi_device_id ssd1306_spi_ids[] = {
	{ "ssd1306-spi", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ssd1306_spi_ids);

static struct spi_driver ssd1306_driver = {
	.driver = {
		.name		= SSD1306_DEVICE_NAME,
		.of_match_table	= ssd1306_of_match,
	},
	.id_table	= ssd1306_spi_ids,
	.probe		= ssd1306_probe,
	.remove		= ssd1306_remove,
};
module_spi_driver(ssd1306_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Somebody");
MODULE_DESCRIPTION("SSD1306 OLED SPI text driver");