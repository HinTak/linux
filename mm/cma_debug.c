/*
 * CMA DebugFS Interface
 *
 * Copyright (c) 2015 Sasha Levin <sasha.levin@oracle.com>
 */


#include <linux/debugfs.h>
#include <linux/cma.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm_types.h>

#include "cma.h"
#include "internal.h"
#ifdef CONFIG_CMA_DEBUG
#include <linux/stacktrace.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/device.h>

/**
 * cma_get_num_pages() - get number of pages
 * @cma: region to check
 *
 * This function returns count of all pages in cma region.
 */
unsigned long cma_get_num_pages(struct cma *cma)
{
	if (!cma)
		return 0;
	return cma->count;
}
#endif /* CONFIG_CMA_DEBUG */

#define K(x) ((x) << (PAGE_SHIFT - 10))

struct cma_mem {
	struct hlist_node node;
	struct page *p;
	unsigned long n;
};

static struct dentry *cma_debugfs_root;

static int cma_debugfs_get(void *data, u64 *val)
{
	unsigned long *p = data;

	*val = *p;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_debugfs_fops, cma_debugfs_get, NULL, "%llx\n");

static int cma_used_get(void *data, u64 *val)
{
	struct cma *cma = data;
	unsigned long used;

	mutex_lock(&cma->lock);
	/* pages counter is smaller than sizeof(int) */
	used = bitmap_weight(cma->bitmap, (int)cma_bitmap_maxno(cma));
	mutex_unlock(&cma->lock);
	*val = (u64)used << cma->order_per_bit;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_used_fops, cma_used_get, NULL, "%llu\n");

static int cma_maxchunk_get(void *data, u64 *val)
{
	struct cma *cma = data;
	unsigned long maxchunk = 0;
	unsigned long start, end = 0;
    unsigned long bitmap_maxno = cma_bitmap_maxno(cma); 

	mutex_lock(&cma->lock);
	for (;;) {
		start = find_next_zero_bit(cma->bitmap, bitmap_maxno, end);
		if (start >= cma->count)
			break;
		end = find_next_bit(cma->bitmap, bitmap_maxno, start);
		maxchunk = max(end-start, maxchunk);
	}
	mutex_unlock(&cma->lock);
	*val = (u64)maxchunk << cma->order_per_bit;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cma_maxchunk_fops, cma_maxchunk_get, NULL, "%llu\n");

static void cma_add_to_cma_mem_list(struct cma *cma, struct cma_mem *mem)
{
	spin_lock(&cma->mem_head_lock);
	hlist_add_head(&mem->node, &cma->mem_head);
	spin_unlock(&cma->mem_head_lock);
}

static struct cma_mem *cma_get_entry_from_list(struct cma *cma)
{
	struct cma_mem *mem = NULL;

	spin_lock(&cma->mem_head_lock);
	if (!hlist_empty(&cma->mem_head)) {
		mem = hlist_entry(cma->mem_head.first, struct cma_mem, node);
		hlist_del_init(&mem->node);
	}
	spin_unlock(&cma->mem_head_lock);

	return mem;
}

static int cma_free_mem(struct cma *cma, int count)
{
	struct cma_mem *mem = NULL;

	while (count) {
		mem = cma_get_entry_from_list(cma);
		if (mem == NULL)
			return 0;

		if (mem->n <= count) {
			cma_release(cma, mem->p, mem->n);
			count -= mem->n;
			kfree(mem);
		} else if (cma->order_per_bit == 0) {
			cma_release(cma, mem->p, count);
			mem->p += count;
			mem->n -= count;
			count = 0;
			cma_add_to_cma_mem_list(cma, mem);
		} else {
			pr_debug("cma: cannot release partial block when order_per_bit != 0\n");
			cma_add_to_cma_mem_list(cma, mem);
			break;
		}
	}

	return 0;

}

static int cma_free_write(void *data, u64 val)
{
	int pages = val;
	struct cma *cma = data;

	return cma_free_mem(cma, pages);
}
DEFINE_SIMPLE_ATTRIBUTE(cma_free_fops, NULL, cma_free_write, "%llu\n");

