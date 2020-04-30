/*
 Copyright (C) 2009-2016, Heartland. Data Inc.  All Rights Reserved.      
                                                                          
 This software is furnished under a license and may be used and copied    
 only in accordance with the terms of such license and with the inclusion 
 of the above copyright notice. No title to and ownership of the software 
 is transferred.                                                          
 Heartland. Data Inc. makes no representation or warranties with          
 respect to the performance of this computer program, and specifically    
 disclaims any responsibility for any damages, special or consequential,  
 connected with the use of this program.                                  
*/

#ifndef	__DT_dt_ether_drv_h__
#define	__DT_dt_ether_drv_h__

#ifdef	__cplusplus
	#define	__DtEXTERN	extern "C"
#else
	#define	__DtEXTERN	extern
#endif

/* TestPoint MacroCode -----------------------------------------------------*/
#ifdef		__DtBaseAddress
#undef		__DtBaseAddress
#endif
#define		__DtBaseAddress		0x20
#define		__DtAllEnable		1
#if ( __DtAllEnable == 1 )
#define		__DtTestPoint(func, step)		__Dt##func##step
#define		__DtTestPointValue(func, step, value, size)		__Dt##func##step(value,size)
#define		__DtTestPointWrite(func, step, value, size)		__Dt##func##step(value,size)
#define		__DtTestPointEventTrigger(func, step, data)		__Dt##func##step(data)
#define		__DtTestPointEventTrigger32(func, step, data)		__Dt##func##step(data)
#define		__DtTestPointFuncCall(func, step, call)		__Dt##func##step(call)
#else
#define		__DtTestPoint(func, step)		
#define		__DtTestPointValue(func, step, value, size)		
#define		__DtTestPointWrite(func, step, value, size)		
#define		__DtTestPointEventTrigger(func, step, data)		
#define		__DtTestPointEventTrigger32(func, step, data)		
#define		__DtTestPointFuncCall(func, step, call)		call
#endif
__DtEXTERN		void	_TP_BusOut( unsigned int addr, unsigned int dat );
__DtEXTERN		void	_TP_MemoryOutput( unsigned int addr, unsigned int dat, void *value, unsigned int size );
__DtEXTERN		void	_TP_WritePoint( unsigned int addr, unsigned int dat, void *value, unsigned int size );
__DtEXTERN		void	_TP_EventTrigger( unsigned int addr, unsigned int dat, unsigned int event_id );
__DtEXTERN		void	_TP_EventTrigger32( unsigned int addr, unsigned int dat, unsigned int event_id );

/* TestPoint FuncList ------------------------------------------------------*/
#define		__DtFunc_dt10_cpu_freq		0
#define		__DtFunc_dt10_mem_check		1

/* TestPoint StepList ------------------------------------------------------*/
#define		__DtStep_0		0
#define		__DtStep_1		1
#define		__DtStep_2		2
#define		__DtStep_3		3
#define		__DtStep_4		4
#define		__DtStep_5		5
#define		__DtStep_6		6
#define		__DtStep_7		7

/* TestPoint DisableList ---------------------------------------------------*/
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_0	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0000 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_1	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0001 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_2	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0002 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_3	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0003 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_4	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0004 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_5	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0005 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_6	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0006 );
#define	__Dt__DtFunc_dt10_cpu_freq__DtStep_7	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0007 );
#define	__Dt__DtFunc_dt10_mem_check__DtStep_0	/*Free*/	_TP_BusOut( __DtBaseAddress,  0x0008 );

#endif	/* __DT_dt_ether_drv_h__ */

