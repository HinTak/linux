
#ifndef __SDP_BUSERR_H__
#define __SDP_BUSERR_H__

#define BUSERR_MAX_ENTRIES 32

struct sdp_buserr_entry {
	char *src;
	char *dst;
	u32 addr;
};

struct sdp_buserr_param {
	int n_entries;
	struct sdp_buserr_entry entries[BUSERR_MAX_ENTRIES];
};

int sdp_buserr_register_notifier(struct notifier_block *nb);
int sdp_buserr_unregister_notifier(struct notifier_block *nb);

#endif

