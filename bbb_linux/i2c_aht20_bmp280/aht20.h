#ifndef __AHT20_H__
#define __AHT20_H__

#define    AHT_CMD_INIT         0xBE
#define    AHT_CMD_MEASURE      0xAC
#define    AHT_CMD_RESET        0xBA
#define    AHT_CMD_STATUS       0x71
#define    AHT_MEASURE_DATA1    0x33
#define    AHT_MEASURE_DATA2    0x00
#define    AHT_MEASURE_WAIT_MS  80
#define    AHT_DATA_LENS        7
#define    AHT_MAX_POLL         10
#define    AHT_STATUS_CAL       BIT(3)
#define    AHT_STATUS_BUSY      BIT(7)

#define    AHT_DEVICE_NAME      "ahtt20"

struct aht20_dev {
    struct i2c_client *client;
    struct miscdevice misc;
    struct mutex lock;
    u8 temperature;
    u8 humidity;
};

#endif /* __AHT20_H__ */