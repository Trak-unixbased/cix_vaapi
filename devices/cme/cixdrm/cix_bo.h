/*
 * Copyright (C) 2023 CiX Technologies Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __CIX_BO_H__
#define __CIX_BO_H__

//#include "xf86atomic.h"
#define CIX_BO_MAX_PLANES 4

enum cix_bo_type {
	CIX_BO_DUMB,
	CIX_BO_DMABUF
};

struct cix_bo
{
	int fd;	/* drm device fd */
	uint32_t fb;	/* fb id */
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint64_t modifier;
	uint32_t afbc_crop_left;
	uint32_t afbc_crop_right;
	uint32_t afbc_crop_top;
	uint32_t afbc_crop_bottom;
	size_t size;
	uint32_t plane_cnt;
	uint32_t plane_sizes[CIX_BO_MAX_PLANES];
	uint32_t nfds;
	int prime_fds[CIX_BO_MAX_PLANES];	/* dmabuf fds */
	uint32_t handles[CIX_BO_MAX_PLANES];
	uint32_t offsets[CIX_BO_MAX_PLANES];
	uint32_t pitches[CIX_BO_MAX_PLANES];
	void *ptrs[CIX_BO_MAX_PLANES];
	enum cix_bo_type type;
	uint32_t name;
	//atomic_t refcnt;
	int refcnt;
};

#endif
