/*
 * frontend_tztv.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBFRONTEND_TZTV_H_
#define _DVBFRONTEND_TZTV_H_
#if 1 /* Todo : enable later */
#include <linux/dvb/frontend.h>

/* Used for ATV_STATUS , REMOVE CHECK*/ 
typedef enum fe_atv_status {
	FE_ATV_HAS_SIGNAL	= 0x01,
	FE_ATV_HAS_LOCK		= 0x02,
} fe_atv_status_t;

#define DTV_STAT_BER			70
#define DTV_STAT_REFERENCE_BER		71
#define DTV_STAT_BITERRORLEVEL		72
#define DTV_STAT_SQI			73
#define DTV_STAT_SSI			74
#define DTV_STAT_CELLID			75
#define DTV_STAT_PKT_ERR 		76
#define DTV_LOCK_TIME 			77
#define DTV_SYNC_TIME 			78
#define DTV_SCAN_MODE 			79
#define DTV_STREAM_ID_LIST 		80

/* Satellite setting */
#define DTV_SAT_POSITIONER		81
#define DTV_LNB_HI_OSC			82
#define DTV_LNB_LOW_OSC			83
#define DTV_DISEQC_MODE			84
#define DTV_SAT_LINECOMPENSATION	85
#define DTV_SAT_BW_FREQ			86
#define DTV_SATCR_CONFIG		87
#define DTV_SEND_POSITIONER_COMMAND	88
#define DTV_SEND_SATCR_COMMAND		89
#define DTV_LNB_POWER			90

#define ATV_PARAMS			91
#define ATV_STATUS			92
#define ATV_RF_STRENGTH			93

#define DTV_STAT_SQI_MARGIN		94
#define DTV_STAT_SSI_MARGIN		95

/* REMOVE CHECK  ~~ */
/*  Values for the 'audmode' field */
enum fe_audmode{
	FE_TUNER_MODE_MONO        = 0x0000,
	FE_TUNER_MODE_STEREO      = 0x0001,
	FE_TUNER_MODE_LANG2       = 0x0002,
	FE_TUNER_MODE_SAP         = 0x0002,
	FE_TUNER_MODE_LANG1       = 0x0003,
	FE_TUNER_MODE_LANG1_LANG2 = 0x0004,
};



/*
 *      A N A L O G   V I D E O   S T A N D A R D
 */

enum fe_analog_tv_standard {
	/* one bit for each */
	FE_STD_PAL_B          = 0x00000001,
	FE_STD_PAL_B1         = 0x00000002,
	FE_STD_PAL_G          = 0x00000004,
	FE_STD_PAL_H          = 0x00000008,
	FE_STD_PAL_I          = 0x00000010,
	FE_STD_PAL_D          = 0x00000020,
	FE_STD_PAL_D1         = 0x00000040,
	FE_STD_PAL_K          = 0x00000080,

	FE_STD_PAL_M          = 0x00000100,
	FE_STD_PAL_N          = 0x00000200,
	FE_STD_PAL_Nc         = 0x00000400,
	FE_STD_PAL_60         = 0x00000800,

	FE_STD_NTSC_M         = 0x00001000,  /* BTSC */
	FE_STD_NTSC_M_JP      = 0x00002000,  /* EIA-J */
	FE_STD_NTSC_443       = 0x00004000,
	FE_STD_NTSC_M_KR      = 0x00008000,  /* FM A2 */

	FE_STD_SECAM_B        = 0x00010000,
	FE_STD_SECAM_D        = 0x00020000,
	FE_STD_SECAM_G        = 0x00040000,
	FE_STD_SECAM_H        = 0x00080000,
	FE_STD_SECAM_K        = 0x00100000,
	FE_STD_SECAM_K1       = 0x00200000,
	FE_STD_SECAM_L        = 0x00400000,
	FE_STD_SECAM_LC       = 0x00800000,

	/* ATSC/HDTV */
	FE_STD_ATSC_8_VSB     = 0x01000000,
	FE_STD_ATSC_16_VSB    = 0x02000000,
};


#define FE_STD_NTSC           (FE_STD_NTSC_M    |\
	FE_STD_NTSC_M_JP     |\
	FE_STD_NTSC_M_KR)
