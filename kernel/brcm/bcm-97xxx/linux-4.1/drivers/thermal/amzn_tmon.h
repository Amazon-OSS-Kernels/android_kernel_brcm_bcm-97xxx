/***************************************************************************
 * Copyright 2017 Amazon Technologies, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License Version 2. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 ***************************************************************************/

#ifndef AMZN_TMON_H__
#define AMZN_TMON_H__

#define DRIVER_NAME			"amzn_thermistor"
#define THERMAL_NAME			"Thermistor"
#define HUNDRED_MS_SCAN_INTERVAL	0x0a
#define THERMISTOR_BASE_ADDR 		0xF040B100
#define THERMISTOR_BASE_ADDR_SIZE	0x58
#define THERMISTOR_R_VAL		47500
#define MIN_SCAN_INTERVAL		9
#define TEMP_CRIT 			80000

struct thermistor_thermal_data {
	uint32_t which;
};

struct thermistor_device_data {
	uint32_t	top;
	uint32_t	bottom;
	uint32_t	mid1;
	uint32_t	mid0;
	int             temp0;
	int             temp1;
	struct timespec update_ts; //last update time stamp
};

struct thermistor_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct bcm_thermal_platform_data *pdata;
	struct thermal_dev *enclosure_therm_fw;
	struct thermal_dev *fconnector_therm_fw;
};

/***************************************************************************
 *TMON - Temperature Monitor
 ***************************************************************************/
#define AMZN_TMON_AFE_CTRL                       0x00000000 /* Control Register */
#define AMZN_TMON_TOP_DATA                       0x00000004 /* TOP Data Register */
#define AMZN_TMON_BOT_DATA                       0x00000008 /* BOT Data Register */
#define AMZN_TMON_MID1_DATA                      0x0000000c /* MID1 Data Register */
#define AMZN_TMON_MID0_DATA                      0x00000010 /* MID0 Data Register */
#define AMZN_TMON_VCTAT_DATA                     0x00000014 /* VCTAT Data Register */
#define AMZN_TMON_GEN_CTRL                       0x00000018 /* General Control Register */
#define AMZN_TMON_STATUS                         0x0000001c /* Status Register */
#define AMZN_TMON_TOP_UPPER_BOUNDS               0x00000020 /* TOP UPPER Bounds Register */
#define AMZN_TMON_TOP_LOWER_BOUNDS               0x00000024 /* TOP LOWER Bounds Register */
#define AMZN_TMON_BOT_UPPER_BOUNDS               0x00000028 /* BOT UPPER Bounds Register */
#define AMZN_TMON_BOT_LOWER_BOUNDS               0x0000002c /* BOT LOWER Bounds Register */
#define AMZN_TMON_MID1_UPPER_BOUNDS              0x00000030 /* MID1 UPPER Bounds Register */
#define AMZN_TMON_MID1_LOWER_BOUNDS              0x00000034 /* MID1 LOWER Bounds Register */
#define AMZN_TMON_MID0_UPPER_BOUNDS              0x00000038 /* MID0 UPPER Bounds Register */
#define AMZN_TMON_MID0_LOWER_BOUNDS              0x0000003c /* MID0 LOWER Bounds Register */
#define AMZN_TMON_VCTAT_UPPER_BOUNDS             0x00000040 /* VCTAT UPPER Bounds Register */
#define AMZN_TMON_VCTAT_LOWER_BOUNDS             0x00000044 /* VCTAT LOWER Bounds Register */
#define AMZN_TMON_TEST_CTRL_1                    0x00000048 /* Test Control 1 Register */
#define AMZN_TMON_TEST_CTRL_2                    0x0000004c /* Test Control 2 Register */
#define AMZN_TMON_SPARE                          0x00000050 /* Spare Register */
#define AMZN_TMON_REVID                          0x00000054 /* Revision ID */
#define AMZN_ADDR_MASK                           0X000000FF
#define AMZN_TMON_UPPER_LIMIT			 0xfffffffe/* Reset upper bound */
#define AMZN_TMON_LOWER_LIMIT 			 0x00000001/* Reset lower bound */


