#include "config.h"
#include "warp.h"
#include "devMMA8451Q.h"
#include "fsl_gpio_driver.h"

#include "recog.h"

extern volatile WarpI2CDeviceState deviceMMA8451QState;

// Windows
uint8_t window_count = 0;
// Global Data Variables
int16_t AX, AY, AZ = 0;
// 10S Mean probability average (8 windows (7 saved + latest))
int32_t rollingProbability[7] = {0, 0, 0, 0, 0, 0, 0, 0}; // 8 Element Rolling Average least->most recent

// Feature Rollings
// E(X)LP E(Y)LP E(Z)LP
// E(X)HP E(Y)HP E(Z)HP
// VarHPX VarHPY VarHPZ
// MinMaxX MinMaxY MinMax Z

// LP_n = 0.02*X_n + 0.98 * LP_n-1
// 128 LP_n = 2*X_n + 126 * LP_n-1
// HP_n = X_n - LP_n
int16_t X_LP, X_HP = 0;
int16_t Y_LP, Y_HP = 0;
int16_t Z_LP, Z_HP = 0;

int32_t EX_LP, EX_HP, EX_HP2 = 0;
int32_t EY_LP, EY_HP, EY_HP2 = 0;
int32_t EZ_LP, EZ_HP, EZ_HP2 = 0;

int16_t X_min, X_max = 0;
int16_t Y_min, Y_max = 0;
int16_t Z_min, Z_max = 0;

int32_t X_var, Y_var, Z_var = 0;
int32_t X_minmax, Y_minmax, Z_minmax = 0;

// Classifier Parameters
int32_t nij_0_p_X = -9400;
int32_t var_0_X[] = {1337, 10367, 10256, 48, 27, 154, 117472144, 5066724, 232176832, 2351, 397, 509};
int32_t theta_0[] = {-103, 933, 1004, 8, -6, 2, 12711, 2889, 17488, -24, -17, -13};
int32_t nij_1_p_X = -9600;
int32_t var_1_X[] = {1208, 1075, 499, 624, 1073, 471, 43998658, 155047902, 48822305, 1513, 1952, 1518};
int32_t theta_1[] = {-19, 1956, 479, 18, 10, -4, 168208, 269831, 118371, -1077, -994, -740};

// PMAP index/100 corresponds to probabilty bounded by value
const int32_t pmap[] = {-460, -391, -350, -321, -299, -281, -265, -252, -240,       // 0.01 to 0.09
                        -230, -220, -212, -204, -196, -189, -183, -177, -171, -166, // 0.1 to 0.19
                        -160, -156, -151, -146, -142, -138, -134, -130, -127, -123, // 0.20 to 0.29
                        -120, -117, -113, -110, -107, -104, -102, -99, -96, -94,    // 0.30 to 0.39
                        -91, -89, -86, -84, -82, -79, -77, -75, -73, -71,           // 0.40 to 0.49
                        -69, -67, -65, -63, -61, -59, -57, -56, -54, -52,           // 0.50 to 0.59
                        -51, -49, -47, -46, -44, -43, -41, -40, -38, -37,           // 0.60 to 0.69
                        -35, -34, -32, -31, -30, -28, -27, -26, -24, -23,           // 0.70 to 0.79
                        -22, -21, -19, -18, -17, -16, -15, -13, -12, -11,           // 0.80 to 0.89
                        -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, -1, -1};           // 0.90 to 0.99

enum
{
    BLED = GPIO_MAKE_PIN(HW_GPIOB, 13), // blue - on = low
    GLED = GPIO_MAKE_PIN(HW_GPIOB, 11), // Green - on = low
    RLED = GPIO_MAKE_PIN(HW_GPIOB, 10), // red - on = low
};

