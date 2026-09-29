/*
 * Copyright 2025 Cix Technology Group Co., Ltd.
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

#ifndef _CIX_VA_DDI_H_
#define _CIX_VA_DDI_H_

#include <stdint.h>
#include <va/va_backend.h>

#define MMAPI_EXPORT __attribute__((visibility("default")))

class CixVaInterface {
public:
    virtual ~CixVaInterface() {}

    static VAStatus Initialize(
        VADriverContextP ctx
    );

    static VAStatus LoadFunction(
        VADriverContextP ctx
    );

    static VAStatus Terminate(
        VADriverContextP ctx
    );

    static VAStatus QueryConfigEntrypoints(
        VADriverContextP ctx,
        VAProfile profile,
        VAEntrypoint *entrypoint_list,
        int *num_entrypoints
    );

    static VAStatus QueryConfigProfiles(
        VADriverContextP ctx,
        VAProfile *profile_list,
        int *num_profiles
    );

    static VAStatus QueryConfigAttributes(
        VADriverContextP ctx,
        VAConfigID config_id,
        VAProfile *profile,
        VAEntrypoint *entrypoint,
        VAConfigAttrib *attrib_list,
        int *num_attribs
    );

    static VAStatus CreateConfig(
        VADriverContextP ctx,
        VAProfile profile,
        VAEntrypoint entrypoint,
        VAConfigAttrib *attrib_list,
        int num_attribs,
        VAConfigID *config_id
    );

    static VAStatus DestroyConfig(
        VADriverContextP ctx,
        VAConfigID config_id
    );

    static VAStatus GetConfigAttributes(
        VADriverContextP ctx,
        VAProfile profile,
        VAEntrypoint entrypoint,
        VAConfigAttrib *attrib_list,
        int num_attribs
    );

    static VAStatus CreateSurfaces(
        VADriverContextP ctx,
        int width,
        int height,
        int format,
        int num_surfaces,
        VASurfaceID *surfaces
    );

    static VAStatus DestroySurfaces(
        VADriverContextP ctx,
        VASurfaceID *surface_list,
        int num_surfaces
    );

    static VAStatus CreateSurfaces2(
        VADriverContextP ctx,
        unsigned int format,
        unsigned int width,
        unsigned int height,
        VASurfaceID *surfaces,
        unsigned int num_surfaces,
        VASurfaceAttrib *attrib_list,
        unsigned int num_attribs
    );

    static VAStatus CreateContext(
        VADriverContextP ctx,
        VAConfigID config_id,
        int picture_width,
        int picture_height,
        int flag,
        VASurfaceID *render_targets,
        int num_render_targets,
        VAContextID *context
    );

    static VAStatus DestroyContext(
        VADriverContextP ctx,
        VAContextID context
    );

    static VAStatus CreateBuffer(
        VADriverContextP ctx,
        VAContextID context,
        VABufferType type,
        unsigned int size,
        unsigned int num_elements,
        void *data,
        VABufferID *buf_id
    );

    static VAStatus BufferSetNumElements(
        VADriverContextP ctx,
        VABufferID buf_id,
        unsigned int num_elements
    );

    static VAStatus MapBuffer(
        VADriverContextP ctx,
        VABufferID buf_id,
        void **pbuf
    );

    static VAStatus UnmapBuffer(
        VADriverContextP ctx,
        VABufferID buf_id
    );

    static VAStatus DestroyBuffer(
        VADriverContextP ctx,
        VABufferID buffer_id
    );

    static VAStatus BeginPicture(
        VADriverContextP ctx,
        VAContextID context,
        VASurfaceID render_target
    );

    static VAStatus RenderPicture(
        VADriverContextP ctx,
        VAContextID context,
        VABufferID *buffers,
        int num_buffers
    );

    static VAStatus EndPicture(
        VADriverContextP ctx,
        VAContextID context
    );

    static VAStatus SyncSurface(
        VADriverContextP ctx,
        VASurfaceID render_target
    );

#if VA_CHECK_VERSION(1, 9, 0)
    static VAStatus SyncBuffer(
        VADriverContextP ctx,
        VABufferID buf_id,
        uint64_t timeout_ns
    );
#endif

    static VAStatus QuerySurfaceStatus(
        VADriverContextP ctx,
        VASurfaceID render_target,
        VASurfaceStatus *status
    );

    static VAStatus QuerySurfaceError(
        VADriverContextP ctx,
        VASurfaceID render_target,
        VAStatus error_status,
        void **error_info
    );

    static VAStatus QuerySurfaceAttributes(
        VADriverContextP dpy,
        VAConfigID config,
        VASurfaceAttrib *attrib_list,
        unsigned int *num_attribs
    );

    static VAStatus QueryVideoProcFilters(
        VADriverContextP ctx,
        VAContextID context,
        VAProcFilterType *filters,
        unsigned int *num_filters
    );

    static VAStatus QueryVideoProcFilterCaps(
        VADriverContextP ctx,
        VAContextID context,
        VAProcFilterType type,
        void *filter_caps,
        unsigned int *num_filter_caps
    );

    static VAStatus QueryVideoProcPipelineCaps(
        VADriverContextP ctx,
        VAContextID context,
        VABufferID *filters,
        unsigned int num_filters,
        VAProcPipelineCaps *pipeline_caps
    );

    static VAStatus QueryImageFormats(
        VADriverContextP ctx,
        VAImageFormat *format_list,
        int *num_formats
    );

    static VAStatus CreateImage(
        VADriverContextP ctx,
        VAImageFormat *format,
        int width,
        int height,
        VAImage *image
    );

    static VAStatus DeriveImage(
        VADriverContextP ctx,
        VASurfaceID surface,
        VAImage *image
    );

    static VAStatus DestroyImage(
        VADriverContextP ctx,
        VAImageID image
    );

    static VAStatus GetImage(
        VADriverContextP ctx,
        VASurfaceID surface,
        int x,
        int y,
        unsigned int width,
        unsigned int height,
        VAImageID image
    );

    static VAStatus BufferInfo(
        VADriverContextP ctx,
        VABufferID buf_id,
        VABufferType *type,
        unsigned int *size,
        unsigned int *num_elements
    );

    static VAStatus AcquireBufferHandle(
        VADriverContextP ctx,
        VABufferID buf_id,
        VABufferInfo *buf_info
    );

    static VAStatus ReleaseBufferHandle(
        VADriverContextP ctx,
        VABufferID buf_id
    );

    static VAStatus ExportSurfaceHandle(
        VADriverContextP ctx,
        VASurfaceID surface_id,
        uint32_t mem_type,
        uint32_t flags,
        void *descriptor
    );

    static VAStatus PutSurface(
        VADriverContextP ctx,
        VASurfaceID surface,
        void *draw,
        int16_t src_x,
        int16_t src_y,
        uint16_t src_w,
        uint16_t src_h,
        int16_t dest_x,
        int16_t dest_y,
        uint16_t dest_w,
        uint16_t dest_h,
        VARectangle *cliprects,
        uint32_t number_cliprects,
        uint32_t flags
    );

    static VAStatus SetImagePalette(
        VADriverContextP ctx,
        VAImageID image,
        unsigned char *palette
    );

    static VAStatus PutImage(
        VADriverContextP ctx,
        VASurfaceID surface,
        VAImageID image,
        int src_x,
        int src_y,
        unsigned int src_width,
        unsigned int src_height,
        int dest_x,
        int dest_y,
        unsigned int dest_width,
        unsigned int dest_height
    );

    static VAStatus QuerySubpictureFormats(
        VADriverContextP ctx,
        VAImageFormat *format_list,
        unsigned int *flags,
        unsigned int *num_formats
    );

    static VAStatus CreateSubpicture(
        VADriverContextP ctx,
        VAImageID image,
        VASubpictureID *subpicture
    );

    static VAStatus DestroySubpicture(
        VADriverContextP ctx,
        VASubpictureID subpicture
    );

    static VAStatus SetSubpictureImage(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VAImageID image
    );

    static VAStatus SetSubpictureChromakey(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        unsigned int chromakey_min,
        unsigned int chromakey_max,
        unsigned int chromakey_mask
    );

    static VAStatus SetSubpictureGlobalAlpha(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        float global_alpha
    );

    static VAStatus AssociateSubpicture(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VASurfaceID *target_surfaces,
        int num_surfaces,
        short src_x,
        short src_y,
        unsigned short src_width,
        unsigned short src_height,
        short dest_x,
        short dest_y,
        unsigned short dest_width,
        unsigned short dest_height,
        unsigned int flags
    );

    static VAStatus DeassociateSubpicture(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VASurfaceID *target_surfaces,
        int num_surfaces
    );

    static VAStatus QueryDisplayAttributes(
        VADriverContextP ctx,
        VADisplayAttribute *attr_list,
        int *num_attributes
    );

    static VAStatus GetDisplayAttributes(
        VADriverContextP ctx,
        VADisplayAttribute *attr_list,
        int num_attributes
    );

    static VAStatus SetDisplayAttributes(
        VADriverContextP ctx,
        VADisplayAttribute *attr_list,
        int num_attributes
    );
};

#endif // _CIX_VA_DDI_H_