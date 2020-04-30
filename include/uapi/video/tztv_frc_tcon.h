#ifndef __UAPI__TZTV__FRC__TCON__H__
#define __UAPI__TZTV__FRC__TCON__H__

#ifdef  __cplusplus
extern "C" {
#endif

#define FRC_IOCTL_BASE			'f'
#define FRC_IO(nr)			_IO(FRC_IOCTL_BASE, nr)
#define FRC_IOR(nr, type)		_IOR(FRC_IOCTL_BASE, nr, type)
#define FRC_IOW(nr, type)		_IOW(FRC_IOCTL_BASE, nr, type)
#define FRC_IOWR(nr, type)		_IOWR(FRC_IOCTL_BASE, nr, type)

#define TCON_IOCTL_BASE			't'
#define TCON_IO(nr)			_IO(TCON_IOCTL_BASE, nr)
#define TCON_IOR(nr, type)		_IOR(TCON_IOCTL_BASE, nr, type)
#define TCON_IOW(nr, type)		_IOW(TCON_IOCTL_BASE, nr, type)
#define TCON_IOWR(nr, type)		_IOWR(TCON_IOCTL_BASE, nr, type)

#define FRC_EW_IOCTL_BASE			'e'
#define FRC_EW_IO(nr)				_IO(FRC_EW_IOCTL_BASE, nr)
#define FRC_EW_IOR(nr, type)		_IOR(FRC_EW_IOCTL_BASE, nr, type)
#define FRC_EW_IOW(nr, type)		_IOW(FRC_EW_IOCTL_BASE, nr, type)
#define FRC_EW_IOWR(nr, type)		_IOWR(FRC_EW_IOCTL_BASE, nr, type)

#define EXTENDED_NUMBER_OF_LDCC_MAP_SIZE	8
#define EXTENDED_NUMBER_OF_LOCAL_AREA		48

/*
 *********************** STRUCT FOR FRC IOCTLs ***************************
 */
struct fi_TDRect_t {
	int x;
	int y;
	int width;
	int height;
};

struct fi_CD3dControlInfo_t {
	int e3dMode;
	int bGameModeOnOff;
	int bInternalTest;
	int eSourceMode;
	int b3DFlickerless;
	int e3dFormat;
	int bPcMode;
	int eFilmMode;
	int eAutoMotionMode;
	int eDetected3dMode;
	int h_3d_resolution;
	int v_3d_resolution;
};

struct fi_CDVideoExtensionInfo_t {
	int iBitRate;
	int eVideoFormat;
	int bLevelType;
};

struct fi_TDResolutionInfo_t {
	unsigned int	hResolution;	/* active horizontal resolution */
	unsigned int	vResolution;	/* active vertical resolution */
	unsigned int	hStart;			/* horizontal active resolution start */
	unsigned int	vStart;			/* vertical active resolution start */
	unsigned int	hTotal;			/* total horizontal resolution */
	unsigned int	vTotal;			/* total vertical resolution */
	unsigned int	hFreq;			/* horizontal frequency */
	unsigned int	vFreq;			/* vertical frequency (Hz * 100) */
	int				bProgressScan;	/* progress / interace */
	int				bHSyncPositive;	/* horizontal sync polarity */
	int				bVSyncPositive;	/* vertical sync polarity */
	int				bNearFrequency;	/* near frequency, PC resolution only */
	int				bNotOptimumMode;/* [MFM prodcut] In PC/DVI source, if current source resolution is larger than panel resolution, this flag is set as true. */
	struct fi_CD3dControlInfo_t			t3dControlInfo;
	struct fi_CDVideoExtensionInfo_t	tVideoExInfo;		/* [For WiseLink Movie] Video Extension Information */
};

struct fi_resolution_info {
	int eRes;
	struct fi_TDResolutionInfo_t tSetResInfo;
};

struct fi_factory_data {
	int iUID;
	int iValue;
};

struct fi_TDWhiteBalance_t {
	unsigned int rGain;
	unsigned int gGain;
	unsigned int bGain;
	unsigned int rOffset;
	unsigned int gOffset;
	unsigned int bOffset;
};

struct fi_sharpness {
	int eType;
	unsigned int uValue;
};

struct fi_picture_test_pattern {
	int eFRCPattern;
	int RGBPattern;
};

struct fi_backend_command {
	int eCommand;
	unsigned int uiCnt;
	unsigned short value;
};

struct fi_i2c_data {
	unsigned int u32Addr;
	unsigned int u32Value;
};

struct fi_i2c_data_mask {
	unsigned int u32Addr;
	unsigned int u32Mask;
	unsigned int u32Value;
};

struct fi_write_command {
	unsigned char command;
	unsigned char sub_cmd;
	unsigned short data;
};

struct fi_send_command {
	unsigned int u32Addr;
	unsigned int u32Value;
};

struct fi_multi_cmd {
	unsigned short cmd_num;
	unsigned short *cmd_data;
};

struct fi_backend_info {
	char *strGetInfo;
	int iMaxLength;
};

struct fi_TDSourceResInfo_t {
	 int SourceMode;
	 int Resolution;
	 int PictureMode;
	 int ColorFormat;
	 int Backlight;
	 int bSportsMode;
};

struct fi_TDAutoMotionPlus_t {
	int bIsTTXOn;
	int bIsPipOn;
	int bFrameDoubling;
	int bIsGameMode;
	int bIsFactoryMode;
	int e3dMode;
	int uBlurReduction;
	int uJudderReduction;
};

struct fi_auto_motion_plus {
	int eAutoMotionMode;
	struct fi_TDAutoMotionPlus_t AutoMotionPlus;
};

struct fi_3d_inout_mode_data {
	int iResolution;
	int i3Dformat;
	int i3DIn_Scaler;
	int i3DOut_Scaler;
	int iWidth_Scaler;
	int iHeight_Scaler;
	int i3DIn_Graphic;
	int i3DOut_Graphic;
	int iWidth_Graphic;
	int iHeight_Graphic;
	int h_3d_resolution;
	int v_3d_resolution;
	int iTemp_1;
	int iTemp_2;
};

struct fi_system_config {
	int region;
	int hv_flip;
	int fdisplay_on_off;
	int pc_mode_on_off;
	int home_panel_frc;
	int shop_3d_cube;
	int panel_cell_init_time;
	int tcon_init_time;
	int btemidel_50_bon;
	int btemidel_50_mov;
	int btemidel_60_dyn;
	int btemidel_60_mov;
	int slavdelay48_bon;
	int slavdelay60_bon;
	int slavdelay48;
	int slavdelay60;
	int ssc_vx1_on_off;
	int ssc_vx1_period;
	int ssc_vx1_modulation;
	int ssc_lvds_on_off;
	int ssc_lvds_mfr;
	int ssc_lvds_mrr;
	int ssc_ddr_on_off;
	int ssc_ddr_period;
	int ssc_ddr_modulation;
	int ssc_ddr_mfr;
	int ssc_ddr_mrr;
	int dimming_type;
	int support_3d;
	int frc_vx1_rx_eq;
	int reserved1;
	int reserved2;
};

struct fi_panel_info {
	int width;
	int height;
	int vfreq_type;
	int support_3d;
	int support_pc_3d;
	int mute_for_gamemode;
};

struct fi_start_stop_autoview {
	int start_stop;
	int start_type;
	int video_x;
	int video_y;
	int video_width;
	int video_height;
};

struct fi_pip_info {
	int pip;
	int res;
	int source;
	int hresolution;
	int vresolution;
};

struct fi_dimming_register_info {
	unsigned int cmd_num;
	unsigned int *cmd_address;
	unsigned int register_num;
	unsigned int *register_address;
};

struct fi_flash_status {
	unsigned int frc;
	unsigned int tcon;
	unsigned int reserved;
};

struct fi_video_size_info {
	unsigned int crop_x;
	unsigned int crop_y;
	unsigned int crop_w;
	unsigned int crop_h;
	unsigned int geo_x;
	unsigned int geo_y;
	unsigned int geo_w;
	unsigned int geo_h;
};

struct fi_request_str {
	int request;
	int size;
	unsigned char* str;
};

struct fi_request_pointer {
	int request;
	int size;
	int* value;
};

struct fi_request {
	int request;
	int value;
};

/*
 ***************************** STRUCTS FOR TCON IOCTLs *********************************
 */

struct ti_checksum {
	int len;
	char *pValueStr;
};

struct ti_lut_data {
	int eCurrentLDCCPhase;
	int bDebugMode;
	int eUpdateMode;
};

struct ti_3d_effect {
	int eCurrentLDCCPhase;
	int b3DOnOff;
	int bDebugMode;
};

struct ti_update_eeprom {
	int len;
	char *pstrTconPath;
};

struct ti_test_pattern {
	signed int iSetValue;
	int ePatternMode;
};

struct ti_flash_version {
	int len;
	char *pStr;
};

struct ti_panel_wp {
	int eWPType;
	int bOnOff;
	unsigned int u32Delay;
};

struct ti_factory_tcon_partition {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_factory_dcc_debug {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_tcon_bctr_t {
	int iDynCtrOn;
	int iMovCtrOn;
	int iIirOption;
	int iIirVel;
	int iSlpFac;
	int iIirLth;
	int iIirLCoef;
	int iIirHCoef;
	int iUMaxDynValue;
	int iUMaxMovValue;
	int iUMinDynValue;
	int iUMinMovValue;
	int iMaxDynDrop;
	int iMaxMovDrop;
	int iMinDynDrop;
	int iMinMovDrop;
	int iMaxDownTh1;
	int iMaxDownTh2;
	int iMaxHDynTh;
	int iMaxHMovTh;
	int iMinHDynTh;
	int iMinHMovTh;
	int iHistoDynBin;
	int iHistoMovBin;
	int iCtrOnGlimit;
};

struct ti_factory_bctr {
	unsigned int UniqueID;
	signed int iSetValue;
};

struct ti_Tcon_Map_t {
	int DCCSELMAP[EXTENDED_NUMBER_OF_LDCC_MAP_SIZE];
	int POSISEL[EXTENDED_NUMBER_OF_LOCAL_AREA];
};

struct ti_TconDCCDebug_t {
	int bLDCCDebug;
	int bUSBDebug;
	struct ti_Tcon_Map_t tTconMap;
};

struct ti_TconPartion_t {
	int DCCX1;
	int DCCX2;
	int DCCX3;
	int DCCX4;
	int DCCX5;
	int DCCX6;
	int DCCX7;

	int DCCY1;
	int DCCY2;
	int DCCY3;
	int DCCY4;
	int DCCY5;

	int DCCh1;
	int DCCh2;
	int DCCh3;
	int DCCh4;
	int DCCh5;
	int DCCh6;
	int DCCh7;

	int DCCv1;
	int DCCv2;
	int DCCv3;
	int DCCv4;
	int DCCv5;
};

struct fti_i2c {
	unsigned char	slaveAddr;
	unsigned char	*pSubAddr;
	unsigned char	*pDataBuffer;
	unsigned int	dataSize;
};

struct ti_TconTemperature_t {
	int TEMP_READ;
	int TEMP_LAST;
	int TEMP_DELTA[EXTENDED_NUMBER_OF_LDCC_MAP_SIZE];
	int TEMP_SEL[EXTENDED_NUMBER_OF_LOCAL_AREA];
	int DEL_STEP_200;
	int TIME_TO_COLD;
};

struct ti_TconGlimitLBT_t {
	int LBT0;
	int LBT1;
	int LBT2;
	int LBT3;
	int LBT4;
	int LBT5;
	int LBT6;
	int LBT7;
	int LBT8;
	int LBT9;
	int LBT10;
	int LBT11;
	int LBT12;
	int LBT13;
	int LBT14;
	int LBT15;
	int LBT16;
	int LBT17;
	int LBT18;
	int LBT19;
	int LBT20;
};

struct ti_tcon_factory_data {
	struct ti_tcon_bctr_t bctr;
	struct ti_TconDCCDebug_t dcc_on;
	struct ti_TconDCCDebug_t dcc_off;
	struct ti_TconPartion_t partition;
	struct ti_TconTemperature_t temp_on;
	struct ti_TconTemperature_t temp_off;
	struct ti_TconGlimitLBT_t glimit_lbt;
};

struct ti_request_str {
	int request;
	int size;
	unsigned char* str;
};

struct ti_request_pointer {
	int request;
	int size;
	int* value;
};

struct ti_request {
	int request;
	int value;
};

/*
 ***************************** FRC IOCTLs *********************************
 */

/* UpgradeFirmware */
#define FRC_IOCTL_UPGRADE_FIRMAWARE			FRC_IOWR(0x01, int)

/* UpgradeLDTable */
#define FRC_IOCTL_UPGRADE_LD_TABLE			FRC_IOWR(0x02, int)

/* ExecuteMonitorTask */
#define FRC_IOCTL_EXECUTE_MONITOR_TASK		FRC_IOWR(0x03, int)

/* SetInputSize */
#define FRC_IOCTL_SET_INPUT_SIZE			FRC_IOWR(0x04, int)

/* SetResolutionInfo */
#define FRC_IOCTL_SET_RESOLUTION_INFO		FRC_IOWR(0x05, struct fi_resolution_info)

/* SetMute */
#define FRC_IOCTL_SET_MUTE					FRC_IOWR(0x06, int)

/* SetSeamlessMute */
#define FRC_IOCTL_SET_SEAMLESS_MUTE			FRC_IOWR(0x07, int)

/* SetAgingPattern */
#define FRC_IOCTL_SET_AGING_PATTERN			FRC_IOWR(0x08, int)

/* CtrlPatternBeforeDDR */
#define FRC_IOCTL_CTRL_PATTERN_BEFORE_DDR	FRC_IOWR(0x09, int)

/* CtrlPatternAfterDDR */
#define FRC_IOCTL_CTRL_PATTERN_AFTER_DDR	FRC_IOWR(0x0A, int)

/* CtrlOSDPatternBeforeDDR */
#define FRC_IOCTL_CTRL_OSD_PATTERN_BEFORE_DDR	FRC_IOWR(0x0B, int)

/* CtrlOSDPatternAfterDDR */
#define FRC_IOCTL_CTRL_OSD_PATTERN_AFTER_DDR	FRC_IOWR(0x0C, int)

/* SetFactoryData */
#define FRC_IOCTL_SET_FACTORY_DATA			FRC_IOWR(0x0D, struct fi_factory_data)

/* SetWhiteBalance */
#define FRC_IOCTL_SET_WHITE_BALANCE			FRC_IOWR(0x0E, struct fi_TDWhiteBalance_t)

/* SetWhiteBalanceThreshold */
#define FRC_IOCTL_SET_WHITE_BALANCE_THRESHOLD	FRC_IOWR(0x0F, struct fi_TDWhiteBalance_t)

/* SetSharpness */
#define FRC_IOCTL_SET_SHARPNESS				FRC_IOWR(0x10, struct fi_sharpness)

/* SetDigitalNR */
#define FRC_IOCTL_SET_DIGITAL_NR			FRC_IOWR(0x11, int)

/* SetAutoMotionPlus */
#define FRC_IOCTL_SET_AUTO_MOTION_PLUS		FRC_IOWR(0x12, struct fi_auto_motion_plus)

/* SetPCMode */
#define FRC_IOCTL_SET_PC_MODE				FRC_IOWR(0x13, int)

/* SetGameMode */
#define FRC_IOCTL_SET_GAME_MODE				FRC_IOWR(0x14, int)

/* SetFilmMode */
#define FRC_IOCTL_SET_FILM_MODE				FRC_IOWR(0x15, int)

/* SetPictureInfo */
#define FRC_IOCTL_SET_PICTURE_INFO			FRC_IOWR(0x16, struct fi_TDSourceResInfo_t)

/* Set3DSyncOnOff */
#define FRC_IOCTL_SET_3D_SYNC_ON_OFF			FRC_IOWR(0x17, int)

/* Set3DMode */
#define FRC_IOCTL_SET_3D_MODE				FRC_IOWR(0x18, int)

/* Set2DMode */
#define FRC_IOCTL_SET_2D_MODE				FRC_IOWR(0x19, int)

/* Set3DStrength */
#define FRC_IOCTL_SET_3D_STRENGTH			FRC_IOWR(0x1A, int)

/* Set3DLRControl */
#define FRC_IOCTL_SET_3D_LR_CONTROL			FRC_IOWR(0x1B, int)

/* Set3DViewpoint */
#define FRC_IOCTL_SET_3D_VIEWPOINT			FRC_IOWR(0x1C, int)

/* SetBypass */
#define FRC_IOCTL_SET_BYPASS				FRC_IOWR(0x1D, int)

/* SetGlimit */
#define FRC_IOCTL_SET_GLIMIT				FRC_IOWR(0x1E, int)

/* StartAutoViewOsdDetection */
#define FRC_IOCTL_START_AV_OSD_DETECTION	FRC_IOWR(0x1F, int)

/* GetAutoViewOsdLevel */
#define FRC_IOCTL_GET_AV_OSD_LEVEL			FRC_IOWR(0x20, int)

/* GetAutoViewDetectedMode */
#define FRC_IOCTL_GET_AV_DETECT_MODE			FRC_IOWR(0x21, int)

/* CheckEndAutoViewDetection */
#define FRC_IOCTL_CHECK_END_AV_DETECTION	FRC_IOWR(0x22, int)

/* WriteI2CData */
#define FRC_IOCTL_WRITE_I2C_DATA			FRC_IOWR(0x23, struct fi_i2c_data)

/* WriteI2CMaskData */
#define FRC_IOCTL_WRITE_I2C_MASK_DATA		FRC_IOWR(0x24, struct fi_i2c_data_mask)

/* SendCommand */
#define FRC_IOCTL_SEND_COMMAND				FRC_IOWR(0x25, struct fi_send_command)

/* ReadI2CData */
#define FRC_IOCTL_READ_I2C_DATA				FRC_IOWR(0x26, struct fi_i2c_data)

/* GetVersion */
#define FRC_IOCTL_GET_VERSION				FRC_IOWR(0x27, int)

/* GetBackendInfo */
#define FRC_IOCTL_GET_BACKEND_INFO			FRC_IOWR(0x28, struct fi_backend_info)

/* SetTCONWpLevel */
#define FRC_IOCTL_SET_TCON_WP_LEVEL			FRC_IOWR(0x29, int)

/* SetTCON1EnableLevel */
#define FRC_IOCTL_SET_TCON1_ENABLE_LEVEL	FRC_IOWR(0x2A, int)

/* SetTCON2EnableLevel */
#define FRC_IOCTL_SET_TCON2_ENABLE_LEVEL	FRC_IOWR(0x2B, int)

/* SupportTCONI2CEnable */
#define FRC_IOCTL_SUPPORT_TCON_I2C_ENABLE	FRC_IOWR(0x2C, int)

/* WriteCommand */
#define FRC_IOCTL_WRITE_COMMAND				FRC_IOWR(0x2D, struct fi_write_command)

/* ReadCommand */
#define FRC_IOCTL_READ_COMMAND				FRC_IOWR(0x2E, int)

/* ChangeUdFhdDisplayMode */
#define FRC_IOCTL_CHANGE_UDFHD_DISPLAY_MODE	FRC_IOWR(0x2F, int)

/* SetOSDWhiteBalance */
#define FRC_IOCTL_SET_OSD_WHITE_BALANCE		FRC_IOWR(0x30, int)

/* GetPanelFreq */
#define FRC_IOCTL_GET_PANEL_FREQ			FRC_IOWR(0x31, int)

/* CheckCrcForLvds */
#define FRC_IOCTL_CHECK_CRC_FOR_LVDS		FRC_IOWR(0x32, int)

/* CheckCrcForTCon */
#define FRC_IOCTL_CHECK_CRC_FOR_TCON		FRC_IOWR(0x33, int)

/* Set3DBrightnessInformation */
#define FRC_IOCTL_SET_3D_BRI_INFO			FRC_IOWR(0x34, int)

/* SetPictureSize */
#define FRC_IOCTL_SET_PICTURE_SIZE			FRC_IOWR(0x35, int)

/* IsSupportFRCPattern */
#define FRC_IOCTL_IS_SUPPORT_FRC_PATTERN	FRC_IOWR(0x36, int)

/* GetPictureTestPatternNum */
#define FRC_IOCTL_GET_PIC_TEST_PATTERN_NUM	FRC_IOWR(0x37, struct fi_picture_test_pattern)

/* SetPictureTestPatternNum */
#define FRC_IOCTL_SET_PIC_TEST_PATTERN_NUM	FRC_IOWR(0x38, struct fi_picture_test_pattern)

/* SetSpreadSpectrum */
#define FRC_IOCTL_SET_SPREAD_SPECTRUM		FRC_IOWR(0x39, int)

/* SetLedMotionPlus */
#define FRC_IOCTL_SET_LED_MOTION_PLUS		FRC_IOWR(0x3A, int)

/* CtrlPVCC */
#define FRC_IOCTL_CTRL_PVCC					FRC_IOWR(0x3B, int)

/* SendBackendCommand */
#define FRC_IOCTL_SEND_BACKEND_COMMAND		FRC_IOWR(0x3C, struct fi_backend_command)

/* ReadBackendCommand */
#define FRC_IOCTL_READ_BACKEND_COMMAND		FRC_IOWR(0x3D, struct fi_backend_command)

/* get frc type */
#define FRC_IOCTL_GET_FRC_TYPE				FRC_IOWR(0x3E, int)

/* tztv_i2c_read_frc, tztv_i2c_read_frc_sub1, tztv_i2c_read_frc_sub2 */
#define FRC_IOCTL_I2C_READ					FRC_IOWR(0x3F, struct fti_i2c)

/* tztv_i2c_write_frc, tztv_i2c_write_frc_sub1, tztv_i2c_write_frc_sub2 */
#define FRC_IOCTL_I2C_WRITE					FRC_IOWR(0x40, struct fti_i2c)

/* Get 3D InOut Mode */
#define FRC_IOCTL_GET_3D_INOUT_MODE			FRC_IOWR(0x41, struct fi_3d_inout_mode_data)

/* Set System Config */
#define FRC_IOCTL_SET_SYSTEM_CONFIG			FRC_IOWR(0x42, struct fi_system_config)

/* Get Panel Info */
#define FRC_IOCTL_GET_PANEL_INFO			FRC_IOWR(0x43, struct fi_panel_info)

/* Start/Stop Autoview */
#define FRC_IOCTL_START_STOP_AUTOVIEW		FRC_IOWR(0x44, struct fi_start_stop_autoview)

/* Set Pip Info*/
#define FRC_IOCTL_SET_PIP_INFO				FRC_IOWR(0x45, struct fi_pip_info)

/* Set Dimming register info */
#define FRC_IOCTL_SET_DIMMING_REGISTER_INFO	FRC_IOWR(0x46, struct fi_dimming_register_info)

/* SetMpegNR */
#define FRC_IOCTL_SET_MPEG_NR				FRC_IOWR(0x47, int)

/* Read SPI Flash Status */
#define FRC_IOCTL_READ_SPI_FLASH_STATUS		FRC_IOWR(0x48, struct fi_flash_status)

/* Set Video Size Info */
#define FRC_IOCTL_SET_VIDEO_SIZE_INFO		FRC_IOWR(0x49, struct fi_video_size_info)

/* Set 21:9 mode */
#define FRC_IOCTL_SET_21_9_MODE				FRC_IOWR(0x4A, int)

/* Set Panorama mode */
#define FRC_IOCTL_SET_PANORAMA_MODE			FRC_IOWR(0x4B, int)

/* Set Video postion */
#define FRC_IOCTL_SET_VIDEO_POSITION		FRC_IOWR(0x4C, int)

/* Get video graphic mix type */
#define FRC_IOCTL_GET_VIDEO_GRAPHIC_MIX_TYPE	FRC_IOWR(0x4D, int)

/* Send Multi Command */
#define FRC_IOCTL_SEND_MULTI_COMMAND		FRC_IOWR(0x4E, struct fi_multi_cmd)

/* Upgrade SRP */
#define FRC_IOCTL_UPGRADE_SRP				FRC_IOWR(0x4F, int)

/* interface to get str */
#define FRC_IOCTL_INTERFACE_STR				FRC_IOWR(0xFD, struct fi_request_str)

/* interface to use pointer */
#define FRC_IOCTL_INTERFACE_POINTER				FRC_IOWR(0xFE, struct fi_request_pointer)

/* interface */
#define FRC_IOCTL_INTERFACE					FRC_IOWR(0xFF, struct fi_request)

/*
 ***************************** TCON IOCTLs *********************************
 */

/* GetChecksum */
#define TCON_IOCTL_GET_CHECKSUM				TCON_IOWR(0x01, struct ti_checksum)

/* SetLUTData */
#define TCON_IOCTL_SET_LUT_DATA				TCON_IOWR(0x02, struct ti_lut_data)

/* SetPartition */
#define TCON_IOCTL_SET_PARTITION			TCON_IOWR(0x03, int)

/* GetTemperature */
#define TCON_IOCTL_GET_TEMPERATURE			TCON_IOWR(0x04, int)

/* t_Tcon_Set3DOptimize */
#define TCON_IOCTL_SET_3D_OPTIMIZE			TCON_IOWR(0x05, int)

/* Set3DEffect */
#define TCON_IOCTL_SET_3D_EFFECT			TCON_IOWR(0x06, struct ti_3d_effect)

/* CheckDCCSel */
#define TCON_IOCTL_CHECK_DCC_SEL			TCON_IOWR(0x07, int)

/* GetDccVersion */
#define TCON_IOCTL_GET_DCC_VERSION			TCON_IOWR(0x08, int)

/* UpdateEeprom */
#define TCON_IOCTL_UPDATE_EEPROM			TCON_IOWR(0x09, struct ti_update_eeprom)

/* SetTestPattern */
#define TCON_IOCTL_SET_TEST_PATTERN			TCON_IOWR(0x0A, struct ti_test_pattern)

/* CheckCrcTcon */
#define TCON_IOCTL_CHECK_CRC_TCON			TCON_IOWR(0x0B, int)

/* GetTCONFlashVersion */
#define TCON_IOCTL_GET_TCON_FLASH_VERSION	TCON_IOWR(0x0C, struct ti_flash_version)

/* SetMute */
#define TCON_IOCTL_SET_MUTE					TCON_IOWR(0x0D, int)

/* SetLedMotionPlus */
#define TCON_IOCTL_SET_LED_MOTION_PLUS		TCON_IOWR(0x0E, int)

/* SetGameModeOnOff */
#define TCON_IOCTL_SET_GAME_MODE_ON_OFF		TCON_IOWR(0x0F, int)

/* SetPanelWp */
#define TCON_IOCTL_SET_PANEL_WP				TCON_IOWR(0x10, struct ti_panel_wp)

/* SetFactoryTconPartition */
#define TCON_IOCTL_SET_FACTORY_TCON_PARTITION	TCON_IOWR(0x11, struct ti_factory_tcon_partition)

/* SetFactoryDCCDebug */
#define TCON_IOCTL_SET_FACTORY_DCC_DEBUG	TCON_IOWR(0x12, struct ti_factory_dcc_debug)

/* SetPictureInformation */
#define TCON_IOCTL_SET_PICTURE_INFO			TCON_IOWR(0x13, struct fi_TDSourceResInfo_t)

/* GetPictureInformation */
#define TCON_IOCTL_GET_PICTURE_INFO			TCON_IOWR(0x14, int)

/* SetFactoryBCTR */
#define TCON_IOCTL_SET_FACTORY_BCTR			TCON_IOWR(0x15, struct ti_factory_bctr)

/* Set3DBrightnessInformation */
#define TCON_IOCTL_SET_3D_BRI_INFO			TCON_IOWR(0x16, int)

/* Get Tcon Type */
#define TCON_IOCTL_GET_TCON_TYPE			TCON_IOWR(0x17, int)

/* tztv_i2c_read_tcon, tztv_i2c_read_tcon_gamma, tztv_i2c_read_tcon_temp_sensor, tztv_i2c_read_tcon_eeprom */
#define TCON_IOCTL_I2C_READ					TCON_IOWR(0x18, struct fti_i2c)

/* tztv_i2c_write_tcon, tztv_i2c_write_tcon_gamma, tztv_i2c_write_tcon_temp_sensor, tztv_i2c_write_tcon_eeprom */
#define TCON_IOCTL_I2C_WRITE				TCON_IOWR(0x19, struct fti_i2c)

/* factory data */
#define TCON_IOCTL_FACTORY_DATA				TCON_IOWR(0x1A, struct ti_tcon_factory_data)

/* Get Cooling Mode */
#define TCON_IOCTL_GET_COOLING_MODE			TCON_IOWR(0x1B, int)

/* Get Warming Time */
#define TCON_IOCTL_GET_WARMING_TIME			TCON_IOWR(0x1C, int)

/* Set Current Temperature */
#define TCON_IOCTL_SET_CURRENT_TEMPERATURE	TCON_IOWR(0x1D, int)

/* Set Last Temperature */
#define TCON_IOCTL_SET_LAST_TEMPERATURE		TCON_IOWR(0x1E, int)

/* Demura Upgrade */
#define TCON_IOCTL_UPGRADE_DEMURA_FIRMWARE	TCON_IOWR(0x1F, int)

/* Set Demura Bypass */
#define TCON_IOCTL_SET_DEMURA_BYPASS		TCON_IOWR(0x20, int)

/* Set Panel Curved Mode */
#define TCON_IOCTL_SET_PANEL_CURVED_MODE	TCON_IOWR(0x21, int)

/* Load TCON Driver */ /* fixme : unused */
#define TCON_IOCTL_LOAD_DRIVER				TCON_IOWR(0x22, int)

/* interface to get str */
#define TCON_IOCTL_INTERFACE_STR				TCON_IOWR(0xFD, struct ti_request_str)

/* interface to use pointer */
#define TCON_IOCTL_INTERFACE_POINTER				TCON_IOWR(0xFE, struct ti_request_pointer)

/* interface */
#define TCON_IOCTL_INTERFACE				TCON_IOWR(0xFF, struct ti_request)


/*
 ***************************** FRC EW IOCTLs *********************************
 */
#define FRC_EW_IOCTL_DDR_TEST					FRC_EW_IOWR(0x01, int)
#define FRC_EW_IOCTL_SPI_TEST					FRC_EW_IOWR(0x02, int)
#define FRC_EW_IOCTL_SYSTEM_TEST				FRC_EW_IOWR(0x03, int)
#define FRC_EW_IOCTL_LD_TEST					FRC_EW_IOWR(0x04, int)
#define FRC_EW_IOCTL_DSP_TEST					FRC_EW_IOWR(0x05, int)
#define FRC_EW_IOCTL_HS_TEST					FRC_EW_IOWR(0x06, int)
#define FRC_EW_IOCTL_SYSTEM_SELFDIAGNOSIS_TEST	FRC_EW_IOWR(0x07, int)
#define FRC_EW_IOCTL_STANDALONE_TEST			FRC_EW_IOWR(0x08, int)
#define FRC_EW_IOCTL_PMIC_TEST					FRC_EW_IOWR(0x09, int)

/* interface */
#define FRC_EW_IOCTL_INTERFACE					FRC_EW_IOWR(0xF0, struct fi_request)


#ifdef  __cplusplus
}
#endif


#endif	/* __UAPI__TZTV__FRC__TCON__H__ */
