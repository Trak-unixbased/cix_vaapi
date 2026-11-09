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

#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <string.h>
#include <algorithm>
#include <va/va_drmcommon.h>
#include <libdrm/drm_fourcc.h>
#include "device_common.h"
#include "v4l2_stateful_device.h"
#include "cme_device.h"
#include "surface_pool.h"

static const std::map<uint32_t, VAImageFormat> va_image_formats = {
    {VA_FOURCC_Y800, {VA_FOURCC_Y800, VA_LSB_FIRST, 8, 0, 0, 0, 0, 0}},
    {VA_FOURCC_I420, {VA_FOURCC_I420, VA_LSB_FIRST, 12, 0, 0, 0, 0, 0}},
    {VA_FOURCC_NV12, {VA_FOURCC_NV12, VA_LSB_FIRST, 12, 0, 0, 0, 0, 0}},
    {VA_FOURCC_NV21, {VA_FOURCC_NV21, VA_LSB_FIRST, 12, 0, 0, 0, 0, 0}},
    {VA_FOURCC_YUY2, {VA_FOURCC_YUY2, VA_LSB_FIRST, 16, 0, 0, 0, 0, 0}},
    {VA_FOURCC_UYVY, {VA_FOURCC_UYVY, VA_LSB_FIRST, 16, 0, 0, 0, 0, 0}},
    {VA_FOURCC_P010, {VA_FOURCC_P010, VA_LSB_FIRST, 24, 0, 0, 0, 0, 0}},
    {VA_FOURCC_RGBP, {VA_FOURCC_RGBP, VA_LSB_FIRST, 24, 24, 0, 0, 0, 0}},
};

// DRM format of the whole surface (VA_EXPORT_SURFACE_COMPOSED_LAYERS).
// VA and DRM FOURCCs differ for several formats (I420/YUV420, Y800/R8,
// YUY2/YUYV): the previous code exported the VA FOURCC as drm_format.
static uint32_t VaFourccToDrmFormat(uint32_t fourcc)
{
    switch (fourcc) {
        case VA_FOURCC_NV12: return DRM_FORMAT_NV12;
        case VA_FOURCC_NV21: return DRM_FORMAT_NV21;
        case VA_FOURCC_P010: return DRM_FORMAT_P010;
        case VA_FOURCC_I420: return DRM_FORMAT_YUV420;
        case VA_FOURCC_Y800: return DRM_FORMAT_R8;
        case VA_FOURCC_YUY2: return DRM_FORMAT_YUYV;
        case VA_FOURCC_UYVY: return DRM_FORMAT_UYVY;
        default: return 0;
    }
}

// DRM format of one plane exported as its own layer
// (VA_EXPORT_SURFACE_SEPARATE_LAYERS, used by mpv, Kodi and FFmpeg hwmap).
static uint32_t VaFourccToDrmPlaneFormat(uint32_t fourcc, uint32_t plane)
{
    switch (fourcc) {
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
            return plane == 0 ? DRM_FORMAT_R8 : DRM_FORMAT_GR88;
        case VA_FOURCC_P010:
            return plane == 0 ? DRM_FORMAT_R16 : DRM_FORMAT_GR1616;
        case VA_FOURCC_I420:
        case VA_FOURCC_Y800:
            return DRM_FORMAT_R8;
        case VA_FOURCC_YUY2:
            return DRM_FORMAT_YUYV;
        case VA_FOURCC_UYVY:
            return DRM_FORMAT_UYVY;
        default:
            return 0;
    }
}

static uint32_t RtFormatToFourcc(uint32_t rt_format)
{
    if (rt_format & VA_RT_FORMAT_YUV420_10)
        return VA_FOURCC_P010;
    if (rt_format & VA_RT_FORMAT_YUV420)
        return VA_FOURCC_NV12;
    if (rt_format & VA_RT_FORMAT_YUV422)
        return VA_FOURCC_YUY2;
    if (rt_format & VA_RT_FORMAT_YUV400)
        return VA_FOURCC_Y800;
    if (rt_format & VA_RT_FORMAT_RGBP)
        return VA_FOURCC_RGBP;
    return 0;
}

static uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

MediaDevice::MediaDevice() :
    surface_count(0),
    surface_pools(),
    buffer_pool(),
    configs(),
    config_count(0),
    frame_dump(nullptr),
    cme_(new MediaDeviceCme(*this)),
    v4l2_(new MediaDeviceV4l2Stateful(*this)),
    initialized_(false)
{
    frame_dump = DUMP_OpenFrameFile();
}

MediaDevice::~MediaDevice() {
    // 1. Codec contexts first: their destructors drain the VPU and call
    //    RemoveContextFromSurfaces(), which walks surface_pools.
    v4l2_.reset();
    // 2. Surfaces next: a surface used by VPP has a cleanup callback that
    //    calls into cme_. Deleting the pools after cme_ (previous order) was a
    //    use-after-free on vaTerminate().
    for (auto pool : surface_pools)
        delete pool;
    surface_pools.clear();
    // 3. Finally the CME backend.
    cme_.reset();
    DUMP_Close(frame_dump);
}

MediaDevice* MediaDevice::CreateDevice() {
    MediaDevice* device = new MediaDevice();
    if (!device->Initialize()) {
        delete device;
        return nullptr;
    }
    return device;
}

