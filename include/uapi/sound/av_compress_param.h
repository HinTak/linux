#ifndef __AV_COMPRESS_PARAM_H
#define __AV_COMPRESS_PARAM_H

typedef enum
{
	AUDIO_8KHZ,
	AUDIO_11_025KHZ,
	AUDIO_12KHZ,
	AUDIO_16KHZ,
	AUDIO_22_05KHZ,
	AUDIO_24KHZ,
	AUDIO_32KHZ,
	AUDIO_44_1KHZ,
	AUDIO_48KHZ,
	AUDIO_88_2KHZ,
	AUDIO_96KHZ,
	AUDIO_176_4KHZ,
	AUDIO_192KHZ,
	AUDIO_SAMPLING_FREQ_MAX
} Audio_SamplingFreq_e;

typedef enum
{
	AUDIO_BM_CUT_FREQ_80 = 80,
	AUDIO_BM_CUT_FREQ_100 = 100,
	AUDIO_BM_CUT_FREQ_120 = 120,	
	AUDIO_BM_CUT_FREQ_MAX
} Audio_CutFreqType_e;

typedef enum
{

 SIP_POST_BASSMNG_SPK_SETUP_LLLL1=     0, //RW       No Redirection necessary  
 SIP_POST_BASSMNG_SPK_SETUP_LLLL0=	        1, //RW       Configuration3
 SIP_POST_BASSMNG_SPK_SETUP_LLLS1=	       2, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LLLS0=        3, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LLL01=	        4, //RW       No Redirection necessary  
 SIP_POST_BASSMNG_SPK_SETUP_LLL00=	        5, //RW       Configuration3  

 SIP_POST_BASSMNG_SPK_SETUP_LLSL1=	        6, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LLSL0=	        7, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LLSS1=	        8, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LLSS0=	        9, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LLS01=	       10, //RW       Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_LLS00=	       11, //RW       Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_LL0L1=	       12, //RW       No Redirection necessary  
 SIP_POST_BASSMNG_SPK_SETUP_LL0L0=	       13, //RW       Configuration3  
 SIP_POST_BASSMNG_SPK_SETUP_LL0S1=	       14, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LL0S0=	       15, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LL001=	       16, //RW       No Redirection necessary
 SIP_POST_BASSMNG_SPK_SETUP_LL000=	       17, //RW       Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_LSLL1=	       18, //RW       Configuration3 with subwoofer  
 SIP_POST_BASSMNG_SPK_SETUP_LSLL0=	       19, //RW       Configuration3  
 SIP_POST_BASSMNG_SPK_SETUP_LSLS1=	       20, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LSLS0=	       21, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LSL01=	       22, //RW       Configuration3 with subwoofer
 SIP_POST_BASSMNG_SPK_SETUP_LSL00=	       23, //RW       Configuration3

 SIP_POST_BASSMNG_SPK_SETUP_LSSL1=	       24, //RW       Configuration1   
 SIP_POST_BASSMNG_SPK_SETUP_LSSL0=	       25, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LSSS1=	       26, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LSSS0=       27, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LSS01=       28, //RW       Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_LSS00=	       29, //RW       Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_LS0L1=	       30, //RW       Configuration3 with subwoofer  
 SIP_POST_BASSMNG_SPK_SETUP_LS0L0=	       31, //RW       Configuration3  
 SIP_POST_BASSMNG_SPK_SETUP_LS0S1=	       32, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_LS0S0=       33 ,//RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_LS001=	       34, //RW       Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_LS000=	       35, //RW       Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_L0LL1=	       36, //RW       No Redirection necessary  
 SIP_POST_BASSMNG_SPK_SETUP_L0LL0=	       37, //RW       Configuration3  
 SIP_POST_BASSMNG_SPK_SETUP_L0LS1=	       38, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_L0LS0=	       39, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_L0L01=	       40, //RW       No Redirection necessary
 SIP_POST_BASSMNG_SPK_SETUP_L0L00=	       41, //RW       Configuration3

 SIP_POST_BASSMNG_SPK_SETUP_L0SL1=	       42, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_L0SL0=	       43, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_L0SS1=	       44, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_L0SS0=       45, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_L0S01=	       46, //RW   	 Configuration1	
 SIP_POST_BASSMNG_SPK_SETUP_L0S00=	       47, //RW 		 Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_L00L1=	       48, //RW       No Redirection necessary  
 SIP_POST_BASSMNG_SPK_SETUP_L00L0=	       49, //RW       Configuration3  
 SIP_POST_BASSMNG_SPK_SETUP_L00S1=	       50, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_L00S0=	       51, //RW       Configuration2  
 SIP_POST_BASSMNG_SPK_SETUP_L0001	=       52, //RW   	 No Redirection necessary	
 SIP_POST_BASSMNG_SPK_SETUP_L0000=	       53, //RW 		 Configuration2

 SIP_POST_BASSMNG_SPK_SETUP_SLLL1=	       54, //RW       Configuration1   
 SIP_POST_BASSMNG_SPK_SETUP_SLLL0=	       55, //RW       Configuration4  
 SIP_POST_BASSMNG_SPK_SETUP_SLLS1=	       56, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SLLS0=	       57, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SLL01=	       58, //RW   	 Configuration1 
 SIP_POST_BASSMNG_SPK_SETUP_SLL00	=       59, //RW 		 Configuration4

 SIP_POST_BASSMNG_SPK_SETUP_SLSL1	=       60, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SLSL0=	       61, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SLSS1=	       62, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SLSS0=	       63, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SLS01=	       64, //RW   	 Configuration1
// SIP_POST_BASSMNG_SPK_SETUP_SLS00=	       65, //RW 

 SIP_POST_BASSMNG_SPK_SETUP_SL0L1=	       66, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_SL0L0=	       67, //RW       Configuration4  
 SIP_POST_BASSMNG_SPK_SETUP_SL0S1=	       68, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SL0S0=	       69, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SL001=	       70, //RW   	 Configuration1
// SIP_POST_BASSMNG_SPK_SETUP_SL000=	       71, //RW 

 SIP_POST_BASSMNG_SPK_SETUP_SSLL1=	       72, //RW       Configuration1   
 SIP_POST_BASSMNG_SPK_SETUP_SSLL0=	       73, //RW       Configuration4  
 SIP_POST_BASSMNG_SPK_SETUP_SSLS1=	       74, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SSLS0=	       75, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SSL01=	       76, //RW       Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_SSL00=	       77, //RW 		 Configuration4

 SIP_POST_BASSMNG_SPK_SETUP_SSSL1	=       78 ,//RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SSSL0=	       79, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SSSS1	=       80, //RW       Configuration1   
 SIP_POST_BASSMNG_SPK_SETUP_SSSS0	=       81, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_SSS01	=       82, //RW       Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_SSS00	=       83, //RW 		 Configuration1

 SIP_POST_BASSMNG_SPK_SETUP_SS0L1	 =      84, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_SS0L0=	       85, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_SS0S1	=       86, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_SS0S0	=       87, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_SS001	=       88, //RW   	 Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_SS000	=       89, //RW 		 Configuration1

 SIP_POST_BASSMNG_SPK_SETUP_S0LL1=	       90, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S0LL0=	       91, //RW       Configuration4  
 SIP_POST_BASSMNG_SPK_SETUP_S0LS1=	       92, //RW       Configuration1  
// SIP_POST_BASSMNG_SPK_SETUP_S0LS0=	       93, //RW         
 SIP_POST_BASSMNG_SPK_SETUP_S0L01	=       94, //RW   	 Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_S0L00	=       95, //RW 		 Configuration4

 SIP_POST_BASSMNG_SPK_SETUP_S0SL1=	       96, //RW       Configuration1   
// SIP_POST_BASSMNG_SPK_SETUP_S0SL0=	       97 ,//RW         
 SIP_POST_BASSMNG_SPK_SETUP_S0SS1	 =      98, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S0SS0	=       99, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S0S01	 =     100, //RW   	 Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_S0S00	=      101, //RW 		 Configuration1

 SIP_POST_BASSMNG_SPK_SETUP_S00L1	=      102, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S00L0	=      103, //RW       Configuration4  
 SIP_POST_BASSMNG_SPK_SETUP_S00S1	=      104, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S00S0	=      105, //RW       Configuration1  
 SIP_POST_BASSMNG_SPK_SETUP_S0001	=      106, //RW   	 Configuration1
 SIP_POST_BASSMNG_SPK_SETUP_S0000	=      107, //RW 		 Configuration1

 SIP_POST_BASSMNG_SPK_SETUP_CONF0	=	  108, //RW	     Configuration1, Front  set to S, Subwoofer ON
 SIP_POST_BASSMNG_SPK_SETUP_CONF1	=	  109, //RW	     Configuration1, Rear   set to S, Subwoofer ON
 SIP_POST_BASSMNG_SPK_SETUP_CONF2	=	  110, //RW	     Configuration1, Center set to S, Subwoofer ON
 SIP_POST_BASSMNG_SPK_SETUP_CONF3	=	  111, //RW	     Configuration1, Front  set to L, Subwoofer OFF
 SIP_POST_BASSMNG_SPK_SETUP_CONF4	=	  112, //RW	     Configuration1, RSB    set to S, Subwoofer ON

 SIP_POST_BASSMNG_SPK_SETUP_MAX
} Audio_BmSpkType_e;

