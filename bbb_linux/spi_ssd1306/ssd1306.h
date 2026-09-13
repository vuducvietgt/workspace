/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SSD1306_H_
#define _SSD1306_H_

#include <linux/types.h>

#define SSD1306_DEVICE_NAME	"ssd1306"

/* Byte type flag passed to ssd1306_write() */
#define SSD1306_CMD		1
#define SSD1306_DATA		0

/* Panel geometry */
#define SSD1306_WIDTH		128
#define SSD1306_HEIGHT		64
#define SSD1306_PAGES		(SSD1306_HEIGHT / 8)

/* Printable ASCII font range */
#define FONT_WIDTH		5
#define FONT_FIRST_CHAR		0x20
#define FONT_LAST_CHAR		0x7E

/* SSD1306 command opcodes referenced by name in the driver */
#define SSD1306_CMD_DISPLAY_OFF		0xAE
#define SSD1306_CMD_DISPLAY_ON			0xAF
#define SSD1306_CMD_SET_PAGE_ADDR_BASE		0xB0
#define SSD1306_CMD_SET_COL_LOW_BASE		0x00
#define SSD1306_CMD_SET_COL_HIGH_BASE		0x10

struct ssd1306_dev {
	struct spi_device	*spi;
	struct miscdevice	misc;
	struct gpio_desc	*dc_gpio;
	struct gpio_desc	*reset_gpio;
	struct mutex		lock;
	u8			cur_page;
	u8			cur_col;
};

#endif /* _SSD1306_H_ */