bool MediaDevice::Initialize() {
    CIX_VAAPI_CHECK_RETURN_CODE(v4l2_ != nullptr && cme_ != nullptr, false,
        "Failed to create backends\n");
    bool v4l2_ok = v4l2_->Initialize();
    bool cme_ok = cme_->Initialize();
    // The driver is usable as long as one backend works (e.g. VPP only when
    // the VPU nodes are not accessible).
    initialized_ = v4l2_ok || cme_ok;
    if (!initialized_)
        CIX_VAAPI_ERROR("Neither the V4L2 VPU nor the CME backend could be initialized\n");
    return initialized_;
}

int32_t MediaDevice::GetMaxProfiles() const {
    return v4l2_->GetMaxProfiles() + cme_->GetMaxProfiles();
}

int32_t MediaDevice::GetMaxImageFormats() const {
    return va_image_formats.size();
}

void MediaDevice::ComputeDefaultLayout(
    uint32_t width, uint32_t height, uint32_t format,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t pitches[CIX_VAAPI_MAX_PLANES])
{
    uint32_t num_planes = GetNumPlanes(format);
    uint32_t chroma_height = (height + 1) / 2;

    for (uint32_t i = 0; i < num_planes && i < CIX_VAAPI_MAX_PLANES; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        uint32_t plane_height = height;
        if (i > 0 && (format == VA_FOURCC_NV12 || format == VA_FOURCC_NV21 ||
                      format == VA_FOURCC_P010 || format == VA_FOURCC_I420))
            plane_height = chroma_height;
        uint32_t plane_width = width;
        if (i > 0 && format == VA_FOURCC_I420)
            plane_width = (width + 1) / 2 * 2;  // GetRowBpp() returns 4 bpp for chroma
        pitches[i] = AlignUp((plane_width * bpp + 7) >> 3, 64);
        lengths[i] = pitches[i] * plane_height;
    }
}

int32_t MediaDevice::QuerySurfaceLengthsAndStrides(
    uint32_t width, uint32_t height, uint32_t format,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t pitches[CIX_VAAPI_MAX_PLANES])
{
    uint32_t num_planes = GetNumPlanes(format);
    if (num_planes == 0)
        return -1;

    // The VPU decides the layout of the formats it can output; everything
    // else (CME-only formats such as RGBP, YUY2 or Y800) uses the software
    // layout, as the decoder would silently substitute another format.
    if (v4l2_->QuerySurfaceLengthsAndStrides(width, height, format, lengths, pitches) == 0)
        return 0;

    ComputeDefaultLayout(width, height, format, lengths, pitches);
    return 0;
}

VAStatus MediaDevice::SyncBuffer(VABufferID id) {
    return v4l2_->SyncBuffer(id);
}

VAStatus MediaDevice::MapCodedBuffer(VABufferID buf_id, void **pbuf) {
    return v4l2_->MapCodedBuffer(buf_id, pbuf);
}