typedef enum                                                                            
{                                                                                       
	AUDIO_SPEAKER_SHAPE_FLAT,                                                           
	AUDIO_SPEAKER_SHAPE_CURVE,                                                          
	AUDIO_SPEAKER_SHAPE_BOX,                                                            
	AUDIO_SPEAKER_SHAPE_MAX                                                             
} Audio_DNSe_SpkShape_e;                                                                
                                                                                        
typedef enum                                                                            
{                                                                                       
	AUDIO_SPEAKER_TYPE_SAT,                                                             
	AUDIO_SPEAKER_TYPE_SPK,                                                             
	AUDIO_SPEAKER_TYPE_SPK_MOVABLE,                                                     
	AUDIO_SPEAKER_TYPE_MAX                                                              
} Audio_DNSe_SpkType_e;   

typedef enum
{
	AUDIO_DNMIX_MODE_LORO,
	AUDIO_DNMIX_MODE_LTRT,		
	AUDIO_DNMIX_MODE_MAX
} Audio_DnMixMode_e;

typedef enum {
	 AUDIO_SPEAKER_POS_UNKNOWN,
	 AUDIO_SPEAKER_POS_FRONT, //모든 스피커가 Front에 있음.
	 AUDIO_SPEAKER_POS_FRONT_TOP, //스피커가 Front와 Top에 있음
	 AUDIO_SPEAKER_POS_FRONT_SIDE, //  스피커가 Front와 side에 있음 
	 AUDIO_SPEAKER_POS_FRONT_SIDE_TOP, // 스피커가 front, side, top모든 방향에 있음
	 AUDIO_SPEAKER_POS_MAX
 } Audio_DNSe_SpkPosition_e;

