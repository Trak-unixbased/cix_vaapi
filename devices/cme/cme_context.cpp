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

#include <cstring>
#include <unistd.h>
#include "cme_context.h"
#include "cme_device.h"
#include "log.h"

CmeContext::CmeContext(uint32_t id, int w, int h)
    : context_id(id), width(w), height(h),
      output_surface_id(VA_INVALID_SURFACE)
{
}

CmeContext::~CmeContext() {
}

int CmeContext::VaFourccToCmeFormat(uint32_t va_fourcc) {
    switch (va_fourcc) {
        case VA_FOURCC_NV12: return CME_FORMAT_NV12;
        case VA_FOURCC_P010: return CME_FORMAT_P010;
        case VA_FOURCC_I420: return CME_FORMAT_I420;
        case VA_FOURCC_NV21: return CME_FORMAT_NV21;
        case VA_FOURCC_YUY2: return CME_FORMAT_YUYV_422;
        case VA_FOURCC_RGBP: return CME_FORMAT_RGB_888;
        default: return CME_FORMAT_NV12;
    }
}

int CmeContext::VaColorStandardToCmeColorSpace(uint32_t va_standard, uint32_t va_range) {
    bool full_range = (va_range == VA_SOURCE_RANGE_FULL);
    switch (va_standard) {
        case VAProcColorStandardBT601:
            return full_range ? CME_CS_BT601_FULL : CME_CS_BT601_LIMIT;
        case VAProcColorStandardBT709:
            return full_range ? CME_CS_BT709_FULL : CME_CS_BT709_LIMIT;
        case VAProcColorStandardBT2020:
            return full_range ? CME_CS_BT2020_FULL : CME_CS_BT2020_LIMIT;
        default:
            return CME_COLOR_SPACE_DEFAULT;
    }
}

cme_img* CmeContext::CreateCmeImageFromSurface(Surface* surface, SurfacePool* pool, MediaDevice* device) {
    if (!surface || !pool)
        return nullptr;

    Buffer* buf = surface->GetBuffer();
    if (!buf)
        return nullptr;

    auto *cme = device->GetCme();
    if (!cme)
        return nullptr;

    cme_img* img = cme->LookupCmeImg(buf);
    if (img)
        return img;

    uint32_t nplanes = pool->GetNumPlanes();
    if (nplanes > MAX_CME_PLANES) {
        CIX_VAAPI_ERROR("CreateCmeImageFromSurface: unsupported plane count %u\n", nplanes);
        return nullptr;
    }

    uint32_t va_format = pool->GetFormat();

    external_buffer_ctrl ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.nplanes = nplanes;
    ctrl.width = pool->GetWidth();
    ctrl.height = pool->GetHeight();
    ctrl.format = VaFourccToCmeFormat(va_format);

    int fd = buf->GetFd();
    for (uint32_t i = 0; i < nplanes && i < MAX_CME_PLANES; i++) {
        ctrl.dma_fd[i] = fd;
        ctrl.offset[i] = pool->GetOffset(i);
        ctrl.stride[i][0] = pool->GetPitch(i);
        if (va_format == VA_FOURCC_I420 || va_format == VA_FOURCC_NV12 ||
            va_format == VA_FOURCC_NV21 || va_format == VA_FOURCC_P010) {
            ctrl.stride[i][1] = (i == 0) ? pool->GetHeight() : (pool->GetHeight() + 1) / 2;
        } else {
            ctrl.stride[i][1] = pool->GetHeight();
        }
    }

    int error = 0;
    img = import_from_external_buffer(&ctrl, &error);
    if (!img) {
        CIX_VAAPI_ERROR("import_from_external_buffer failed: error=%d\n", error);
        return nullptr;
    }

    cme->RegisterCmeImg(buf, img);
    surface->SetCleanupCallback(
        [cme](Surface* s) { cme->CleanupCmeImg(s); });

    return img;
}