static int cma_alloc_mem(struct cma *cma, int count)
{
	struct cma_mem *mem;
	struct page *p;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	p = cma_alloc(cma, count, 0);
	if (!p) {
		kfree(mem);
		return -ENOMEM;
	}

	mem->p = p;
	mem->n = count;

	cma_add_to_cma_mem_list(cma, mem);

	return 0;
}

static int cma_alloc_write(void *data, u64 val)
{
	int pages = val;
	struct cma *cma = data;

	return cma_alloc_mem(cma, pages);
}
DEFINE_SIMPLE_ATTRIBUTE(cma_alloc_fops, NULL, cma_alloc_write, "%llu\n");

#ifdef CONFIG_CMA_DEBUG
#include <linux/stacktrace.h>
#include <linux/page_ext.h>
#include <linux/mmzone.h>
#include <linux/page-flags.h>
#include <linux/uaccess.h>
#include "internal.h"

#ifdef CONFIG_PAGE_OWNER
static ssize_t
print_cma_page_owner(char __user *buf, size_t count, unsigned long pfn,
		struct page *page, struct page_ext *page_ext)
{
	int ret;
	int pageblock_mt, page_mt;
	char *kbuf;
	unsigned long entries[PAGE_OWNER_STACK_DEPTH];
	struct stack_trace trace = {
		.nr_entries = 0,
		.entries = entries,
		.max_entries = PAGE_OWNER_STACK_DEPTH,
		.skip = 0
	};
	depot_stack_handle_t handle;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = snprintf(kbuf, count,
			"Page allocated via order %u, mask 0x%x\n",
			page_ext->order, page_ext->gfp_mask);

	if (ret >= count)
		goto err;

	ret += snprintf(kbuf + ret, count - ret,
			"[detail][count:%d, mapping:%p, mapcount:%d index:%lu]\n",
			page_ref_count(page), READ_ONCE(page->mapping),
			atomic_read(&page->_mapcount), page->index);

	handle = READ_ONCE(page_ext->handle);
	if (!handle) {
		pr_alert("page_owner info is not active (free page?)\n");
		goto err;
	}

	depot_fetch_stack(handle, &trace);
	/* Print information relevant to grouping pages by mobility */
	pageblock_mt = get_pfnblock_migratetype(page, pfn);
	page_mt  = gfpflags_to_migratetype(page_ext->gfp_mask);
	ret += snprintf(kbuf + ret, count - ret,
			"PFN %lu Page %p Block %lu type %d %s Flags %s%s%s%s%s%s%s%s%s%s%s%s\n"
			"PID %d\n",
			pfn,
			page,
			pfn >> pageblock_order,
			pageblock_mt,
			pageblock_mt != page_mt ? "Fallback" : "        ",
			PageLocked(page)    ? "K" : " ",
			PageError(page)     ? "E" : " ",
			PageReferenced(page)    ? "R" : " ",
			PageUptodate(page)  ? "U" : " ",
			PageDirty(page)     ? "D" : " ",
			PageLRU(page)       ? "L" : " ",
			PageActive(page)    ? "A" : " ",
			PageSlab(page)      ? "S" : " ",
			PageWriteback(page) ? "W" : " ",
			PageCompound(page)  ? "C" : " ",
			PageSwapCache(page) ? "B" : " ",
			PageMappedToDisk(page)  ? "M" : " ",
			page_ext->pid);

	if (ret >= count)
		goto err;

	ret += snprint_stack_trace(kbuf + ret, count - ret, &trace, 0);
	if (ret >= count)
		goto err;

	ret += snprintf(kbuf + ret, count - ret, "\n");
	if (ret >= count)
		goto err;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;

err:
	kfree(kbuf);

	return -ENOMEM;
}

