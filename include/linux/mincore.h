/*
 * include/linux/mincore.h
 *
 * This header file provides definitions & declarations for functions
 * and data used in fs/minimal_core.c for creating a corefile with
 * minimal info
 *       --> thread of crashing process
 *       --> note sections (NT_PRPSINFO, NT_SIGINFO, NT_AUXV, NT_FILE)
 *       --> crashing thread stack
 * The corefile created is compressed & encrypted
 * it needs a modified GDB which is shared with HQ
 *
 * Copyright 2014: Samsung Electronics Co, Pvt Ltd,
 * Author : Manoharan Vijaya Raghavan (r.manoharan@samsung.com).
 */

#ifndef _MINCORE_H
#define _MINCORE_H

#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/elf.h>

#include <linux/zlib.h>
#include <linux/zutil.h>
#include <linux/crc32.h>
#include <linux/crypto.h>
#include <linux/sort.h>
#include <crypto/crypto_wrapper.h>

#ifdef CONFIG_SECURITY_SMACK
#define MINCORE_SMACK_LABEL "System"
#endif


#ifndef CORE_DUMP_USE_REGSET
	/* Mincore needs CORE_DUMP_USE_REGSET to be defined */
	BUILD_BUG();
#endif /* CORE_DUMP_USE_REGSET */

#define MCORE_CHECK_BYTE_UINT32(A, k) \
	(cc_u8)(0xff & (A[(k) >> 2] >> (((k) & 3 ) << 3)))

typedef unsigned int cc_u32;
typedef unsigned char cc_u8;
struct minimal_coredump_params {
	const siginfo_t *siginfo;
	struct pt_regs *regs;
	struct file *file;
#ifdef __KERNEL__
	int minimal_core_state;
	unsigned long crc;
	/* shift register contents */
	unsigned char *comp_buf;
	unsigned int total_size;
	unsigned long seed_crc;
	z_stream def_strm;
#define AES_BLK_SIZE 16
#define AES_KEY_LEN 16
#define RSA_ENC_AES_KEY_SIZE 128
#define AES_ENC_BUF_SIZE (16 * 1024)

#define ENCODE_IN_SIZE 96
#define ENCODE_OUT_SIZE 128
#define RSA_KEYBYTE_SIZE 128
	int bufs_index;
	char buf_encode_out[ENCODE_OUT_SIZE];
	char buf_encode_in[ENCODE_IN_SIZE];
	char buf_encode_in_aes[16];
	cc_u32 rsa_keybytelen;
	struct crypto_cipher *cip;
	char *aes_enc_buf;
	cc_u8 aes_key[AES_KEY_LEN];	/* Generated AES Key. */
	int rsa_enc_aes_key;		/* RSA Encrypted Key. */
	rsakey_t *rsa_key;		/* RSA Key. */
	MPI rsa_out;			/* RSA Encrypted base. */
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	int overflow_tsk_flag;
	struct list_head file_rlimit_list;
#endif /* CONFIG_MINCORE_RLIMIT_NOFILE */

#endif /* __KERNEL__ */
};

/* 4294967295, HARDCODED, but will avoid an unncessary kmalloc
* will work as long as ulong is 32 bits and it is so in 64 bit too */
#define ULONG_LEN 10