/* Secam macros */
#define FE_STD_SECAM_DK       (FE_STD_SECAM_D   |\
	FE_STD_SECAM_K   |\
	FE_STD_SECAM_K1)
/* All Secam Standards */
#define FE_STD_SECAM      (FE_STD_SECAM_B   |\
	FE_STD_SECAM_G   |\
	FE_STD_SECAM_H   |\
	FE_STD_SECAM_DK  |\
	FE_STD_SECAM_L       |\
	FE_STD_SECAM_LC)
/* PAL macros */
#define FE_STD_PAL_BG     (FE_STD_PAL_B     |\
	FE_STD_PAL_B1    |\
	FE_STD_PAL_G)
#define FE_STD_PAL_DK     (FE_STD_PAL_D     |\
	FE_STD_PAL_D1    |\
	FE_STD_PAL_K)
/*
 * "Common" PAL - This macro is there to be compatible with the old
 * V4L1 concept of "PAL": /BGDKHI.
 * Several PAL standards are mising here: /M, /N and /Nc
 */
#define FE_STD_PAL        (FE_STD_PAL_BG    |\
	FE_STD_PAL_DK    |\
	FE_STD_PAL_H     |\
	FE_STD_PAL_I)
/* Chroma "agnostic" standards */
#define FE_STD_B      (FE_STD_PAL_B     |\
	FE_STD_PAL_B1    |\
	FE_STD_SECAM_B)
#define FE_STD_G      (FE_STD_PAL_G     |\
	FE_STD_SECAM_G)
#define FE_STD_H      (FE_STD_PAL_H     |\
	FE_STD_SECAM_H)
#define FE_STD_L      (FE_STD_SECAM_L   |\
	FE_STD_SECAM_LC)
#define FE_STD_GH     (FE_STD_G     |\
	FE_STD_H)
#define FE_STD_DK     (FE_STD_PAL_DK    |\
	FE_STD_SECAM_DK)
#define FE_STD_BG     (FE_STD_B     |\
	FE_STD_G)
#define FE_STD_MN     (FE_STD_PAL_M     |\
	FE_STD_PAL_N     |\
	FE_STD_PAL_Nc    |\
	FE_STD_NTSC)

/* Standards where MTS/BTSC stereo could be found */
#define FE_STD_MTS        (FE_STD_NTSC_M    |\
	FE_STD_PAL_M     |\
	FE_STD_PAL_N     |\
	FE_STD_PAL_Nc)

/* Standards for Countries with 60Hz Line frequency */
#define FE_STD_525_60     (FE_STD_PAL_M     |\
	FE_STD_PAL_60    |\
	FE_STD_NTSC      |\
	FE_STD_NTSC_443)
/* Standards for Countries with 50Hz Line frequency */
#define FE_STD_625_50     (FE_STD_PAL       |\
	FE_STD_PAL_N     |\
	FE_STD_PAL_Nc    |\
	FE_STD_SECAM)

#define FE_STD_ATSC           (FE_STD_ATSC_8_VSB    |\
	FE_STD_ATSC_16_VSB)
/* Macros with none and all analog standards */
#define FE_STD_UNKNOWN        0
#define FE_STD_ALL            (FE_STD_525_60    |\
	FE_STD_625_50)


typedef struct fe_analog_params
{
	unsigned int frequency;
	unsigned int audmode;
	__u64 std;
}fe_analog_params_t;

/* ~~ REMOVE CHECK  */
struct fe_dbg_cmd {
	__u32 	id;
	__s32	buf[8];
};

#define FE_DBG_CMD_RF_PWR_TEST				(0x01) /* buf[0] - test result */
#define FE_DBG_CMD_SET_RF_PWR				(0x02)
#define FE_DBG_CMD_CHK_PLL_LOCK_T			(0x03) /* buf[0] - check result, -1 failed */
#define FE_DBG_CMD_CHK_PLL_LOCK_S			(0x04) /* buf[0] - check result, -1 failed */

/* 
 * For FE_DBG_CMD_GET_RF_TOP
 * buf[0] -- Tune type, 0:DTV, 1:ATV.
 * buf[1] -- Take over point value(For si2190 dirver, refer to si2190_priv.h, DTV_RF_TOP_xxx, ATV_RF_TOP_xxx)
 */
