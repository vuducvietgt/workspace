#ifndef __AHT20_H__
#define __AHT20_H__

#define    AHT_CMD_INIT     0xBE
#define    AHT_CMD_MEASURE  0xAC
#define    AHT_CMD_RESET    0xBA
#define    AHT_CMD_STATUS   0x71
#define    AHT_STATUS_CAL   BIT(3)
#define    AHT_STATUS_BUSY  BIT(7)

struct aht20_dev {
    struct i2c_client *client;
    struct miscdevice misc;
    struct mutex lock;
    u8 temperature;
    u8 humidity;
};

#endif /* __AHT20_H__ */