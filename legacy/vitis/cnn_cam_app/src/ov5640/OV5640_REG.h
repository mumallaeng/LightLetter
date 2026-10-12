/*
 *  OV5640_REG.h
 *
 *  OV5640 register addresses and the MIPI CSI-2 initialisation tables for
 *  the Digilent Pcam 5C on a Zybo Z7-20.
 *
 *  ---------------------------------------------------------------------
 *  WHERE THESE TABLES CAME FROM, AND WHY THEY ARE NOT THE PZ7020 ONES
 *
 *  The PZ7020 project drives the same sensor over an 8 bit DVP bus in
 *  RGB565. The Pcam 5C wires the sensor's MIPI CSI-2 output instead, and
 *  that changes the register set in three places that matter:
 *
 *    0x3034 / 0x3035 / 0x3036 / 0x3037   PLL. MIPI needs a much faster
 *                                        serial clock than DVP does.
 *    0x300E                              MIPI lane count and power down.
 *                                        On DVP this register is left alone.
 *    0x4300 / 0x501F                     output format. DVP path asks for
 *                                        RGB565 (0x6F / 0x01); MIPI path
 *                                        here asks for RAW10 (0x00 / 0x03)
 *                                        because AXI_BayerToRGB in the PL
 *                                        does the demosaic.
 *
 *  So the tables below are Digilent's, converted from their C++ source
 *  (sdk/appsrc/pcam_vdma_hdmi/ov5640/OV5640.h) into plain C arrays. The
 *  comments are theirs and are worth reading - the PLL diagram in
 *  ov5640_cfg_init explains where 672 MHz comes from.
 *
 *  Do not mix these with the PZ7020 tables. Half-applying one on top of the
 *  other leaves the PLL configured for one bus and the output formatter for
 *  the other, and the failure looks like "no data" rather than "wrong data".
 *  ---------------------------------------------------------------------
 */

#ifndef OV5640_REG_H
#define OV5640_REG_H

/*===========================================================================
 *  Registers used directly by the driver
 *===========================================================================*/
#define REG_SYS_CTRL0           0x3008  /* [7] reset, [6] power down        */
#define REG_SYS_CTRL0_PWDN      0x42    /* software power down              */
#define REG_SYS_CTRL0_PWUP      0x02    /* normal operation                 */
#define REG_SYS_CTRL0_RESET     0x82    /* software reset                   */

#define REG_SC_PLL_CTRL0        0x3034
#define REG_SYS_ROOT_DIVIDER    0x3103

#define REG_CHIP_ID_HIGH        0x300A  /* reads 0x56                       */
#define REG_CHIP_ID_LOW         0x300B  /* reads 0x40                       */

#define REG_MIPI_CTRL00         0x300E  /* lane count / power down          */

#define REG_TIMING_TC_REG20     0x3820  /* vertical flip                    */
#define REG_TIMING_TC_REG21     0x3821  /* horizontal mirror                */

#define REG_FORMAT_CONTROL      0x4300  /* output format                    */
#define REG_FORMAT_MUX_CONTROL  0x501F  /* ISP output mux                   */
#define REG_PRE_ISP_TEST_SET1   0x503D  /* test pattern                     */
#define REG_ISP_CONTROL01       0x5001

#define REG_GROUP_ACCESS        0x3212
#define GROUP_START             0x00
#define GROUP_END               0x10
#define GROUP_LAUNCH            0xA0

/*===========================================================================
 *  Initialisation tables
 *
 *  Element type OV5640_RegVal and the terminator OV5640_REG_END are defined
 *  in OV5640.h, which includes this file after them.
 *===========================================================================*/