Surface* CmeContext::GetSurfaceByIdSynced(VASurfaceID surface_id, MediaDevice* device) {
    SurfacePool* pool = device->GetSurfacePoolBySurfaceID(surface_id);
    if (!pool)
        return nullptr;

    device->SyncSurface(surface_id);
    return pool->GetSurfaceByID(surface_id);
}

VAStatus CmeContext::BeginPicture(VASurfaceID render_target, MediaDevice* device) {
    CIX_VAAPI_DEBUG("VPP BeginPicture: output_surface=%u\n", render_target);
    output_surface_id = render_target;
    pipeline_entries.clear();
    return VA_STATUS_SUCCESS;
}

VAStatus CmeContext::RenderPicture(VABufferID* buffers, int num_buffers, MediaDevice* device) {
    BufferPool* bpool = device->GetBufferPool();
    for (int i = 0; i < num_buffers; i++) {
        BufferInfo* buf_info = bpool->GetBufferInfo(buffers[i]);
        if (!buf_info || !buf_info->buf || !buf_info->buf->GetPtr()) {
            CIX_VAAPI_ERROR("VPP RenderPicture: invalid buffer %u\n", buffers[i]);
            continue;
        }
        CmePipelineEntry entry;
        entry.param_buf_id = buffers[i];
        entry.params = (VAProcPipelineParameterBuffer*)buf_info->buf->GetPtr();
        pipeline_entries.push_back(entry);
        CIX_VAAPI_DEBUG("VPP RenderPicture: added pipeline entry %d, surface=%u\n",
            i, entry.params->surface);
    }
    return VA_STATUS_SUCCESS;
}

int CmeContext::ExecuteSinglePipeline(VAProcPipelineParameterBuffer* params,
                                       Surface* output_surface, MediaDevice* device) {
    if (!params || !output_surface) {
        CIX_VAAPI_ERROR("VPP ExecuteSinglePipeline: null params or output_surface\n");
        return -1;
    }

    SurfacePool* src_pool = device->GetSurfacePoolBySurfaceID(params->surface);
    SurfacePool* dst_pool = device->GetSurfacePoolBySurfaceID(output_surface_id);
    if (!src_pool || !dst_pool) {
        CIX_VAAPI_ERROR("VPP ExecuteSinglePipeline: surface pool not found\n");
        return -1;
    }

    Surface* src_surface = GetSurfaceByIdSynced(params->surface, device);
    CIX_VAAPI_CHECK_RETURN_CODE(src_surface != nullptr, -1,
            "VPP ExecuteSinglePipeline: failed to get source surface %u from decoder\n", params->surface);

    cme_img* src_cme = CreateCmeImageFromSurface(src_surface, src_pool, device);
    cme_img* dst_cme = CreateCmeImageFromSurface(output_surface, dst_pool, device);
    if (!src_cme || !dst_cme) {
        CIX_VAAPI_ERROR("VPP ExecuteSinglePipeline: failed to create cme_img\n");
        return -1;
    }

    // Build crop/resize/flip/rotation/cvtcolor control
    cme_crfrc_ctrl crfrc_ctrl;
    memset(&crfrc_ctrl, 0, sizeof(crfrc_ctrl));
    crfrc_ctrl.rotate = CME_ROTATE_0;
    crfrc_ctrl.flip_mode = CME_HAL_TRANSFORM_FLIP_NONE;

    // Set crop region if specified
    if (params->surface_region) {
        const VARectangle* region = params->surface_region;
        crfrc_ctrl.enable_crop = 1;
        crfrc_ctrl.crop_x = region->x;
        crfrc_ctrl.crop_y = region->y;
        crfrc_ctrl.crop_w = region->width;
        crfrc_ctrl.crop_h = region->height;
    }

    // Set color space from surface_color_standard and input_color_properties
    if (params->surface_color_standard != VAProcColorStandardExplicit) {
        int cme_cs = VaColorStandardToCmeColorSpace(params->surface_color_standard,
            params->input_color_properties.color_range);
        dst_cme->colorspace_mode = cme_cs;
        src_cme->colorspace_mode = cme_cs;
    } else if (params->input_color_properties.matrix_coefficients) {
        // Explicit mode: use matrix_coefficients to derive color space
        // Map BT.601(5/6), BT.709(1), BT.2020(9)
        uint8_t mc = params->input_color_properties.matrix_coefficients;
        uint32_t range = params->input_color_properties.color_range;
        if (mc == 1) {
            dst_cme->colorspace_mode = VaColorStandardToCmeColorSpace(VAProcColorStandardBT709, range);
            src_cme->colorspace_mode = dst_cme->colorspace_mode;
        } else if (mc == 5 || mc == 6) {
            dst_cme->colorspace_mode = VaColorStandardToCmeColorSpace(VAProcColorStandardBT601, range);
            src_cme->colorspace_mode = dst_cme->colorspace_mode;
        } else if (mc == 9) {
            dst_cme->colorspace_mode = VaColorStandardToCmeColorSpace(VAProcColorStandardBT2020, range);
            src_cme->colorspace_mode = dst_cme->colorspace_mode;
        }
    }

    // Set rotation/flip from rotation_state (uint32_t, not a pointer)
    switch (params->rotation_state) {
        case VA_ROTATION_90:
            crfrc_ctrl.rotate = CME_ROTATE_90;
            break;
        case VA_ROTATION_180:
            crfrc_ctrl.rotate = CME_ROTATE_180;
            break;
        case VA_ROTATION_270:
            crfrc_ctrl.rotate = CME_ROTATE_270;
            break;
        default:
            break;
    }

    CIX_VAAPI_INFO("VPP ExecuteSinglePipeline: src(%ux%u) -> dst(%ux%u), crop=%d(%d,%d,%d,%d), rotate=%d\n",
        src_cme->width, src_cme->height, dst_cme->width, dst_cme->height,
        crfrc_ctrl.enable_crop, crfrc_ctrl.crop_x, crfrc_ctrl.crop_y,
        crfrc_ctrl.crop_w, crfrc_ctrl.crop_h, crfrc_ctrl.rotate);

    int wait_fd = 0;
    CME_RET ret = cme_2d_crop_resize_flip_rotation_cvtcolor(src_cme, dst_cme, &crfrc_ctrl, true, &wait_fd);
    if (ret != CME_RET_SUCCESS) {
        CIX_VAAPI_ERROR("VPP cme_2d_crop_resize_flip_rotation_cvtcolor failed: ret=%d\n", ret);
        return -1;
    }

    return 0;
}

