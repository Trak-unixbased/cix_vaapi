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

#include <cstring>
#include <mutex>
#include <va/va.h>
#include <va/va_vpp.h>
#include <va/va_drmcommon.h>
#include "cme_device.h"
#include "device_common.h"
#include "log.h"

// Context IDs start at 0x80000000 so they never collide with V4L2 contexts.
#define CME_CONTEXT_ID_BASE 0x80000000u

// ==================== Constructor / Destructor / Initialize ====================

MediaDeviceCme::MediaDeviceCme(MediaDevice &media_device) :
    media_device_(media_device),
    cme_context_count(0),
    initialized_(false),
    available_(false)
{
}

MediaDeviceCme::~MediaDeviceCme() {
#if CIX_VAAPI_HAVE_CME
    for (auto &pair : cme_contexts)
        delete pair.second;
    cme_contexts.clear();

    // Surfaces are destroyed before this object (see ~MediaDevice), so the
    // map should be empty; release anything left over anyway.
    {
        std::lock_guard<std::mutex> lock(img_map_mutex_);
        for (auto &pair : img_map_)
            free_cme_img(pair.second);
        img_map_.clear();
    }

    if (available_)
        cme_destroy();
#endif
}

// libcme is initialized lazily here instead of in the constructor, and its
// result is now taken into account: VPP used to be advertised even when
// cme_initialize() failed.
bool MediaDeviceCme::Initialize() {
    if (initialized_)
        return available_;
    initialized_ = true;

#if CIX_VAAPI_HAVE_CME
    CME_RET ret = cme_initialize();
    if (ret != CME_RET_SUCCESS) {
        CIX_VAAPI_WARNING("cme_initialize failed (ret=%d): video processing disabled\n", ret);
        available_ = false;
    } else {
        available_ = true;
    }
#else
    CIX_VAAPI_INFO("Built without libcme: video processing disabled\n");
    available_ = false;
#endif

    return available_;
}

// ==================== Resource callbacks (parallel to V4l2Stateful) ====================

SurfacePool *MediaDeviceCme::GetSurfacePoolBySurfaceID(VASurfaceID id) {
    return media_device_.GetSurfacePoolBySurfaceID(id);
}

BufferPool *MediaDeviceCme::GetBufferPool() {
    return media_device_.GetBufferPool();
}

uint32_t MediaDeviceCme::GetNumPlanes(uint32_t format) {
    return media_device_.GetNumPlanes(format);
}

uint32_t MediaDeviceCme::GetRowBpp(uint32_t format, int32_t plane) {
    return media_device_.GetRowBpp(format, plane);
}

// ==================== Config management ====================

// VPP: fixed return VAProfileNone (only 1), nothing when libcme is unavailable
VAStatus MediaDeviceCme::QueryConfigProfiles(
    VAProfile *profile_list,
    int *num_profiles) {
    if (!available_) {
        *num_profiles = 0;
        return VA_STATUS_SUCCESS;
    }
    profile_list[0] = VAProfileNone;
    *num_profiles = 1;
    return VA_STATUS_SUCCESS;
}

// VPP: only VAProfileNone -> VAEntrypointVideoProc
VAStatus MediaDeviceCme::QueryConfigEntrypoints(
    VAProfile profile,
    VAEntrypoint *entrypoint_list,
    int *num_entrypoints) {
    if (available_ && profile == VAProfileNone) {
        entrypoint_list[0] = VAEntrypointVideoProc;
        *num_entrypoints = 1;
        return VA_STATUS_SUCCESS;
    }
    *num_entrypoints = 0;
    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

// VPP: only VAConfigAttribRTFormat
VAStatus MediaDeviceCme::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs) {
    if (!available_ || profile != VAProfileNone || entrypoint != VAEntrypointVideoProc)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

    for (int i = 0; i < num_attribs; i++) {
        if (attrib_list[i].type == VAConfigAttribRTFormat) {
            attrib_list[i].value = VA_RT_FORMAT_YUV420 | VA_RT_FORMAT_YUV420_10 |
                                   VA_RT_FORMAT_YUV422 | VA_RT_FORMAT_RGBP;
        } else {
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
        }
    }
    return VA_STATUS_SUCCESS;
}

