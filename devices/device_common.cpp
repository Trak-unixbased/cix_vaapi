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


MediaDevice::MediaDevice() :
    surface_count(0),
    surface_pools(),
    buffer_pool(),
    configs(),
    config_count(0),
    cme_(new MediaDeviceCme(*this)),
    v4l2_(new MediaDeviceV4l2Stateful(*this)),
    initialized_(false)
{
    frame_dump = DUMP_OpenFrameFile();
}

MediaDevice::~MediaDevice() {
    v4l2_.reset();   // v4l2_ first (may call RemoveContextFromSurfaces)
    cme_.reset();    // then cme_
    for (auto pool : surface_pools) {
        delete pool;
    }
    surface_pools.clear();
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
    initialized_ = v4l2_->Initialize();
    cme_->Initialize();
    return initialized_;
}

int32_t MediaDevice::GetMaxProfiles() const {
    return v4l2_->GetMaxProfiles() + cme_->GetMaxProfiles();
}

int32_t MediaDevice::GetMaxImageFormats() const {
    return va_image_formats.size();
}

int32_t MediaDevice::QuerySurfaceLengthsAndStrides(
    uint32_t width, uint32_t height, uint32_t format,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t pitches[CIX_VAAPI_MAX_PLANES])
{
    return v4l2_->QuerySurfaceLengthsAndStrides(width, height, format, lengths, pitches);
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
    VAStatus status = v4l2_->QueryConfigProfiles(profile_list, num_profiles);
    CIX_VAAPI_CHECK_RETURN_CODE(status == VA_STATUS_SUCCESS,
        status,
        "Failed to query V4L2 config profiles\n");

    int base_num_profiles = *num_profiles;
    int cme_num_profiles = 1;
    status = cme_->QueryConfigProfiles(profile_list + base_num_profiles, &cme_num_profiles);
    CIX_VAAPI_CHECK_RETURN_CODE(status == VA_STATUS_SUCCESS,
        status,
        "Failed to query CME config profiles\n");

    *num_profiles += cme_num_profiles;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::QueryConfigEntrypoints(
    VAProfile profile,
    VAEntrypoint *entrypoint_list,
    int *num_entrypoints)
{
    int cme_num_entrypoints = 0;

    *num_entrypoints = 0;
    VAStatus v4l2_status = v4l2_->QueryConfigEntrypoints(profile, entrypoint_list, num_entrypoints);

    VAStatus cme_status = cme_->QueryConfigEntrypoints(
        profile, entrypoint_list + *num_entrypoints, &cme_num_entrypoints);
    if (cme_status == VA_STATUS_SUCCESS)
        *num_entrypoints += cme_num_entrypoints;

    if (v4l2_status == VA_STATUS_SUCCESS || cme_status == VA_STATUS_SUCCESS)
        return VA_STATUS_SUCCESS;

    return v4l2_status != VA_STATUS_SUCCESS ? v4l2_status : cme_status;
}

VAStatus MediaDevice::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs)
{
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
    VAConfigID id = config_count++;

    configs.emplace_back(id, profile, entrypoint);
    auto &config = configs.back();
    config.attribs.resize(num_attribs);
    for (int i = 0; i < num_attribs; i++) {
        config.attribs[i].type = attrib_list[i].type;
        config.attribs[i].value = attrib_list[i].value;
        CIX_VAAPI_DEBUG("Config %d attribute %d: type = %d, value = %d\n",
            id, i, attrib_list[i].type, attrib_list[i].value);
    }

    CIX_VAAPI_INFO("Created config %d: profile %d, entrypoint %d\n", id, profile, entrypoint);
    *config_id = id;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroyConfig(VAConfigID config_id) {
    for (auto it = configs.begin(); it != configs.end(); it++) {
        if (it->id == config_id) {
            configs.erase(it);
            break;
        }
    }
    return VA_STATUS_SUCCESS;
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
        "Config %d not found during QueryConfigAttributes\n", config_id);

    *profile = config->profile;
    *entrypoint = config->entrypoint;
    *num_attribs = config->attribs.size();
    for (int i = 0; i < config->attribs.size(); i++) {
        attrib_list[i].type = config->attribs[i].type;
        attrib_list[i].value = config->attribs[i].value;
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
        "Config %d not found during QuerySurfaceAttributes\n", config_id);

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
        "Config %d not found during CreateContext\n", config_id);

    if (IS_VPP_ENTRYPOINT(config->entrypoint))
        status = cme_->CreateContext(config, picture_width, picture_height, flag,
            render_targets, num_render_targets, context);
    if (IS_CODEC_ENTRYPOINT(config->entrypoint))
        status = v4l2_->CreateContext(config, picture_width, picture_height, flag,
            render_targets, num_render_targets, context);

    if (status == VA_STATUS_SUCCESS)
        context_config_map[*context] = config_id;

    return status;
}

