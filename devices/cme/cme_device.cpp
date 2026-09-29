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
#include <va/va_drmcommon.h>
#include "cme_device.h"
#include "v4l2_stateful_device.h"
#include "device_common.h"
#include "log.h"

// ==================== Constructor / Destructor / Initialize ====================

// Parallel to MediaDeviceV4l2Stateful constructor (v4l2_stateful_device.cpp)
MediaDeviceCme::MediaDeviceCme(MediaDevice &media_device) :
    media_device_(media_device),
    cme_context_count(0),
    initialized_(false)
{
    CME_RET ret = cme_initialize();
    if (ret != CME_RET_SUCCESS)
        CIX_VAAPI_ERROR("cme_initialize failed: ret=%d\n", ret);
}

// Parallel to MediaDeviceV4l2Stateful destructor: clean up all contexts
MediaDeviceCme::~MediaDeviceCme() {
    for (auto &pair : cme_contexts) {
        delete pair.second;
    }
    cme_contexts.clear();
    cme_destroy();
}

// V4l2Stateful: hardware detection (QueryCodecDevices + GetSupportedProfiles)
// VPP: no hardware dependency, always succeeds (but keep initialized_ guard)
bool MediaDeviceCme::Initialize() {
    if (initialized_)
        return true;
    initialized_ = true;
    return true;
}

// ==================== Resource callbacks (parallel to V4l2Stateful) ====================

// Parallel to V4l2Stateful (v4l2_stateful_device.cpp)
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

// V4l2Stateful: returns scanned codec profiles
// VPP: fixed return VAProfileNone (only 1)
VAStatus MediaDeviceCme::QueryConfigProfiles(
    VAProfile *profile_list,
    int *num_profiles) {
    profile_list[0] = VAProfileNone;
    *num_profiles = 1;
    return VA_STATUS_SUCCESS;
}

// V4l2Stateful: returns VAEntrypointVLD or VAEntrypointEncSliceLP per profile
// VPP: only VAProfileNone -> VAEntrypointVideoProc
VAStatus MediaDeviceCme::QueryConfigEntrypoints(
    VAProfile profile,
    VAEntrypoint *entrypoint_list,
    int *num_entrypoints) {
    if (profile == VAProfileNone) {
        entrypoint_list[0] = VAEntrypointVideoProc;
        *num_entrypoints = 1;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

// V4l2Stateful: returns RTFormat/RC/Width/Height attributes per profile
// VPP: only VAConfigAttribRTFormat = VA_RT_FORMAT_YUV420
VAStatus MediaDeviceCme::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs) {
    if (profile != VAProfileNone || entrypoint != VAEntrypointVideoProc)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

    for (int i = 0; i < num_attribs; i++) {
        if (attrib_list[i].type == VAConfigAttribRTFormat) {
            attrib_list[i].value = VA_RT_FORMAT_YUV420;
        } else {
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
        }
    }
    return VA_STATUS_SUCCESS;
}

// VPP: DRM PRIME export path, NV12, 64..8192
VAStatus MediaDeviceCme::QuerySurfaceAttributes(
    const DeviceConfig *config,
    VASurfaceAttrib *attrib_list,
    unsigned int *num_attribs) {
    if (attrib_list == nullptr) {
        *num_attribs = CIX_VAAPI_MAX_SURFACE_ATTRIBUTES;
        return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(*num_attribs >= CIX_VAAPI_MAX_SURFACE_ATTRIBUTES,
        VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "attrib_list size %d is too small\n", *num_attribs);

    uint32_t i = 0;
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, VA_FOURCC_NV12,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinWidth, 64);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinHeight, 64);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxWidth, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxHeight, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMemoryType,
                            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    *num_attribs = i;

    return VA_STATUS_SUCCESS;
}

// ==================== Context management ====================

// V4l2Stateful: creates V4l2StatefulDecoder/Encoder, sets surface pool + buffer pool
// VPP: creates CmeContext, calls CreateSurfaceById for render targets
VAStatus MediaDeviceCme::CreateContext(
    const DeviceConfig *config,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context) {
    // Create Surface objects for each render target
    for (int i = 0; i < num_render_targets; i++) {
        auto pool = GetSurfacePoolBySurfaceID(render_targets[i]);
        CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
            "Surface pool not found for VPP render target %d\n", render_targets[i]);
        pool->CreateSurfaceById(render_targets[i]);
    }

    // Context ID starts from 0x80000000 (consistent with old architecture)
    uint32_t id = 0x80000000 + cme_context_count;
    cme_contexts[id] = new CmeContext(id, picture_width, picture_height);
    *context = (VAContextID)id;
    CIX_VAAPI_INFO("Created VPP context 0x%x (wxh: %dx%d)\n", id, picture_width, picture_height);
    cme_context_count++;
    return VA_STATUS_SUCCESS;
}