inline int redOn(void)
{
    GPIO_DRV_ClearPinOutput(RLED);
    return 0;
}
inline int redOff(void)
{
    GPIO_DRV_SetPinOutput(RLED);
    return 0;
}
inline int greenOn(void)
{
    GPIO_DRV_ClearPinOutput(GLED);
    return 0;
}
inline int greenOff(void)
{
    GPIO_DRV_SetPinOutput(GLED);
    return 0;
}
inline int blueOn(void)
{
    GPIO_DRV_ClearPinOutput(BLED);
    return 0;
}
inline int blueOff(void)
{
    GPIO_DRV_SetPinOutput(BLED);
    return 0;
}

int32_t get_rolling_10second(int32_t next_p)
{
    int32_t retval = rollingProbability[0]; // oldest
    retval += next_p;                       // newest

    // shuffle and add all the middles
    for (int i = 0; i < 6; i++)
    {
        rollingProbability[i] = rollingProbability[i + 1];
        retval += rollingProbability[i]; // 6 middle elements
    }
    rollingProbability[6] = next_p;
    return retval / 8;
}

// return the post-point log probability value 0.XX
int32_t pmap_output(int32_t x)
{
    for (int i = 101; i >= 0; i--)
    {
        if (x >= pmap[i])
            return i;
    }
    return 0;
}

#define CA 2
#define CB (128 - CA)

int process_features(void)
{
    // ====== FILTERING =======
    // 128 LP_n = 2*X_n + 126 * LP_n-1
    X_LP = (CA * AX + CB * X_LP) / 128;
    X_HP = AX - X_LP;

    // 128 LP_n = 2*X_n + 126 * LP_n-1
    Y_LP = (CA * AY + CB * Y_LP) / 128;
    Y_HP = AY - Y_LP;

    // 128 LP_n = 2*X_n + 126 * LP_n-1
    Z_LP = (CA * AZ + CB * Z_LP) / 128;
    Z_HP = AZ - Z_LP;

    // ====== ACCUMULATE MEANS =======
    EX_LP += X_LP;
    EX_HP += X_HP;
    EX_HP2 += X_HP * X_HP;
    EY_LP += Y_LP;
    EY_HP += Y_HP;
    EY_HP2 += Y_HP * Y_HP;
    EZ_LP += Z_LP;
    EZ_HP += Z_HP;
    EZ_HP2 += Z_HP * Z_HP;

    // ====== MANAGE MINMAX =======
    if (X_HP > X_max || X_max == 0)
    {
        X_max = X_HP;
    }
    else if (X_HP < X_min || X_min == 0)
    {
        X_min = X_HP;
    }
    if (Y_HP > Y_max || Y_max == 0)
    {
        Y_max = Y_HP;
    }
    else if (Y_HP < Y_min || Y_min == 0)
    {
        Y_min = Y_HP;
    }
    if (Z_HP > Z_max || Z_max == 0)
    {
        Z_max = Z_HP;
    }
    else if (Z_HP < Z_min || Z_min == 0)
    {
        Z_min = Z_HP;
    }

    window_count++;
    return 0;
}

int final_features(void)
{
    // ====== DIVIDE MEANS =======
    EX_LP /= 64;
    EY_LP /= 64;
    EZ_LP /= 64;
    EX_HP /= 64;
    EY_HP /= 64;
    EZ_HP /= 64;
    EX_HP2 /= 64;
    EY_HP2 /= 64;
    EZ_HP2 /= 64;

    // ===== GEN VARIANCES =====
    X_var = EX_HP2 - EX_HP * EX_HP;
    Y_var = EY_HP2 - EY_HP * EY_HP;
    Z_var = EZ_HP2 - EZ_HP * EZ_HP;

    // ===== COMPUTE MINMAX =====
    X_minmax = X_max = X_min;
    Y_minmax = Y_max = Y_min;
    Z_minmax = Z_max = Z_min;

    return 0;
}