/* An ELF note in memory */
struct memelfnote {
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

#ifdef CONFIG_ARM64
	#define ARM_NREGS 30
#else
	#define ARM_NREGS 14
#endif

#define LAST_WRITE 1

/* Too huge, but this is only for debugging, DO NOT ENABLE in production */
/* Enable for page level debugging */
/* #define MINIMAL_CORE_DEBUG_DETAIL 1 */
/* Enable this to debug all deflate blocks */
/* #define MINIMAL_CORE_DEBUG_DEFLATEBLOCK 1 */

/* refer to include/linux/zlib.h notes for zlib_deflate() comments for
   * explanation regarding 12 bytes
   */
#define MINIMAL_CORE_COMP_BUFSIZE (2 * PAGE_SIZE + 12)
#define MINIMAL_CORE_INIT_STATE 0
#define MINIMAL_CORE_COMP_STATE 1
#define MINIMAL_CORE_FINI_STATE 2

/* Written based on gzip-1.6 algorithm.doc */
struct gzip_header {
	unsigned char id[2];
	unsigned char cm;
	unsigned char flag;
	unsigned char mtime[4];
	unsigned char xfl;
	unsigned char os;
};

/* from gzip-1.6 util.c */
extern unsigned long updcrc(unsigned long *seed_crc, unsigned char const *s,
				unsigned long n);

struct elf_thread_core_info {
	struct elf_thread_core_info *next;
	struct task_struct *task;
	struct elf_prstatus prstatus;
	struct memelfnote notes[0];
};

struct elf_note_info {
	struct elf_thread_core_info *thread;
	struct memelfnote psinfo;
	struct memelfnote signote;
	struct memelfnote auxv;
	struct memelfnote files;
	siginfo_t csigdata;
	size_t size;
	int thread_notes;
};

extern int minmal_core_fill_note(struct elfhdr *elf, int phdrs,
		struct elf_note_info *info,
		const siginfo_t *siginfo, struct pt_regs *regs);
extern void minimal_core_fill_phdr(struct elf_phdr *phdr, int sz,
		loff_t offset);
extern void minimal_core_free_note(struct elf_note_info *info);

struct svma_struct {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_flags;
	unsigned long vm_pgoff;
	struct file *vm_file;
	unsigned long dumped;
	struct list_head node;
};

struct addrvalue {
	unsigned long start;
	unsigned long end;
	/* To avoid to many loops
	 * this flag will be set to 0
	 * if an addr range is merged with other VMA
	 *                OR
	 * if an addr range is made invalid
	 */
	unsigned int dumped;
};

#define MINCORE_NAME ".mcore"
/* NOTE : MININMAL_CORE_LOCATION should have
* basedir and filedir, the last level is called
* as filedir and it will be the only thing mincore
* will attempt to create if not present
*/
#define MINIMAL_CORE_LOCATION "/opt/share/minicore/"

extern char target_version[];

#define MINIMAL_CORE_TORETAIN 20UL
struct mcore_name_list {
	char *name;
	struct list_head list;
};

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
#define NT_SABSP_FILE_INFO	(int)(0xdeadbeaf)
#define pr_rlimit(fmt, ...)	\
	pr_emerg("Mincore_rlimit_nofile: "fmt, ##__VA_ARGS__)
extern spinlock_t file_rlimit_lock;
extern struct list_head open_file_task_list;
void file_rlimit_dump(void);
struct fdtable *minimal_core_fd_copy(void);
void free_open_file_task_list(void);
void mincore_free_fdtable(struct fdtable *fdt);

struct open_file_info {
	int fd;
	fmode_t	f_mode;
	pid_t owner_pid;
	loff_t f_pos;
	const char *owner_name;
	const char *name;
	struct list_head node;
	struct task_struct *tsk;
};

struct open_file_task {
	pid_t tgid;
	struct list_head node;
};
#endif /* CONFIG_MINCORE_RLIMIT_NOFILE */

#ifdef CONFIG_MINCORE_ANON_VMA

#define NT_SABSP_MAPS	(int)(0xdead0000)
#define MINCORE_ANON		0x0000
#define MINCORE_VDSO		0x0001
#define MINCORE_HEAP		0x0002
#define MINCORE_STACK		0x0004
#define MINCORE_GATE		0x0008
#define MINCORE_SIGPAGE		0x0010
#define MINCORE_FILE            0x0020

#define pr_vma(fmt, ...)	\
	pr_emerg("Mincore_anon_vma: "fmt, ##__VA_ARGS__)

struct anon_vma_info {
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_flags;
	unsigned long vm_perm;
	struct list_head node;
};
#endif /* CONFIG_MINCORE_ANON_VMA */

#endif /* _MINCORE_H */
