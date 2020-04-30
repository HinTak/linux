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

#ifndef	__DT_core_h__
#define	__DT_core_h__

#ifdef	__cplusplus
	#define	__DtEXTERN	extern "C"
#else
	#define	__DtEXTERN	extern
#endif

/* TestPoint MacroCode -----------------------------------------------------*/
#ifdef		__DtBaseAddress
#undef		__DtBaseAddress
#endif
#define		__DtBaseAddress		0x30
#define		__DtAllEnable		1
#if ( __DtAllEnable == 1 )
#define		__DtTestPoint(func, step)		__Dt##func##step
#define		__DtTestPointValue(func, step, value, size)		__Dt##func##step(value,size)
#define		__DtTestPointWrite(func, step, value, size)		__Dt##func##step(value,size)
#define		__DtTestPointEventTrigger(func, step, data)		__Dt##func##step(data)
#define		__DtTestPointKernelInfo(func, step, ...)		__Dt##func##step(__VA_ARGS__)
#define		__DtTestPointEventTrigger32(func, step, data)		__Dt##func##step(data)
#define		__DtTestPointFuncCall(func, step, call)		__Dt##func##step(call)
#else
#define		__DtTestPoint(func, step)		
#define		__DtTestPointValue(func, step, value, size)		
#define		__DtTestPointWrite(func, step, value, size)		
#define		__DtTestPointEventTrigger(func, step, data)		
#define		__DtTestPointKernelInfo(func, step, ...)		
#define		__DtTestPointEventTrigger32(func, step, data)		
#define		__DtTestPointFuncCall(func, step, call)		call
#endif
__DtEXTERN		void	_TP_BusOut( unsigned int addr, unsigned int dat );
__DtEXTERN		void	_TP_MemoryOutput( unsigned int addr, unsigned int dat, void *value, unsigned int size );
__DtEXTERN		void	_TP_WritePoint( unsigned int addr, unsigned int dat, void *value, unsigned int size );
__DtEXTERN		void	_TP_EventTrigger( unsigned int addr, unsigned int dat, unsigned int event_id );
__DtEXTERN		void	_TP_BusKernelInfo( unsigned int addr, unsigned int dat, ... );
__DtEXTERN		void	_TP_EventTrigger32( unsigned int addr, unsigned int dat, unsigned int event_id );

/* TestPoint FuncList ------------------------------------------------------*/
#define		__DtFunc_context_switch		0

/* TestPoint StepList ------------------------------------------------------*/
#define		__DtStep_0		0

/* TestPoint DisableList ---------------------------------------------------*/
#define	__Dt__DtFunc_context_switch__DtStep_0(...)	/*KernelInfo*/	_TP_BusKernelInfo( __DtBaseAddress,  0x0000, __VA_ARGS__ );

#endif	/* __DT_core_h__ */