// V4l2Stateful: delete codec + RemoveContextFromSurfaces
// VPP: delete cme_context (no RemoveContextFromSurfaces, VPP context not bound to pool)
VAStatus MediaDeviceCme::DestroyContext(VAContextID context) {
    CIX_VAAPI_INFO("Destroy VPP context 0x%x\n", context);
    delete cme_contexts[context];
    cme_contexts.erase(context);
    return VA_STATUS_SUCCESS;
}

bool MediaDeviceCme::HasContextId(VAContextID context) const {
    return cme_contexts.find(context) != cme_contexts.end();
}

// ==================== Buffer/Picture ====================

// V4l2Stateful: dispatches by buffer type to codec's SetPicParamBuffer/etc
// VPP: only supports VAProcPipelineParameterBufferType and VAProcFilterParameterBufferType
VAStatus MediaDeviceCme::CreateBuffer(
    VAContextID context,
    VABufferType type,
    unsigned int size,
    unsigned int num_elements,
    void *data,
    VABufferID *buf_id) {
    CIX_VAAPI_INFO("VPP CreateBuffer: context 0x%x, type %d, size %d, num_elements %d\n",
        context, type, size, num_elements);

    auto bpool = GetBufferPool();
    auto buf = bpool->GetFreeSlot(type, context);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create buffer\n");
    CIX_VAAPI_DEBUG("VPP CreateBuffer: assigned buf_id=%u, type=%d, ctx=0x%x\n",
        buf->id, type, context);
    *buf_id = buf->id;

    if (type == VAProcPipelineParameterBufferType ||
        type == VAProcFilterParameterBufferType) {
        buf->buf = new Buffer(size);
        if (data) {
            memcpy(buf->buf->Map(), data, size);
            buf->buf->Unmap();
        }
        return VA_STATUS_SUCCESS;
    }

    return VA_STATUS_ERROR_INVALID_PARAMETER;
}

// V4l2Stateful: codec->SetSurfacePool + InitializeBuffers
// VPP: CmeContext->BeginPicture (sets output_surface, clears pipeline_entries)
VAStatus MediaDeviceCme::BeginPicture(VAContextID context, VASurfaceID render_target) {
    CIX_VAAPI_DEBUG("VPP BeginPicture: output_surface=%u\n", render_target);

    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    return it->second->BeginPicture(render_target, &media_device_);
}

// V4l2Stateful: codec->ProcessBuffers
// VPP: CmeContext->RenderPicture (collects pipeline entries)
VAStatus MediaDeviceCme::RenderPicture(
    VAContextID context,
    VABufferID *buffers,
    int num_buffers) {
    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    return it->second->RenderPicture(buffers, num_buffers, &media_device_);
}

// V4l2Stateful: codec->Submit + ResetFrameData
// VPP: CmeContext->EndPicture (ExecuteSinglePipeline or ExecuteOverlay)
VAStatus MediaDeviceCme::EndPicture(VAContextID context) {
    auto it = cme_contexts.find(context);
    if (it == cme_contexts.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    return it->second->EndPicture(&media_device_);
}

// V4l2Stateful: finds context via pool, finds codec in codecs, locks mutex, codec->SyncSurface
// VPP: finds context via pool, HasContextId, then CmeContext->SyncSurface
VAStatus MediaDeviceCme::SyncSurface(VASurfaceID render_target) {
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %d not found during VPP SyncSurface\n", render_target);

    auto context = pool->GetContextBySurfaceID(render_target);
    if (context == VA_INVALID_ID || !HasContextId(context)) {
        CIX_VAAPI_INFO("Surface %d is not a VPP surface, no need to sync\n", render_target);
        return VA_STATUS_SUCCESS;
    }

    return cme_contexts[context]->SyncSurface(render_target, &media_device_);
}

// V4l2Stateful: opens dec_device, EnumFormats, fills from image_formats map
// VPP: hardcoded 8 formats supported by libcme
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

    int count = sizeof(cme_image_formats) / sizeof(cme_image_formats[0]);
    for (int i = 0; i < count; i++) {
        format_list[i] = cme_image_formats[i];
    }
    *num_formats = count;
    return VA_STATUS_SUCCESS;
}

// ==================== Helper methods ====================

int32_t MediaDeviceCme::GetMaxImageFormats() const {
    return 8;  // libcme supported formats
}

// V4l2Stateful: queries hardware via V4L2 ioctl VIDIOC_G/S_FMT
// VPP: software calculation, stride by bpp and width, aligned to 64 bytes
int32_t MediaDeviceCme::QuerySurfaceLengthsAndStrides(
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t strides[CIX_VAAPI_MAX_PLANES]) {
    uint32_t num_planes = GetNumPlanes(format);
    for (uint32_t i = 0; i < num_planes; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        strides[i] = (((width * bpp) >> 3) + 63) & ~63;
        lengths[i] = strides[i] * height;
        if (format == VA_FOURCC_P010)
            lengths[i] *= 2;
    }
    return 0;
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
        free_cme_img(it->second);
        img_map_.erase(it);
    }
}
