#ifndef INA260_H
#define INA260_H

#include <stdint.h>

// 0x40 overlaps pca9685 addr, soldered sharp teeth to move over addr from 0x40 to 0x45
#define INA260_ADDRESS       0x45 //  0x40

#define INA260_REG_CONFIG    0x00
#define INA260_REG_CURRENT   0x01
#define INA260_REG_VOLTAGE   0x02
#define INA260_REG_POWER     0x03
#define INA260_REG_MASK_EN   0x06
#define INA260_REG_ALERT     0x07
#define INA260_REG_MANUF_ID  0xFE

int ina260_init(int i2c_fd);
float ina260_read_current_mA(int i2c_fd);
float ina260_read_voltage_mV(int i2c_fd);
float ina260_read_power_mW(int i2c_fd);

#endif