/* 74 entries  (Digilent cfg_init_) */
static const OV5640_RegVal ov5640_cfg_init[] = {
        //[7]=0 Software reset; [6]=1 Software power down; Default=0x02
        {0x3008, 0x42},
        //[1]=1 System input clock from PLL; Default read = 0x11
        {0x3103, 0x03},
        //[3:0]=0000 MD2P,MD2N,MCP,MCN input; Default=0x00
        {0x3017, 0x00},
        //[7:2]=000000 MD1P,MD1N, D3:0 input; Default=0x00
        {0x3018, 0x00},
        //[6:4]=001 PLL charge pump, [3:0]=1000 MIPI 8-bit mode
        {0x3034, 0x18},

        //              +----------------+        +------------------+         +---------------------+        +---------------------+
        //XVCLK         | PRE_DIV0       |        | Mult (4+252)     |         | Sys divider (0=16)  |        | MIPI divider (0=16) |
        //+-------+-----> 3037[3:0]=0001 +--------> 3036[7:0]=0x38   +---------> 3035[7:4]=0001      +--------> 3035[3:0]=0001      |
        //12MHz   |     | / 1            | 12MHz  | * 56             | 672MHz  | / 1                 | 672MHz | / 1                 |
        //        |     +----------------+        +------------------+         +----------+----------+        +----------+----------+
        //        |                                                                       |                              |
        //        |                                                                       |                      MIPISCLK|672MHz
        //        |                                                                       |                              |
        //        |     +----------------+        +------------------+         +----------v----------+        +----------v----------+
        //        |     | PRE_DIVSP      |        | R_DIV_SP         |         | PLL R divider       |        | MIPI PHY            | MIPI_CLK
        //        +-----> 303d[5:4]=01   +--------> 303d[2]=0 (+1)   |         | 3037[4]=1 (+1)      |        |                     +------->
        //              | / 1.5          |  8MHz  | / 1              |         | / 2                 |        | / 2                 | 336MHz
        //              +----------------+        +---------+--------+         +----------+----------+        +---------------------+
        //                                                  |                             |
        //                                                  |                             |
        //                                                  |                             |
        //              +----------------+        +---------v--------+         +----------v----------+        +---------------------+
        //              | SP divider     |        | Mult             |         | BIT div (MIPI 8/10) |        | SCLK divider        | SCLK
        //              | 303c[3:0]=0x1  +<-------+ 303b[4:0]=0x19   |         | 3034[3:0]=0x8)      +----+---> 3108[1:0]=01 (2^)   +------->
        //              | / 1            | 200MHz | * 25             |         | / 2                 |    |   | / 2                 | 84MHz
        //              +--------+-------+        +------------------+         +----------+----------+    |   +---------------------+
        //                       |                                                        |               |
        //                       |                                                        |               |
        //                       |                                                        |               |
        //              +--------v-------+                                     +----------v----------+    |   +---------------------+
        //              | R_SELD5 div    | ADCCLK                              | PCLK div            |    |   | SCLK2x divider      |
        //              | 303d[1:0]=001  +------->                             | 3108[5:4]=00 (2^)   |    +---> 3108[3:2]=00 (2^)   +------->
        //              | / 1            | 200MHz                              | / 1                 |        | / 1                 | 168MHz
        //              +----------------+                                     +----------+----------+        +---------------------+
        //                                                                                |
        //                                                                                |
        //                                                                                |
        //                                                                     +----------v----------+        +---------------------+
        //                                                                     | P divider (* #lanes)| PCLK   | Scale divider       |
        //                                                                     | 3035[3:0]=0001      +--------> 3824[4:0]           |
        //                                                                     | / 1                 | 168MHz | / 2                 |
        //                                                                     +---------------------+        +---------------------+

        //PLL1 configuration
        //[7:4]=0001 System clock divider /1, [3:0]=0001 Scale divider for MIPI /1
        {0x3035, 0x11},
        //[7:0]=56 PLL multiplier
        {0x3036, 0x38},
        //[4]=1 PLL root divider /2, [3:0]=1 PLL pre-divider /1
        {0x3037, 0x11},
        //[5:4]=00 PCLK root divider /1, [3:2]=00 SCLK2x root divider /1, [1:0]=01 SCLK root divider /2
        {0x3108, 0x01},
        //PLL2 configuration
        //[5:4]=01 PRE_DIV_SP /1.5, [2]=1 R_DIV_SP /1, [1:0]=00 DIV12_SP /1
        {0x303D, 0x10},
        //[4:0]=11001 PLL2 multiplier DIV_CNT5B = 25
        {0x303B, 0x19},

        {0x3630, 0x2e},
        {0x3631, 0x0e},
        {0x3632, 0xe2},
        {0x3633, 0x23},
        {0x3621, 0xe0},
        {0x3704, 0xa0},
        {0x3703, 0x5a},
        {0x3715, 0x78},
        {0x3717, 0x01},
        {0x370b, 0x60},
        {0x3705, 0x1a},
        {0x3905, 0x02},
        {0x3906, 0x10},
        {0x3901, 0x0a},
        {0x3731, 0x02},
        //VCM debug mode
        {0x3600, 0x37},
        {0x3601, 0x33},
        //System control register changing not recommended
        {0x302d, 0x60},
        //??
        {0x3620, 0x52},
        {0x371b, 0x20},
        //?? DVP
        {0x471c, 0x50},

        {0x3a13, 0x43},
        {0x3a18, 0x00},
        {0x3a19, 0xf8},
        {0x3635, 0x13},
        {0x3636, 0x06},
        {0x3634, 0x44},
        {0x3622, 0x01},
        {0x3c01, 0x34},
        {0x3c04, 0x28},
        {0x3c05, 0x98},
        {0x3c06, 0x00},
        {0x3c07, 0x08},
        {0x3c08, 0x00},
        {0x3c09, 0x1c},
        {0x3c0a, 0x9c},
        {0x3c0b, 0x40},

        //[7]=1 color bar enable, [3:2]=00 eight color bar
        {0x503d, 0x00},
        //[2]=1 ISP vflip, [1]=1 sensor vflip
        {0x3820, 0x46},

        //[7:5]=010 Two lane mode, [4]=0 MIPI HS TX no power down, [3]=0 MIPI LP RX no power down, [2]=1 MIPI enable, [1:0]=10 Debug mode; Default=0x58
        {0x300e, 0x45},
        //[5]=0 Clock free running, [4]=1 Send line short packet, [3]=0 Use lane1 as default, [2]=1 MIPI bus LP11 when no packet; Default=0x04
        {0x4800, 0x14},
        {0x302e, 0x08},
        //[7:4]=0x3 YUV422, [3:0]=0x0 YUYV
        //{0x4300, 0x30},
        //[7:4]=0x6 RGB565, [3:0]=0x0 {b[4:0],g[5:3],g[2:0],r[4:0]}
        {0x4300, 0x6f},
        {0x501f, 0x01},

        {0x4713, 0x03},
        {0x4407, 0x04},
        {0x440e, 0x00},
        {0x460b, 0x35},
        //[1]=0 DVP PCLK divider manual control by 0x3824[4:0]
        {0x460c, 0x20},
        //[4:0]=1 SCALE_DIV=INT(3824[4:0]/2)
        {0x3824, 0x01},

        //MIPI timing
        //        {0x4805, 0x10}, //LPX global timing select=auto
        //        {0x4818, 0x00}, //hs_prepare + hs_zero_min ns
        //        {0x4819, 0x96},
        //        {0x482A, 0x00}, //hs_prepare + hs_zero_min UI
        //
        //        {0x4824, 0x00}, //lpx_p_min ns
        //        {0x4825, 0x32},
        //        {0x4830, 0x00}, //lpx_p_min UI
        //
        //        {0x4826, 0x00}, //hs_prepare_min ns
        //        {0x4827, 0x32},
        //        {0x4831, 0x00}, //hs_prepare_min UI

        //[7]=1 LENC correction enabled, [5]=1 RAW gamma enabled, [2]=1 Black pixel cancellation enabled, [1]=1 White pixel cancellation enabled, [0]=1 Color interpolation enabled
        {0x5000, 0x07},
        //[7]=0 Special digital effects, [5]=0 scaling, [2]=0 UV average disabled, [1]=1 Color matrix enabled, [0]=1 Auto white balance enabled
        {0x5001, 0x03},
    { OV5640_REG_END, 0x00 }
};