typedef enum {
	AE_LOCK,
	AE_UNLOCK,
	AE_UNLOCK_SKIP,
	AE_UNLOCK_REPEAT,
	AE_UNLOCK_LAG
} Ae_LockStatus_e;

typedef enum {
	AE_UNDERFLOW,
	AE_UNDERFLOW_WATERMARK,
	AE_OVERFLOW_WATERMARK,
	AE_OVERFLOW
} Ae_FlowStatus_e;

typedef enum { 
	AE_NOT_STEREO, 
	AE_STEREO, 
	AE_JOINT_STEREO, 
	AE_LC_STEREO 
} Ae_StereoInfo_e;

typedef struct {
	unsigned int	uwFrontChannels;
	unsigned int	uwRearChannels;
	unsigned char	bLfePresent;
	Ae_StereoInfo_e	enumStereoInfo;
} Ae_Channel_s;

typedef struct {
	unsigned short				usBSMode;
	//Audio_Codec_e 	enumCodecType;							-> SDP CODEC
	unsigned int sndCodecType;			//SND_AUDIOCODEC_PCM	-> ALSA CODEC
} Ae_StreamMode_s;

typedef enum 
{
	AE_DOLBYS_NOT_INDICATED, /* Dolby not indicated */
	AE_DOLBYS_NOT, /* Not dolby surround */
	AE_DOLBYS /* Dolby surround */
} Ae_DolbySurround_e;

typedef enum
{
	AE_MPEG_STEREO,
	AE_MPEG_JOINT_STEREO,
	AE_MPEG_DUAL_CHANNEL,
	AE_MPEG_SINGE_CHANNEL,

	AE_MPEG_STEREO_MODE_MAX
} Ae_MpegStereoMode_e;

typedef enum
{
	AE_WMA_UNSUPPORTED_CH_NUM,
	AE_WMA_UNSUPPORTED_STREAM,
	AE_MPEG_STREAM_ERROR,		//AUDIO_081212
	AE_UNSUPPORTED_TYPE_MAX
} Ae_UnsupportedCodecType_e;

typedef struct {
	unsigned int	uwBitRate;  // bit rate in Hz
} Ae_BitRate_s;

typedef struct {
	unsigned char	wChAllocation[8];  // ch alloc 
	unsigned char 	wChOrgAllocation[8];  // Original  allocation info....  
	unsigned int	uiBitSize;	
	unsigned int	uiChNum;		
} Ae_ChAllocation_s;

typedef enum
{
	AUDIO_FORMAT_NONE,
	AUDIO_FORMAT_DDES,
	AUDIO_FORMAT_DTSES_DISCRETE,
	AUDIO_FORMAT_DTSES_MATRIX,
	AUDIO_FORMAT_DTS96_24,
	AUDIO_FORMAT_MAX,
}Ae_Dec_Format_e;

typedef enum
{
    SSL_MODE_OTHER,
    SSL_MODE_3D,
    SSL_MODE_KARAOKE,
    SSL_MODE_SSLVS,    // SSL Virtual Surround
    SSL_MODE_MAX,
} Ae_SSL_Mode_e;

