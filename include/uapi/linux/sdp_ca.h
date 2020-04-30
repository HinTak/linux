#ifndef _CA_H_
#define _CA_H_

#ifndef __ASSEMBLY_

#ifdef CONFIG_ARCH_SDP1106

/*****************************************************************
	Definition - Mechanism
*****************************************************************/
#define CA_IOC_SET_SCRM_KEY         0
#define CA_IOC_SET_DESCRM_KEY       1
#define CA_IOC_CLEAR_SCRM_KEY       2

#define CA_DEV_NODE_LEN     32
#define CA_CRYPT_KEY_LEN    32
#define CA_CRYPT_IV_LEN     32

enum ca_key_type
{
	CA_TYPE_DES     = 1,		/* DES type */
	CA_TYPE_TDES    = 2,		/* TDES type */
	CA_TYPE_AES     = 3,		/* AES type*/
	CA_TYPE_MULTI2  = 4,		/* MULTI2 type*/
	CA_TYPE_MAX     = 100,	    /* MAX type*/
};

enum ca_key_mode
{
	CA_MODE_ECB = 1,		/* ECB mode */
	CA_MODE_CBC = 2,		/* CBC mode */
	CA_MODE_CTR = 3,		/* CTR mode*/
	CA_MODE_MAX = 100,	    /* MAX mode*/
};

enum ca_key_flag
{
	CA_FLAG_IGNORE  = 1,		/* CA_IGNORE flag */
	CA_FLAG_EVEN    = 2,		/* EVEN flag */
	CA_FLAG_ODD     = 3,		/* ODD flag*/
	CA_FLAG_MAX     = 100,	    /* MAX flag*/
};

struct  ca_key_info
{
    /*correnponing device node string ,In case of scramble, it will be dvr node.
        In ohter case,  it will be dmx node
      */
    uint8_t dev_node[CA_DEV_NODE_LEN];
    uint8_t type;
    uint8_t mode;
    uint8_t flag;
    uint8_t key_table[CA_CRYPT_KEY_LEN];	// key value
    unsigned int key_len;		// key length
    uint8_t iv_table[CA_CRYPT_KEY_LEN];	// iv value
    unsigned int iv_len;		// iv length
};

#endif
#endif /*!__ASSEMBLY__*/
#endif	/* _CA_H_ */