/* 36 entries  (Digilent cfg_720p_60fps_) */
static const OV5640_RegVal ov5640_cfg_720p_60fps[] = {
//1280 x 720 binned, RAW10, MIPISCLK=280M, SCLK=56Mz, PCLK=56M
        //PLL1 configuration
        //[7:4]=0010 System clock divider /2, [3:0]=0001 Scale divider for MIPI /1
        {0x3035, 0x21},
        //[7:0]=70 PLL multiplier
        {0x3036, 0x46},
        //[4]=0 PLL root divider /1, [3:0]=5 PLL pre-divider /1.5
        {0x3037, 0x05},
        //[5:4]=01 PCLK root divider /2, [3:2]=00 SCLK2x root divider /1, [1:0]=01 SCLK root divider /2
        {0x3108, 0x11},

        //[6:4]=001 PLL charge pump, [3:0]=1010 MIPI 10-bit mode
        {0x3034, 0x1A},

        //[3:0]=0 X address start high byte
        {0x3800, (0 >> 8) & 0x0F},
        //[7:0]=0 X address start low byte
        {0x3801, 0 & 0xFF},
        //[2:0]=0 Y address start high byte
        {0x3802, (8 >> 8) & 0x07},
        //[7:0]=0 Y address start low byte
        {0x3803, 8 & 0xFF},

        //[3:0] X address end high byte
        {0x3804, (2619 >> 8) & 0x0F},
        //[7:0] X address end low byte
        {0x3805, 2619 & 0xFF},
        //[2:0] Y address end high byte
        {0x3806, (1947 >> 8) & 0x07},
        //[7:0] Y address end low byte
        {0x3807, 1947 & 0xFF},

        //[3:0]=0 timing hoffset high byte
        {0x3810, (0 >> 8) & 0x0F},
        //[7:0]=0 timing hoffset low byte
        {0x3811, 0 & 0xFF},
        //[2:0]=0 timing voffset high byte
        {0x3812, (0 >> 8) & 0x07},
        //[7:0]=0 timing voffset low byte
        {0x3813, 0 & 0xFF},

        //[3:0] Output horizontal width high byte
        {0x3808, (1280 >> 8) & 0x0F},
        //[7:0] Output horizontal width low byte
        {0x3809, 1280 & 0xFF},
        //[2:0] Output vertical height high byte
        {0x380a, (720 >> 8) & 0x7F},
        //[7:0] Output vertical height low byte
        {0x380b, 720 & 0xFF},

        //HTS line exposure time in # of pixels
        {0x380c, (1896 >> 8) & 0x1F},
        {0x380d, 1896 & 0xFF},
        //VTS frame exposure time in # lines
        {0x380e, (984 >> 8) & 0xFF},
        {0x380f, 984 & 0xFF},

        //[7:4]=0x3 horizontal odd subsample increment, [3:0]=0x1 horizontal even subsample increment
        {0x3814, 0x31},
        //[7:4]=0x3 vertical odd subsample increment, [3:0]=0x1 vertical even subsample increment
        {0x3815, 0x31},

        //[2]=0 ISP mirror, [1]=0 sensor mirror, [0]=1 horizontal binning
        {0x3821, 0x01},

        //little MIPI shit: global timing unit, period of PCLK in ns * 2(depends on # of lanes)
        {0x4837, 36}, // 1/56M*2

        //Undocumented anti-green settings
        {0x3618, 0x00}, // Removes vertical lines appearing under bright light
        {0x3612, 0x59},
        {0x3708, 0x64},
        {0x3709, 0x52},
        {0x370c, 0x03},

        //[7:4]=0x0 Formatter RAW, [3:0]=0x0 BGBG/GRGR
        {0x4300, 0x00},
        //[2:0]=0x3 Format select ISP RAW (DPC)
        {0x501f, 0x03},
    { OV5640_REG_END, 0x00 }
};