// VPP: pixel formats handled by libcme, DRM PRIME export, 64..8192
VAStatus MediaDeviceCme::QuerySurfaceAttributes(
    const DeviceConfig *config,
    VASurfaceAttrib *attrib_list,
    unsigned int *num_attribs) {
    (void)config;
    if (attrib_list == nullptr) {
        *num_attribs = CIX_VAAPI_MAX_SURFACE_ATTRIBUTES;
        return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(*num_attribs >= CIX_VAAPI_MAX_SURFACE_ATTRIBUTES,
        VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "attrib_list size %u is too small\n", *num_attribs);

    static const uint32_t formats[] = {
        VA_FOURCC_NV12, VA_FOURCC_NV21, VA_FOURCC_P010,
        VA_FOURCC_I420, VA_FOURCC_YUY2, VA_FOURCC_RGBP,
    };

    uint32_t i = 0;
    for (uint32_t fourcc : formats) {
        SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, fourcc,
                                VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    }
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinWidth, 64);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinHeight, 64);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxWidth, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxHeight, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMemoryType,
                            VA_SURFACE_ATTRIB_MEM_TYPE_VA | VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    *num_attribs = i;

    return VA_STATUS_SUCCESS;
}

// ==================== Context management ====================

// VPP: creates CmeContext, calls CreateSurfaceById for render targets
VAStatus MediaDeviceCme::CreateContext(
    const DeviceConfig *config,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context) {
    (void)config;
    (void)flag;
#if CIX_VAAPI_HAVE_CME
    CIX_VAAPI_CHECK_RETURN_CODE(available_, VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT,
        "Video processing is not available\n");

    // Create Surface objects for each render target
    for (int i = 0; i < num_render_targets; i++) {
        auto pool = GetSurfacePoolBySurfaceID(render_targets[i]);
        CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
            "Surface pool not found for VPP render target %u\n", render_targets[i]);
        CIX_VAAPI_CHECK_RETURN_CODE(pool->CreateSurfaceById(render_targets[i]) == 0,
            VA_STATUS_ERROR_ALLOCATION_FAILED,
            "Failed to allocate VPP render target %u\n", render_targets[i]);
    }

    uint32_t id = CME_CONTEXT_ID_BASE + cme_context_count;
    cme_contexts[id] = new CmeContext(id, picture_width, picture_height);
    *context = (VAContextID)id;
    CIX_VAAPI_INFO("Created VPP context 0x%x (wxh: %dx%d)\n", id, picture_width, picture_height);
    cme_context_count++;
    return VA_STATUS_SUCCESS;
#else
    (void)picture_width; (void)picture_height; (void)render_targets;
    (void)num_render_targets; (void)context;
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
#endif
}

// VPP: delete cme_context (no RemoveContextFromSurfaces, VPP context not bound to pool)
VAStatus MediaDeviceCme::DestroyContext(VAContextID context) {
    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    CIX_VAAPI_INFO("Destroy VPP context 0x%x\n", context);
#if CIX_VAAPI_HAVE_CME
    delete it->second;
#endif
    cme_contexts.erase(it);
    return VA_STATUS_SUCCESS;
}

bool MediaDeviceCme::HasContextId(VAContextID context) const {
    return cme_contexts.find(context) != cme_contexts.end();
}

// ==================== Buffer/Picture ====================