static ssize_t
read_cma_page_owner(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{

	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;
	struct cma *cma = file->f_inode->i_private;

	page = NULL;
	pfn = cma->base_pfn + *ppos;

	drain_all_pages(NULL);

	/* Find an allocated page */
	for (; pfn < (cma->base_pfn+cma->count); pfn++) {
		/*
		* If the new page is in a new MAX_ORDER_NR_PAGES area,
		* validate the area as existing, skip it if not
		*/
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = page_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = lookup_page_ext(page);
		/*
		* Some pages could be missed by concurrent allocation or free,
		* because we don't hold the zone lock.
		*/
		if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
			continue;
		/* Record the next PFN to read in the file offset */
		*ppos = (pfn - cma->base_pfn) + 1;

		return print_cma_page_owner(buf, count, pfn, page, page_ext);
	}

	return 0;
}
static const struct file_operations cma_page_owner_fops = {
	.read		= read_cma_page_owner,
};

static ssize_t
read_user_fallback_usage(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{

	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;
	struct cma *cma = file->f_inode->i_private;
	unsigned long total_used_page = 0;
	unsigned long user_fallback;
	char *kbuf;
	int ret;

	page = NULL;
	pfn = cma->base_pfn;

	drain_all_pages(NULL);

	if (*ppos != 0)
		return 0;

	/* Find an allocated page */
	for (; pfn < (cma->base_pfn + cma->count); pfn++) {
		/*
		* If the new page is in a new MAX_ORDER_NR_PAGES area,
		* validate the area as existing, skip it if not
		*/
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = page_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = lookup_page_ext(page);
		/*
		* Some pages could be missed by concurrent allocation or free,
		* because we don't hold the zone lock.
		*/
		if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
			continue;

		total_used_page += 1 << page_ext->order;
	}

	user_fallback = total_used_page * 4 - K(cma_get_used_pages(cma));

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	
	ret = snprintf(kbuf, count,
			"Total:%6luKB, Total_used:%6luKB(DEV:%6luKB, USER:%6luKB)\n",
			K(cma_get_num_pages(cma)),
			total_used_page * 4, 
			K(cma_get_used_pages(cma)),
			user_fallback);

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);

	*ppos += ret;

	return ret;
}

static const struct file_operations cma_user_fallback_fops = {
	.read		= read_user_fallback_usage,
};
#endif

static ssize_t
show_cma_alloc_info(char __user *buf, size_t count,
		struct cma_buffer *cmabuf, size_t pre_count)
{
	int ret;
	struct stack_trace trace;
	char *kbuf;
	unsigned long usec, sec;
	uint64_t latency;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	sec = jiffies_to_usecs(cmabuf->tick);
	usec = do_div(sec, USEC_PER_SEC);
	latency = div_s64(cmabuf->latency, NSEC_PER_USEC);
	ret = snprintf(kbuf, count, "0x%llx - 0x%llx (%lu kB), "
			"allocated by pid %u (%s), alloc time: %lu.%lu latency %llu us\n",
			(uint64_t)PFN_PHYS(cmabuf->pfn),
			(uint64_t)PFN_PHYS(cmabuf->pfn + cmabuf->count),
			(cmabuf->count * PAGE_SIZE) >> 10,
			cmabuf->pid, cmabuf->comm, sec, usec, latency);

	trace.nr_entries = cmabuf->nr_entries;
	trace.entries = &cmabuf->trace_entries[0];

	ret += snprint_stack_trace(kbuf + ret, count - ret, &trace, 0);

	ret += snprintf(kbuf + ret, count-ret, "\n");

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	if (ret > 0)
		ret += pre_count;

	kfree(kbuf);
	return ret;
}

#define BUF_SIZE 256
static ssize_t
read_cma_alloc_info(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct cma *cma = file->f_inode->i_private;
	loff_t n = *ppos;
	struct cma_buffer *cmabuf;
	char kbuf[BUF_SIZE];
	size_t buf_count = 0;
	if (*ppos == 0) {
		buf_count = snprintf(kbuf, BUF_SIZE,
				"CMARegion stat: %8lu kB total, %8lu kB used, ",
				K(cma_get_num_pages(cma)),
				K(cma_get_used_pages(cma)));
		buf_count += snprintf(kbuf + buf_count, BUF_SIZE-buf_count,
				"%8lu kB max contiguous chunk\n\n",
				K(cma_get_maxchunk_pages(cma)));
		if (copy_to_user(buf, kbuf, buf_count))
			return -EFAULT;
		count -= buf_count;
	}

	mutex_lock(&cma->list_lock);

	cmabuf = list_first_entry(&cma->buffers_list, typeof(*cmabuf), list);
	while (n > 0 && &cmabuf->list != &cma->buffers_list) {
		n--;
		cmabuf = list_next_entry(cmabuf, list);
	}
	if (!n && &cmabuf->list != &cma->buffers_list) {
		mutex_unlock(&cma->list_lock);
		(*ppos)++;
		return show_cma_alloc_info(buf + buf_count,
				count, cmabuf, buf_count);
	}

	mutex_unlock(&cma->list_lock);

	return 0;
}

static ssize_t
read_cma_dev_info(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct cma *cma = file->f_inode->i_private;
	loff_t n = *ppos;
	struct cma_dev_map *cma_dev;
	char kbuf[256];
	size_t buf_count = 0;

	if (*ppos == 0)
		buf_count = snprintf(kbuf, 256, "CMA mapped dev list\n");

	mutex_lock(&cma->list_lock);

	cma_dev = list_first_entry(&cma->map_dev_list, typeof(*cma_dev), list);
	while (n > 0 && &cma_dev->list != &cma->map_dev_list) {
		n--;
		cma_dev = list_next_entry(cma_dev, list);
	}
	if (!n && &cma_dev->list != &cma->map_dev_list) {
		mutex_unlock(&cma->list_lock);
		(*ppos)++;
		if (cma_dev->dev != NULL)
			buf_count += snprintf(kbuf+buf_count, 256 - buf_count,
							"%d. %s\n", (int)*ppos,
							dev_name(cma_dev->dev));
		if (copy_to_user(buf, kbuf, buf_count))
			return -EFAULT;

		return buf_count;
	}

	mutex_unlock(&cma->list_lock);

	return 0;
}

static const struct file_operations cma_alloc_info_fops = {
	.read		= read_cma_alloc_info,
};

static const struct file_operations cma_dev_map_info_fops = {
	.read		= read_cma_dev_info,
};
#endif /*CONFIG_CMA_DEBUG*/

static void cma_debugfs_add_one(struct cma *cma, int idx)
{
	struct dentry *tmp;
	char name[16];
	int u32s;

	snprintf(name, sizeof(name), "cma-%d", idx);

	tmp = debugfs_create_dir(name, cma_debugfs_root);

	debugfs_create_file("alloc", S_IWUSR, tmp, cma,
				&cma_alloc_fops);

	debugfs_create_file("free", S_IWUSR, tmp, cma,
				&cma_free_fops);

	debugfs_create_file("base_pfn", S_IRUGO, tmp,
				&cma->base_pfn, &cma_debugfs_fops);
	debugfs_create_file("count", S_IRUGO, tmp,
				&cma->count, &cma_debugfs_fops);
	debugfs_create_file("order_per_bit", S_IRUGO, tmp,
				&cma->order_per_bit, &cma_debugfs_fops);
	debugfs_create_file("used", S_IRUGO, tmp, cma, &cma_used_fops);
	debugfs_create_file("maxchunk", S_IRUGO, tmp, cma, &cma_maxchunk_fops);

#ifdef CONFIG_CMA_DEBUG
	debugfs_create_file("cmainfo", S_IRUSR, tmp, cma,
				&cma_alloc_info_fops);
#ifdef CONFIG_PAGE_OWNER
	debugfs_create_file("page_owner", S_IRUSR, tmp, cma,
				&cma_page_owner_fops);
	debugfs_create_file("user_fallback", S_IRUSR, tmp, cma,
				&cma_user_fallback_fops);
#endif
	debugfs_create_file("cma_dev_list", S_IRUSR, tmp, cma,
				&cma_dev_map_info_fops);
#endif

	u32s = DIV_ROUND_UP(cma_bitmap_maxno(cma), BITS_PER_BYTE * sizeof(u32));
	debugfs_create_u32_array("bitmap", S_IRUGO, tmp, (u32*)cma->bitmap, u32s);
}

static int __init cma_debugfs_init(void)
{
	int i;

	cma_debugfs_root = debugfs_create_dir("cma", NULL);
	if (!cma_debugfs_root)
		return -ENOMEM;

	for (i = 0; i < cma_area_count; i++)
		cma_debugfs_add_one(&cma_areas[i], i);

	return 0;
}
late_initcall(cma_debugfs_init);
