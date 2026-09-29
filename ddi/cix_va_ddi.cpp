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

#include "cix_va_ddi.h"
#include "device_common.h"
#include <va/va_backend_vpp.h>
#include <cstring>
#include "log.h"

VAStatus CixVaInterface::Terminate(
    VADriverContextP ctx
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    ctx->pDriverData = nullptr;
    delete device;
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QueryConfigEntrypoints(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint *entrypoints,
    int *num_entrypoints
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->QueryConfigEntrypoints(profile, entrypoints, num_entrypoints);
}

VAStatus CixVaInterface::QueryConfigProfiles(
    VADriverContextP ctx,
    VAProfile *profile_list,
    int *num_profiles
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->QueryConfigProfiles(profile_list, num_profiles);
}

VAStatus CixVaInterface::GetConfigAttributes(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->GetConfigAttributes(profile, entrypoint, attrib_list, num_attribs);
}

VAStatus CixVaInterface::CreateConfig(
    VADriverContextP ctx,
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs,
    VAConfigID *config_id
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->CreateConfig(profile, entrypoint, attrib_list, num_attribs, config_id);
}

VAStatus CixVaInterface::DestroyConfig(
    VADriverContextP ctx,
    VAConfigID config_id
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->DestroyConfig(config_id);
}

VAStatus CixVaInterface::QueryConfigAttributes(
    VADriverContextP ctx,
    VAConfigID config_id,
    VAProfile *profile,
    VAEntrypoint *entrypoint,
    VAConfigAttrib *attrib_list,
    int *num_attribs
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->QueryConfigAttributes(config_id, profile, entrypoint, attrib_list, num_attribs);
}

VAStatus CixVaInterface::CreateSurfaces(
    VADriverContextP ctx,
    int width,
    int height,
    int format,
    int num_surfaces,
    VASurfaceID *surfaces
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);

    return device->CreateSurfaces(width, height, format, num_surfaces, surfaces);
}

VAStatus CixVaInterface::DestroySurfaces(
    VADriverContextP ctx,
    VASurfaceID *surface_list,
    int num_surfaces
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->DestroySurfaces(surface_list, num_surfaces);
}

VAStatus CixVaInterface::CreateSurfaces2(
    VADriverContextP ctx,
    unsigned int format,
    unsigned int width,
    unsigned int height,
    VASurfaceID *surfaces,
    unsigned int num_surfaces,
    VASurfaceAttrib *attrib_list,
    unsigned int num_attribs
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->CreateSurfaces2(width, height, format, num_surfaces, surfaces, attrib_list, num_attribs);
}

VAStatus CixVaInterface::CreateContext(
    VADriverContextP ctx,
    VAConfigID config_id,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context
) {
    CIX_VAAPI_ENTER_FUNCTION;

    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->CreateContext(config_id, picture_width, picture_height,
        flag, render_targets, num_render_targets, context);
}

VAStatus CixVaInterface::DestroyContext(
    VADriverContextP ctx,
    VAContextID context
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->DestroyContext(context);
}

VAStatus CixVaInterface::CreateBuffer(
    VADriverContextP ctx,
    VAContextID context,
    VABufferType type,
    unsigned int size,
    unsigned int num_elements,
    void *data,
    VABufferID *buf_id
) {
    // CIX_VAAPI_ENTER_FUNCTION;
    if (size == 0 || num_elements == 0 || buf_id == nullptr)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    VAStatus ret = device->CreateBuffer(context, type, size, num_elements, data, buf_id);
    // CIX_VAAPI_INFO("Create buffer %d\n", buf_id[0]);
    return ret;
}

