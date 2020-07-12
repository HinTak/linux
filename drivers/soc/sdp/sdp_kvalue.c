
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <soc/sdp/sdp_kvalue.h>

#if defined(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

enum {
	SDP_KVALUE_U32,
	SDP_KVALUE_PTR,
	SDP_KVALUE_MAX
};

struct sdp_kvalue_var {
	struct list_head list;
	char *key;
	int len;
	void *val;
	int type;
	int (*action)(void*, void*);
	void *arg;
	bool valid;
};

struct sdp_kvalue {
	struct list_head vars;
	struct mutex lock;
	bool init;
#if defined(CONFIG_DEBUG_FS)
	struct dentry *dir;
#endif

};

static struct sdp_kvalue g_kvalue;
static struct sdp_kvalue* get_kvalue(void)
{
	return &g_kvalue;
}

static struct sdp_kvalue_var* kvalue_add_var(struct sdp_kvalue *kvalue, const char *key)
{
	struct sdp_kvalue_var *var;

	var = kzalloc(sizeof(*var), GFP_KERNEL);
	if (!var)
		return NULL;

	var->key = (char*)key;
	var->len = strlen(key);
	list_add(&var->list, &kvalue->vars);

	return var;
}

static struct sdp_kvalue_var* kvalue_get_var(struct sdp_kvalue *kvalue, const char *key)
{
	struct sdp_kvalue_var *var;
	int len;

	len = strlen(key);
	list_for_each_entry(var, &kvalue->vars, list) {
		if (var->len == len && !strncmp(var->key, key, len))
			return var;
	}

	return NULL;
}

static int kvalue_set_u32(struct sdp_kvalue *kvalue, const char *key, u32 val)
{
	struct sdp_kvalue_var *var;
	int ret = 0;

	if (!key)
		return -EFAULT;

	var = kvalue_get_var(kvalue, key);
	if (!var) {
		var = kvalue_add_var(kvalue, key);
		if (!var)
			return -ENOMEM;
	}

	if (!var->valid) {
		var->type = SDP_KVALUE_U32;
		var->valid = true;
		
#if defined(CONFIG_DEBUG_FS)
		debugfs_create_u32(var->key, 0x444, kvalue->dir, (u32*)&var->val);
#endif
	}

	if (var->type != SDP_KVALUE_U32)
		return -EINVAL;
	
	var->val = (void*)val;

	if (var->action)
		ret = var->action(var->arg, var->val);
	
	return ret;
}

static int kvalue_get_u32(struct sdp_kvalue *kvalue, const char *key, u32 *val)
{
	struct sdp_kvalue_var *var;

	if (!key)
		return -EFAULT;

	if (!val)
		return -EFAULT;

	*val = 0;

	var = kvalue_get_var(kvalue, key);
	if (!var)
		return -ENOENT;

	if (var->type != SDP_KVALUE_U32)
		return -EINVAL;

	*val = (u32)var->val;
	return 0;
}

static int kvalue_set_ptr(struct sdp_kvalue *kvalue, const char *key, void *val)
{
	struct sdp_kvalue_var *var;
	int ret = 0;

	if (!key)
		return -EFAULT;

	var = kvalue_get_var(kvalue, key);
	if (!var) {
		var = kvalue_add_var(kvalue, key);
		if (!var)
			return -ENOMEM;
	}

	if (!var->valid) {
		var->type = SDP_KVALUE_PTR;
		var->valid = true;
		
#if defined(CONFIG_DEBUG_FS)
		if (sizeof(void*) == 4)
			debugfs_create_x32(var->key, 0x444, kvalue->dir, (u32*)&var->val);
		else if (sizeof(void*) == 8)
			debugfs_create_x64(var->key, 0x444, kvalue->dir, (u64*)&var->val);
#endif
	}

	if (var->type != SDP_KVALUE_PTR)
		return -EINVAL;
	
	var->val = (void*)val;

	if (var->action)
		ret = var->action(var->arg, var->val);
	
	return ret;
}

static int kvalue_get_ptr(struct sdp_kvalue *kvalue, const char *key, void **val)
{
	struct sdp_kvalue_var *var;

	if (!key)
		return -EFAULT;

	if (!val)
		return -EFAULT;

	*val = NULL;

	var = kvalue_get_var(kvalue, key);
	if (!var)
		return -ENOENT;

	if (var->type != SDP_KVALUE_PTR)
		return -EINVAL;

	*val = var->val;
	return 0;
}