/* 19 entries  (Digilent cfg_advanced_awb_) */
static const OV5640_RegVal ov5640_cfg_awb_advanced[] = {
        // Enable Advanced AWB
        {0x3406 ,0x00},
        {0x5192 ,0x04},
        {0x5191 ,0xf8},
        {0x518d ,0x26},
        {0x518f ,0x42},
        {0x518e ,0x2b},
        {0x5190 ,0x42},
        {0x518b ,0xd0},
        {0x518c ,0xbd},
        {0x5187 ,0x18},
        {0x5188 ,0x18},
        {0x5189 ,0x56},
        {0x518a ,0x5c},
        {0x5186 ,0x1c},
        {0x5181 ,0x50},
        {0x5184 ,0x20},
        {0x5182 ,0x11},
        {0x5183 ,0x00},
        {0x5001 ,0x03},
    { OV5640_REG_END, 0x00 }
};

/* 19 entries  (Digilent cfg_simple_awb_) */
static const OV5640_RegVal ov5640_cfg_awb_simple[] = {
        // Disable Advanced AWB
        {0x518d ,0x00},
        {0x518f ,0x20},
        {0x518e ,0x00},
        {0x5190 ,0x20},
        {0x518b ,0x00},
        {0x518c ,0x00},
        {0x5187 ,0x10},
        {0x5188 ,0x10},
        {0x5189 ,0x40},
        {0x518a ,0x40},
        {0x5186 ,0x10},
        {0x5181 ,0x58},
        {0x5184 ,0x25},
        {0x5182 ,0x11},

        // Enable simple AWB
        {0x3406 ,0x00},
        {0x5183 ,0x80},
        {0x5191 ,0xff},
        {0x5192 ,0x00},
        {0x5001 ,0x03},
    { OV5640_REG_END, 0x00 }
};

/* 1 entries  (Digilent cfg_disable_awb_) */
static const OV5640_RegVal ov5640_cfg_awb_disable[] = {
        {0x5001 ,0x02},
    { OV5640_REG_END, 0x00 }
};

#endif /* OV5640_REG_H */
