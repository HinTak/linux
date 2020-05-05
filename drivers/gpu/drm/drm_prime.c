/*
 * Copyright © 2012 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *      Rob Clark <rob.clark@linaro.org>
 *
 */

#include <linux/export.h>
#include <linux/dma-buf.h>
#include <drm/drmP.h>

/*
 * DMA-BUF/GEM Object references and lifetime overview:
 *
 * On the export the dma_buf holds a reference to the exporting GEM
 * object. It takes this reference in handle_to_fd_ioctl, when it
 * first calls .prime_export and stores the exporting GEM object in
 * the dma_buf priv. This reference is released when the dma_buf
 * object goes away in the driver .release function.
 *
 * On the import the importing GEM object holds a reference to the
 * dma_buf (which in turn holds a ref to the exporting GEM object).
 * It takes that reference in the fd_to_handle ioctl.
 * It calls dma_buf_get, creates an attachment to it and stores the
 * attachment in the GEM object. When this attachment is destroyed
 * when the imported object is destroyed, we remove the attachment
 * and drop the reference to the dma_buf.
 *
 * Thus the chain of references always flows in one direction
 * (avoiding loops): importing_gem -> dmabuf -> exporting_gem
 *
 * Self-importing: if userspace is using PRIME as a replacement for flink
 * then it will get a fd->handle request for a GEM object that it created.
 * Drivers should detect this situation and return back the gem object
 * from the dma-buf private.
 */

struct drm_prime_member {
	struct list_head entry;
	struct dma_buf *dma_buf;
	uint32_t handle;
};
static int drm_prime_add_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t handle);

static struct dma_buf *drm_prime_lookup_buf_by_handle(struct drm_prime_file_private *prime_fpriv,
						      uint32_t handle)
{
	struct drm_prime_member *member;

	list_for_each_entry(member, &prime_fpriv->head, entry) {
		if (member->handle == handle)
			return member->dma_buf;
	}

	return NULL;
}
void drm_prime_remove_buf_handle_locked(struct drm_prime_file_private *prime_fpriv,
					struct dma_buf *dma_buf)
{
	struct drm_prime_member *member, *safe;

	list_for_each_entry_safe(member, safe, &prime_fpriv->head, entry) {
		if (member->dma_buf == dma_buf) {
			dma_buf_put(dma_buf);
			list_del(&member->entry);
			kfree(member);
		}
	}
}

static struct dma_buf *export_and_register_object(struct drm_device *dev,
						  struct drm_gem_object *obj,
						  uint32_t flags)
{
	struct dma_buf *dmabuf;

	/* prevent races with concurrent gem_close. */
	if (atomic_read(&obj->handle_count) == 0) {
		dmabuf = ERR_PTR(-ENOENT);
		return dmabuf;
	}

	dmabuf = dev->driver->gem_prime_export(dev, obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		return dmabuf;
	}

	/*
	 * Note that callers do not need to clean up the export cache
	 * since the check for obj->handle_count guarantees that someone
	 * will clean it up.
	 */
	obj->export_dma_buf= dmabuf;
	get_dma_buf(obj->export_dma_buf);
	/* Grab a new ref since the callers is now used by the dma-buf */
	drm_gem_object_reference(obj);

	return dmabuf;
}
int drm_gem_prime_handle_to_fd(struct drm_device *dev,
		struct drm_file *file_priv, uint32_t handle, uint32_t flags,
		int *prime_fd)
{
	struct drm_gem_object *obj;
	int ret = 0;
	struct dma_buf *dmabuf;

	mutex_lock(&file_priv->prime.lock);
	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj)  {
		ret = -ENOENT;
		goto out_unlock;
	}

	dmabuf = drm_prime_lookup_buf_by_handle(&file_priv->prime, handle);
	if (dmabuf) {
		get_dma_buf(dmabuf);
		goto out_have_handle;
	}

	mutex_lock(&dev->object_name_lock);
	/* re-export the original imported object */
	if (obj->import_attach) {
		dmabuf = obj->import_attach->dmabuf;
		get_dma_buf(dmabuf);
		goto out_have_obj;
	}

	if (obj->export_dma_buf) {
		get_dma_buf(obj->export_dma_buf);
		dmabuf = obj->export_dma_buf;
		goto out_have_obj;
	}

	dmabuf = export_and_register_object(dev, obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		ret = PTR_ERR(dmabuf);
		mutex_unlock(&dev->object_name_lock);
		goto out;
	}