static int kvalue_set_action(struct sdp_kvalue *kvalue, const char *key, int (*action)(void*, void*), void *arg)
{
	struct sdp_kvalue_var *var;
	int ret = 0;

	if (!key)
		return -EFAULT;

	var = kvalue_get_var(kvalue, key);
	if (!var) {
		var = kvalue_add_var(kvalue, key);
		if (!var)
			return -ENOMEM;
	}

	if (var->action)
		return -EACCES;
	
	var->action = action;
	var->arg = arg;

	if (var->valid)
		ret = var->action(var->arg, var->val);
	
	return ret;
}

int sdp_kvalue_setu32(const char *key, u32 val)
{
	struct sdp_kvalue *kvalue = get_kvalue();
	int ret;

	if (!kvalue->init)
		return -EAGAIN;
	
	mutex_lock(&kvalue->lock);
	ret = kvalue_set_u32(kvalue, key, val);
	mutex_unlock(&kvalue->lock);
	return ret;
}
EXPORT_SYMBOL(sdp_kvalue_setu32);

int sdp_kvalue_getu32(const char *key, u32 *val)
{
	struct sdp_kvalue *kvalue = get_kvalue();
	int ret;

	if (!kvalue->init)
		return -EAGAIN;
	
	mutex_lock(&kvalue->lock);
	ret = kvalue_get_u32(kvalue, key, val);
	mutex_unlock(&kvalue->lock);
	return ret;
}
EXPORT_SYMBOL(sdp_kvalue_getu32);

int sdp_kvalue_setptr(const char *key, void *val)
{
	struct sdp_kvalue *kvalue = get_kvalue();
	int ret;

	if (!kvalue->init)
		return -EAGAIN;
	
	mutex_lock(&kvalue->lock);
	ret = kvalue_set_ptr(kvalue, key, val);
	mutex_unlock(&kvalue->lock);
	return ret;
}
EXPORT_SYMBOL(sdp_kvalue_setptr);

int sdp_kvalue_getptr(const char *key, void **val)
{
	struct sdp_kvalue *kvalue = get_kvalue();
	int ret;

	if (!kvalue->init)
		return -EAGAIN;
	
	mutex_lock(&kvalue->lock);
	ret = kvalue_get_ptr(kvalue, key, val);
	mutex_unlock(&kvalue->lock);
	return ret;
}
EXPORT_SYMBOL(sdp_kvalue_getptr);

int sdp_kvalue_setaction(const char *key, int (*action)(void *arg, void *val), void *arg)
{
	struct sdp_kvalue *kvalue = get_kvalue();
	int ret;

	if (!kvalue->init)
		return -EAGAIN;
	
	mutex_lock(&kvalue->lock);
	ret = kvalue_set_action(kvalue, key, action, arg);
	mutex_unlock(&kvalue->lock);
	return ret;
}
EXPORT_SYMBOL(sdp_kvalue_setaction);


#define SDP_KVALUE_EARLY_MAX 16
static char *early_key[SDP_KVALUE_EARLY_MAX];
static void *early_val[SDP_KVALUE_EARLY_MAX];
static int early_cnt = 0;
int sdp_kvalue_setptr_early(const char *key, void *val)
{
	if (early_cnt >= SDP_KVALUE_EARLY_MAX)
		return -ENOSPC;

	early_key[early_cnt] = (char*)key;
	early_val[early_cnt] = val;
	early_cnt++;
	
	return 0;
}
static int set_early_keys(void)
{
	int i;
	for (i = 0; i < early_cnt; i++)
		sdp_kvalue_setptr(early_key[i], early_val[i]);
	return 0;
}

static int __init sdp_kvalue_init(void)
{
	struct sdp_kvalue *kvalue = get_kvalue();

	mutex_init(&kvalue->lock);

	INIT_LIST_HEAD(&kvalue->vars);

#if defined(CONFIG_DEBUG_FS)
	kvalue->dir = debugfs_create_dir("sdp_kvalue", NULL);
#endif

	kvalue->init = true;
	
	set_early_keys();

	return 0;
}

subsys_initcall(sdp_kvalue_init);