int make_prediction(void)
{
    // Generate Prediction

    // Feature Rollings
    // E(X)LP E(Y)LP E(Z)LP
    // E(X)HP E(Y)HP E(Z)HP
    // VarHPX VarHPY VarHPZ
    // MinMaxX MinMaxY MinMax Z
    int32_t X[12] = {EX_LP, EY_LP, EZ_LP,
                     EX_HP, EY_HP, EZ_HP,
                     X_var, Y_var, Z_var,
                     X_minmax, Y_minmax, Z_minmax};

    int32_t n_ij_0 = 0;
    for (int i = 0; i < 12; i++)
    {
        n_ij_0 += ((X[i] - theta_0[i]) * (X[i] - theta_0[i])) / var_0_X[i];
    }
    n_ij_0 = nij_0_p_X - 0.5 * n_ij_0;

    int32_t n_ij_1 = 0;
    for (int i = 0; i < 12; i++)
    {
        n_ij_1 += ((X[i] - theta_1[i]) * (X[i] - theta_1[i])) / var_1_X[i];
    }
    n_ij_1 = nij_1_p_X - 0.5 * n_ij_1;

    int32_t p_walk_log = 0;
    if (n_ij_1 > n_ij_0)
    {
        p_walk_log = n_ij_1 - n_ij_1;
    }
    else
    {
        p_walk_log = n_ij_1 - n_ij_0;
    }

    // Calculate the window probability in decimal (scaled by 100)
    int p_dec = pmap_output(p_walk_log);
    warpPrint("Window: %d: 0.%2d\n", p_walk_log, p_dec);
    // get the 10S Rolling Average
    int rolling = get_rolling_10second(p_dec);
    warpPrint("Rolling: 0.%2d\n", rolling);

    if (p_dec > 90)
    { // 90%
        greenOn();
        redOff();
        blueOff();
    }
    else if (p_dec > 50)
    { // 50%
        blueOn();
        greenOff();
        redOff();
    }
    else if (p_dec > 10)
    { // 10%
        redOn();
        greenOff();
        blueOff();
    }
    else
    { // 0%
        greenOff();
        redOff();
        blueOff();
    }

    if (p_walk_log > -69)
        return 1;
    return 0;
}

void cleanup(void)
{
    X_LP = X_HP = AX;
    Y_LP = Y_HP = AY;
    Z_LP = Z_HP = AZ;

    EX_LP = EX_HP = EX_HP2 = 0;
    EY_LP = EY_HP = EY_HP2 = 0;
    EZ_LP = EZ_HP = EZ_HP2 = 0;

    X_min = X_max = 0;
    Y_min = Y_max = 0;
    Z_min = Z_max = 0;

    X_var = Y_var = Z_var = 0;
    X_minmax = Y_minmax = Z_minmax = 0;
}

int loopAndPrint(void)
{
    while (1)
    {
        // poll the data-ready reg
        readSensorRegisterMMA8451Q(0x00, 1);
        uint8_t dataReady = deviceMMA8451QState.i2cBuffer[0];
        if ((dataReady & 0x07) == 0x07)
        {
            readSensorRegisterMMA8451Q(kWarpSensorOutputRegisterMMA8451QOUT_X_MSB, 6);
            uint16_t MSBX = deviceMMA8451QState.i2cBuffer[0];
            uint16_t LSBX = deviceMMA8451QState.i2cBuffer[1];
            uint16_t MSBY = deviceMMA8451QState.i2cBuffer[2];
            uint16_t LSBY = deviceMMA8451QState.i2cBuffer[3];
            uint16_t MSBZ = deviceMMA8451QState.i2cBuffer[4];
            uint16_t LSBZ = deviceMMA8451QState.i2cBuffer[5];
            int16_t sigmaX = ((MSBX & 0xFF) << 6) | (LSBX >> 2); // combine
            int16_t sigmaY = ((MSBY & 0xFF) << 6) | (LSBY >> 2); // combine
            int16_t sigmaZ = ((MSBZ & 0xFF) << 6) | (LSBZ >> 2); // combine
            AX = (sigmaX ^ (1 << 13)) - (1 << 13);               // sign extend
            AY = (sigmaY ^ (1 << 13)) - (1 << 13);               // sign extend
            AZ = (sigmaZ ^ (1 << 13)) - (1 << 13);               // sign extend
            warpPrint("%d,%d,%d\n", AX, AY, AZ);
        }
        else
        {
            // warpPrint(".\n");
        }
    }
}