out_have_obj:
	/*
	 * If we've exported this buffer then cheat and add it to the import list
	 * so we get the correct handle back. We must do this under the
	 * protection of dev->object_name_lock to ensure that a racing gem close
	 * ioctl doesn't miss to remove this buffer handle from the cache.
	 */
	ret = drm_prime_add_buf_handle(&file_priv->prime,
				       dmabuf, handle);
	mutex_unlock(&dev->object_name_lock);
	if (ret)
		goto fail_put_dmabuf;

out_have_handle:
	ret = dma_buf_fd(dmabuf, flags);
	/*
	 * We must _not_ remove the buffer from the handle cache since the newly
	 * created dma buf is already linked in the global obj->dma_buf pointer,
	 * and that is invariant as long as a userspace gem handle exists.
	 * Closing the handle will clean out the cache anyway, so we don't leak.
	 */
	if (ret < 0) {
		goto fail_put_dmabuf;
	} else {
		*prime_fd = ret;
		ret = 0;
	}

	goto out;

fail_put_dmabuf:
	dma_buf_put(dmabuf);
out:
	drm_gem_object_unreference_unlocked(obj);
out_unlock:
	mutex_unlock(&file_priv->prime.lock);

	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_handle_to_fd);

int drm_gem_prime_fd_to_handle(struct drm_device *dev,
		struct drm_file *file_priv, int prime_fd, uint32_t *handle)
{
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	int ret;

	dma_buf = dma_buf_get(prime_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	mutex_lock(&file_priv->prime.lock);

	ret = drm_prime_lookup_buf_handle(&file_priv->prime,
			dma_buf, handle);
	if (ret == 0)
		goto out_put;

	/* never seen this one, need to import */
	mutex_lock(&dev->object_name_lock);
	obj = dev->driver->gem_prime_import(dev, dma_buf);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		mutex_unlock(&dev->object_name_lock);
		goto out_put;
	}

	if (obj->export_dma_buf) {
		WARN_ON(obj->export_dma_buf != dma_buf);
	} else {
		obj->export_dma_buf = dma_buf;
		get_dma_buf(dma_buf);
	}

	/* drm_gem_handle_create_tail unlocks dev->object_name_lock. */
	ret = drm_gem_handle_create_tail(file_priv, obj, handle);
	drm_gem_object_unreference_unlocked(obj);
	if (ret)
		goto out_put;

	ret = drm_prime_add_buf_handle(&file_priv->prime,
			dma_buf, *handle);
	if (ret)
		goto fail;

	mutex_unlock(&file_priv->prime.lock);

	dma_buf_put(dma_buf);

	return 0;

fail:
	/* hmm, if driver attached, we are relying on the free-object path
	 * to detach.. which seems ok..
	 */
	drm_gem_handle_delete(file_priv, *handle);
out_put:
	dma_buf_put(dma_buf);
	mutex_unlock(&file_priv->prime.lock);
	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_fd_to_handle);

int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;
	uint32_t flags;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EINVAL;

	if (!dev->driver->prime_handle_to_fd)
		return -ENOSYS;

	/* check flags are valid */
	if (args->flags & ~DRM_CLOEXEC)
		return -EINVAL;

	/* we only want to pass DRM_CLOEXEC which is == O_CLOEXEC */
	flags = args->flags & DRM_CLOEXEC;

	return dev->driver->prime_handle_to_fd(dev, file_priv,
			args->handle, flags, &args->fd);
}

int drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EINVAL;

	if (!dev->driver->prime_fd_to_handle)
		return -ENOSYS;

	return dev->driver->prime_fd_to_handle(dev, file_priv,
			args->fd, &args->handle);
}