int CmeContext::ExecuteOverlay(std::vector<CmePipelineEntry>& entries,
                                Surface* output_surface, MediaDevice* device) {
    int n_layers = entries.size();
    if (n_layers < 2 || n_layers > MAX_LAYERS) {
        CIX_VAAPI_ERROR("VPP ExecuteOverlay: invalid layer count %d\n", n_layers);
        return -1;
    }

    SurfacePool* dst_pool = device->GetSurfacePoolBySurfaceID(output_surface_id);
    if (!dst_pool) {
        CIX_VAAPI_ERROR("VPP ExecuteOverlay: output surface pool not found\n");
        return -1;
    }

    cme_img* src_cme[MAX_LAYERS];
    int alpha[MAX_LAYERS];
    cme_img* dst_cme = nullptr;
    CME_RET ret = CME_RET_SUCCESS;
    int wait_fd = 0;
    Surface* output_surface_ptr = nullptr;
    memset(src_cme, 0, sizeof(src_cme));

    for (int i = 0; i < n_layers; i++) {
        VAProcPipelineParameterBuffer* params = entries[i].params;
        SurfacePool* src_pool = device->GetSurfacePoolBySurfaceID(params->surface);
        if (!src_pool) {
            CIX_VAAPI_ERROR("VPP ExecuteOverlay: source pool not found for layer %d\n", i);
            return -1;
        }
        Surface* src_surface = GetSurfaceByIdSynced(params->surface, device);
        if (!src_surface) {
            CIX_VAAPI_ERROR("VPP ExecuteOverlay: source surface not found for layer %d\n", i);
            return -1;
        }

        src_cme[i] = CreateCmeImageFromSurface(src_surface, src_pool, device);
        if (!src_cme[i]) {
            CIX_VAAPI_ERROR("VPP ExecuteOverlay: failed to create cme_img for layer %d\n", i);
            return -1;
        }

        // Set position from output_region
        if (params->output_region) {
            src_cme[i]->x = params->output_region->x;
            src_cme[i]->y = params->output_region->y;
        }

        // Default to fully opaque. Only override when blend_state explicitly
        // provides a non-zero global alpha value.
        alpha[i] = 65535;
        if (params->blend_state &&
            (params->blend_state->flags & VA_BLEND_GLOBAL_ALPHA) &&
            params->blend_state->global_alpha > 0.0f) {
            alpha[i] = (int)(params->blend_state->global_alpha * 65535.0f);
        }

        CIX_VAAPI_INFO("VPP ExecuteOverlay: layer %d, surface=%u, alpha=%d\n",
            i, params->surface, alpha[i]);
    }

    output_surface_ptr = dst_pool->GetSurfaceByID(output_surface_id);
    if (!output_surface_ptr) {
        CIX_VAAPI_ERROR("VPP ExecuteOverlay: output surface not found\n");
        return -1;
    }

    dst_cme = CreateCmeImageFromSurface(output_surface_ptr, dst_pool, device);
    if (!dst_cme) {
        CIX_VAAPI_ERROR("VPP ExecuteOverlay: failed to create dst cme_img\n");
        return -1;
    }

    ret = cme_2d_overlay(n_layers, src_cme, dst_cme, alpha, true, &wait_fd);
    if (ret != CME_RET_SUCCESS) {
        CIX_VAAPI_ERROR("VPP cme_2d_overlay failed: ret=%d\n", ret);
        return -1;
    }

    return 0;
}

