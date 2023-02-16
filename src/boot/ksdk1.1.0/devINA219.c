/*!
 * Heavily modified implementation of the Adafruit INA219 Arduino Library
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */
#include <stdlib.h>

#include "config.h"

#include "fsl_misc_utilities.h"
#include "fsl_device_registers.h"
#include "fsl_i2c_master_driver.h"
#include "fsl_spi_master_driver.h"
#include "fsl_rtc_driver.h"
#include "fsl_clock_manager.h"
#include "fsl_power_manager.h"
#include "fsl_mcglite_hal.h"
#include "fsl_port_hal.h"

#include "devINA219.h"
#include "warp.h"

//#define ENABLE_PRINTF_DEBUG

volatile uint8_t ina219_i2cBuffer[kWarpSizesI2cBufferBytes]; // 4

uint32_t ina219_currentMultiplier_uA = 1;
uint32_t ina219_powerMultiplier_uW = 1;
uint16_t ina219_calValue = 1;

uint32_t readRegisterINA219(uint8_t deviceRegister, int numberOfBytes)
{
    // warpPrint("Read R 0x%02x\n", deviceRegister);
    uint8_t cmdBuf[1] = {0xFF};
    i2c_status_t status;

    i2c_device_t slave =
        {
            .address = INA219_ADDRESS,
            .baudRate_kbps = kWarpDefaultI2cBaudRateKbps
        };

    cmdBuf[0] = deviceRegister;
    warpEnableI2Cpins();

    status = I2C_DRV_MasterReceiveDataBlocking(
        0 /* I2C peripheral instance */,
        &slave,
        cmdBuf,
        1,
        (uint8_t *)ina219_i2cBuffer,
        numberOfBytes,
        kWarpDefaultI2cTimeoutMilliseconds);

    if (status != kStatus_I2C_Success)
    {
        return 1;
    }

    #ifdef ENABLE_PRINTF_DEBUG
        warpPrint("Read 0x%02x: 0x%02x%02x\n", deviceRegister, ina219_i2cBuffer[0], ina219_i2cBuffer[1]);
    #endif 

    return 0;
}

uint32_t writeRegisterINA218(uint8_t deviceRegister, uint16_t payload)
{
    uint8_t payloadByte[1], commandByte[1];
    i2c_status_t status;

    i2c_device_t slave =
        {
            .address = INA219_ADDRESS,
            .baudRate_kbps = kWarpDefaultI2cBaudRateKbps
        };

    commandByte[0] = deviceRegister;
    payloadByte[0] = (payload >> 8) & 0xFF;
    payloadByte[1] = payload & 0xFF;
    warpEnableI2Cpins();

    #ifdef ENABLE_PRINTF_DEBUG
        warpPrint("Write R 0x%02x w 0x%02x,0x%02x \n", deviceRegister, payloadByte[0], payloadByte[1]);
    #endif

    status = I2C_DRV_MasterSendDataBlocking(
        0 /* I2C instance */,
        &slave,
        commandByte,
        1,
        payloadByte,
        2,
        kWarpDefaultI2cTimeoutMilliseconds);

    if (status != kStatus_I2C_Success)
    {
        return 1;
    }

    return 0;
}

uint32_t initINA219()
{
    uint32_t status = 0;

    // min theoretical current LSB with a calib reg of F000 is ~ 7uA per bit

    // The OLED is specced to draw a maximum of 25mA with a supply voltage of 5v, across the 0.1 Ohm shunt. max
    // Select the Vbus full scale measurement range as 16V = VBUS_MAX
    // Select the smallest PGA gain of 0.04 = VSHUNT_MAX
    // Rshunt is equal to 0.1
    // This gives a max possible measured current of 0.04/0.1 = 0.4 A
    // The max expected current is around __0.05__ A
    // MinimumLSB = MaxExpected_I//32767 = 0.000_001_526 = 1.5uA
    // MaximumLSB = MaxExpected_I//4096 = 0.000_012_207 = 12uA
    // Choose the smallest nice one
    // Current_LSB = 000_002 = 0.000_01 = 10uA per bit
    // Compute the calibration register
    // Cal = trunc (0.04096 / (Current_LSB * RSHUNT)) = 40960 = 0xA000
    ina219_calValue = 0xA000;
    // Compute the power LSB
    // PowerLSB = 20 * CurrentLSB = 0.0002 (0.2 mW per bit)
    // Compute the maximum current and shunt voltage values before overflow
    // Max_Current = Current_LSB * 32767 = 0.32767
    // Max_Current = 0.327 A before overflow, MAXPI = 0.4
    //   Therefore Max_Current_Before_Overflow = Max_Current
    // Max_ShuntVoltage = Max_Current_Before_Overflow * RSHUNT = 0.032767
    //   Max_ShuntVoltage_Before_Overflow = Max_ShuntVoltage = 0.032
    // Compute the Maximum Power
    // MaximumPower = Max_Current_Before_Overflow * VBUS_MAX = 5.2W

    // Set multipliers to convert raw current/power values
    ina219_currentMultiplier_uA = 10;    // Current LSB = 10uA per bit
    ina219_powerMultiplier_uW = 200; // Power LSB = 1mW per bit (2/1)

    // Set Calibration register to 'Cal' calculated above
    status += writeRegisterINA218(INA219_REG_CALIBRATION, ina219_calValue);

    // Set Config register to take into account the settings above + others
    uint16_t config = INA219_CONFIG_BVOLTAGERANGE_16V |        // 16V Full-Scale Bus Range
                      INA219_CONFIG_GAIN_1_40MV |              // Lowest PGA Gain
                      INA219_CONFIG_BADCRES_12BIT |            // 12-bit bus res = 0..4097
                      INA219_CONFIG_SADCRES_12BIT_1S_532US |   // 1 x 12-bit shunt sample
                      INA219_CONFIG_MODE_SANDBVOLT_CONTINUOUS; // shunt and bus voltage continuous

    status += writeRegisterINA218(INA219_REG_CONFIG, config);
    
    // wait for the registers to be properly set
    OSA_TimeDelay(100); 
    // give it a moment
    // read the lines back
    status += readRegisterINA219(INA219_REG_CALIBRATION, 2);
    status += readRegisterINA219(INA219_REG_CONFIG, 2);

    return status;
}

