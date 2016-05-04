/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#define HAVE_STDINT_H
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

#include <sys/mman.h>
#include <sys/ioctl.h>
#include "xf86drm.h"

#include "nouveau_drm.h"

struct nouveau_bo
{
	struct kms_bo base;
	uint64_t map_handle;
	unsigned map_count;
};

static int
nouveau_get_prop(struct kms_driver *kms, unsigned key, unsigned *out)
{
	switch (key) {
	case KMS_BO_TYPE:
		*out = KMS_BO_TYPE_SCANOUT_X8R8G8B8 | KMS_BO_TYPE_CURSOR_64X64_A8R8G8B8;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
nouveau_destroy(struct kms_driver *kms)
{
	free(kms);
	return 0;
}

static int
nouveau_bo_create(struct kms_driver *kms,
		 const unsigned width, const unsigned height,
		 const enum kms_bo_type type, const unsigned *attr,
		 struct kms_bo **out)
{
	struct drm_nouveau_gem_new arg;
	unsigned size, pitch;
	struct nouveau_bo *bo;
	int i, ret;

	for (i = 0; attr[i]; i += 2) {
		switch (attr[i]) {
		case KMS_WIDTH:
		case KMS_HEIGHT:
		case KMS_BO_TYPE:
			break;
		default:
			return -EINVAL;
		}
	}

	bo = calloc(1, sizeof(*bo));
	if (!bo)
		return -ENOMEM;

	if (type == KMS_BO_TYPE_CURSOR_64X64_A8R8G8B8) {
		pitch = 64 * 4;
		size = 64 * 64 * 4;
	} else if (type == KMS_BO_TYPE_SCANOUT_X8R8G8B8) {
		pitch = width * 4;
		pitch = (pitch + 512 - 1) & ~(512 - 1);
		size = pitch * height;
	} else {
		free(bo);
		return -EINVAL;
	}

	memset(&arg, 0, sizeof(arg));
	arg.info.size = size;
	arg.info.domain = NOUVEAU_GEM_DOMAIN_MAPPABLE | NOUVEAU_GEM_DOMAIN_VRAM;
	arg.info.tile_mode = 0;
	arg.info.tile_flags = 0;
	arg.align = 512;
	arg.channel_hint = 0;

	ret = drmCommandWriteRead(kms->fd, DRM_NOUVEAU_GEM_NEW, &arg, sizeof(arg));
	if (ret)
		goto err_free;

	bo->base.kms = kms;
	bo->base.handle = arg.info.handle;
	bo->base.size = size;
	bo->base.pitch = pitch;
	bo->map_handle = arg.info.map_handle;

	*out = &bo->base;

	return 0;

err_free:
	free(bo);
	return ret;
}

static int
nouveau_bo_get_prop(struct kms_bo *bo, unsigned key, unsigned *out)
{
	switch (key) {
	default:
		return -EINVAL;
	}
}

static int
nouveau_bo_map(struct kms_bo *_bo, void **out)
{
	struct nouveau_bo *bo = (struct nouveau_bo *)_bo;
	void *map = NULL;

	if (bo->base.ptr) {
		bo->map_count++;
		*out = bo->base.ptr;
		return 0;
	}

	map = mmap(0, bo->base.size, PROT_READ | PROT_WRITE, MAP_SHARED, bo->base.kms->fd, bo->map_handle);
	if (map == MAP_FAILED)
		return -errno;

	bo->base.ptr = map;
	bo->map_count++;
	*out = bo->base.ptr;

	return 0;
}

static int
nouveau_bo_unmap(struct kms_bo *_bo)
{
	struct nouveau_bo *bo = (struct nouveau_bo *)_bo;
	bo->map_count--;
	return 0;
}

static int
nouveau_bo_destroy(struct kms_bo *_bo)
{
	struct nouveau_bo *bo = (struct nouveau_bo *)_bo;
	struct drm_gem_close arg;
	int ret;

	if (bo->base.ptr) {
		/* XXX Sanity check map_count */
		munmap(bo->base.ptr, bo->base.size);
		bo->base.ptr = NULL;
	}

	memset(&arg, 0, sizeof(arg));
	arg.handle = bo->base.handle;

	ret = drmIoctl(bo->base.kms->fd, DRM_IOCTL_GEM_CLOSE, &arg);
	if (ret)
		return -errno;

	free(bo);
	return 0;
}

int
nouveau_create(int fd, struct kms_driver **out)
{
	struct kms_driver *kms;

	kms = calloc(1, sizeof(*kms));
	if (!kms)
		return -ENOMEM;

	kms->fd = fd;

	kms->bo_create = nouveau_bo_create;
	kms->bo_map = nouveau_bo_map;
	kms->bo_unmap = nouveau_bo_unmap;
	kms->bo_get_prop = nouveau_bo_get_prop;
	kms->bo_destroy = nouveau_bo_destroy;
	kms->get_prop = nouveau_get_prop;
	kms->destroy = nouveau_destroy;
	*out = kms;

	return 0;
}