int recog_init(void)
{
    // prep I2C Hardware
    initMMA8451Q(0x1D, kWarpDefaultSupplyVoltageMillivoltsMMA8451Q);

    // Configure Dynamic Range
    writeSensorRegisterMMA8451Q(XYZ_DATA_CFG_REG, XYZ_DATA_CFG_FS4G);

    // re-enable the chip
    uint8_t F_SET = F_MODE_DISABLED;
    uint8_t CTRL_SET = CTRL1_ACTIVE | CTRL1_RNORMAL | CTRL1_ODR_50;
    configureSensorMMA8451Q(F_SET, CTRL_SET);

    // check comms with WHOAMI
    warpPrint("Read ID: %d\n", readSensorRegisterMMA8451Q(0x0D, 1));
    uint16_t WHOAMI = deviceMMA8451QState.i2cBuffer[0];
    if (WHOAMI != 0x1A)
    {
        warpPrint("WARN === MMA WHOAMI Failed - 0x%02x\n", WHOAMI);
        return 1;
    }
    warpPrint("MMA init complete - 0x1A\n");
    OSA_TimeDelay(200);

    // Enable LEDs
    PORT_HAL_SetMuxMode(PORTB_BASE, 13u, kPortMuxAsGpio);
    PORT_HAL_SetMuxMode(PORTB_BASE, 11u, kPortMuxAsGpio);
    PORT_HAL_SetMuxMode(PORTB_BASE, 10u, kPortMuxAsGpio);
    redOff();
    greenOff();
    blueOff();

    return 0;
}

int recog(void)
{
    while (1)
    {
        // Has a full window been completed?
        if (window_count == 63)
        {
            window_count = 0;
            // compute final features
            final_features();
            // print features
            // warpPrint("%d %d %d %d %d %d %d %d %d %d %d %d\n", EX_LP, EY_LP, EZ_LP, EX_HP, EY_HP, EZ_HP, X_var, Y_var, Z_var, X_minmax, Y_minmax, Z_minmax);
            // make prediction
            make_prediction();
            // cleanup for next window
            cleanup();
        }

        // poll the data-ready reg for XYZ ready (0x07)
        readSensorRegisterMMA8451Q(0x00, 1);
        if (((uint8_t)deviceMMA8451QState.i2cBuffer[0] & 0x07) == 0x07)
        {
            // read out new data
            readSensorRegisterMMA8451Q(0x01, 6);
            uint16_t MSBX = deviceMMA8451QState.i2cBuffer[0];
            uint16_t LSBX = deviceMMA8451QState.i2cBuffer[1];
            uint16_t MSBY = deviceMMA8451QState.i2cBuffer[2];
            uint16_t LSBY = deviceMMA8451QState.i2cBuffer[3];
            uint16_t MSBZ = deviceMMA8451QState.i2cBuffer[4];
            uint16_t LSBZ = deviceMMA8451QState.i2cBuffer[5];
            int16_t sigmaX = ((MSBX & 0xFF) << 6) | (LSBX >> 2); // combine SB
            int16_t sigmaY = ((MSBY & 0xFF) << 6) | (LSBY >> 2); // combine SB
            int16_t sigmaZ = ((MSBZ & 0xFF) << 6) | (LSBZ >> 2); // combine SB
            AX = (sigmaX ^ (1 << 13)) - (1 << 13);               // sign extend
            AY = (sigmaY ^ (1 << 13)) - (1 << 13);               // sign extend
            AZ = (sigmaZ ^ (1 << 13)) - (1 << 13);               // sign extend
            // incorporate this new data
            process_features();
        }
    }
}
