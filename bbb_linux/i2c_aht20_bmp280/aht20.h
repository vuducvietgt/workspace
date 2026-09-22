#ifndef __AHT20_H__
#define __AHT20_H__

#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/bitops.h>

#define AHT_CMD_INIT         0xBE
#define AHT_CMD_MEASURE      0xAC
#define AHT_CMD_RESET        0xBA
#define AHT_CMD_STATUS       0x71
#define AHT_MEASURE_DATA1    0x33
#define AHT_MEASURE_DATA2    0x00
#define AHT_MEASURE_WAIT_MS  80
#define AHT_DATA_LEN         7
#define AHT_MAX_POLL         10
#define AHT_STATUS_CAL       BIT(3)
#define AHT_STATUS_BUSY      BIT(7)

#define AHT_DEVICE_NAME      "aht20"

struct aht20_dev {
	struct i2c_client *client;
	struct miscdevice misc;
	struct mutex lock;
	int temperature;   /* milli-°C */
	int humidity;      /* milli-%RH */
};

#endif /* __AHT20_H__ */