#define FE_DBG_CMD_GET_RF_TOP				       (0x05) 
#define FE_DBG_CMD_SET_RF_TOP				       (0x06) 
/* 
 * For FE_DBG_CMD_GET_RF_TOP_VALUE
 * buf[0] -- DTV RF TOP M2DB
 * buf[1] -- ATV RF TOP M2DB
 */
#define FE_DBG_CMD_GET_RF_TOP_M2DB			(0x07) /* buf[0]: DTV_RF_TOP_M2DB, buf[1]: ATV_RF_TOP_M2DB */
#define FE_DBG_CMD_GET_TUNER_MARGIN			(0x08)
#define FE_DBG_CMD_CHK_RF_PWR_STATUS        (0x09) /* buf[0] - check result */

//factory command(tuner).
#define FE_DBG_CMD_SVC_X_TAL_START			(0x0101)/* buf[0] - output value *///-> done
#define FE_DBG_CMD_RM_BIST_ATV				(0x0102)/* buf[0] - intput value *///-> done
#define FE_DBG_CMD_RM_BIST_DTV				(0x0103)/* buf[0] - intput value *///-> done
#define FE_DBG_CMD_RM_BIST_CABLE				(0x0104)/* buf[0] - intput value *///-> done
//factory & test command
#define FE_DBG_CMD_SET_AGC_SPEED				(0x0105) /* buf[0] - input value,  */
/* 
 * Get tuner init status.
 * buf[0] -- 1: init successfule, 0: init failed.
 */
#define FE_DBG_CMD_CHK_TUNER_INIT			(0x0106) /* buf[0] - check result */
#define FE_DBG_CMD_GET_TUNER_TYPE           (0x0107)
/*
 *	buf[0] = 
 *	UNKNOWN = 0
 * 	S_TC = 1
 * 	S_TCS2,
 * 	S_T2CS2,
 * 	S_T2C,
 * 	S_T2CS2_G,
 * 	S_T2C_G,
 *	D_T2CS2,
 *	D_TCS2,
 * 	D_TC,
 * 	S_ISDB,
 * 	S_T2C_COL,
 * 	S_DTMB,
 * 	TYPE_MAX
 */

//factory command(demodulater)
#define FE_DBG_CMD_TS_CLOCK_DELAY_TC			(0x0201)//eu model
#define FE_DBG_CMD_TS_CLOCK_DELAY_S			(0x0202)//eu model

// tuner get config
#define FE_DBG_CMD_GET_SUPPORT_RF_TYPE		(0x0301) /* 0 : DTV&ATV, 1 : DTV only, 2 : ATV only */
#define FE_DBG_CMD_GET_SUPPORT_AUTO_T2		(0x0302) /* Auto Store시에 T와 T2에 대한 Auto Detect가 가능한지 여부. */
#define FE_DBG_CMD_GET_T2_AUTOSIGNAL_TYPE	(0x0303)
#define FE_DBG_CMD_GET_SUPPORT_CROSSTALK_NOISE_REDUCTION (0x0304) /* For Analog Channel Clean View. return : 0 - Not supported , 1 - Supported. */
#define FE_DBG_CMD_SET_CROSSTALK_NOISE_REDUCTION     (0x0305) /* Set Crosstalk Noise Reduction true : On, false : Off */
#define FE_DBG_CMD_GET_TMCC_PRESET_DATA              (0x0306) /* Get TMCC Preset Data of demodulator */
#define FE_DBG_CMD_SET_TMCC_PRESET_DATA              (0x0307) /* Set TMCC Preset Data to demodulator */
#define FE_DBG_CMD_GET_TMCC_DATA                     (0x0308) /* Get TMCC Data of demodulator */
#define FE_DBG_CMD_GET_DETUNING_500K                 (0x0309) /* 1 : Need -500Hz Detuning, 0 : No Need */
#define FE_DBG_CMD_GET_SLOPE_CHECK                   (0x0310) /* 1 : Need to check the Slope of AFT Level , 0 : No Need */
#define FE_DBG_CMD_GET_SUPPORT_AUTOQAM               (0x0311) /* 1 : Support to detect 256 QAM and 64QAM  , 0 : Not Support */
#define FE_DBG_CMD_GET_DTV_LOCKCHECK                 (0x0312) 
/*
 *   buf[0] = 
 *	LOCK_CHECK_TYPE_NONE   = 0x00,
 * 	LOCK_CHECK_TYPE_SYNC   = 0x01,
 *	LOCK_CHECK_TYPE_MASTER = 0x02,
 *	LOCK_CHECK_TYPE_BOTH   = 0x03,
 */