VAStatus MediaDevice::QueryConfigProfiles(
    VAProfile *profile_list,
    int *num_profiles)
{
    CIX_VAAPI_CHECK_RETURN_CODE(profile_list != nullptr && num_profiles != nullptr,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid parameters for QueryConfigProfiles\n");

    int v4l2_num_profiles = 0;
    VAStatus status = v4l2_->QueryConfigProfiles(profile_list, &v4l2_num_profiles);
    CIX_VAAPI_CHECK_RETURN_CODE(status == VA_STATUS_SUCCESS,
        status,
        "Failed to query V4L2 config profiles\n");

    int cme_num_profiles = 0;
    status = cme_->QueryConfigProfiles(profile_list + v4l2_num_profiles, &cme_num_profiles);
    CIX_VAAPI_CHECK_RETURN_CODE(status == VA_STATUS_SUCCESS,
        status,
        "Failed to query CME config profiles\n");

    *num_profiles = v4l2_num_profiles + cme_num_profiles;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QueryConfigEntrypoints(
    VAProfile profile,
    VAEntrypoint *entrypoint_list,
    int *num_entrypoints)
{
    CIX_VAAPI_CHECK_RETURN_CODE(entrypoint_list != nullptr && num_entrypoints != nullptr,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid parameters for QueryConfigEntrypoints\n");

    int v4l2_num_entrypoints = 0;
    int cme_num_entrypoints = 0;

    VAStatus v4l2_status = v4l2_->QueryConfigEntrypoints(profile, entrypoint_list, &v4l2_num_entrypoints);
    if (v4l2_status != VA_STATUS_SUCCESS)
        v4l2_num_entrypoints = 0;

    VAStatus cme_status = cme_->QueryConfigEntrypoints(
        profile, entrypoint_list + v4l2_num_entrypoints, &cme_num_entrypoints);
    if (cme_status != VA_STATUS_SUCCESS)
        cme_num_entrypoints = 0;

    *num_entrypoints = v4l2_num_entrypoints + cme_num_entrypoints;

    if (v4l2_status == VA_STATUS_SUCCESS || cme_status == VA_STATUS_SUCCESS)
        return VA_STATUS_SUCCESS;

    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus MediaDevice::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs)
{
    if (num_attribs > 0 && attrib_list == nullptr)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (IS_VPP_ENTRYPOINT(entrypoint))
        return cme_->GetConfigAttributes(profile, entrypoint, attrib_list, num_attribs);
    if (IS_CODEC_ENTRYPOINT(entrypoint))
        return v4l2_->GetConfigAttributes(profile, entrypoint, attrib_list, num_attribs);
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
}

VAStatus MediaDevice::CreateConfig(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs,
    VAConfigID *config_id)
{
    CIX_VAAPI_CHECK_RETURN_CODE(config_id != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "config_id is null\n");
    CIX_VAAPI_CHECK_RETURN_CODE(num_attribs >= 0 && (num_attribs == 0 || attrib_list != nullptr),
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid attribute list\n");

    // Validate the profile / entrypoint pair: vaCreateConfig() used to succeed
    // for anything, so applications probing support with it got a context
    // that failed much later.
    VAEntrypoint entrypoints[8];
    int num_entrypoints = 0;
    if (QueryConfigEntrypoints(profile, entrypoints, &num_entrypoints) != VA_STATUS_SUCCESS ||
        num_entrypoints == 0) {
        CIX_VAAPI_INFO("CreateConfig: unsupported profile %d\n", profile);
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }
    if (std::find(entrypoints, entrypoints + num_entrypoints, entrypoint) ==
        entrypoints + num_entrypoints) {
        CIX_VAAPI_INFO("CreateConfig: unsupported entrypoint %d for profile %d\n",
            entrypoint, profile);
        return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
    }

    VAConfigAttrib rt_format = { VAConfigAttribRTFormat, 0 };
    GetConfigAttributes(profile, entrypoint, &rt_format, 1);
    for (int i = 0; i < num_attribs; i++) {
        if (attrib_list[i].type == VAConfigAttribRTFormat &&
            rt_format.value != VA_ATTRIB_NOT_SUPPORTED &&
            (attrib_list[i].value & ~rt_format.value) != 0) {
            CIX_VAAPI_INFO("CreateConfig: unsupported RT format 0x%x\n", attrib_list[i].value);
            return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
        }
    }

    VAConfigID id = config_count++;

    configs.emplace_back(id, profile, entrypoint);
    auto &config = configs.back();
    config.attribs.resize(num_attribs);
    for (int i = 0; i < num_attribs; i++) {
        config.attribs[i].type = attrib_list[i].type;
        config.attribs[i].value = attrib_list[i].value;
        CIX_VAAPI_DEBUG("Config %u attribute %d: type = %d, value = 0x%x\n",
            id, i, attrib_list[i].type, attrib_list[i].value);
    }

    CIX_VAAPI_INFO("Created config %u: profile %d, entrypoint %d\n", id, profile, entrypoint);
    *config_id = id;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroyConfig(VAConfigID config_id) {
    for (auto it = configs.begin(); it != configs.end(); it++) {
        if (it->id == config_id) {
            configs.erase(it);
            return VA_STATUS_SUCCESS;
        }
    }
    return VA_STATUS_ERROR_INVALID_CONFIG;
}

VAStatus MediaDevice::QueryConfigAttributes(
    VAConfigID config_id,
    VAProfile *profile,
    VAEntrypoint *entrypoint,
    VAConfigAttrib *attrib_list,
    int *num_attribs)
{
    auto config = GetConfig(config_id);
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr,
        VA_STATUS_ERROR_INVALID_CONFIG,
        "Config %u not found during QueryConfigAttributes\n", config_id);

    if (profile)
        *profile = config->profile;
    if (entrypoint)
        *entrypoint = config->entrypoint;
    if (num_attribs)
        *num_attribs = config->attribs.size();
    if (attrib_list) {
        for (size_t i = 0; i < config->attribs.size(); i++) {
            attrib_list[i].type = config->attribs[i].type;
            attrib_list[i].value = config->attribs[i].value;
        }
    }

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QuerySurfaceAttributes(
    VAConfigID config_id,
    VASurfaceAttrib *attrib_list,
    unsigned int *num_attribs)
{
    auto config = GetConfig(config_id);
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr, VA_STATUS_ERROR_INVALID_CONFIG,
        "Config %u not found during QuerySurfaceAttributes\n", config_id);

    if (IS_VPP_ENTRYPOINT(config->entrypoint))
        return cme_->QuerySurfaceAttributes(config, attrib_list, num_attribs);
    if (IS_CODEC_ENTRYPOINT(config->entrypoint))
        return v4l2_->QuerySurfaceAttributes(config, attrib_list, num_attribs);
    return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
}

VAStatus MediaDevice::CreateContext(
    VAConfigID config_id,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context)
{
    VAStatus status = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
    auto config = GetConfig(config_id);
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr, VA_STATUS_ERROR_INVALID_CONFIG,
        "Config %u not found during CreateContext\n", config_id);
    CIX_VAAPI_CHECK_RETURN_CODE(context != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "context is null\n");
    CIX_VAAPI_CHECK_RETURN_CODE(num_render_targets >= 0 &&
        (num_render_targets == 0 || render_targets != nullptr),
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid render target list\n");

    for (int i = 0; i < num_render_targets; i++) {
        CIX_VAAPI_CHECK_RETURN_CODE(GetSurfacePoolBySurfaceID(render_targets[i]) != nullptr,
            VA_STATUS_ERROR_INVALID_SURFACE,
            "Render target %u is not a valid surface\n", render_targets[i]);
    }

    if (IS_VPP_ENTRYPOINT(config->entrypoint))
        status = cme_->CreateContext(config, picture_width, picture_height, flag,
            render_targets, num_render_targets, context);
    else if (IS_CODEC_ENTRYPOINT(config->entrypoint))
        status = v4l2_->CreateContext(config, picture_width, picture_height, flag,
            render_targets, num_render_targets, context);

    if (status == VA_STATUS_SUCCESS)
        context_config_map[*context] = config_id;

    return status;
}

VAStatus MediaDevice::DestroyContext(VAContextID context) {
    VAStatus status = VA_STATUS_ERROR_INVALID_CONTEXT;

    if (cme_->HasContextId(context))
        status = cme_->DestroyContext(context);
    else if (v4l2_->HasContextId(context))
        status = v4l2_->DestroyContext(context);

    if (status == VA_STATUS_SUCCESS) {
        context_config_map.erase(context);
        buffer_pool.ReleaseContext(context);
    }
    return status;
}

void MediaDevice::RemoveContextFromSurfaces(VAContextID context) {
    for (auto pool : surface_pools)
        pool->RemoveContext(context);
}

VAStatus MediaDevice::CreateBuffer(
    VAContextID context,
    VABufferType type,
    unsigned int size,
    unsigned int num_elements,
    void *data,
    VABufferID *buf_id)
{
    if (cme_->HasContextId(context))
        return cme_->CreateBuffer(context, type, size, num_elements, data, buf_id);
    if (v4l2_->HasContextId(context))
        return v4l2_->CreateBuffer(context, type, size, num_elements, data, buf_id);
    return VA_STATUS_ERROR_INVALID_CONTEXT;
}

VAStatus MediaDevice::DestroyBuffer(VABufferID buf_id) {
    if (buffer_pool.ReleaseSlot(buf_id) != 0)
        return VA_STATUS_ERROR_INVALID_BUFFER;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QueryBufferInfo(
    VABufferID buf_id,
    VABufferType *type,
    unsigned int *size,
    unsigned int *num_elements)
{
    auto info = buffer_pool.GetBufferInfo(buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(info != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
        "Buffer %u not found during BufferInfo\n", buf_id);

    if (type)
        *type = info->type;
    if (size)
        *size = info->size;
    if (num_elements)
        *num_elements = info->num_elements;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::BeginPicture(VAContextID context, VASurfaceID render_target) {
    CIX_VAAPI_CHECK_RETURN_CODE(GetSurfacePoolBySurfaceID(render_target) != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Render target %u is not a valid surface\n", render_target);

    if (cme_->HasContextId(context)) {
        SyncSurface(render_target);
        return cme_->BeginPicture(context, render_target);
    }
    if (v4l2_->HasContextId(context)) {
        auto it = context_config_map.find(context);
        DeviceConfig *config = it != context_config_map.end() ? GetConfig(it->second) : nullptr;
        if (config && IS_ENCODE_ENTRYPOINT(config->entrypoint))
            SyncSurface(render_target);
        return v4l2_->BeginPicture(context, render_target);
    }
    return VA_STATUS_ERROR_INVALID_CONTEXT;
}

VAStatus MediaDevice::RenderPicture(
    VAContextID context,
    VABufferID *buffers,
    int num_buffers)
{
    if (cme_->HasContextId(context))
        return cme_->RenderPicture(context, buffers, num_buffers);
    if (v4l2_->HasContextId(context))
        return v4l2_->RenderPicture(context, buffers, num_buffers);
    return VA_STATUS_ERROR_INVALID_CONTEXT;
}

VAStatus MediaDevice::EndPicture(VAContextID context) {
    if (cme_->HasContextId(context))
        return cme_->EndPicture(context);
    if (v4l2_->HasContextId(context))
        return v4l2_->EndPicture(context);
    return VA_STATUS_ERROR_INVALID_CONTEXT;
}

VAStatus MediaDevice::SyncSurface(VASurfaceID render_target) {
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    if (pool == nullptr)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    VAContextID context = pool->GetContextBySurfaceID(render_target);
    if (context == VA_INVALID_ID) {
        CIX_VAAPI_DEBUG("Surface %u has no associated context, no need to sync\n", render_target);
        return VA_STATUS_SUCCESS;
    }
    if (cme_->HasContextId(context))
        return cme_->SyncSurface(render_target);
    if (v4l2_->HasContextId(context))
        return v4l2_->SyncSurface(render_target);

    // The owning context is gone: nothing is pending on this surface anymore.
    pool->SetSurfaceContext(render_target, VA_INVALID_ID);
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QuerySurfaceStatus(VASurfaceID render_target, VASurfaceStatus *status) {
    CIX_VAAPI_CHECK_RETURN_CODE(status != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "status is null\n");

    auto pool = GetSurfacePoolBySurfaceID(render_target);
    if (pool == nullptr)
        return VA_STATUS_ERROR_INVALID_SURFACE;

    VAContextID context = pool->GetContextBySurfaceID(render_target);
    *status = (context == VA_INVALID_ID) ? VASurfaceReady : VASurfaceRendering;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QueryImageFormats(VAImageFormat *format_list, int *num_formats) {
    CIX_VAAPI_CHECK_RETURN_CODE(format_list != nullptr && num_formats != nullptr,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid parameters for QueryImageFormats\n");

    // Both lists are bounded by va_image_formats; merge them without
    // duplicates so the result never exceeds ctx->max_image_formats.
    std::vector<VAImageFormat> merged;
    VAImageFormat tmp[16];
    int count = 0;

    if (v4l2_->QueryImageFormats(tmp, &count) == VA_STATUS_SUCCESS) {
        for (int i = 0; i < count; i++)
            merged.push_back(tmp[i]);
    }

    count = 0;
    if (cme_->QueryImageFormats(tmp, &count) == VA_STATUS_SUCCESS) {
        for (int i = 0; i < count; i++) {
            bool exists = false;
            for (const auto &f : merged)
                exists |= (f.fourcc == tmp[i].fourcc);
            if (!exists)
                merged.push_back(tmp[i]);
        }
    }

    int max = GetMaxImageFormats();
    int n = 0;
    for (const auto &f : merged) {
        if (n >= max)
            break;
        if (va_image_formats.find(f.fourcc) == va_image_formats.end())
            continue;
        format_list[n++] = f;
    }
    *num_formats = n;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::CreateSurfaces(
    int32_t width,
    int32_t height,
    int32_t format,
    int32_t num_surfaces,
    VASurfaceID *surfaces
) {
    return CreateSurfaces2(
        width, height, format, num_surfaces, surfaces, nullptr, 0);
}

VAStatus MediaDevice::CreateSurfaces2(
    int32_t width,
    int32_t height,
    int32_t format,
    int32_t num_surfaces,
    VASurfaceID *surfaces,
    VASurfaceAttrib *attrib_list,
    int32_t num_attribs
) {
    CIX_VAAPI_CHECK_RETURN_CODE(width > 0 && height > 0 && width <= 8192 && height <= 8192,
        VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED,
        "Unsupported surface size %dx%d\n", width, height);
    CIX_VAAPI_CHECK_RETURN_CODE(num_surfaces > 0 && surfaces != nullptr,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid surface list\n");

    uint32_t format_fourcc = RtFormatToFourcc((uint32_t)format);
    CIX_VAAPI_CHECK_RETURN_CODE(format_fourcc != 0, VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT,
        "Unsupported RT format 0x%x\n", format);

    if (attrib_list != nullptr && num_attribs > 0) {
        for (int i = 0; i < num_attribs; i++) {
            const VASurfaceAttrib &attrib = attrib_list[i];
            switch (attrib.type) {
                case VASurfaceAttribPixelFormat:
                    format_fourcc = attrib.value.value.i;
                    break;
                case VASurfaceAttribMemoryType:
                    // Importing client memory (DRM PRIME, user pointers) is not
                    // implemented. Report it instead of silently allocating new
                    // memory that the client never sees.
                    if ((uint32_t)attrib.value.value.i != VA_SURFACE_ATTRIB_MEM_TYPE_VA) {
                        CIX_VAAPI_WARNING("Surface import (memory type 0x%x) is not supported\n",
                            attrib.value.value.i);
                        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
                    }
                    break;
                case VASurfaceAttribExternalBufferDescriptor:
                    if (attrib.value.value.p != nullptr) {
                        CIX_VAAPI_WARNING("External surface buffers are not supported\n");
                        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    CIX_VAAPI_CHECK_RETURN_CODE(va_image_formats.find(format_fourcc) != va_image_formats.end(),
        VA_STATUS_ERROR_INVALID_IMAGE_FORMAT,
        "Unsupported surface pixel format %s\n", GetFourccString(format_fourcc));

    SurfacePool *pool = GetSurfacePool(width, height, format_fourcc);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr && pool->GetSize() > 0,
        VA_STATUS_ERROR_ALLOCATION_FAILED,
        "Failed to create surface pool %dx%d %s\n", width, height, GetFourccString(format_fourcc));

    // We don't really allocate memory for surface here, but just store surface info.
    for (int i = 0; i < num_surfaces; i++) {
        uint32_t index = surface_count++;
        if (index == VA_INVALID_SURFACE)
            index = surface_count++;
        pool->AddSurfaceID(index);
        surfaces[i] = index;

        CIX_VAAPI_INFO("Created Surface %u: %dx%d, format %s\n",
            index, width, height, GetFourccString(format_fourcc));
    }

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroySurfaces(
    VASurfaceID *surface_list,
    int num_surfaces
) {
    if (surface_list == nullptr || num_surfaces <= 0)
        return num_surfaces == 0 ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_PARAMETER;

    for (int i = 0; i < num_surfaces; i++) {
        VASurfaceID index = surface_list[i];
        auto pool = GetSurfacePoolBySurfaceID(index);
        if (pool)
            pool->RemoveSurfaceID(index);
    }

    return VA_STATUS_SUCCESS;
}

Surface *MediaDevice::GetOrCreateSurfaceMemory(SurfacePool *pool, VASurfaceID surface_id)
{
    Surface *surface = pool->GetSurfaceByID(surface_id);
    if (surface != nullptr)
        return surface;

    // The surface is still owned by a codec: wait for the picture instead of
    // handing out a blank buffer (previous behaviour of vaMapBuffer()).
    if (pool->GetContextBySurfaceID(surface_id) != VA_INVALID_ID) {
        CIX_VAAPI_INFO("Waiting for the surface %u to be ready\n", surface_id);
        SyncSurface(surface_id);
        surface = pool->GetSurfaceByID(surface_id);
        if (surface != nullptr)
            return surface;
        if (pool->GetContextBySurfaceID(surface_id) != VA_INVALID_ID)
            return nullptr;
    }

    // If the surface hasn't been created, just create one and bind to it
    CIX_VAAPI_DEBUG("Allocate memory for surface %u\n", surface_id);
    surface = pool->FetchSurface();
    if (surface == nullptr)
        return nullptr;
    surface->SetId(surface_id);
    return surface;
}

Surface *MediaDevice::GetImageSurface(VABufferID buf_id, bool create)
{
    auto imageinfo = image_pool.GetImageInfo(buf_id);
    if (imageinfo == nullptr || imageinfo->surface_id == VA_INVALID_SURFACE)
        return nullptr;

    VASurfaceID surface_id = imageinfo->surface_id;
    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    if (pool == nullptr)
        return nullptr;

    return create ? GetOrCreateSurfaceMemory(pool, surface_id) : pool->GetSurfaceByID(surface_id);
}

VAStatus MediaDevice::MapBuffer(
    VABufferID buf_id,
    void **pbuf
) {
    CIX_VAAPI_DEBUG("MapBuffer: %u\n", buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pbuf != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "pbuf is null\n");

    if (CIX_VA_IS_BUFFER_ID(buf_id)) {
        auto buf = buffer_pool.GetBufferInfo(buf_id);
        CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
            "Buffer %u not found during MapBuffer\n", buf_id);
        if (buf->type == VAEncCodedBufferType)
            return MapCodedBuffer(buf_id, pbuf);
        if (buf->buf != nullptr && buf->buf->Map() != nullptr) {
            *pbuf = buf->buf->Map();
            return VA_STATUS_SUCCESS;
        }
        CIX_VAAPI_ERROR("Buffer %u (type %d) cannot be mapped\n", buf_id, buf->type);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_BUFFER,
        "Invalid buffer ID %u when map buffer\n", buf_id);

    Surface *surface = GetImageSurface(buf_id, true);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
        "Image buffer %u is not bound to a valid surface\n", buf_id);

    CIX_VAAPI_DEBUG("Map image buffer %u of surface %u\n", buf_id, surface->GetId());
    void *ptr = surface->GetBuffer() ? surface->GetBuffer()->Map() : nullptr;
    CIX_VAAPI_CHECK_RETURN_CODE(ptr != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to map image buffer %u\n", buf_id);
    surface->SyncCpuAccessStart();
    *pbuf = ptr;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::UnmapBuffer(
    VABufferID buf_id
) {
    CIX_VAAPI_DEBUG("UnmapBuffer: %u\n", buf_id);
    if (CIX_VA_IS_BUFFER_ID(buf_id)) {
        auto buf = buffer_pool.GetBufferInfo(buf_id);
        CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
            "Buffer %u not found during UnmapBuffer\n", buf_id);
        return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_BUFFER,
        "Invalid buffer ID %u when unmap buffer\n", buf_id);

    Surface *surface = GetImageSurface(buf_id, false);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr && surface->GetBuffer() != nullptr,
        VA_STATUS_ERROR_INVALID_BUFFER, "Failed to unmap buffer %u\n", buf_id);

    if (surface->GetBuffer()->GetPtr() != nullptr) {
        DUMP_Write(frame_dump, surface->GetBuffer()->GetPtr(), surface->GetBuffer()->GetSize());
        surface->SyncCpuAccessEnd();
        surface->GetBuffer()->Unmap();
    }
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DeriveImage(
    VASurfaceID surface,
    VAImage *image
) {
    CIX_VAAPI_DEBUG("Derive image from surface %u\n", surface);
    CIX_VAAPI_CHECK_RETURN_CODE(image != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "Invalid image\n");

    auto pool = GetSurfacePoolBySurfaceID(surface);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %u not found during DeriveImage\n", surface);

    auto imageinfo = image_pool.GetFreeSlot();
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to derive image\n");

    VAStatus status = FillImageInfo(image, pool, imageinfo->id);
    if (status != VA_STATUS_SUCCESS)
        return status;

    imageinfo->surface_id = surface;
    imageinfo->width = pool->GetWidth();
    imageinfo->height = pool->GetHeight();
    imageinfo->format = pool->GetFormat();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::CreateImage(
    VAImageFormat *format,
    int width,
    int height,
    VAImage *image
) {
    CIX_VAAPI_CHECK_RETURN_CODE(format != nullptr && image != nullptr && width > 0 && height > 0,
        VA_STATUS_ERROR_INVALID_PARAMETER, "Invalid CreateImage parameters\n");
    CIX_VAAPI_DEBUG("Create image: %dx%d, format %s\n",
        width, height, GetFourccString(format->fourcc));
    CIX_VAAPI_CHECK_RETURN_CODE(va_image_formats.find(format->fourcc) != va_image_formats.end(),
        VA_STATUS_ERROR_INVALID_IMAGE_FORMAT,
        "Unsupported image format %s\n", GetFourccString(format->fourcc));

    auto pool = GetSurfacePool(width, height, format->fourcc);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_ALLOCATION_FAILED,
        "Failed to create image layout\n");
    auto imageinfo = image_pool.GetFreeSlot();
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create image\n");

    VAStatus status = FillImageInfo(image, pool, imageinfo->id);
    if (status != VA_STATUS_SUCCESS)
        return status;

    imageinfo->width = width;
    imageinfo->height = height;
    imageinfo->format = format->fourcc;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::GetImage(
    VASurfaceID surface,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    VAImageID image
) {
    CIX_VAAPI_DEBUG("Get image from surface %u, region (%d, %d), %ux%u\n",
        surface, x, y, width, height);

    auto imageinfo = image_pool.GetImageInfo(image);
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_INVALID_IMAGE,
        "Invalid image %u\n", image);

    auto pool = GetSurfacePoolBySurfaceID(surface);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %u not found during GetImage\n", surface);

    //! Only direct mapping from surface to image is supported: the image
    //! buffer is the surface memory itself, so the layouts must be identical.
    CIX_VAAPI_CHECK_RETURN_CODE(
        x == 0 && y == 0 && width == pool->GetWidth() && height == pool->GetHeight() &&
        imageinfo->width == pool->GetWidth() && imageinfo->height == pool->GetHeight() &&
        imageinfo->format == pool->GetFormat(),
        VA_STATUS_ERROR_UNIMPLEMENTED,
        "Only full-surface GetImage without conversion is supported: region (%d, %d) %ux%u, "
        "surface %ux%u %s, image %ux%u %s\n",
        x, y, width, height, pool->GetWidth(), pool->GetHeight(),
        GetFourccString(pool->GetFormat()), imageinfo->width, imageinfo->height,
        GetFourccString(imageinfo->format));

    imageinfo->surface_id = surface;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroyImage(
    VAImageID image
) {
    auto imageinfo = image_pool.GetImageInfo(image);
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_INVALID_IMAGE,
        "Invalid image %u\n", image);

    imageinfo->surface_id = VA_INVALID_SURFACE;
    imageinfo->width = 0;
    imageinfo->height = 0;
    imageinfo->format = 0;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::AcquireBufferHandle(
    VABufferID buf_id,
    VABufferInfo *buf_info
) {
    CIX_VAAPI_DEBUG("Acquire buffer handle: %u\n", buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(buf_info != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "buf_info is null\n");
    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_BUFFER,
        "Only image buffers can be exported (buffer %u)\n", buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(buf_info->mem_type == 0 ||
        buf_info->mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME,
        VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE,
        "Unsupported memory type 0x%x\n", buf_info->mem_type);

    Surface *surface = GetImageSurface(buf_id, true);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr && surface->GetBuffer() != nullptr,
        VA_STATUS_ERROR_INVALID_BUFFER,
        "Failed to acquire buffer handle by buffer ID %u\n", buf_id);

    surface->SyncCpuAccessStart();
    buf_info->type = VAImageBufferType;
    buf_info->mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
    buf_info->handle = (uintptr_t)surface->GetBuffer()->GetFd();
    buf_info->mem_size = surface->GetBuffer()->GetSize();

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::ReleaseBufferHandle(
    VABufferID buf_id
) {
    CIX_VAAPI_DEBUG("Release buffer handle: %u\n", buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_BUFFER,
        "Only image buffers can be exported (buffer %u)\n", buf_id);

    Surface *surface = GetImageSurface(buf_id, false);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
        "Failed to release buffer handle by buffer ID %u\n", buf_id);
    surface->SyncCpuAccessEnd();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::ExportSurfaceHandle(
    VASurfaceID surface_id,
    uint32_t mem_type,
    uint32_t flags,
    void *descriptor
) {
    CIX_VAAPI_DEBUG("Export surface ID: %u, mem_type 0x%x, flags 0x%x\n",
        surface_id, mem_type, flags);

    CIX_VAAPI_CHECK_RETURN_CODE(mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE,
        "Unsupported memory type 0x%x\n", mem_type);
    CIX_VAAPI_CHECK_RETURN_CODE(descriptor != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "descriptor is null\n");

    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "Failed to find surface pool for surface %u\n", surface_id);

    const uint32_t fourcc = pool->GetFormat();
    const uint32_t num_planes = std::min<uint32_t>(pool->GetNumPlanes(), CIX_VAAPI_MAX_PLANES);
    const bool separate = (flags & VA_EXPORT_SURFACE_SEPARATE_LAYERS) != 0;
    CIX_VAAPI_CHECK_RETURN_CODE(VaFourccToDrmFormat(fourcc) != 0,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface format %s cannot be exported as DRM PRIME\n", GetFourccString(fourcc));

    Surface *surf = GetOrCreateSurfaceMemory(pool, surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(surf != nullptr && surf->GetBuffer() != nullptr,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to get memory of surface %u\n", surface_id);

    int fd = dup(surf->GetBuffer()->GetFd());
    CIX_VAAPI_CHECK_RETURN_CODE(fd >= 0, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to duplicate dmabuf fd\n");
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    VADRMPRIMESurfaceDescriptor *desc = (VADRMPRIMESurfaceDescriptor *)descriptor;
    memset(desc, 0, sizeof(*desc));
    desc->fourcc = fourcc;  // VA FOURCC of the whole surface
    desc->width = pool->GetWidth();
    desc->height = pool->GetHeight();
    desc->num_objects = 1;
    desc->objects[0].fd = fd;
    desc->objects[0].size = surf->GetBuffer()->GetSize();
    desc->objects[0].drm_format_modifier = DRM_FORMAT_MOD_LINEAR;

    if (separate && num_planes > 1 && fourcc != VA_FOURCC_YUY2 && fourcc != VA_FOURCC_UYVY) {
        desc->num_layers = num_planes;
        for (uint32_t i = 0; i < num_planes; i++) {
            desc->layers[i].drm_format = VaFourccToDrmPlaneFormat(fourcc, i);
            desc->layers[i].num_planes = 1;
            desc->layers[i].object_index[0] = 0;
            desc->layers[i].offset[0] = pool->GetOffset(i);
            desc->layers[i].pitch[0] = pool->GetPitch(i);
        }
    } else {
        desc->num_layers = 1;
        desc->layers[0].drm_format = separate ? VaFourccToDrmPlaneFormat(fourcc, 0) :
                                                VaFourccToDrmFormat(fourcc);
        desc->layers[0].num_planes = num_planes;
        for (uint32_t i = 0; i < num_planes; i++) {
            desc->layers[0].object_index[i] = 0;
            desc->layers[0].offset[i] = pool->GetOffset(i);
            desc->layers[0].pitch[i] = pool->GetPitch(i);
        }
    }

    return VA_STATUS_SUCCESS;
}

SurfacePool *MediaDevice::GetSurfacePool(
    uint32_t width,
    uint32_t height
) {
    // Check if a matching surface pool already exists
    for (auto pool : surface_pools) {
        if (pool->Match(width, height)) {
            CIX_VAAPI_DEBUG("Found matching surface pool for %ux%u\n", width, height);
            return pool;
        }
    }

    return nullptr;
}

SurfacePool *MediaDevice::GetSurfacePool(
    uint32_t width,
    uint32_t height,
    uint32_t format
) {
    // Check if a matching surface pool already exists
    for (auto pool : surface_pools) {
        if (pool->Match(width, height, format)) {
            CIX_VAAPI_DEBUG("Found matching surface pool for %ux%u, format %s\n",
                width, height, GetFourccString(format));
            return pool;
        }
    }

    // Create a surface pool with the given parameters
    uint32_t pitches[CIX_VAAPI_MAX_PLANES] = {0};
    uint32_t lengths[CIX_VAAPI_MAX_PLANES] = {0};
    uint32_t num_planes = GetNumPlanes(format);
    if (num_planes == 0 || QuerySurfaceLengthsAndStrides(width, height, format, lengths, pitches) != 0) {
        CIX_VAAPI_ERROR("Cannot compute the layout of a %ux%u %s surface\n",
            width, height, GetFourccString(format));
        return nullptr;
    }

    surface_pools.push_back(new SurfacePool(width, height, format, num_planes, lengths, pitches));
    return surface_pools.back();
}

SurfacePool *MediaDevice::GetSurfacePoolBySurfaceID(VASurfaceID id) {
    if (id == VA_INVALID_SURFACE)
        return nullptr;

    for (auto pool : surface_pools) {
        if (pool->HasSurfaceID(id))
            return pool;
    }

    CIX_VAAPI_DEBUG("No surface pool found for surface ID %u\n", id);
    return nullptr;
}

uint32_t MediaDevice::GetNumPlanes(uint32_t format) {
    switch (format) {
        case VA_FOURCC_YUY2:
        case VA_FOURCC_UYVY:
        case VA_FOURCC_Y800:
            return 1;
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
        case VA_FOURCC_P010:
            return 2;
        case VA_FOURCC_I420:
        case VA_FOURCC_RGBP:
            return 3;
    }

    return 0;
}

uint32_t MediaDevice::GetRowBpp(uint32_t format, int32_t plane) {
    switch (format) {
        case VA_FOURCC_YUY2:
        case VA_FOURCC_UYVY:
            return plane == 0 ? 16 : 0;
        case VA_FOURCC_Y800:
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
        case VA_FOURCC_RGBP:
            return 8;
        case VA_FOURCC_P010:
            return 16;
        case VA_FOURCC_I420:
            return plane == 0 ? 8 : 4;
    }

    return 0;
}

DeviceConfig *MediaDevice::GetConfig(VAConfigID id)
{
    for (auto &config : configs) {
        if (config.id == id)
            return &config;
    }
    return nullptr;
}

VAStatus MediaDevice::FillImageInfo(
    VAImage *image,
    SurfacePool *pool,
    VABufferID buf
) {
    auto f = va_image_formats.find(pool->GetFormat());
    CIX_VAAPI_CHECK_RETURN_CODE(f != va_image_formats.end(), VA_STATUS_ERROR_INVALID_IMAGE_FORMAT,
        "Unsupported image format %s\n", GetFourccString(pool->GetFormat()));

    const auto &format = f->second;
    memset(image, 0, sizeof(*image));
    image->image_id = buf;
    image->buf = buf;
    image->format = format;
    image->data_size = pool->GetSize();
    image->width = pool->GetWidth();
    image->height = pool->GetHeight();
    image->num_planes = pool->GetNumPlanes();
    for (uint32_t i = 0; i < image->num_planes; i++) {
        image->pitches[i] = pool->GetPitch(i);
        image->offsets[i] = pool->GetOffset(i);
    }

    return VA_STATUS_SUCCESS;
}
