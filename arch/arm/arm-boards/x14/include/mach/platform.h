#ifndef	__PLATFORM_H__
#define	__PLATFORM_H__

//------------------------------------------------------------------------------
//  Include Files
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//  Macros
//------------------------------------------------------------------------------

/*
 * Physical base addresses
 */
#define X14_CA9X4_MPIC		(0x16000000)

#define X14_MPCORE_SCU		(X14_CA9X4_MPIC + 0x0000)
#define X14_MPCORE_GIC_CPU	(X14_CA9X4_MPIC + 0x0100)
#define X14_MPCORE_GIT		(X14_CA9X4_MPIC + 0x0200)
#define X14_MPCORE_TWD		(X14_CA9X4_MPIC + 0x0600)
#define X14_MPCORE_GIC_DIST	(X14_CA9X4_MPIC + 0x1000)

//------------------------------------------------------------------------------
//
//  Define:  agate_BASE_REG_TIMER_PA
//
//  Locates the timer register base.
//
#define X14_BASE_REG_TIMER0_PA              (0x1F006040)
#define X14_BASE_REG_TIMER1_PA              (0x1F006080)
#define X14_BASE_REG_TIMER2_PA              (0xA0007780)


//------------------------------------------------------------------------------
//  Function prototypes
//------------------------------------------------------------------------------
int Mstar_ehc_platform_init(void);

#endif // __PLATFORM_H__

/* 	END */