VAStatus CmeContext::EndPicture(MediaDevice* device) {
    CIX_VAAPI_DEBUG("VPP EndPicture: %zu pipeline entries\n", pipeline_entries.size());

    if (pipeline_entries.empty()) {
        CIX_VAAPI_WARNING("VPP EndPicture: no pipeline entries\n");
        return VA_STATUS_SUCCESS;
    }

    SurfacePool* dst_pool = device->GetSurfacePoolBySurfaceID(output_surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(dst_pool != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "VPP EndPicture: output surface pool not found\n");

    Surface* output_surface = dst_pool->GetSurfaceByID(output_surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(output_surface != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "VPP EndPicture: output surface not found\n");

    int ret;
    if (pipeline_entries.size() == 1) {
        ret = ExecuteSinglePipeline(pipeline_entries[0].params, output_surface, device);
    } else {
        ret = ExecuteOverlay(pipeline_entries, output_surface, device);
    }

    pipeline_entries.clear();

    return (ret == 0) ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus CmeContext::SyncSurface(VASurfaceID render_target, MediaDevice* device) {
    CIX_VAAPI_DEBUG("VPP SyncSurface: %u\n", render_target);

    // Wait for the surface to be ready (dequeued by the codec).
    // The codec sets the surface ID on dequeue, so we poll until
    // GetSurfaceByID finds it or timeout.
    SurfacePool* pool = device->GetSurfacePoolBySurfaceID(render_target);
    if (!pool) {
        CIX_VAAPI_ERROR("VPP SyncSurface: surface pool not found for %u\n", render_target);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    // Poll for surface readiness (up to 2 seconds)
    for (int i = 0; i < 200; i++) {
        Surface* surf = pool->GetSurfaceByID(render_target);
        if (surf) {
            CIX_VAAPI_DEBUG("VPP SyncSurface: surface %u is ready\n", render_target);
            return VA_STATUS_SUCCESS;
        }
        usleep(10000); // 10ms
    }

    CIX_VAAPI_ERROR("VPP SyncSurface: surface %u timed out waiting for codec\n", render_target);
    return VA_STATUS_ERROR_OPERATION_FAILED;
}