// VPP: only supports VAProcPipelineParameterBufferType and VAProcFilterParameterBufferType
VAStatus MediaDeviceCme::CreateBuffer(
    VAContextID context,
    VABufferType type,
    unsigned int size,
    unsigned int num_elements,
    void *data,
    VABufferID *buf_id) {
    CIX_VAAPI_DEBUG("VPP CreateBuffer: context 0x%x, type %d, size %u, num_elements %u\n",
        context, type, size, num_elements);

    if (type != VAProcPipelineParameterBufferType &&
        type != VAProcFilterParameterBufferType)
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;

    uint64_t total = (uint64_t)size * num_elements;
    CIX_VAAPI_CHECK_RETURN_CODE(total > 0 && total <= UINT32_MAX,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid VPP buffer size\n");

    auto bpool = GetBufferPool();
    auto buf = bpool->GetFreeSlot(type, context);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create buffer\n");
    CIX_VAAPI_DEBUG("VPP CreateBuffer: assigned buf_id=%u, type=%d, ctx=0x%x\n",
        buf->id, type, context);

    buf->buf = new Buffer((uint32_t)total);
    if (buf->buf->GetPtr() == nullptr) {
        bpool->ReleaseSlot(buf->id);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    buf->size = size;
    buf->num_elements = num_elements;
    // Without initial data the client fills the buffer with vaMapBuffer().
    if (data)
        memcpy(buf->buf->GetPtr(), data, (size_t)total);

    *buf_id = buf->id;
    return VA_STATUS_SUCCESS;
}

// VPP: CmeContext->BeginPicture (sets output_surface, clears pipeline_entries)
VAStatus MediaDeviceCme::BeginPicture(VAContextID context, VASurfaceID render_target) {
    CIX_VAAPI_DEBUG("VPP BeginPicture: output_surface=%u\n", render_target);

    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

#if CIX_VAAPI_HAVE_CME
    return it->second->BeginPicture(render_target, &media_device_);
#else
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
#endif
}

// VPP: CmeContext->RenderPicture (collects pipeline entries)
VAStatus MediaDeviceCme::RenderPicture(
    VAContextID context,
    VABufferID *buffers,
    int num_buffers) {
    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

#if CIX_VAAPI_HAVE_CME
    return it->second->RenderPicture(buffers, num_buffers, &media_device_);
#else
    (void)buffers; (void)num_buffers;
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
#endif
}

// VPP: CmeContext->EndPicture (ExecuteSinglePipeline or ExecuteOverlay)
VAStatus MediaDeviceCme::EndPicture(VAContextID context) {
    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

#if CIX_VAAPI_HAVE_CME
    return it->second->EndPicture(&media_device_);
#else
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
#endif
}

// VPP: finds context via pool, HasContextId, then CmeContext->SyncSurface
VAStatus MediaDeviceCme::SyncSurface(VASurfaceID render_target) {
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %u not found during VPP SyncSurface\n", render_target);

    auto context = pool->GetContextBySurfaceID(render_target);
    auto it = cme_contexts.find(context);
    if (context == VA_INVALID_ID || it == cme_contexts.end()) {
        CIX_VAAPI_DEBUG("Surface %u is not a VPP surface, no need to sync\n", render_target);
        return VA_STATUS_SUCCESS;
    }

#if CIX_VAAPI_HAVE_CME
    return it->second->SyncSurface(render_target, &media_device_);
#else
    return VA_STATUS_SUCCESS;
#endif
}

// VPP: hardcoded formats supported by libcme
VAStatus MediaDeviceCme::QueryImageFormats(
    VAImageFormat *format_list,
    int *num_formats) {
    static const VAImageFormat cme_image_formats[] = {
        {VA_FOURCC_NV12, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0},
        {VA_FOURCC_NV21, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0},
        {VA_FOURCC_P010, VA_LSB_FIRST, 24,  0, 0, 0, 0, 0},
        {VA_FOURCC_I420, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0},
        {VA_FOURCC_Y800, VA_LSB_FIRST,  8,  0, 0, 0, 0, 0},
        {VA_FOURCC_YUY2, VA_LSB_FIRST, 16,  0, 0, 0, 0, 0},
        {VA_FOURCC_UYVY, VA_LSB_FIRST, 16,  0, 0, 0, 0, 0},
        {VA_FOURCC_RGBP, VA_LSB_FIRST, 24, 24, 0, 0, 0, 0},
    };

    if (!available_) {
        *num_formats = 0;
        return VA_STATUS_SUCCESS;
    }

    int count = sizeof(cme_image_formats) / sizeof(cme_image_formats[0]);
    for (int i = 0; i < count; i++)
        format_list[i] = cme_image_formats[i];
    *num_formats = count;
    return VA_STATUS_SUCCESS;
}

// ==================== Helper methods ====================

int32_t MediaDeviceCme::GetMaxImageFormats() const {
    return 8;  // libcme supported formats
}

// ====== CME image lifecycle ======

cme_img* MediaDeviceCme::LookupCmeImg(Buffer* buf) {
    std::lock_guard<std::mutex> lock(img_map_mutex_);
    auto it = img_map_.find(buf);
    if (it != img_map_.end())
        return it->second;
    return nullptr;
}

void MediaDeviceCme::RegisterCmeImg(Buffer* buf, cme_img* img) {
    std::lock_guard<std::mutex> lock(img_map_mutex_);
    img_map_[buf] = img;
}

void MediaDeviceCme::CleanupCmeImg(Surface* surface) {
    Buffer* buf = surface->GetBuffer();
    if (!buf)
        return;
    std::lock_guard<std::mutex> lock(img_map_mutex_);
    auto it = img_map_.find(buf);
    if (it != img_map_.end()) {
#if CIX_VAAPI_HAVE_CME
        free_cme_img(it->second);
#endif
        img_map_.erase(it);
    }
}
