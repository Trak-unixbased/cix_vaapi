/*
* Copyright 2026 Cix Technology Group Co., Ltd.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __CME_IMG_H
#define __CME_IMG_H

#include "cme_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CME_PLANES  3

typedef enum {
    CME_BUFFER_INTERNAL_DMA = 0,
    CME_BUFFER_EXTERNAL_DMA,
    CME_BUFFER_EXTERNAL_MEMORY,
    CME_BUFFER_OPENCL_CLMEM,  /* OpenCL CLMem allocated with CL_MEM_ALLOC_HOST_PTR */
}CME_BUFFER_MODE;

typedef struct {
    int nplanes;                        /* plane number of this image */
    void* vir_addr[MAX_CME_PLANES];     /* virtual address */
    int dma_fd[MAX_CME_PLANES];         /* dma fd ,we can only support one fd for one cme img now*/
    int offset[MAX_CME_PLANES];         /* offset */
    void* cl_mem[MAX_CME_PLANES];       /* OpenCL cl_mem for CLMEM mode (physical memory) */
    void* plane_wrapper[MAX_CME_PLANES]; /* Cached Image2D wrapper for all buffer modes */
    void* cl_plane0_buf;                /* OpenCL buffer for plane 0 (for contiguous multi-plane DMA-BUF import) */
    int map_by_cme;                     /* cme may map exteranl memeory if needed, but not necessarily*/

    int x;
    int y;
    int width;                          /* width */
    int height;                         /* height */
    int stride[MAX_CME_PLANES][2];      /* width & hegiht stride */
    int tlength;                        /* total length */
    IMG_FORMAT format;                  /* format */
    int buffer_mode;                    /* INTERNAL_DMA,EXTERNAL_DMA,EXTERNAL_MEMORY,OPENCL_CLMEM*/
    int colorspace_mode;                /* CME_COLOR_SPACE_MODE */
}cme_img;

typedef struct {
    int width;
    int height;
    IMG_FORMAT format;
    int ifconherent;
    int colormode;
    int use_clmem;          /* 1: use OpenCL CLMEM (CL_MEM_ALLOC_HOST_PTR), 0: use DMA-BUF */
    int stride[MAX_CME_PLANES][2];  /* Custom stride [plane][0]=width stride, [plane][1]=height stride.
                                       Set to 0 to use default stride calculation. */
}alloc_img_ctrl;

typedef struct {
    void* vir_addr[MAX_CME_PLANES];     /* virtual address ,optinal*/
    int nplanes;
    int dma_fd[MAX_CME_PLANES];         /* dma fd*/
    int offset[MAX_CME_PLANES];         /* offset of each plane */
    int width;
    int height;
    int stride[MAX_CME_PLANES][2];
    int format;
}external_buffer_ctrl;

/**
 * allocate one image by cme. it use dmabuf usually.
 * @param ctrl
 *       parameters of allocation demand
 * @returns cme_img
 *       NULL: fail, or pointer to allocated cme_img
 */
cme_img* alloc_cme_img(alloc_img_ctrl* ctrl, int *error);

/**
 * free one image by cme.
 * @param img
 *       pointer to cme_img
 * @returns
 *       success or else negative error code.
 */
CME_RET free_cme_img(cme_img* img);

/**
 * import cme img from external buffer
 * @param external_buffer_ctrl
 *       parameters of external buffer
 * @returns cme_img
 *       NULL: fail, or pointer to allocated cme_img
 */
cme_img* import_from_external_buffer(external_buffer_ctrl* ctrl, int *error);

#ifdef __cplusplus
}
#endif

#endif
