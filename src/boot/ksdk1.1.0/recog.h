// FIFO Setup Reg
typedef enum {
    F_MODE_DISABLED = (0 << 6), 
    F_MODE_CIRCULAR = (1 << 6), 
    F_MODE_FILL     = (2 << 6), 
    F_MODE_TRIGGER  = (3 << 6) 
} F_SETUP_CFG;

// Control Reg 1
typedef enum {
    CTRL1_STANDBY = 0,
    CTRL1_ACTIVE  = 1,

    CTRL1_RNORMAL = (0 << 1),
    CTRL1_RFAST   = (1 << 1),

    CTRL1_LNOISE  = (1 << 2),

    CTRL1_ODR_800 = (0 <<  3),
    CTRL1_ODR_400 = (1 <<  3),
    CTRL1_ODR_200 = (2 <<  3),
    CTRL1_ODR_100 = (3 <<  3),
    CTRL1_ODR_50  = (4 <<  3),
    CTRL1_ODR_12  = (5 <<  3),
    CTRL1_ODR_6   = (6 <<  3),
    CTRL1_ODR_1   = (7 <<  3),

    CTRL1_SDR_800 = (0 <<  6),
    CTRL1_SDR_400 = (1 <<  6),
    CTRL1_SDR_200 = (2 <<  6),
    CTRL1_SDR_100 = (3 <<  6)
} CTRL1_CFG;

#define XYZ_DATA_CFG_REG 0x0E
typedef enum {
    XYZ_DATA_CFG_FS2G = 0,
    XYZ_DATA_CFG_FS4G = 1,
    XYZ_DATA_CFG_FS8G = 2,
} XYZ_DATA_CFG;

int recog_init(void);
int recog(void);
int loopAndPrint(void);
int32_t pmap_output(int32_t);
