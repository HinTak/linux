#ifndef _SDP_SOC_H_
#define _SDP_SOC_H_

#if defined(CONFIG_MACH_FOXAP)
#	include <mach/foxap/sdp1202.h>
#elif defined(CONFIG_MACH_FOXB)
#	include <mach/foxb/sdp1207.h>
#elif defined(CONFIG_MACH_GOLFS)
#	include <mach/golfs/sdp1302.h>
#elif defined(CONFIG_MACH_GOLFP)
#	include <mach/golfp/sdp1304.h>
#elif defined(CONFIG_MACH_ECHOP)
#	include <mach/echop/sdp1106.h>
#else
# 	include <mach/map.h>
#endif 

#endif