/* TMON :: AFE_CTRL :: adc_pwrdn [01:01] */
#define AMZN_TMON_AFE_CTRL_adc_pwrdn_MASK                          0x00000002
#define AMZN_TMON_AFE_CTRL_adc_pwrdn_SHIFT                         1
#define AMZN_TMON_AFE_CTRL_adc_pwrdn_DEFAULT                       0x00000001


/* TMON :: TOP_DATA :: top [27:00] */
#define AMZN_TMON_TOP_DATA_top_MASK                                0x0fffffff
#define AMZN_TMON_TOP_DATA_top_SHIFT                               0
#define AMZN_TMON_TOP_DATA_top_DEFAULT                             0x00000000

/* TMON :: BOT_DATA :: bot [27:00] */
#define AMZN_TMON_BOT_DATA_bot_MASK                                0x0fffffff
#define AMZN_TMON_BOT_DATA_bot_SHIFT                               0
#define AMZN_TMON_BOT_DATA_bot_DEFAULT                             0x00000000

/* TMON :: MID1_DATA :: mid1 [27:00] */
#define AMZN_TMON_MID1_DATA_mid1_MASK                              0x0fffffff
#define AMZN_TMON_MID1_DATA_mid1_SHIFT                             0
#define AMZN_TMON_MID1_DATA_mid1_DEFAULT                           0x00000000

/* TMON :: MID0_DATA :: mid0 [27:00] */
#define AMZN_TMON_MID0_DATA_mid0_MASK                              0x0fffffff
#define AMZN_TMON_MID0_DATA_mid0_SHIFT                             0
#define AMZN_TMON_MID0_DATA_mid0_DEFAULT                           0x00000000

/* TMON :: GEN_CTRL :: enable [07:07] */
#define AMZN_TMON_GEN_CTRL_enable_MASK                             0x00000080
#define AMZN_TMON_GEN_CTRL_enable_SHIFT                            7
#define AMZN_TMON_GEN_CTRL_enable_DEFAULT                          0x00000000

/* TMON :: GEN_CTRL :: measurement_interval [06:00] */
#define AMZN_TMON_GEN_CTRL_measurement_interval_MASK               0x0000007f
#define AMZN_TMON_GEN_CTRL_measurement_interval_SHIFT              0
#define AMZN_TMON_GEN_CTRL_measurement_interval_DEFAULT            0x0000000a

/* TMON :: STATUS :: meas_interval_err [02:02] */
#define AMZN_TMON_STATUS_meas_interval_err_MASK                    0x00000004
#define AMZN_TMON_STATUS_meas_interval_err_SHIFT                   2

/* TMON :: STATUS :: out_of_bounds [01:01] */
#define AMZN_TMON_STATUS_out_of_bounds_MASK                        0x00000002
#define AMZN_TMON_STATUS_out_of_bounds_SHIFT                       1
#define AMZN_TMON_STATUS_out_of_bounds_DEFAULT                     0x00000000

/* TMON :: STATUS :: data_ready [00:00] */
#define AMZN_TMON_STATUS_data_ready_MASK                           0x00000001
#define AMZN_TMON_STATUS_data_ready_SHIFT                          0
#define AMZN_TMON_STATUS_data_ready_DEFAULT                        0x00000000



typedef struct murata_table {
	int temperature;
	int rlow;
	int rcenter;
	int rhigh;
}murata_table_t;

