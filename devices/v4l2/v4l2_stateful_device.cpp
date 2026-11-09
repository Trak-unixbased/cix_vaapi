/*
 * Copyright 2025-2026 Cix Technology Group Co., Ltd.
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
#include <errno.h>
#include <string.h>
#include <string>
#include <climits>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <va/va.h>
#include <va/va_enc_hevc.h>
#include <va/va_drmcommon.h>
#include "cix_va_compat.h"
#include "device_common.h"
#include "v4l2_stateful_device.h"
#include "v4l2_stateful_dec.h"
#include "v4l2_stateful_enc.h"

#define MAX_VIDEO_DEVICE_NUM 256     // maximum number of video devices to probe
#define MIN_VIDEO_DEVICE_NUM 32      // minimum number of video devices to scan

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

MediaDeviceV4l2Stateful::MediaDeviceV4l2Stateful(MediaDevice &media_device) :
    media_device_(media_device),
    fd_(-1),
    image_count(0),
    buffer_count(0),
    codecs(),
    codec_count(0),
    initialized_(false)
{
}

MediaDeviceV4l2Stateful::~MediaDeviceV4l2Stateful()
{
    while (!codecs.empty())
        DestroyContext(codecs.begin()->first);
    Close();
}

bool MediaDeviceV4l2Stateful::Initialize()
{
    if (initialized_)
        return true;

    QueryCodecDevices();
    if (dec_device.empty() && enc_device.empty()) {
        CIX_VAAPI_ERROR("No valid V4L2 codec devices found "
            "(check that the VPU driver is loaded and that /dev/video* is accessible)\n");
        return false;
    }

    supported_profiles.clear();
    GetSupportedProfiles();
    CIX_VAAPI_CHECK_RETURN_CODE(!supported_profiles.empty(), false,
        "No supported codec profiles found on V4L2 devices\n");

    initialized_ = true;
    return true;
}

SurfacePool *MediaDeviceV4l2Stateful::GetSurfacePoolBySurfaceID(VASurfaceID id)
{
    return media_device_.GetSurfacePoolBySurfaceID(id);
}

SurfacePool *MediaDeviceV4l2Stateful::GetSurfacePool(
    uint32_t width,
    uint32_t height)
{
    return media_device_.GetSurfacePool(width, height);
}

BufferPool *MediaDeviceV4l2Stateful::GetBufferPool()
{
    return media_device_.GetBufferPool();
}

void MediaDeviceV4l2Stateful::RemoveContextFromSurfaces(VAContextID context)
{
    media_device_.RemoveContextFromSurfaces(context);
}

uint32_t MediaDeviceV4l2Stateful::GetNumPlanes(uint32_t format)
{
    return media_device_.GetNumPlanes(format);
}

uint32_t MediaDeviceV4l2Stateful::GetRowBpp(uint32_t format, int32_t plane)
{
    return media_device_.GetRowBpp(format, plane);
}

V4l2Stateful *MediaDeviceV4l2Stateful::GetCodec(VAContextID context) const
{
    auto it = codecs.find(context);
    return it != codecs.end() ? it->second : nullptr;
}

void MediaDeviceV4l2Stateful::QueryCodecDevices() {
    bool found_decoder = false;
    bool found_encoder = false;
    bool permission_denied = false;
    std::string device_name = "/dev/video";

    for (int i = 0; i < MAX_VIDEO_DEVICE_NUM; i++) {
        std::string device_file = device_name + std::to_string(i);
        struct stat st;
        // Check file existence
        if (stat(device_file.c_str(), &st) != 0) {
            if (i > MIN_VIDEO_DEVICE_NUM)
                break;
            continue;
        }

        if (Open(device_file.c_str()) != 0) {
            permission_denied |= (errno == EACCES || errno == EPERM);
            continue;
        }

        enum v4l2_buf_type out_type;
        enum v4l2_buf_type cap_type;
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) != 0) {
            Close();
            continue;
        }

        // capabilities describes the whole physical device, device_caps the
        // opened node: use the latter when available.
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ?
            cap.device_caps : cap.capabilities;

        if ((caps & V4L2_CAP_VIDEO_M2M_MPLANE) ||
            ((caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE))) {
            out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else {
            // Only multi-planar memory-to-memory devices are driven by this
            // backend (all contexts use the MPLANE buffer types).
            Close();
            continue;
        }

        if (!found_decoder && isDecoder(out_type, cap_type)) {
            found_decoder = true;
            dec_device = device_file;
            CIX_VAAPI_INFO("Found V4L2 decoder %s (%s)\n", device_file.c_str(), (const char *)cap.card);
            QueryDecodeCaps();
        }

        if (!found_encoder && isEncoder(out_type, cap_type)) {
            found_encoder = true;
            enc_device = device_file;
            CIX_VAAPI_INFO("Found V4L2 encoder %s (%s)\n", device_file.c_str(), (const char *)cap.card);
            QueryEncodeCaps();
        }

        Close();

        if (found_decoder && found_encoder)
            break;
    }

    if (!found_decoder && !found_encoder && permission_denied)
        CIX_VAAPI_ERROR("Permission denied on /dev/video*: add the user to the 'video' group\n");
}

VAStatus MediaDeviceV4l2Stateful::QueryConfigProfiles(
    VAProfile *profile_list,
    int *num_profiles
) {
    int count = 0;
    for (const auto &profile : supported_profiles) {
        if (profile.support_dec || profile.support_enc)
            profile_list[count++] = profile.profile;
    }

    *num_profiles = count;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::QueryConfigEntrypoints(
    VAProfile profile,
    VAEntrypoint *entrypoints,
    int *num_entrypoints
) {
    int count = 0;
    for (const auto &supported_profile : supported_profiles) {
        if (profile == supported_profile.profile) {
            if (supported_profile.support_dec)
                entrypoints[count++] = VAEntrypointVLD;

            // 10-bit encoding is not supported for now.
            if (supported_profile.support_enc && !IS_10BIT_PROFILE(profile)) {
                entrypoints[count++] = VAEntrypointEncSlice;
                entrypoints[count++] = VAEntrypointEncSliceLP;
            }

            *num_entrypoints = count;
            return count > 0 ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
        }
    }

    *num_entrypoints = 0;
    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus MediaDeviceV4l2Stateful::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs
) {
    const bool is_enc = IS_ENCODE_ENTRYPOINT(entrypoint);

    for (int i = 0; i < num_attribs; i++) {
        switch (attrib_list[i].type) {
            case VAConfigAttribRTFormat:
                attrib_list[i].value = VA_RT_FORMAT_YUV420;
                /* AV1 Main (Profile0) allows 8- and 10-bit 420 in one VA profile. */
                if (IS_10BIT_PROFILE(profile) && entrypoint == VAEntrypointVLD)
                    attrib_list[i].value |= VA_RT_FORMAT_YUV420_10;
                break;
            case VAConfigAttribMaxPictureWidth:
            case VAConfigAttribMaxPictureHeight:
                attrib_list[i].value = 8192;
                break;
            case VAConfigAttribDecSliceMode:
                attrib_list[i].value = is_enc ? VA_ATTRIB_NOT_SUPPORTED : VA_DEC_SLICE_MODE_NORMAL;
                break;
            case VAConfigAttribRateControl:
                attrib_list[i].value = is_enc ? (VA_RC_CQP | VA_RC_CBR | VA_RC_VBR) :
                                                VA_ATTRIB_NOT_SUPPORTED;
                break;
            case VAConfigAttribEncPackedHeaders:
                attrib_list[i].value = is_enc ? VA_ENC_PACKED_HEADER_NONE : VA_ATTRIB_NOT_SUPPORTED;
                break;
            case VAConfigAttribEncInterlaced:
                attrib_list[i].value = is_enc ? VA_ENC_INTERLACED_NONE : VA_ATTRIB_NOT_SUPPORTED;
                break;
            case VAConfigAttribEncSliceStructure:
                attrib_list[i].value = is_enc ? VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS :
                                                VA_ATTRIB_NOT_SUPPORTED;
                break;
            case VAConfigAttribProcessingRate:
                attrib_list[i].value = VA_PROCESSING_RATE_NONE;
                break;
            case VAConfigAttribEncQuantization:
                attrib_list[i].value = is_enc ? VA_ENC_QUANTIZATION_NONE : VA_ATTRIB_NOT_SUPPORTED;
                break;
            case VAConfigAttribEncMaxRefFrames:
                // 1 reference frame, B frames are not supported for now
                attrib_list[i].value = is_enc ? 1 : VA_ATTRIB_NOT_SUPPORTED;
                break;
            default:
                // Including VAConfigAttribEncHEVCFeatures / BlockSizes, which
                // used to be returned untouched (garbage for the caller).
                attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
                break;
        }
    }

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::QuerySurfaceAttributes(
    const DeviceConfig *config,
    VASurfaceAttrib *attrib_list,
    unsigned int *num_attribs) {
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr, VA_STATUS_ERROR_INVALID_CONFIG,
        "null config during QuerySurfaceAttributes (V4L2)\n");

    if (attrib_list == nullptr) {
        *num_attribs = CIX_VAAPI_MAX_SURFACE_ATTRIBUTES;
        return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(*num_attribs >= CIX_VAAPI_MAX_SURFACE_ATTRIBUTES,
        VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "attrib_list size %u is too small\n", *num_attribs);

    uint32_t i = 0;
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, VA_FOURCC_NV12,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    if (IS_10BIT_PROFILE(config->profile) && IS_DECODE_ENTRYPOINT(config->entrypoint)) {
        SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, VA_FOURCC_P010,
                                VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    }
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinWidth, 144);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinHeight, 144);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxWidth, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxHeight, 8192);
    // Surfaces are allocated by the driver and can be exported as DRM PRIME
    // (vaExportSurfaceHandle). Importing user pointers / V4L2 buffers is not
    // implemented, so those memory types are no longer advertised.
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMemoryType,
                            VA_SURFACE_ATTRIB_MEM_TYPE_VA | VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    *num_attribs = i;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::CreateContext(
    const DeviceConfig *config,
    int picture_width,
    int picture_height,
    int flag,
    VASurfaceID *render_targets,
    int num_render_targets,
    VAContextID *context
) {
    (void)flag;
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr, VA_STATUS_ERROR_INVALID_CONFIG,
        "null config during CreateContext (V4L2)\n");
    CIX_VAAPI_CHECK_RETURN_CODE(picture_width > 0 && picture_height > 0 &&
        picture_width <= 8192 && picture_height <= 8192,
        VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED,
        "Unsupported picture size %dx%d\n", picture_width, picture_height);

    const bool is_dec = IS_DECODE_ENTRYPOINT(config->entrypoint);
    const bool is_enc = IS_ENCODE_ENTRYPOINT(config->entrypoint);
    CIX_VAAPI_CHECK_RETURN_CODE(is_dec || is_enc, VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT,
        "Unsupported entrypoint %d\n", config->entrypoint);
    CIX_VAAPI_CHECK_RETURN_CODE(!(is_dec ? dec_device : enc_device).empty(),
        VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT,
        "No V4L2 %s device available\n", is_dec ? "decoder" : "encoder");

    uint32_t compressed_format = GetV4l2PixFmtFromConfig(config);
    CIX_VAAPI_CHECK_RETURN_CODE(compressed_format != 0, VA_STATUS_ERROR_INVALID_CONFIG,
        "Cannot get output port format from config %u\n", config->id);

    SurfacePool *pool = nullptr;
    uint32_t raw_format = V4L2_PIX_FMT_NV12;
    if (render_targets != nullptr && num_render_targets > 0) {
        pool = GetSurfacePoolBySurfaceID(render_targets[0]);
        raw_format = GetV4l2PixFmtFromSurface(render_targets[0]);
    } else {
        pool = GetSurfacePool(picture_width, picture_height);
        // The pool stores a VA FOURCC: translate it (I420 != YU12).
        raw_format = pool ? GetV4l2PixFmtFromVaFourcc(pool->GetFormat()) : V4L2_PIX_FMT_NV12;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(!is_dec || pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "A decoder context needs render targets\n");

    uint32_t id = codec_count;
    V4l2Stateful *codec = nullptr;
    if (is_dec)
        codec = new V4l2StatefulDecoder(
            dec_device.c_str(),
            id,
            picture_width,
            picture_height,
            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            compressed_format,
            raw_format
        );
    else
        codec = new V4l2StatefulEncoder(
            enc_device.c_str(),
            id,
            picture_width,
            picture_height,
            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            raw_format,
            compressed_format
        );

    if (!codec->IsValid()) {
        CIX_VAAPI_ERROR("Failed to open the V4L2 %s for context %u\n",
            is_dec ? "decoder" : "encoder", id);
        delete codec;
        return VA_STATUS_ERROR_HW_BUSY;
    }

    if (pool)
        codec->SetSurfacePool(pool);
    codec->SetBufferPool(GetBufferPool());

    // Set rate control mode
    if (codec->IsEncoder()) {
        uint32_t rc_mode = GetRateControlModeFromConfig(config);
        if (rc_mode != 0 && codec->SetRateControlMode(rc_mode) != 0)
            CIX_VAAPI_WARNING("SetRateControlMode(0x%x) failed for context %u\n", rc_mode, id);

        if (codec->SetProfile(config->profile) != 0)
            CIX_VAAPI_WARNING("Failed to set profile %d for context %u\n", config->profile, id);
    }

    // Insert the codec first: on failure DestroyContext() releases everything
    // (surface ownership included) the same way as a normal destruction.
    codecs[id] = codec;
    codec_count++;

    if (codec->AddSurfaces(num_render_targets > 0 ? num_render_targets : 0) != 0 ||
        codec->CreateBuffers() != 0) {
        CIX_VAAPI_ERROR("Failed to allocate the V4L2 buffers of context %u\n", id);
        DestroyContext(id);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    CIX_VAAPI_INFO("Created V4L2 stateful context %u\n", id);
    *context = (VAContextID)id;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::DestroyContext(
    VAContextID context
) {
    auto it = codecs.find(context);
    if (it == codecs.end())
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    CIX_VAAPI_INFO("Destroy V4L2 stateful context %u\n", context);
    V4l2Stateful *codec = it->second;
    codecs.erase(it);
    delete codec;

    RemoveContextFromSurfaces(context);

    return VA_STATUS_SUCCESS;
}

bool MediaDeviceV4l2Stateful::HasContextId(VAContextID context) const {
    return codecs.find(context) != codecs.end();
}

VAStatus MediaDeviceV4l2Stateful::CreateBuffer(
    VAContextID context,
    VABufferType type,
    unsigned int size,
    unsigned int num_elements,
    void *data,
    VABufferID *buf_id
) {
    CIX_VAAPI_DEBUG("CreateBuffer: context %u, type %d, size %u, num_elements %u\n",
        context, type, size, num_elements);
    if ((type != VASliceParameterBufferType && num_elements > 1) ||
        (type != VAEncCodedBufferType && data == nullptr))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    auto codec = GetCodec(context);
    if (codec == nullptr)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    auto bpool = GetBufferPool();
    auto buf = bpool->GetFreeSlot(type, context);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create buffer\n");
    buf->size = size;
    buf->num_elements = num_elements;
    VABufferID id = buf->id;
    CIX_VAAPI_DEBUG("Codec CreateBuffer: assigned buf_id=%u, type=%d, ctx=0x%x\n",
        id, type, context);

    VAStatus status = VA_STATUS_SUCCESS;
    switch (type) {
        case VAPictureParameterBufferType:
            codec->SetPicParamBuffer(data, size, id);
            break;
        case VAIQMatrixBufferType:
            if (codec->SetIQMatrix(data, size) != 0)
                status = VA_STATUS_ERROR_OPERATION_FAILED;
            break;
        case VASliceParameterBufferType:
            codec->SetSliceParamBuffer(data, size, num_elements);
            break;
        case VASliceDataBufferType:
            if (codec->SetSliceDataBuffer(data, size, id) != 0)
                status = VA_STATUS_ERROR_ALLOCATION_FAILED;
            break;
#if CIX_VA_HAS_SEQUENCE_PARAM_BUFFER
        case VASequenceParameterBufferType:
            if (codec->SetSpsBuffer(data, size, id) != 0)
                status = VA_STATUS_ERROR_ALLOCATION_FAILED;
            break;
#endif
        case VAEncCodedBufferType:
        case VAEncPackedHeaderParameterBufferType:
        case VAEncPackedHeaderDataBufferType:
            break;
        case VAEncSequenceParameterBufferType:
            codec->SetSeqParamBuffer(data, size);
            break;
        case VAEncPictureParameterBufferType:
            codec->SetPicParamBuffer(data, size, id);
            break;
        case VAEncSliceParameterBufferType:
            codec->SetSliceParamBuffer(data, size, num_elements);
            break;
        case VAEncMiscParameterBufferType: {
            if (size < sizeof(VAEncMiscParameterBuffer)) {
                status = VA_STATUS_ERROR_INVALID_PARAMETER;
                break;
            }
            VAEncMiscParameterBuffer *header = (VAEncMiscParameterBuffer *)data;
            uint32_t payload = size - sizeof(*header);
            if (header->type == VAEncMiscParameterTypeRateControl)
                codec->SetRateControl((void *)header->data, payload);
            else if (header->type == VAEncMiscParameterTypeFrameRate)
                codec->SetFrameRate((void *)header->data, payload);
            // HRD, quality level, max frame size, ROI... are ignored.
            break;
        }
        default:
            status = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
            break;
    }

    if (status != VA_STATUS_SUCCESS) {
        // Do not leak the slot when the buffer is rejected.
        bpool->ReleaseSlot(id);
        return status;
    }

    *buf_id = id;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::BeginPicture(
    VAContextID context,
    VASurfaceID render_target
) {
    CIX_VAAPI_DEBUG("Begin picture: %u\n", render_target);
    auto codec = GetCodec(context);
    if (codec == nullptr)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    auto pool = GetSurfacePoolBySurfaceID(render_target);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %u not found during BeginPicture\n", render_target);
    codec->SetSurfacePool(pool);

    // Note: the frame data must not be reset here, clients (FFmpeg) create
    // all the parameter / slice buffers before calling vaBeginPicture().
    if (codec->InitializeBuffers(render_target) != 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::RenderPicture(
    VAContextID context,
    VABufferID *buffers,
    int num_buffers
) {
    auto codec = GetCodec(context);
    if (codec == nullptr)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    if (codec->ProcessBuffers(buffers, num_buffers) != 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::EndPicture(
    VAContextID context
) {
    auto codec = GetCodec(context);
    if (codec == nullptr)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    // The result of Submit() used to be ignored: a picture that was never
    // queued to the VPU was reported as successfully decoded and the client
    // then waited in vaSyncSurface() until the timeout.
    int32_t ret = codec->Submit();
    codec->ResetFrameData();
    if (ret == 0)
        return VA_STATUS_SUCCESS;
    return codec->IsEncoder() ? VA_STATUS_ERROR_OPERATION_FAILED : VA_STATUS_ERROR_DECODING_ERROR;
}

VAStatus MediaDeviceV4l2Stateful::SyncSurface(
    VASurfaceID render_target
) {
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %u not found during SyncSurface\n", render_target);

    auto context = pool->GetContextBySurfaceID(render_target);
    if (context == VA_INVALID_ID) {
        CIX_VAAPI_DEBUG("Surface %u has no associated context, no need to sync\n", render_target);
        return VA_STATUS_SUCCESS;
    }

    auto codec = GetCodec(context);
    CIX_VAAPI_CHECK_RETURN_CODE(codec != nullptr,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Context %u got from surface %u not found\n", context, render_target);

    CIX_VAAPI_CHECK_RETURN_CODE(codec->SyncSurface(render_target) == 0,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to sync surface %u\n", render_target);

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::QueryImageFormats(
    VAImageFormat *format_list,
    int *num_formats
) {
    CIX_VAAPI_DEBUG("Query image formats\n");

    int32_t count = 0;
    std::vector<uint32_t> formats;
    if (!dec_device.empty() && Open(dec_device.c_str()) == 0) {
        EnumFormats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, formats);
        Close();
    }

    for (auto format : formats) {
        auto image_format = image_formats.find(format);
        if (image_format == image_formats.end())
            continue;
        bool exists = false;
        for (int i = 0; i < count; i++)
            exists |= (format_list[i].fourcc == image_format->second.fourcc);
        if (!exists && count < (int32_t)image_formats.size())
            format_list[count++] = image_format->second;
    }

    *num_formats = count;
    CIX_VAAPI_INFO("Reported %d image formats:\n", count);
    for (int i = 0; i < count; i++)
        CIX_VAAPI_INFO("    %s\n", GetFourccString(format_list[i].fourcc));

    return VA_STATUS_SUCCESS;
}

VAProfile MediaDeviceV4l2Stateful::GetVAProfileFromV4l2(
    uint32_t cid,
    uint32_t profile)
{

    switch (cid) {
        case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
            switch (profile) {
                case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
                    // VAProfileH264Baseline is deprecated, map to VAProfileH264ConstrainedBaseline
                case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
                    return VAProfileH264ConstrainedBaseline;
                case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
                    return VAProfileH264Main;
                case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
                    return VAProfileH264High;
#if VA_CHECK_VERSION(1, 18, 0)
                case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
                    return VAProfileH264High10;
#endif
                default:
                    return VAProfileNone;
            }
        case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
            switch (profile) {
                case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN:
                    return VAProfileHEVCMain;
                case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10:
                    return VAProfileHEVCMain10;
                default:
                    return VAProfileNone;
            }
        case V4L2_CID_MPEG_VIDEO_VP9_PROFILE:
            switch (profile) {
                case V4L2_MPEG_VIDEO_VP9_PROFILE_0:
                    return VAProfileVP9Profile0;
                case V4L2_MPEG_VIDEO_VP9_PROFILE_1:
                    return VAProfileVP9Profile1;
                case V4L2_MPEG_VIDEO_VP9_PROFILE_2:
                    return VAProfileVP9Profile2;
                default:
                    return VAProfileNone;
            }
        case V4L2_CID_MVE_VIDEO_AV1_PROFILE:
            /* MVX menu: Main only (index 0); ignore other indices if ever added. */
            return profile == 0 ? VAProfileAV1Profile0 : VAProfileNone;
        case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
            switch (profile) {
                case V4L2_MPEG_VIDEO_VP8_PROFILE_0:
                case V4L2_MPEG_VIDEO_VP8_PROFILE_1:
                case V4L2_MPEG_VIDEO_VP8_PROFILE_2:
                case V4L2_MPEG_VIDEO_VP8_PROFILE_3:
                    return VAProfileVP8Version0_3;
                default:
                    return VAProfileNone;
            }
        default:
            return VAProfileNone;
    }
}

const char *MediaDeviceV4l2Stateful::GetProfileString(VAProfile profile)
{
    switch (profile) {
        case VAProfileH264ConstrainedBaseline:
            return "H264 Constrained Baseline";
        case VAProfileH264Main:
            return "H264 Main";
        case VAProfileH264High:
            return "H264 High";
#if VA_CHECK_VERSION(1, 18, 0)
        case VAProfileH264High10:
            return "H264 High 10";
#endif
        case VAProfileHEVCMain:
            return "HEVC Main";
        case VAProfileHEVCMain10:
            return "HEVC Main 10";
        case VAProfileVP9Profile0:
            return "VP9 Profile 0";
        case VAProfileVP9Profile1:
            return "VP9 Profile 1";
        case VAProfileVP9Profile2:
            return "VP9 Profile 2";
        case VAProfileVP9Profile3:
            return "VP9 Profile 3";
        case VAProfileVP8Version0_3:
            return "VP8 Version 0-3";
        case VAProfileAV1Profile0:
            return "AV1 Main";
        default:
            return "Unknown profile";
    }
}

void MediaDeviceV4l2Stateful::EnumProfiles(uint32_t cid, bool is_enc) {
    struct v4l2_queryctrl query_ctrl;
    memset(&query_ctrl, 0, sizeof(query_ctrl));

    query_ctrl.id = cid;
    if (xioctl(fd_, VIDIOC_QUERYCTRL, &query_ctrl) != 0) {
        CIX_VAAPI_DEBUG("Control 0x%x is not available\n", cid);
        return;
    }
    CIX_VAAPI_CHECK_RETURN(!(query_ctrl.flags & V4L2_CTRL_FLAG_DISABLED),
        "Control 0x%x is disabled\n", cid);

    if (query_ctrl.type != V4L2_CTRL_TYPE_MENU)
        return;

    struct v4l2_querymenu query_menu;
    memset(&query_menu, 0, sizeof(query_menu));
    query_menu.id = query_ctrl.id;
    for (int32_t index = query_ctrl.minimum; index <= query_ctrl.maximum; index++) {
        query_menu.index = index;
        if (xioctl(fd_, VIDIOC_QUERYMENU, &query_menu) != 0)
            continue;

        VAProfile profile = GetVAProfileFromV4l2(cid, query_menu.index);
        if (profile == VAProfileNone)
            continue;

        bool exists = false;
        for (auto &supported_profile : supported_profiles) {
            if (supported_profile.profile == profile) {
                if (is_enc)
                    supported_profile.support_enc = true;
                else
                    supported_profile.support_dec = true;
                exists = true;
                break;
            }
        }

        if (exists)
            continue;

        if (is_enc)
            supported_profiles.emplace_back(profile, false, true);
        else
            supported_profiles.emplace_back(profile, true, false);

        CIX_VAAPI_INFO("Supported profile: %s (%s)\n", GetProfileString(profile),
            is_enc ? "encode" : "decode");
    }
}

void MediaDeviceV4l2Stateful::GetSupportedProfiles() {
    if (!dec_device.empty() && Open(dec_device.c_str()) == 0) {
        EnumProfiles(V4L2_CID_MPEG_VIDEO_H264_PROFILE, false);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, false);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_VP9_PROFILE, false);
        EnumProfiles(V4L2_CID_MVE_VIDEO_AV1_PROFILE, false);
        // EnumProfiles(V4L2_CID_MPEG_VIDEO_VP8_PROFILE, false);
        Close();
    }

    if (!enc_device.empty() && Open(enc_device.c_str()) == 0) {
        EnumProfiles(V4L2_CID_MPEG_VIDEO_H264_PROFILE, true);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, true);
        // EnumProfiles(V4L2_CID_MPEG_VIDEO_VP9_PROFILE, true);
        // EnumProfiles(V4L2_CID_MPEG_VIDEO_VP8_PROFILE, true);
        Close();
    }

    // HEVC and AV1 decoding need the raw sequence header (and, for AV1, the
    // raw frame header) which only the CIX build of libva passes to the
    // driver. With the upstream libva (Ubuntu 26.04) every such picture fails
    // the integrity check, so do not advertise these profiles: clients fall
    // back to software decoding instead of stalling.
    for (auto &p : supported_profiles) {
        bool keep = true;
        if (IS_H265_PROFILE(p.profile))
            keep = CIX_VA_HAS_SEQUENCE_PARAM_BUFFER;
        else if (IS_AV1_PROFILE(p.profile))
            keep = CIX_VA_HAS_SEQUENCE_PARAM_BUFFER && CIX_VA_HAS_AV1_EXT;
        if (p.support_dec && !keep) {
            CIX_VAAPI_INFO("%s decoding needs the CIX libva extensions, not advertised\n",
                GetProfileString(p.profile));
            p.support_dec = false;
        }
    }
}

uint32_t MediaDeviceV4l2Stateful::GetRateControlModeFromConfig(const DeviceConfig *config) const
{
    if (!config)
        return 0;
    for (const auto &attrib : config->attribs) {
        if (attrib.type == VAConfigAttribRateControl &&
            attrib.value != VA_ATTRIB_NOT_SUPPORTED) {
            uint32_t rc_mode = attrib.value;
            if (rc_mode & VA_RC_CBR)
                return VA_RC_CBR;
            if (rc_mode & VA_RC_VBR)
                return VA_RC_VBR;
            if (rc_mode & VA_RC_CQP)
                return VA_RC_CQP;
            return 0;
        }
    }
    return 0;
}

uint32_t MediaDeviceV4l2Stateful::GetV4l2PixFmtFromVaFourcc(uint32_t fourcc)
{
    uint32_t v4l2_fmt = VaFourccToV4l2PixFmt(fourcc);
    return v4l2_fmt ? v4l2_fmt : V4L2_PIX_FMT_NV12; // use NV12 by default
}

uint32_t MediaDeviceV4l2Stateful::GetV4l2PixFmtFromSurface(VASurfaceID id)
{
    auto pool = GetSurfacePoolBySurfaceID(id);
    if (pool == nullptr)
        return V4L2_PIX_FMT_NV12;

    return GetV4l2PixFmtFromVaFourcc(pool->GetFormat());
}

uint32_t MediaDeviceV4l2Stateful::GetV4l2PixFmtFromConfig(const DeviceConfig *config)
{
    if (config == nullptr)
        return 0;

    if (IS_H264_PROFILE(config->profile))
        return V4L2_PIX_FMT_H264;
    else if (IS_H265_PROFILE(config->profile))
        return V4L2_PIX_FMT_HEVC;
    else if (IS_VP9_PROFILE(config->profile))
        return V4L2_PIX_FMT_VP9;
    else if (IS_AV1_PROFILE(config->profile))
        return V4L2_PIX_FMT_AV1;
    else if (IS_VP8_PROFILE(config->profile))
        return V4L2_PIX_FMT_VP8;

    return 0;
}

bool MediaDeviceV4l2Stateful::isDecoder(uint32_t out_type, uint32_t cap_type)
{
    return hasCompressedFormat(out_type) &&
           hasUncompressedFormat(cap_type);
}

bool MediaDeviceV4l2Stateful::isEncoder(uint32_t out_type, uint32_t cap_type)
{
    return hasUncompressedFormat(out_type) &&
           hasCompressedFormat(cap_type);
}

bool MediaDeviceV4l2Stateful::hasCompressedFormat(uint32_t type)
{
    struct v4l2_fmtdesc fmt_desc;
    memset(&fmt_desc, 0, sizeof(fmt_desc));
    fmt_desc.type = type;
    fmt_desc.index = 0;

    while (xioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        if (fmt_desc.flags & V4L2_FMT_FLAG_COMPRESSED)
            return true;
        fmt_desc.index++;
    }

    return false;
}

bool MediaDeviceV4l2Stateful::hasUncompressedFormat(uint32_t type)
{
    struct v4l2_fmtdesc fmt_desc;
    memset(&fmt_desc, 0, sizeof(fmt_desc));
    fmt_desc.type = type;
    fmt_desc.index = 0;

    while (xioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        if (!(fmt_desc.flags & V4L2_FMT_FLAG_COMPRESSED))
            return true;
        fmt_desc.index++;
    }

    return false;
}

void MediaDeviceV4l2Stateful::EnumFormats(uint32_t type, std::vector<uint32_t> &formats)
{
    struct v4l2_fmtdesc fmt_desc;
    memset(&fmt_desc, 0, sizeof(fmt_desc));
    fmt_desc.type = type;
    fmt_desc.index = 0;

    if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
        type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        CIX_VAAPI_DEBUG("Device supported output formats:\n");
    } else {
        CIX_VAAPI_DEBUG("Device supported capture formats:\n");
    }

    while (xioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        CIX_VAAPI_DEBUG("    %s\n", (const char *)fmt_desc.description);
        formats.push_back(fmt_desc.pixelformat);
        fmt_desc.index++;
    }
}

int32_t MediaDeviceV4l2Stateful::QuerySurfaceLengthsAndStrides(
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t strides[CIX_VAAPI_MAX_PLANES]
) {
    // Only ask the VPU about the formats it can produce or consume.
    if (format != VA_FOURCC_NV12 && format != VA_FOURCC_NV21 &&
        format != VA_FOURCC_P010 && format != VA_FOURCC_I420)
        return -1;

    uint32_t num_planes = GetNumPlanes(format);
    uint32_t v4l2_format = VaFourccToV4l2PixFmt(format);
    if (num_planes == 0 || num_planes > CIX_VAAPI_MAX_PLANES || v4l2_format == 0)
        return -1;

    if (dec_device.empty() || Open(dec_device.c_str()) != 0)
        return -1;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    xioctl(fd_, VIDIOC_G_FMT, &fmt);

    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    f.width = width;
    f.height = height;
    f.pixelformat = V4L2_PIX_FMT_H264;
    xioctl(fd_, VIDIOC_S_FMT, &fmt);

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(fd_, VIDIOC_G_FMT, &fmt);

    f.width = width;
    f.height = height;
    f.pixelformat = v4l2_format;
    f.num_planes = num_planes;
    for (uint32_t i = 0; i < num_planes; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        f.plane_fmt[i].bytesperline = (((width * bpp) >> 3) + 63) & ~63; // Align to 64 bytes
        f.plane_fmt[i].sizeimage = 0;
    }
    int ret = xioctl(fd_, VIDIOC_S_FMT, &fmt);
    Close();

    // The results used to be used unchecked: a failed S_FMT or a substituted
    // format produced zero-sized surfaces.
    if (ret != 0 || f.pixelformat != v4l2_format || f.num_planes != num_planes) {
        CIX_VAAPI_DEBUG("VPU layout query for %s %ux%u failed, using default layout\n",
            GetFourccString(format), width, height);
        return -1;
    }

    for (uint32_t i = 0; i < num_planes; i++) {
        if (f.plane_fmt[i].sizeimage == 0 || f.plane_fmt[i].bytesperline == 0)
            return -1;
        lengths[i] = f.plane_fmt[i].sizeimage;
        strides[i] = f.plane_fmt[i].bytesperline;
    }

    return 0;
}

VAStatus MediaDeviceV4l2Stateful::SyncBuffer(VABufferID id) {
    // Sync the buffer with the device
    auto buf = GetBufferPool()->GetBufferInfo(id);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr,
        VA_STATUS_ERROR_INVALID_BUFFER,
        "Buffer %u not found during SyncBuffer\n", id);

    auto codec = GetCodec(buf->ctx);
    if (codec != nullptr && codec->SyncBuffer(id) != 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::MapCodedBuffer(
    VABufferID buf_id,
    void **pbuf
) {
    auto buf = GetBufferPool()->GetBufferInfo(buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_INVALID_BUFFER,
        "Coded buffer %u not found\n", buf_id);

    auto codec = GetCodec(buf->ctx);
    CIX_VAAPI_CHECK_RETURN_CODE(codec != nullptr,
        VA_STATUS_ERROR_INVALID_BUFFER,
        "Coded buffer %u has no associated codec\n", buf_id);

    // Sync buffer and then map
    CIX_VAAPI_CHECK_RETURN_CODE(SyncBuffer(buf_id) == VA_STATUS_SUCCESS,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to sync coded buffer %u before map\n", buf_id);

    auto vbuf = codec->GetCapPort().GetBufferByUserData(buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Coded buffer %u not found in codec\n", buf_id);

    uint8_t *data = (uint8_t *)vbuf->Map(0);
    CIX_VAAPI_CHECK_RETURN_CODE(data != nullptr, VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to map coded buffer %u\n", buf_id);

    uint32_t offset = vbuf->GetDataOffset(0);
    uint32_t used = vbuf->GetBytesUsed(0);
    memset(&coded_buffer_segment, 0, sizeof(coded_buffer_segment));
    coded_buffer_segment.buf = data + offset;
    coded_buffer_segment.size = used > offset ? used - offset : 0;
    coded_buffer_segment.bit_offset = 0;
    coded_buffer_segment.next = nullptr;
    *pbuf = &coded_buffer_segment;
    CIX_VAAPI_INFO("Mapped coded buffer %u: buf = %p, size = %u\n", buf_id,
        coded_buffer_segment.buf, coded_buffer_segment.size);

    return VA_STATUS_SUCCESS;
}

int32_t MediaDeviceV4l2Stateful::Open(const char* device_name) {
    CIX_VAAPI_DEBUG("Open device %s\n", device_name);
    fd_ = open(device_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
        return -1;

    return 0;
}

int32_t MediaDeviceV4l2Stateful::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    return 0;
}