typedef struct{

	unsigned int ui24SamplingFreq : 24;
	unsigned int ui8NoOfChans  : 8;
	unsigned int ui24DataSize   : 24;
	unsigned int ui8BitsPerSample : 8;
	unsigned int ui32StartAddress;
}Ae_CertDecodedInfo_s;

typedef struct{
	unsigned int ui16bse_mixmdata  : 16;
	unsigned int ui16bse_extpgmscl : 16;
	unsigned int ui16bse_mixdef  : 16;
	unsigned int ui16bse_paninfoe : 16;
	unsigned int ui16bse_paninfo :16 ;
	unsigned int ui16bse_panmean : 16;
}Ae_CertMetaInfo_s;

typedef struct{
	unsigned int ui32Data0;
	unsigned int ui32Data1;
	unsigned int ui32Data2;
}Ae_CertCbInfo_s;

typedef union {
	Ae_CertCbInfo_s stCbData;
	Ae_CertMetaInfo_s stMetaData;
	Ae_CertDecodedInfo_s stDecodeInfo;
}CertData_un;

typedef struct {
	unsigned int	uiBaseAddr;	
	unsigned int	uiSize;		
} Audio_Recoding_Karaoke_e;

typedef struct {
	unsigned char*	pu8Addr;
	unsigned int	u32Size;
	unsigned int	u32Pts;
	unsigned int  u32SampleRate;
} Audio_DvData_s;

typedef struct {
	unsigned int	uiLAddr;
	unsigned int	uiRAddr;
	unsigned int	uiCAddr;	
	unsigned int	uiLsAddr;
	unsigned int	uiRsAddr;
	unsigned int	uiLbAddr;
	unsigned int	uiRbAddr;
	unsigned int	uiSWAddr;	
	unsigned int	u32Size;
	unsigned int  u32SampleRate;	
	unsigned int	u32BitWidth;
	unsigned int	u32Ch;	
	unsigned int	u32LittleEndian;	
} Audio_Verance_s;

union AeEventParam
{
	//TODO ALSA SF로 변환필요한지 확인
	Audio_SamplingFreq_e enumSmplFreq;	
	Ae_LockStatus_e enumLockStatus;
	Ae_FlowStatus_e enumFlowStatus;
	//TODO ALSA Codec으로 변환필요한지 확인
	//Audio_Codec_e enumCodecType;
	unsigned int sndCodecType;
	Ae_Channel_s strtChannel;
	Ae_StreamMode_s strtStreamMode;	
	Ae_DolbySurround_e enumDolbyInfo;	
	unsigned char condReset;
	unsigned int	uwFrmSize;
	//	AE_DualChannelInfo strtDualChannelInfo;
	Ae_MpegStereoMode_e enumMpegStereoMode; //SoC_D00000743
	Ae_UnsupportedCodecType_e enumUnsupportType;
	Ae_BitRate_s strtBitRate;
	Ae_ChAllocation_s strChAllocation;
	Ae_Dec_Format_e enumAudioFormat;
	CertData_un unCertData;
	Audio_SamplingFreq_e enumOrgSmplFreq;
	Audio_Recoding_Karaoke_e enumRecodingKaroke;
	Audio_DvData_s strAudio_DvData;
	Audio_Verance_s strAudio_Verance;	
	unsigned int uiNumOfChannels;
};

typedef enum _aeEventId_t
{
	EVID_AE_FREQ,
	EVID_AE_AVSYNC,
	EVID_AE_FLOW,
	EVID_AE_CODECSTANDARD,
	EVID_AE_CHANNEL,
	EVID_AE_DOLBYSURROUND,
	EVID_AE_STREAMMODE,
	EVID_AE_RESET,
	EVID_AE_WATCHDOG,
	EVID_AE_FRM_SIZE,
	//	EVID_AE_DUALCHANNEL,	
	EVID_AE_MIXER_UNDERFLOW_WM,	
	EVID_AE_MP3_ERROR, 
	EVID_AE_MPEG_STEREO_MODE, 
	EVID_AE_UNSUPPORTED_CODEC,
	EVID_AE_BITRATE,
	EVID_AE_DECODE_DONE,
	EVID_AE_DEC_CH_ALLOC,	
	EVID_AE_DEC_BIT_WIDTH,	
	EVID_AE_LEGACY_START, 
	EVID_AE_DECODE_INFO,
	EVID_AE_ORG_SAMPLE_RATE,
	EVID_AE_DEC_RECODING_DONE_KARAOKE,
	EVID_AE_DEC_VERANCE_DONE,	
	EVID_AE_DEC_AUDIO_FORMAT,
	EVID_AE_DV_DONE,
	EVID_AE_SPDIF_CODECSTANDARD,

	MAX_AE_CB
} AeEventId_t;

struct sdp_event
{
	AeEventId_t	eventId;
	unsigned short decInstance;
	union AeEventParam	evParam;
	unsigned int reserved[128];
};




#endif