/*!
 *  Raw bus voltage (16-bit signed integer, so +-32767)
 */
uint16_t getBusVoltageRawINA219(void)
{
    uint16_t success = readRegisterINA219(INA219_REG_BUSVOLTAGE, 2);
    // Shift to the right 3 to drop CNVR and OVF and multiply by LSB
    uint16_t value = ina219_i2cBuffer[1] | (ina219_i2cBuffer[0] << 8); // MSB-LSB bytes
    return (int16_t)((value >> 3) * 4); 
}

/*!
 *  Bus voltage in millivolts 
 */
uint32_t getBusVoltagemVINA219(void)
{
    uint16_t value = getBusVoltageRawINA219();
    return value;
}

/*!
 *  Raw shunt voltage (16-bit signed integer, so +-32767)
 */
uint16_t getShuntVoltageRawINA219(void)
{
    uint16_t success = readRegisterINA219(INA219_REG_SHUNTVOLTAGE, 2);
    uint16_t value = ina219_i2cBuffer[1] | (ina219_i2cBuffer[0] << 8); // MSB-LSB bytes
    return value;
}

/*!
 *  Shunt voltage in uV (so +-327000uV)
 */
uint32_t getShuntVoltageuVINA219(void)
{
    uint16_t value = getShuntVoltageRawINA219();
    return value * 10;
}

/*!
 *  Raw current value (16-bit signed integer, so +-32767)
 */
uint16_t getCurrentRawINA219(void)
{
    uint16_t success = readRegisterINA219(INA219_REG_CURRENT, 2);
    uint16_t value = ina219_i2cBuffer[1] | (ina219_i2cBuffer[0] << 8); // MSB-LSB bytes
    return value;
}

/*!
 *  Current value in uA, taking into account the config settings and current LSB
 */
uint32_t getCurrentuAINA219(void)
{
    uint32_t valueDec = getCurrentRawINA219();
    valueDec  *= ina219_currentMultiplier_uA;
    return valueDec;
}

/*!
 *  Raw power value (16-bit signed integer, so +-32767)
 */
uint16_t getPowerRawINA219(void)
{
    uint16_t success = readRegisterINA219(INA219_REG_POWER, 2);
    uint16_t value = ina219_i2cBuffer[1] | (ina219_i2cBuffer[0] << 8); // MSB-LSB bytes
    return value;
}

/*!
 *  Power value in uW, taking into account the config settings and current LSB
 */
uint32_t getPoweruWINA219(void)
{
    uint32_t valueDec = getPowerRawINA219();
    valueDec *= ina219_powerMultiplier_uW;
    return valueDec;
}

/*
 * Milli-scale functions
*/

int getCurrentmAINA219(void)
{
    return getCurrentuAINA219() / 1000;
}

int getPowermWINA219(void)
{
    return getPoweruWINA219() / 1000;
}

void get1000Currents(void){
    warpPrint("Current uA,Power uW,Shunt uV,Bus mV\n");
    for (int i = 0; i < 1000; i++){
        warpPrint("%d,%d,%d,%d\n", getCurrentuAINA219(), getPoweruWINA219(), getShuntVoltageuVINA219(), getBusVoltagemVINA219());
        OSA_TimeDelay(10);
    }
}