#define FE_DBG_CMD_SET_TSID                          (0x0313) /* Set TSID of tuner */
#define FE_DBG_CMD_SET_TS_DELAY                      (0x0314) /* Set TS Delay of tuner */
#define FE_DBG_CMD_SET_TS_CLOCK_MARGIN1              (0x0315) /* Set TS Delay of tuner */
#define FE_DBG_CMD_SET_TS_CLOCK_MARGIN2              (0x0316) /* Set TS Delay of tuner */
#define FE_DBG_CMD_GET_SATCR_STATE                   (0x0318) 
/*
   buf[0] = slotNumber; [1-8]
   buf[1] = SatCrFreq; [MHz]
   buf[2] = ucProgress [0-100]
 */
#define FE_DBG_CMD_GET_OFFSET_INFO                   (0x0319)
/*
 *	buf[0]=
 *   OFFSET_RANGE_500K  = 0x00,
 *	OFFSET_RANGE_1M    = 0x01,
 *	OFFSET_RANGE_2M    = 0x02,
 */

#define FE_DBG_CMD_GET_NUM_OF_PLPID					(0x0320)	/* get num of mPLP id */
#define FE_DBG_CMD_GET_PLPID						(0x0321)	/* get mPLP id list */
#define FE_DBG_CMD_SET_SLEEP					(0x0322)	/* set demod sleep */
#define FE_DBG_CMD_SET_TSMUTE					(0x0323)	/* set TS Mute */
#define FE_DBG_CMD_GET_TUNER_VERSION			(0x0324)	/* get Tuner Version */
#define FE_DBG_CMD_SET_SAT_PATH					(0x0325)	/* set Satellite Path */
#define FE_DBG_CMD_GET_TYPE_AUTOSEARCH			(0x0326)	/* get Type Auto search */
#define FE_DBG_CMD_GET_CURRENT_SIGNAL_KIND		(0x0327)	/* get Current Signal Kind */
#define FE_DBG_CMD_GET_DEMODULATION_TYPE		(0x0328)	/* get Demod Type */
#define FE_DBG_CMD_GET_TMCC_EWBS				 (0x0329) /* Get EWBS flag */
#define FE_DBG_CMD_CHECK_SIGNAL_RSSI			 (0x0330) /* check rssi value to know RF cable in/out */
#define FE_DBG_CMD_CHECK_RESUME_DONE			 (0x0331) /* check resume done for getting lock status */
#define FE_DBG_CMD_SET_EWBS_FREQ				 (0x0332) /* set frequency for ewbs duing suspend */

/* ATSC 3.0 */
#define FE_DBG_CMD_SET_ADD_PLP_ID			 (0x0333) /* add plp id */
#define FE_DBG_CMD_SET_DEL_PLP_ID			 (0x0334) /* del plp id */
#define FE_DBG_CMD_SET_PLP_LIST				 (0x0335) /* set plp list */
#define FE_DBG_CMD_GET_SLT_FLAG			 (0x0336) /* get slt flag */
#define FE_DBG_CMD_GET_L1_TIME_DATA		 (0x0337) /* get l1 time data */

#define FE_DBG_CMD_CHECK_ONE_SIGNAL_RSSI	 (0x0340) /* check rssi value to determine tune or skip */
#define FE_DBG_CMD_CHECK_ANTENNA_CONNECTION	 (0x0341) /* check antenna connection by reading GPIO */

#define FE_SEND_DBG_CMD _IOR('o', 81, struct fe_dbg_cmd) /* Debug and factory commands, driver defined private. */
#endif
#endif