/*
 * drm_prime_pages_to_sg
 *
 * this helper creates an sg table object from a set of pages
 * the driver is responsible for mapping the pages into the
 * importers address space
 */
struct sg_table *drm_prime_pages_to_sg(struct page **pages, int nr_pages)
{
	struct sg_table *sg = NULL;
	struct scatterlist *iter;
	int i;
	int ret;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg)
		goto out;

	ret = sg_alloc_table(sg, nr_pages, GFP_KERNEL);
	if (ret)
		goto out;

	for_each_sg(sg->sgl, iter, nr_pages, i)
		sg_set_page(iter, pages[i], PAGE_SIZE, 0);

	return sg;
out:
	kfree(sg);
	return NULL;
}
EXPORT_SYMBOL(drm_prime_pages_to_sg);

/* export an sg table into an array of pages and addresses
   this is currently required by the TTM driver in order to do correct fault
   handling */
int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
				     dma_addr_t *addrs, int max_pages)
{
	unsigned count;
	struct scatterlist *sg;
	struct page *page;
	u32 len, offset;
	int pg_index;
	dma_addr_t addr;

	pg_index = 0;
	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		len = sg->length;
		offset = sg->offset;
		page = sg_page(sg);
		addr = sg_dma_address(sg);

		while (len > 0) {
			if (WARN_ON(pg_index >= max_pages))
				return -1;
			pages[pg_index] = page;
			if (addrs)
				addrs[pg_index] = addr;

			page++;
			addr += PAGE_SIZE;
			len -= PAGE_SIZE;
			pg_index++;
		}
	}
	return 0;
}
EXPORT_SYMBOL(drm_prime_sg_to_page_addr_arrays);
/* helper function to cleanup a GEM/prime object */
void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg)
{
	struct dma_buf_attachment *attach;
	struct dma_buf *dma_buf;
	attach = obj->import_attach;
	if (sg)
		dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
	dma_buf = attach->dmabuf;
	dma_buf_detach(attach->dmabuf, attach);
	/* remove the reference */
	dma_buf_put(dma_buf);
}
EXPORT_SYMBOL(drm_prime_gem_destroy);

void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv)
{
	INIT_LIST_HEAD(&prime_fpriv->head);
	mutex_init(&prime_fpriv->lock);
}
EXPORT_SYMBOL(drm_prime_init_file_private);

void drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv)
{
	struct drm_prime_member *member, *safe;
	list_for_each_entry_safe(member, safe, &prime_fpriv->head, entry) {
		list_del(&member->entry);
		kfree(member);
	}
}
EXPORT_SYMBOL(drm_prime_destroy_file_private);

static int drm_prime_add_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t handle)
{
	struct drm_prime_member *member;

	member = kmalloc(sizeof(*member), GFP_KERNEL);
	if (!member)
		return -ENOMEM;

	get_dma_buf(dma_buf);
	member->dma_buf = dma_buf;
	member->handle = handle;
	list_add(&member->entry, &prime_fpriv->head);
	return 0;
}

int drm_prime_lookup_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf, uint32_t *handle)
{
	struct drm_prime_member *member;

	list_for_each_entry(member, &prime_fpriv->head, entry) {
		if (member->dma_buf == dma_buf) {
			*handle = member->handle;
			return 0;
		}
	}
	return -ENOENT;
}
EXPORT_SYMBOL(drm_prime_lookup_buf_handle);

void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv, struct dma_buf *dma_buf)
{
	struct drm_prime_member *member, *safe;

	mutex_lock(&prime_fpriv->lock);
	list_for_each_entry_safe(member, safe, &prime_fpriv->head, entry) {
		if (member->dma_buf == dma_buf) {
			dma_buf_put(dma_buf);
			list_del(&member->entry);
			kfree(member);
		}
	}
	mutex_unlock(&prime_fpriv->lock);
}
EXPORT_SYMBOL(drm_prime_remove_buf_handle);