VAStatus MediaDevice::DestroyContext(VAContextID context) {
    context_config_map.erase(context);
    if (cme_->HasContextId(context))
        return cme_->DestroyContext(context);
    if (v4l2_->HasContextId(context))
        return v4l2_->DestroyContext(context);
    return VA_STATUS_ERROR_INVALID_CONTEXT;
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

VAStatus MediaDevice::BeginPicture(VAContextID context, VASurfaceID render_target) {
    if (cme_->HasContextId(context)) {
        SyncSurface(render_target);
        return cme_->BeginPicture(context, render_target);
    }
    if (v4l2_->HasContextId(context)) {
        VAConfigID config_id = context_config_map[context];
        DeviceConfig *config = GetConfig(config_id);
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
    if (pool) {
        VAContextID context = pool->GetContextBySurfaceID(render_target);
        if (context == VA_INVALID_ID) {
            CIX_VAAPI_INFO("Surface %d has no associated context, no need to sync\n", render_target);
            return VA_STATUS_SUCCESS;
        }
        if (cme_->HasContextId(context))
            return cme_->SyncSurface(render_target);
        if (v4l2_->HasContextId(context))
            return v4l2_->SyncSurface(render_target);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    return VA_STATUS_ERROR_INVALID_SURFACE;
}

VAStatus MediaDevice::QueryImageFormats(VAImageFormat *format_list, int *num_formats) {
    return v4l2_->QueryImageFormats(format_list, num_formats);
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
    int32_t format_fourcc = format == VA_RT_FORMAT_YUV420_10 ?
                            VA_FOURCC_P010 : VA_FOURCC_NV12;

    // Get format fourcc from attrib_list
    if (attrib_list != nullptr && num_attribs > 0) {
        for (int i = 0; i < num_attribs; i++) {
            if (attrib_list[i].type == VASurfaceAttribPixelFormat) {
                format_fourcc = attrib_list[i].value.value.i;
                break;
            }
        }
    }

    SurfacePool *pool = GetSurfacePool(width, height, format_fourcc);

    // We don't really allocate memory for surface here, but just store surface info.
    for (int i = 0; i < num_surfaces; i++) {
        uint32_t index = surface_count++;
        pool->AddSurfaceID(index);
        surfaces[i] = index;

        // auto &surface = this->surfaces.back();
        // surface.attrib_list.resize(num_attribs);
        // for (int i = 0; i < num_attribs; i++) {
        //     surface.attrib_list[i].type = attrib_list[i].type;
        //     surface.attrib_list[i].flags = attrib_list[i].flags;
        //     surface.attrib_list[i].value = attrib_list[i].value;
        // }

        CIX_VAAPI_INFO("Created Surface %d: %dx%d, format %s\n",
            index, width, height, GetFourccString(format_fourcc));
    }

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroySurfaces(
    VASurfaceID *surface_list,
    int num_surfaces
) {
    for (int i = 0; i < num_surfaces; i++) {
        uint32_t index = surface_list[i];
        auto pool = GetSurfacePoolBySurfaceID(index);
        if (pool)
            pool->RemoveSurfaceID(index);

    }

    return VA_STATUS_SUCCESS;
}


VAStatus MediaDevice::MapBuffer(
    VABufferID buf_id,
    void **pbuf
) {
    CIX_VAAPI_DEBUG("MapBuffer: %d\n", buf_id);
    if (CIX_VA_IS_BUFFER_ID(buf_id)) {
        auto buf = buffer_pool.GetBufferInfo(buf_id);
        if (buf && buf->type == VAEncCodedBufferType)
            return MapCodedBuffer(buf_id, pbuf);
    }

    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_PARAMETER,
        "Invalid buffer ID when map buffer\n");

    VAImageID image_id = buf_id;
    VASurfaceID surface_id = image_pool.GetImageInfo(buf_id)->surface_id;
    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to find surface pool for buffer %d (surface %d)\n", buf_id, surface_id);
    auto surface = pool->GetSurfaceByID(surface_id);
    if (surface == nullptr) {
        // If the surface hasn't been created, just create one and bind to it
        CIX_VAAPI_DEBUG("Create surface %d for image %d\n", surface_id, image_id);
        surface = pool->FetchSurface();
        CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
            "Failed to fetch surface for buffer %d (surface %d)\n", buf_id, surface_id);
        pool->DestroyUnusedSurface(surface_id);
        surface->SetId(surface_id);
    }
    CIX_VAAPI_DEBUG("Map image buffer %d of surface %d\n", buf_id, surface_id);
    surface->SyncForReadStart();
    *pbuf = surface->GetBuffer()->Map();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::UnmapBuffer(
    VABufferID buf_id
) {
    CIX_VAAPI_DEBUG("UnmapBuffer: %d\n", buf_id);
    if (CIX_VA_IS_BUFFER_ID(buf_id)) {
        auto buf = buffer_pool.GetBufferInfo(buf_id);
        if (buf && buf->type == VAEncCodedBufferType)
            return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(CIX_VA_IS_IMAGE_ID(buf_id), VA_STATUS_ERROR_INVALID_PARAMETER,
        "Invalid buffer ID when unmap buffer\n");

    VAImageID image_id = buf_id;
    VASurfaceID surface_id = image_pool.GetImageInfo(buf_id)->surface_id;
    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to find surface pool for buffer %d (surface %d)\n", buf_id, surface_id);
    auto surface = pool->GetSurfaceByID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to unmap buffer %d\n", buf_id);
    DUMP_Write(frame_dump, surface->GetBuffer()->GetPtr(), surface->GetBuffer()->GetSize());
    surface->GetBuffer()->Unmap();
    surface->SyncForReadEnd();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DeriveImage(
    VASurfaceID surface,
    VAImage *image
) {
    CIX_VAAPI_DEBUG("Derive image from surface %d\n", surface);
    CIX_VAAPI_CHECK_RETURN_CODE(image != nullptr, VA_STATUS_ERROR_INVALID_PARAMETER,
        "Invalid image\n");

    auto pool = GetSurfacePoolBySurfaceID(surface);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %d not found during DeriveImage\n", surface);

    auto imageinfo = image_pool.GetFreeSlot();
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to derive image\n");
    imageinfo->surface_id = surface;
    imageinfo->width = pool->GetWidth();
    imageinfo->height = pool->GetHeight();
    return FillImageInfo(image, pool, imageinfo->id);
}

VAStatus MediaDevice::CreateImage(
    VAImageFormat *format,
    int width,
    int height,
    VAImage *image
) {
    CIX_VAAPI_DEBUG("Create image: %dx%d, format %s\n",
        width, height, GetFourccString(format->fourcc));
    auto pool = GetSurfacePool(width, height, format->fourcc);
    auto imageinfo = image_pool.GetFreeSlot();
    CIX_VAAPI_CHECK_RETURN_CODE(imageinfo != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create image\n");
    imageinfo->width = width;
    imageinfo->height = height;
    return FillImageInfo(image, pool, imageinfo->id);
}

VAStatus MediaDevice::GetImage(
    VASurfaceID surface,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    VAImageID image
) {
    CIX_VAAPI_DEBUG("Get image from surface %d, region (%d, %d), %dx%d\n",
        surface, x, y, width, height);

    auto imageinfo = image_pool.GetImageInfo(image);
    if (imageinfo != nullptr)
        imageinfo->surface_id = surface;

    //! for POC, only direct mapping from surface to image is supported
    auto pool = GetSurfacePoolBySurfaceID(surface);
    CIX_VAAPI_CHECK_RETURN_CODE(
        x == 0 && y == 0 && width == pool->GetWidth() && height == pool->GetHeight(),
        VA_STATUS_ERROR_INVALID_PARAMETER,
        "Invalid region (%d, %d), %dx%d\n", x, y, width, height);

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::DestroyImage(
    VAImageID image
) {
    auto imageinfo = image_pool.GetImageInfo(image);
    if (imageinfo != nullptr) {
        imageinfo->surface_id = VA_INVALID_SURFACE;
        imageinfo->width = 0;
        imageinfo->height = 0;
    }

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::AcquireBufferHandle(
    VABufferID buf_id,
    VABufferInfo *buf_info
) {
    CIX_VAAPI_DEBUG("Acquire buffer handle: %d\n", buf_id);
    VAImageID image_id = buf_id;
    VASurfaceID surface_id = image_pool.GetImageInfo(buf_id)->surface_id;
    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to find surface pool for buffer %d (surface %d)\n", buf_id, surface_id);
    auto surface = pool->GetSurfaceByID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to aquire buffer handle by buffer ID %d\n", buf_id);
    surface->SyncForReadStart();
    buf_info->handle = surface->GetBuffer()->GetFd();
    buf_info->mem_size = surface->GetBuffer()->GetSize();

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::ReleaseBufferHandle(
    VABufferID buf_id
) {
    CIX_VAAPI_DEBUG("Release buffer handle: %d\n", buf_id);
    VAImageID image_id = buf_id;
    VASurfaceID surface_id = image_pool.GetImageInfo(buf_id)->surface_id;
    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to find surface pool for buffer %d (surface %d)\n", buf_id, surface_id);
    auto surface = pool->GetSurfaceByID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to release buffer handle by buffer ID %d\n", buf_id);
    surface->SyncForReadEnd();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDevice::ExportSurfaceHandle(
    VASurfaceID surface_id,
    uint32_t mem_type,
    uint32_t flags,
    void *descriptor
) {
    CIX_VAAPI_DEBUG("Export surface ID: %d, mem_type %d, flags 0x%x\n",
        surface_id, mem_type, flags);

    CIX_VAAPI_CHECK_RETURN_CODE(mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE,
        "Unsupported memory type %d\n", mem_type);

    auto pool = GetSurfacePoolBySurfaceID(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to find surface pool for surface %d\n", surface_id);

    VADRMPRIMESurfaceDescriptor *desc = (VADRMPRIMESurfaceDescriptor *)descriptor;
    desc->fourcc = pool->GetFormat();
    desc->width = pool->GetWidth();
    desc->height = pool->GetHeight();
    desc->num_objects = 1;
    desc->objects[0].size = pool->GetSize();
    desc->objects[0].drm_format_modifier = DRM_FORMAT_MOD_LINEAR;
    desc->num_layers = 1;
    desc->layers[0].drm_format = pool->GetFormat();
    desc->layers[0].num_planes = pool->GetNumPlanes();
    for (int i = 0; i < desc->layers[0].num_planes; i++) {
        desc->layers[0].object_index[i] = 0;
        desc->layers[0].offset[i] = pool->GetOffset(i);
        desc->layers[0].pitch[i] = pool->GetPitch(i);
    }

    // Export dmabuf fd
    auto ctx = pool->GetContextBySurfaceID(surface_id);
    auto surf = pool->GetSurfaceByID(surface_id);
    if (ctx == VA_INVALID_ID && surf == nullptr) {
        surf = pool->FetchSurface();
        CIX_VAAPI_CHECK_RETURN_CODE(surf != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
            "Failed to fetch surface for surface ID %d\n", surface_id);
        pool->DestroyUnusedSurface(surface_id);
        surf->SetId(surface_id);
    } else if (surf == nullptr) {
        CIX_VAAPI_INFO("Waiting for the surface %d to be ready\n", surface_id);
        if (cme_->HasContextId(ctx))
            cme_->SyncSurface(surface_id);
        else if (v4l2_->HasContextId(ctx))
            v4l2_->SyncSurface(surface_id);
        surf = pool->GetSurfaceByID(surface_id);
        CIX_VAAPI_CHECK_RETURN_CODE(surf != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
            "Failed to get surface for surface ID %d after sync\n", surface_id);
    }

    desc->objects[0].fd = dup(surf->GetBuffer()->GetFd());
    if (desc->objects[0].fd < 0) {
        CIX_VAAPI_ERROR("Failed to duplicate dmabuf fd\n");
        return VA_STATUS_ERROR_OPERATION_FAILED;
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
            CIX_VAAPI_DEBUG("Found matching surface pool for %dx%d\n",
                width, height);
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
            CIX_VAAPI_DEBUG("Found matching surface pool for %dx%d, format %s\n",
                width, height, GetFourccString(format));
            return pool;
        }
    }

    // Create a surface pool with the given parameters
    uint32_t pitches[CIX_VAAPI_MAX_PLANES] = {0};
    uint32_t lengths[CIX_VAAPI_MAX_PLANES] = {0};
    uint32_t num_planes = GetNumPlanes(format);
    v4l2_->QuerySurfaceLengthsAndStrides(width, height, format, lengths, pitches);
    surface_pools.push_back(new SurfacePool(width, height, format, num_planes, lengths, pitches));

    return surface_pools.back();
}

SurfacePool *MediaDevice::GetSurfacePoolBySurfaceID(VASurfaceID id) {
    for (auto pool : surface_pools) {
        if (pool->HasSurfaceID(id)) {
            CIX_VAAPI_DEBUG("Found matching surface pool for surface ID %d\n", id);
            return pool;
        }
    }

    CIX_VAAPI_ERROR("No surface pool found for surface ID %d\n", id);
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
    image->image_id = buf;
    image->buf = buf;
    image->format.fourcc = format.fourcc;
    image->format.byte_order = format.byte_order;
    image->format.bits_per_pixel = format.bits_per_pixel;
    image->format.depth = format.depth;
    image->format.red_mask = format.red_mask;
    image->format.green_mask = format.green_mask;
    image->format.blue_mask = format.blue_mask;
    image->format.alpha_mask = format.alpha_mask;
    image->data_size = pool->GetSize();
    image->width = pool->GetWidth();
    image->height = pool->GetHeight();
    image->num_planes = pool->GetNumPlanes();
    for (int i = 0; i < image->num_planes; i++) {
        image->pitches[i] = pool->GetPitch(i);
        image->offsets[i] = pool->GetOffset(i);
    }

    return VA_STATUS_SUCCESS;
}

