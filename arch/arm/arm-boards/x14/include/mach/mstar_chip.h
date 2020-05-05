#if defined(CONFIG_MSTAR_PreX14)
#define MSTAR_MIU0_BUS_BASE                      0x40000000
#elif defined(CONFIG_MSTAR_X14)
#define MSTAR_MIU0_BUS_BASE                      0x20000000
#endif
#define MSTAR_MIU1_BUS_BASE                      0xA0000000

extern unsigned int query_frequency(void);
