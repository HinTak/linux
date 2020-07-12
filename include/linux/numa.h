#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H


#ifdef CONFIG_NODES_SHIFT
#define NODES_SHIFT     CONFIG_NODES_SHIFT
#else
#define NODES_SHIFT     0
#endif

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define NUMA_NO_NODE    (-1)
#define NUMA_VIRTUAL_NODE    (-2)	/* Declare virtual NODE for special purpose */  
#define NUMA_EMEM_VNODE NUMA_VIRTUAL_NODE  

#endif /* _LINUX_NUMA_H */