VAStatus CixVaInterface::BufferSetNumElements(
    VADriverContextP ctx,
    VABufferID buf_id,
    unsigned int num_elements
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::MapBuffer(
    VADriverContextP ctx,
    VABufferID buf_id,
    void **pbuf
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->MapBuffer(buf_id, pbuf);
}

VAStatus CixVaInterface::UnmapBuffer(
    VADriverContextP ctx,
    VABufferID buf_id
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->UnmapBuffer(buf_id);
}

VAStatus CixVaInterface::DestroyBuffer(
    VADriverContextP ctx,
    VABufferID buf_id
) {
    // CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    // CIX_VAAPI_INFO("Destroy buffer %d\n", buf_id);
    return device->DestroyBuffer(buf_id);
}

VAStatus CixVaInterface::BeginPicture(
    VADriverContextP ctx,
    VAContextID context,
    VASurfaceID render_target
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->BeginPicture(context, render_target);
}

VAStatus CixVaInterface::RenderPicture(
    VADriverContextP ctx,
    VAContextID context,
    VABufferID *buffers,
    int num_buffers
) {
    if (buffers == nullptr || num_buffers == 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->RenderPicture(context, buffers, num_buffers);
}

VAStatus CixVaInterface::EndPicture(
    VADriverContextP ctx,
    VAContextID context
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->EndPicture(context);
}

VAStatus CixVaInterface::SyncSurface(
    VADriverContextP ctx,
    VASurfaceID render_target
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->SyncSurface(render_target);
}

#if VA_CHECK_VERSION(1, 9, 0)
VAStatus CixVaInterface::SyncBuffer(
    VADriverContextP ctx,
    VABufferID buf_id,
    uint64_t timeout_ns
) {
    if (buf_id == VA_INVALID_ID)
        return VA_STATUS_SUCCESS;

    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->SyncBuffer(buf_id);
}
#endif

VAStatus CixVaInterface::QuerySurfaceStatus(
    VADriverContextP ctx,
    VASurfaceID render_target,
    VASurfaceStatus *status
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QuerySurfaceError(
    VADriverContextP ctx,
    VASurfaceID render_target,
    VAStatus error_status,
    void **error_info
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QuerySurfaceAttributes(
    VADriverContextP ctx,
    VAConfigID config_id,
    VASurfaceAttrib *attrib_list,
    unsigned int *num_attribs
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);

    if (num_attribs == nullptr)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    return device->QuerySurfaceAttributes(config_id, attrib_list, num_attribs);
}

VAStatus CixVaInterface::QueryVideoProcFilters(
    VADriverContextP ctx,
    VAContextID context,
    VAProcFilterType *filters,
    unsigned int *num_filters
) {
    CIX_VAAPI_ENTER_FUNCTION;
    // No explicit filters needed - scaling/cvtcolor are implicitly supported
    (void)ctx;
    (void)context;
    (void)filters;
    *num_filters = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QueryVideoProcFilterCaps(
    VADriverContextP ctx,
    VAContextID context,
    VAProcFilterType type,
    void *filter_caps,
    unsigned int *num_filter_caps
) {
    CIX_VAAPI_ENTER_FUNCTION;
    (void)ctx;
    (void)context;
    (void)type;
    (void)filter_caps;
    *num_filter_caps = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QueryVideoProcPipelineCaps(
    VADriverContextP ctx,
    VAContextID context,
    VABufferID *filters,
    unsigned int num_filters,
    VAProcPipelineCaps *pipeline_caps
) {
    CIX_VAAPI_ENTER_FUNCTION;
    (void)ctx;
    (void)filters;
    (void)num_filters;

    if (pipeline_caps == nullptr) {
        CIX_VAAPI_ERROR("QueryVideoProcPipelineCaps: pipeline_caps is null for context 0x%x\n", context);
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    // Zero-init entire struct to prevent caller from reading uninitialized fields
    // (especially input/output_color_standards pointers and counts)
    memset(pipeline_caps, 0, sizeof(VAProcPipelineCaps));

    pipeline_caps->pipeline_flags = 0;
    pipeline_caps->filter_flags = 0;
    pipeline_caps->num_backward_references = 0;
    pipeline_caps->num_forward_references = 0;
    pipeline_caps->rotation_flags = (1 << VA_ROTATION_NONE) | (1 << VA_ROTATION_90) |
        (1 << VA_ROTATION_180) | (1 << VA_ROTATION_270);
    pipeline_caps->blend_flags = VA_BLEND_GLOBAL_ALPHA;
    pipeline_caps->mirror_flags = 0;
    pipeline_caps->num_additional_outputs = 0;
    pipeline_caps->max_input_width = 8192;
    pipeline_caps->max_input_height = 8192;
    pipeline_caps->min_input_width = 64;
    pipeline_caps->min_input_height = 64;
    pipeline_caps->max_output_width = 8192;
    pipeline_caps->max_output_height = 8192;
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QueryImageFormats(
    VADriverContextP ctx,
    VAImageFormat *format_list,
    int *num_formats
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->QueryImageFormats(format_list, num_formats);
}

VAStatus CixVaInterface::CreateImage(
    VADriverContextP ctx,
    VAImageFormat *format,
    int width,
    int height,
    VAImage *image
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->CreateImage(format, width, height, image);
}

VAStatus CixVaInterface::DeriveImage(
    VADriverContextP ctx,
    VASurfaceID surface,
    VAImage *image
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->DeriveImage(surface, image);
}

VAStatus CixVaInterface::DestroyImage(
    VADriverContextP ctx,
    VAImageID image
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->DestroyImage(image);
}

VAStatus CixVaInterface::GetImage(
    VADriverContextP ctx,
    VASurfaceID surface,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    VAImageID image
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->GetImage(surface, x, y, width, height, image);
}

VAStatus CixVaInterface::BufferInfo(
    VADriverContextP ctx,
    VABufferID buf_id,
    VABufferType *type,
    unsigned int *size,
    unsigned int *num_elements
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::AcquireBufferHandle(
    VADriverContextP ctx,
    VABufferID buf_id,
    VABufferInfo *buf_info
) {
    CIX_VAAPI_ENTER_FUNCTION;
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->AcquireBufferHandle(buf_id, buf_info);
}

VAStatus CixVaInterface::ReleaseBufferHandle(
    VADriverContextP ctx,
    VABufferID buf_id
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::ExportSurfaceHandle(
    VADriverContextP ctx,
    VASurfaceID surface,
    uint32_t mem_type,
    uint32_t flags,
    void *descriptor
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    return device->ExportSurfaceHandle(surface, mem_type, flags, descriptor);
}

VAStatus CixVaInterface::PutSurface(
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
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::SetImagePalette(
    VADriverContextP ctx,
    VAImageID image,
    unsigned char* palette
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::PutImage(
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
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QuerySubpictureFormats(
    VADriverContextP ctx,
    VAImageFormat* format_list,
    unsigned int* flags,
    unsigned int* num_formats
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::CreateSubpicture(
    VADriverContextP ctx,
    VAImageID image,
    VASubpictureID* subpicture
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::DestroySubpicture(
    VADriverContextP ctx,
    VASubpictureID subpicture
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::SetSubpictureImage(
    VADriverContextP ctx,
    VASubpictureID subpicture,
    VAImageID image
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::SetSubpictureChromakey(
    VADriverContextP ctx,
    VASubpictureID subpicture,
    unsigned int chromakey_min,
    unsigned int chromakey_max,
    unsigned int chromakey_mask
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::SetSubpictureGlobalAlpha(
    VADriverContextP ctx,
    VASubpictureID subpicture,
    float global_alpha
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::AssociateSubpicture(
    VADriverContextP ctx,
    VASubpictureID subpicture,
    VASurfaceID* target_surfaces,
    int num_surfaces,
    short srcx,
    short srcy,
    unsigned short srcw,
    unsigned short srch,
    short destx,
    short desty,
    unsigned short destw,
    unsigned short desth,
    unsigned int flags
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::DeassociateSubpicture(
    VADriverContextP ctx,
    VASubpictureID subpicture,
    VASurfaceID* target_surfaces,
    int num_surfaces
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::QueryDisplayAttributes(
    VADriverContextP ctx,
    VADisplayAttribute* attr_list,
    int* num_attributes
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::GetDisplayAttributes(
    VADriverContextP ctx,
    VADisplayAttribute* attr_list,
    int num_attributes
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::SetDisplayAttributes(
    VADriverContextP ctx,
    VADisplayAttribute* attr_list,
    int num_attributes
) {
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    CIX_VAAPI_WARNING("%s() is not implemented!\n", __func__);
    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::LoadFunction(VADriverContextP ctx)
{
    MediaDevice *device = static_cast<MediaDevice *>(ctx->pDriverData);
    struct VADriverVTable *pVTable = ctx->vtable;

    ctx->version_major                       = VA_MAJOR_VERSION;
    ctx->version_minor                       = VA_MINOR_VERSION;
    ctx->max_profiles                        = device->GetMaxProfiles();
    ctx->max_entrypoints                     = device->GetMaxEntrypoints();
    ctx->max_attributes                      = (int32_t)VAConfigAttribTypeMax;
    ctx->max_subpic_formats                  = device->GetMaxSubpicFormats();
    ctx->max_display_attributes              = device->GetMaxDisplayAttributes();
    ctx->max_image_formats                   = device->GetMaxImageFormats();
    ctx->str_vendor                          = device->GetMaxVendorString();
    ctx->vtable_tpi                          = nullptr;
    ctx->vtable_glx                          = nullptr;
    ctx->vtable_egl                          = nullptr;

    pVTable->vaTerminate                     = CixVaInterface::Terminate;
    pVTable->vaQueryConfigEntrypoints        = CixVaInterface::QueryConfigEntrypoints;
    pVTable->vaQueryConfigProfiles           = CixVaInterface::QueryConfigProfiles;
    pVTable->vaQueryConfigAttributes         = CixVaInterface::QueryConfigAttributes;
    pVTable->vaCreateConfig                  = CixVaInterface::CreateConfig;
    pVTable->vaDestroyConfig                 = CixVaInterface::DestroyConfig;
    pVTable->vaGetConfigAttributes           = CixVaInterface::GetConfigAttributes;

    pVTable->vaCreateSurfaces                = CixVaInterface::CreateSurfaces;
    pVTable->vaDestroySurfaces               = CixVaInterface::DestroySurfaces;
    pVTable->vaCreateSurfaces2               = CixVaInterface::CreateSurfaces2;

    pVTable->vaCreateContext                 = CixVaInterface::CreateContext;
    pVTable->vaDestroyContext                = CixVaInterface::DestroyContext;
    pVTable->vaCreateBuffer                  = CixVaInterface::CreateBuffer;
    pVTable->vaBufferSetNumElements          = CixVaInterface::BufferSetNumElements;
    pVTable->vaMapBuffer                     = CixVaInterface::MapBuffer;
    pVTable->vaUnmapBuffer                   = CixVaInterface::UnmapBuffer;
    pVTable->vaDestroyBuffer                 = CixVaInterface::DestroyBuffer;
    pVTable->vaBeginPicture                  = CixVaInterface::BeginPicture;
    pVTable->vaRenderPicture                 = CixVaInterface::RenderPicture;
    pVTable->vaEndPicture                    = CixVaInterface::EndPicture;
    pVTable->vaSyncSurface                   = CixVaInterface::SyncSurface;
#if VA_CHECK_VERSION(1, 9, 0)
    pVTable->vaSyncSurface2                  = nullptr;
    pVTable->vaSyncBuffer                    = CixVaInterface::SyncBuffer;
#endif
    pVTable->vaQuerySurfaceStatus            = CixVaInterface::QuerySurfaceStatus;
    pVTable->vaQuerySurfaceError             = CixVaInterface::QuerySurfaceError;
    pVTable->vaQuerySurfaceAttributes        = CixVaInterface::QuerySurfaceAttributes;
    pVTable->vaPutSurface                    = CixVaInterface::PutSurface;
    pVTable->vaQueryImageFormats             = CixVaInterface::QueryImageFormats;

    pVTable->vaCreateImage                   = CixVaInterface::CreateImage;
    pVTable->vaDeriveImage                   = CixVaInterface::DeriveImage;
    pVTable->vaDestroyImage                  = CixVaInterface::DestroyImage;
    pVTable->vaSetImagePalette               = CixVaInterface::SetImagePalette;
    pVTable->vaGetImage                      = CixVaInterface::GetImage;
    pVTable->vaPutImage                      = CixVaInterface::PutImage;
    pVTable->vaQuerySubpictureFormats        = CixVaInterface::QuerySubpictureFormats;
    pVTable->vaCreateSubpicture              = CixVaInterface::CreateSubpicture;
    pVTable->vaDestroySubpicture             = CixVaInterface::DestroySubpicture;
    pVTable->vaSetSubpictureImage            = CixVaInterface::SetSubpictureImage;
    pVTable->vaSetSubpictureChromakey        = CixVaInterface::SetSubpictureChromakey;
    pVTable->vaSetSubpictureGlobalAlpha      = CixVaInterface::SetSubpictureGlobalAlpha;
    pVTable->vaAssociateSubpicture           = CixVaInterface::AssociateSubpicture;
    pVTable->vaDeassociateSubpicture         = CixVaInterface::DeassociateSubpicture;
    pVTable->vaQueryDisplayAttributes        = CixVaInterface::QueryDisplayAttributes;
    pVTable->vaGetDisplayAttributes          = CixVaInterface::GetDisplayAttributes;
    pVTable->vaSetDisplayAttributes          = CixVaInterface::SetDisplayAttributes;
    pVTable->vaQueryProcessingRate           = nullptr;
#if VA_CHECK_VERSION(1,10,0)
    pVTable->vaCopy                          = nullptr;
#endif

    // vaTrace
    pVTable->vaBufferInfo                    = CixVaInterface::BufferInfo;
    pVTable->vaLockSurface                   = nullptr;
    pVTable->vaUnlockSurface                 = nullptr;

    pVTable->vaGetSurfaceAttributes          = nullptr;
    pVTable->vaAcquireBufferHandle           = CixVaInterface::AcquireBufferHandle;
    pVTable->vaReleaseBufferHandle           = CixVaInterface::ReleaseBufferHandle;
    pVTable->vaExportSurfaceHandle           = CixVaInterface::ExportSurfaceHandle;

    // VPP vtable
    if (ctx->vtable_vpp == nullptr) {
        ctx->vtable_vpp = (VADriverVTableVPP *)calloc(1, sizeof(VADriverVTableVPP));
        if (ctx->vtable_vpp)
            ctx->vtable_vpp->version = VA_DRIVER_VTABLE_VPP_VERSION;
    }
    if (ctx->vtable_vpp) {
        ctx->vtable_vpp->vaQueryVideoProcFilters        = CixVaInterface::QueryVideoProcFilters;
        ctx->vtable_vpp->vaQueryVideoProcFilterCaps     = CixVaInterface::QueryVideoProcFilterCaps;
        ctx->vtable_vpp->vaQueryVideoProcPipelineCaps   = CixVaInterface::QueryVideoProcPipelineCaps;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus CixVaInterface::Initialize(VADriverContextP ctx)
{
    VAStatus status = VA_STATUS_SUCCESS;
    int32_t fd = 0;

    MediaDevice *device = MediaDevice::CreateDevice();
    ctx->pDriverData = device;

    if (CixVaInterface::LoadFunction(ctx) != VA_STATUS_SUCCESS) {
        ctx->pDriverData = nullptr;
        delete device;
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    return VA_STATUS_SUCCESS;
}

#ifdef __cplusplus
extern "C" {
#endif

#define VA_DRV_INIT(_major,_minor) __vaDriverInit_##_major##_##_minor
#define VA_DRV_INIT_FUNC(va_major_version, va_minor_version) VA_DRV_INIT(va_major_version,va_minor_version)
#define VA_DRV_INIT_FUNC_NAME VA_DRV_INIT_FUNC(VA_MAJOR_VERSION,VA_MINOR_VERSION)

MMAPI_EXPORT VAStatus VA_DRV_INIT_FUNC_NAME(VADriverContextP ctx)
{
    SetLogLevel();
    return CixVaInterface::Initialize(ctx);
}

#ifdef __cplusplus
}
#endif