murata_table_t murata_table[] =
//TEMP.,		R-low,	R-center (nominal),	R-high
//(deg.C),	(k ohm),	(k ohm),	(k ohm)
//========,	========,	========,	========
{
{-40000,	4010824,	4205686,	4409573},
{-39000,	3739106,	3917989,	4105020},
{-38000,	3487620,	3651903,	3823541},
{-37000,	3254729,	3405663,	3563240},
{-36000,	3038939,	3177663,	3322387},
{-35000,	2838884,	2966435,	3099407},
{-34000,	2653318,	2770639,	2892858},
{-33000,	2481100,	2589050,	2701427},
{-32000,	2321185,	2420548,	2523912},
{-31000,	2172617,	2264107,	2359214},
{-30000,	2034520,	2118789,	2206327},
{-29000,	1906091,	1983734,	2064332},
{-28000,	1786594,	1858153,	1932385},
{-27000,	1675351,	1741323,	1809713},
{-26000,	1571743,	1632582,	1695607},
{-25000,	1475197,	1531319,	1589416},
{-24000,	1385010,	1436785,	1490346},
{-23000,	1300909,	1348686,	1398078},
{-22000,	1222448,	1266547,	1312106},
{-21000,	1149212,	1189927,	1231961},
{-20000,	1080823,	1118422,	1157212},
{-19000,	1016931,	1051660,	1087465},
{-18000,	 957214,	 989298,	1022355},
{-17000,	 901372,	 931019,	 961545},
{-16000,	 849133,	 876533,	 904726},
{-15000,	 800241,	 825569,	 851613},
{-14000,	 754464,	 777880,	 801942},
{-13000,	 711584,	 733235,	 755470},
{-12000,	 671400,	 691423,	 711971},
{-11000,	 633728,	 652247,	 671240},
{-10000,	 598397,	 615526,	 633082},
{ -9000,	 565197,	 581041,	 597270},
{ -8000,	 534041,	 548697,	 563699},
{ -7000,	 504789,	 518347,	 532216},
{ -6000,	 477314,	 489858,	 502680},
{ -5000,	 451499,	 463104,	 474959},
{ -4000,	 427263,	 438001,	 448964},
{ -3000,	 404470,	 414406,	 424543},
{ -2000,	 383025,	 392218,	 401591},
{ -1000,	 362842,	 371347,	 380013},
{     0,	 343837,	 351706,	 359719},
{  1000,	 325919,	 333197,	 340604},
{  2000,	 309037,	 315769,	 322615},
{  3000,	 293128,	 299353,	 305680},
{  4000,	 278129,	 283885,	 289731},
{  5000,	 263983,	 269304,	 274705},
{  6000,	 250637,	 255555,	 260544},
{  7000,	 238042,	 242587,	 247193},
{  8000,	 226151,	 230349,	 234602},
{  9000,	 214920,	 218798,	 222723},
{ 10000,	 204311,	 207890,	 211511},
{ 11000,	 194284,	 197587,	 200927},
{ 12000,	 184805,	 187852,	 190931},
{ 13000,	 175841,	 178651,	 181487},
{ 14000,	 167361,	 169951,	 172563},
{ 15000,	 159337,	 161722,	 164126},
{ 16000,	 151738,	 153933,	 156145},
{ 17000,	 144544,	 146563,	 148596},
{ 18000,	 137730,	 139586,	 141453},
{ 19000,	 131275,	 132980,	 134693},
{ 20000,	 125158,	 126722,	 128293},
{ 21000,	 119360,	 120793,	 122232},
{ 22000,	 113861,	 115174,	 116490},
{ 23000,	 108646,	 109846,	 111049},
{ 24000,	 103697,	 104793,	 105891},
{ 25000,	  99000,	 100000,	 101000},
{ 26000,	  94452,	  95451,	  96450},
{ 27000,	  90137,	  91132,	  92129},
{ 28000,	  86042,	  87032,	  88025},
{ 29000,	  82155,	  83138,	  84125},
{ 30000,	  78463,	  79439,	  80418},
{ 31000,	  74957,	  75923,	  76894},
{ 32000,	  71626,	  72582,	  73543},
{ 33000,	  68460,	  69405,	  70355},
{ 34000,	  65451,	  66383,	  67322},
{ 35000,	  62589,	  63509,	  64436},
{ 36000,	  59867,	  60774,	  61688},
{ 37000,	  57278,	  58170,	  59071},
{ 38000,	  54813,	  55692,	  56579},
{ 39000,	  52467,	  53331,	  54204},
{ 40000,	  50234,	  51083,	  51942},
{ 41000,	  48108,	  48942,	  49786},
{ 42000,	  46083,	  46902,	  47731},
{ 43000,	  44154,	  44958,	  45772},
{ 44000,	  42315,	  43104,	  43903},
{ 45000,	  40562,	  41336,	  42119},
{ 46000,	  38889,	  39647,	  40416},
{ 47000,	  37293,	  38036,	  38789},
{ 48000,	  35770,	  36498,	  37236},
{ 49000,	  34317,	  35030,	  35753},
{ 50000,	  32930,	  33628,	  34336},
{ 51000,	  31608,	  32290,	  32984},
{ 52000,	  30345,	  31013,	  31692},
{ 53000,	  29139,	  29792,	  30456},
{ 54000,	  27987,	  28625,	  29275},
{ 55000,	  26886,	  27510,	  28146},
{ 56000,	  25833,	  26443,	  27065},
{ 57000,	  24827,	  25423,	  26032},
{ 58000,	  23865,	  24448,	  25042},
{ 59000,	  22944,	  23514,	  24096},
{ 60000,	  22064,	  22621,	  23189},
{ 61000,	  21222,	  21765,	  22321},
{ 62000,	  20415,	  20946,	  21489},
{ 63000,	  19644,	  20162,	  20693},
{ 64000,	  18905,	  19411,	  19929},
{ 65000,	  18197,	  18692,	  19198},
{ 66000,	  17520,	  18003,	  18498},
{ 67000,	  16872,	  17344,	  17827},
{ 68000,	  16251,	  16712,	  17184},
{ 69000,	  15656,	  16106,	  16566},
{ 70000,	  15085,	  15524,	  15974},
{ 71000,	  14537,	  14965,	  15405},
{ 72000,	  14011,	  14429,	  14858},
{ 73000,	  13506,	  13914,	  14333},
{ 74000,	  13022,	  13420,	  13830},
{ 75000,	  12557,	  12946,	  13346},
{ 76000,	  12113,	  12492,	  12882},
{ 77000,	  11686,	  12056,	  12437},
{ 78000,	  11276,	  11637,	  12009},
{ 79000,	  10882,	  11235,	  11598},
{ 80000,	  10504,	  10848,	  11203},
{ 81000,	  10141,	  10477,	  10823},
{ 82000,	   9792,	  10120,	  10458},
{ 83000,	   9456,	   9776,	  10106},
{ 84000,	   9134,	   9446,	   9768},
{ 85000,	   8824,	   9129,	   9443},
{ 86000,	   8525,	   8823,	   9130},
{ 87000,	   8239,	   8529,	   8829},
{ 88000,	   7963,	   8246,	   8539},
{ 89000,	   7697,	   7974,	   8260},
{ 90000,	   7442,	   7712,	   7992},
{ 91000,	   7197,	   7461,	   7734},
{ 92000,	   6961,	   7219,	   7485},
{ 93000,	   6734,	   6986,	   7246},
{ 94000,	   6516,	   6761,	   7016},
{ 95000,	   6305,	   6545,	   6793},
{ 96000,	   6102,	   6336,	   6578},
{ 97000,	   5906,	   6134,	   6371},
{ 98000,	   5717,	   5940,	   6171},
{ 99000,	   5535,	   5752,	   5978},
{100000,	   5359,	   5572,	   5792},
{101000,	   5190,	   5398,	   5614},
{102000,	   5028,	   5231,	   5441},
{103000,	   4871,	   5069,	   5275},
{104000,	   4720,	   4914,	   5115},
{105000,	   4574,	   4763,	   4960},
{106000,	   4434,	   4618,	   4810},
{107000,	   4298,	   4478,	   4666},
{108000,	   4167,	   4343,	   4526},
{109000,	   4040,	   4212,	   4391},
{110000,	   3918,	   4086,	   4261},
{111000,	   3800,	   3965,	   4136},
{112000,	   3686,	   3847,	   4014},
{113000,	   3576,	   3733,	   3897},
{114000,	   3470,	   3624,	   3783},
{115000,	   3368,	   3517,	   3674},
{116000,	   3269,	   3415,	   3568},
{117000,	   3174,	   3317,	   3466},
{118000,	   3081,	   3221,	   3367},
{119000,	   2992,	   3129,	   3272},
{120000,	   2906,	   3040,	   3179},
{121000,	   2822,	   2953,	   3090},
{122000,	   2741,	   2869,	   3003},
{123000,	   2663,	   2788,	   2918},
{124000,	   2587,	   2709,	   2837},
{125000,	   2514,	   2633,	   2758},
};

#endif /* #ifndef AMZN_TMON_H__ */

/* End of File */
