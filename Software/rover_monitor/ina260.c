#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "ina260.h"

static int read_register(int i2c_fd, uint8_t reg, int16_t *value) {
    uint8_t buf[2];
    if (write(i2c_fd, &reg, 1) != 1)
        return -1;
    if (read(i2c_fd, buf, 2) != 2)
        return -2;

    *value = (buf[0] << 8) | buf[1];
    return 0;
}

#define DEV_ID 0x5449

int ina260_init(int i2c_fd) {
    int16_t raw=0;
    int rc=0;

    // No specific init needed for default config
    if ((rc=read_register(i2c_fd, INA260_REG_MANUF_ID, &raw)) == 0) {
        if (raw == DEV_ID) {
            printf("ina260 ID 0x%04X Match 0x%04X\n", raw, DEV_ID);
            return 0;
        } else{
            printf("ina260 ID 0x%04X error! should be: 0x%04X\n", raw, DEV_ID);
            return 1;
        }
    }  
    printf("%s(%d) reg[0x%02X] read ERROR! %d\n", __func__,__LINE__,INA260_REG_MANUF_ID, rc);
    return 2;
}

float ina260_read_current_mA(int i2c_fd) {
    int16_t raw;
    if (read_register(i2c_fd, INA260_REG_CURRENT, &raw) != 0)
        return -9999.0;
    return raw * 1.25; // 1.25 mA per bit
}

float ina260_read_voltage_mV(int i2c_fd) {
    int16_t raw;
    if (read_register(i2c_fd, INA260_REG_VOLTAGE, &raw) != 0)
        return -9999.0;
    return raw * 1.25; // 1.25 mV per bit
}

float ina260_read_power_mW(int i2c_fd) {
    int16_t raw;
    if (read_register(i2c_fd, INA260_REG_POWER, &raw) != 0)
        return -9999.0;
    return raw * 10.0; // 10 mW per bit
}
