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
#include <string>
#include <climits>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <va/va.h>
#include <va/va_enc_hevc.h>
#include "mvx-v4l2-controls.h"
#include "device_common.h"
#include "v4l2_stateful_device.h"
#include "v4l2_stateful_dec.h"
#include "v4l2_stateful_enc.h"

#define MAX_VIDEO_DEVICE_NUM INT_MAX // maximum number of video devices to probe
#define MIN_VIDEO_DEVICE_NUM 32      // minimum number of video devices to scan

std::map<uint32_t, int32_t> format_map = {
    {VA_RT_FORMAT_YUV420, V4L2_PIX_FMT_NV12},
};

MediaDeviceV4l2Stateful::MediaDeviceV4l2Stateful(MediaDevice &media_device) :
    media_device_(media_device),
    fd_(-1),
    buffer_count(0),
    image_count(0),
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
        CIX_VAAPI_ERROR("No valid V4L2 codec devices found\n");
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

void MediaDeviceV4l2Stateful::QueryCodecDevices() {
    bool found_decoder = false;
    bool found_encoder = false;
    std::string device_name = "/dev/video";
    for (int i = 0; i < MAX_VIDEO_DEVICE_NUM; i++) {
        std::string device_file = device_name + std::to_string(i);
        struct stat buffer;
        // Check file existence
        if (stat (device_file.c_str(), &buffer) != 0 && i > MIN_VIDEO_DEVICE_NUM)
            break;

        Open(device_file.c_str());
        if (fd_ < 0)
            continue;

        enum v4l2_buf_type out_type;
        enum v4l2_buf_type cap_type;
        struct v4l2_capability cap;
        if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) != 0) {
            Close();
            continue;
        }

        if ((cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE)) ||
            (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
            out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        } else if ((cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT)) ||
            (cap.capabilities & V4L2_CAP_VIDEO_M2M)) {
            out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        } else {
            Close();
            continue;
        }

        if (!found_decoder && isDecoder(out_type, cap_type)) {
            found_decoder = true;
            dec_device = device_file;
            QueryDecodeCaps();
        }

        if (!found_encoder && isEncoder(out_type, cap_type)) {
            found_encoder = true;
            enc_device = device_file;
            QueryEncodeCaps();
        }

        if (found_decoder && found_encoder) {
            Close();
            break;
        }

        Close();
    }
}

VAStatus MediaDeviceV4l2Stateful::QueryConfigProfiles(
    VAProfile *profile_list,
    int *num_profiles
) {
    int count = 0;
    for (auto profile : supported_profiles) {
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
    for (auto &supported_profile : supported_profiles) {
        if (profile == supported_profile.profile) {
            if (supported_profile.support_dec)
                entrypoints[count++] = VAEntrypointVLD;

            // 10-bit encoding is not supported for now.
            if (supported_profile.support_enc && !IS_10BIT_PROFILE(profile))
                entrypoints[count++] = VAEntrypointEncSliceLP;

            *num_entrypoints = count;
            return VA_STATUS_SUCCESS;
        }
    }

    return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

VAStatus MediaDeviceV4l2Stateful::GetConfigAttributes(
    VAProfile profile,
    VAEntrypoint entrypoint,
    VAConfigAttrib *attrib_list,
    int num_attribs
) {
    for (int i = 0; i < num_attribs; i++) {
        if (attrib_list[i].type == VAConfigAttribRTFormat) {
            attrib_list[i].value = VA_RT_FORMAT_YUV420;
            /* AV1 Main (Profile0) allows 8- and 10-bit 420 in one VA profile. */
            if ((IS_10BIT_PROFILE(profile)) && entrypoint == VAEntrypointVLD)
                attrib_list[i].value |= VA_RT_FORMAT_YUV420_10;
        } else if (attrib_list[i].type == VAConfigAttribRateControl) {
            attrib_list[i].value = VA_RC_CQP | VA_RC_CBR | VA_RC_VBR;
        } else if (attrib_list[i].type == VAConfigAttribEncPackedHeaders) {
            attrib_list[i].value = VA_ENC_PACKED_HEADER_NONE;
        } else if (attrib_list[i].type == VAConfigAttribEncInterlaced) {
            attrib_list[i].value = VA_ENC_INTERLACED_NONE;
        } else if (attrib_list[i].type == VAConfigAttribEncSliceStructure) {
            attrib_list[i].value = VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS;
        } else if (attrib_list[i].type == VAConfigAttribMaxPictureWidth) {
            attrib_list[i].value = 8192;
        } else if (attrib_list[i].type == VAConfigAttribMaxPictureHeight) {
            attrib_list[i].value = 8192;
        } else if (attrib_list[i].type == VAConfigAttribProcessingRate) {
            attrib_list[i].value = VA_PROCESSING_RATE_NONE;
        } else if (attrib_list[i].type == VAConfigAttribEncQuantization) {
            attrib_list[i].value = VA_ENC_QUANTIZATION_NONE;
        } else if (attrib_list[i].type == VAConfigAttribEncTileSupport) {
            attrib_list[i].value = 1;
        } else if (attrib_list[i].type == VAConfigAttribEncMaxRefFrames) {
            // attrib_list[i].value = (1 << 16) | 1; // 1 reference frame, 1 reference frame for B frames
            attrib_list[i].value = 1; // 1 reference frame, doesn't support B frame for now
        } else if (attrib_list[i].type == VAConfigAttribEncHEVCFeatures) {
            // VAConfigAttribValEncHEVCFeatures features;
            // features.sao = 3;
            // features.deblocking_filter_disable = 3;
            // attrib_list[i].value = features.value;
        // } else if (attrib_list[i].type == VAConfigAttribEncHEVCBlockSizes) {
        } else {
            attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
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
        "attrib_list size %d is too small\n", *num_attribs);

    uint32_t i = 0;
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, VA_FOURCC_NV12,
                            VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    if (IS_10BIT_PROFILE(config->profile)) {
        SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribPixelFormat, VA_FOURCC_P010,
                                VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE);
    }
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinWidth, 144);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMinHeight, 144);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxWidth, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMaxHeight, 8192);
    SetSurfaceAttribInteger(&attrib_list[i++], VASurfaceAttribMemoryType,
                            VA_SURFACE_ATTRIB_MEM_TYPE_VA | VA_SURFACE_ATTRIB_MEM_TYPE_V4L2 |
                                VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR,
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
    CIX_VAAPI_CHECK_RETURN_CODE(config != nullptr, VA_STATUS_ERROR_INVALID_CONFIG,
        "null config during CreateContext (V4L2)\n");

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
        raw_format = pool ? pool->GetFormat() : V4L2_PIX_FMT_NV12;
    }

    uint32_t id = codec_count;
    if (IS_DECODE_ENTRYPOINT(config->entrypoint))
        codecs[id] = new V4l2StatefulDecoder(
            dec_device.c_str(),
            id,
            picture_width,
            picture_height,
            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            compressed_format,
            raw_format
        );
    else if (IS_ENCODE_ENTRYPOINT(config->entrypoint))
        codecs[id] = new V4l2StatefulEncoder(
            enc_device.c_str(),
            id,
            picture_width,
            picture_height,
            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
            raw_format,
            compressed_format
        );

    if (pool)
        codecs[id]->SetSurfacePool(pool);

    // Set rate control mode
    if (codecs[id]->IsEncoder()) {
        uint32_t rc_mode = GetRateControlModeFromConfig(config);
        if (rc_mode != 0 && codecs[id]->SetRateControlMode(rc_mode) != 0)
            CIX_VAAPI_WARNING("SetRateControlMode(0x%x) failed for context %u\n", rc_mode, id);

        if (codecs[id]->SetProfile(config->profile) != 0)
             CIX_VAAPI_WARNING("Failed to set profile %d for context %d\n", config->profile, id);
    }

    codecs[id]->AddSurfaces(num_render_targets);

    // Create buffers
    codecs[id]->CreateBuffers();

    codecs[id]->SetBufferPool(GetBufferPool());

    CIX_VAAPI_INFO("Created V4L2 stateful context %d\n", codec_count);
    *context = (VAContextID)id;
    codec_count++;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::DestroyContext(
    VAContextID context
) {
    auto codec = codecs[context];
    CIX_VAAPI_INFO("Destroy V4L2 stateful context %d\n", context);
    delete codec;
    codecs.erase(context);

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
    CIX_VAAPI_INFO("CreateBuffer: context %d, type %d, size %d, num_elements %d\n",
        context, type, size, num_elements);
    if ((type != VASliceParameterBufferType && num_elements > 1) ||
        (type != VAEncCodedBufferType && data == nullptr))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    auto bpool = GetBufferPool();
    auto buf = bpool->GetFreeSlot(type, context);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, VA_STATUS_ERROR_MAX_NUM_EXCEEDED,
        "Failed to create buffer\n");
    CIX_VAAPI_DEBUG("Codec CreateBuffer: assigned buf_id=%u, type=%d, ctx=0x%x\n",
        buf->id, type, context);
    *buf_id = buf->id;

    auto codec = codecs[context];
    if (type == VAPictureParameterBufferType) {
        codec->SetPicParamBuffer(data, size, *buf_id);
    } else if (type == VAIQMatrixBufferType) {
        if (codec->SetIQMatrix(data, size) != 0)
            return VA_STATUS_ERROR_OPERATION_FAILED;
    } else if (type == VASliceParameterBufferType) {
        codec->SetSliceParamBuffer(data, size, num_elements);
    } else if (type == VASliceDataBufferType) {
        codec->SetSliceDataBuffer(data, size, *buf_id);
#if (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
    } else if (type == VASequenceParameterBufferType) {
        codec->SetSpsBuffer(data, size, *buf_id);
#endif
    } else if (type == VAEncCodedBufferType) {
    } else if (type == VAEncSequenceParameterBufferType) {
        codec->SetSeqParamBuffer(data, size);
    } else if (type == VAEncPictureParameterBufferType) {
        codec->SetPicParamBuffer(data, size, *buf_id);
    } else if (type == VAEncSliceParameterBufferType) {
        codec->SetSliceParamBuffer(data, size, num_elements);
    } else if (type == VAEncPackedHeaderParameterBufferType) {
    } else if (type == VAEncPackedHeaderDataBufferType) {
    // } else if (type == VAEncPackedHeaderRawData) {
    } else if (type == VAEncMiscParameterBufferType) {
        VAEncMiscParameterBuffer *header = (VAEncMiscParameterBuffer *)data;
        if (header->type == VAEncMiscParameterTypeRateControl) {
            codec->SetRateControl((void *)header->data, size - sizeof(*header));
        // } else if (header->type == VAEncMiscParameterTypeHRD) {
        } else if (header->type == VAEncMiscParameterTypeFrameRate) {
            codec->SetFrameRate((void *)header->data, size - sizeof(*header));
        // } else if (header->type == VAEncMiscParameterTypeQualityLevel) {
        // } else if (header->type == VAEncMiscParameterTypeMaxFrameSize) {
        // } else if (header->type == VAEncMiscParameterTypeROI) {
        }
    } else {
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::BeginPicture(
    VAContextID context,
    VASurfaceID render_target
) {
    CIX_VAAPI_DEBUG("Begin picture: %d\n", render_target);
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    auto codec = codecs[context];
    codec->SetSurfacePool(pool);

    const std::lock_guard<std::mutex> lock(mutex);
    if (codec->InitializeBuffers(render_target) != 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::RenderPicture(
    VAContextID context,
    VABufferID *buffers,
    int num_buffers
) {
    auto codec = codecs[context];
    if (codec->ProcessBuffers(buffers, num_buffers) != 0)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::EndPicture(
    VAContextID context
) {
    auto codec = codecs[context];
    codec->Submit();
    codec->ResetFrameData();
    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::SyncSurface(
    VASurfaceID render_target
) {
    auto pool = GetSurfacePoolBySurfaceID(render_target);
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr,
        VA_STATUS_ERROR_INVALID_SURFACE,
        "Surface %d not found during SyncSurface\n", render_target);

    auto context = pool->GetContextBySurfaceID(render_target);
    if (context == VA_INVALID_ID) {
        CIX_VAAPI_INFO("Surface %d has no associated context, no need to sync\n", render_target);
        return VA_STATUS_SUCCESS;
    }

    CIX_VAAPI_CHECK_RETURN_CODE(HasContextId(context),
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Context %d got from surface %d not found\n", render_target, context);

    const std::lock_guard<std::mutex> lock(mutex);
    CIX_VAAPI_CHECK_RETURN_CODE(codecs[context]->SyncSurface(render_target) == 0,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to sync surface %d\n", render_target);

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::QueryImageFormats(
    VAImageFormat *format_list,
    int *num_formats
) {
    CIX_VAAPI_DEBUG("Query image formats\n");

    int32_t count = 0;
    std::vector<uint32_t> formats;
    if (Open(dec_device.c_str()) == 0) {
        EnumFormats(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, formats);
        Close();
    }

    for (auto format : formats) {
        auto image_format = image_formats.find(format);
        if (image_format != image_formats.end())
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
    struct v4l2_queryctrl query_ctrl = {0};

    query_ctrl.id = cid;
    CIX_VAAPI_CHECK_RETURN(
        ioctl(fd_, VIDIOC_QUERYCTRL, &query_ctrl) == 0,
        "Failed to query control %d\n", cid);
    CIX_VAAPI_CHECK_RETURN(!(query_ctrl.flags & V4L2_CTRL_FLAG_DISABLED),
        "Control %d is disabled\n", cid);

    if (query_ctrl.type == V4L2_CTRL_TYPE_MENU) {
        struct v4l2_querymenu query_menu = {0};
        query_menu.id = query_ctrl.id;
        for (query_menu.index = query_ctrl.minimum;
            query_menu.index <= query_ctrl.maximum;
            query_menu.index++) {
            if (ioctl (fd_, VIDIOC_QUERYMENU, &query_menu) >= 0) {
                VAProfile profile = GetVAProfileFromV4l2(cid, query_menu.index);
                bool exists = false;

                if (profile == VAProfileNone)
                    continue;

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

                CIX_VAAPI_INFO("Supported profile: %s\n", GetProfileString(profile));
            }
        }
    }
}

void MediaDeviceV4l2Stateful::GetSupportedProfiles() {
    if (Open(dec_device.c_str()) == 0) {
        EnumProfiles(V4L2_CID_MPEG_VIDEO_H264_PROFILE, false);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, false);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_VP9_PROFILE, false);
        EnumProfiles(V4L2_CID_MVE_VIDEO_AV1_PROFILE, false);
        // EnumProfiles(V4L2_CID_MPEG_VIDEO_VP8_PROFILE, false);
        Close();
    }

    if (Open(enc_device.c_str()) == 0) {
        EnumProfiles(V4L2_CID_MPEG_VIDEO_H264_PROFILE, true);
        EnumProfiles(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, true);
    //     EnumProfiles(V4L2_CID_MPEG_VIDEO_VP9_PROFILE, true);
    //     EnumProfiles(V4L2_CID_MPEG_VIDEO_VP8_PROFILE, true);
        Close();
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
    switch (fourcc) {
        case VA_FOURCC_NV12:
            return V4L2_PIX_FMT_NV12;
        case VA_FOURCC_NV21:
            return V4L2_PIX_FMT_NV21;
        case VA_FOURCC_I420:
            return V4L2_PIX_FMT_YUV420;
        case VA_FOURCC_P010:
            return V4L2_PIX_FMT_P010;
    }

    return V4L2_PIX_FMT_NV12; // use NV12 by default
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
    fmt_desc.type = type;
    fmt_desc.index = 0;

    while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        if (fmt_desc.flags & V4L2_FMT_FLAG_COMPRESSED)
            return true;
        fmt_desc.index++;
    }

    return false;
}

bool MediaDeviceV4l2Stateful::hasUncompressedFormat(uint32_t type)
{
    struct v4l2_fmtdesc fmt_desc;
    fmt_desc.type = type;
    fmt_desc.index = 0;

    while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        if (!(fmt_desc.flags & V4L2_FMT_FLAG_COMPRESSED))
            return true;
        fmt_desc.index++;
    }

    return false;
}

void MediaDeviceV4l2Stateful::EnumFormats(uint32_t type, std::vector<uint32_t> &formats)
{
    struct v4l2_fmtdesc fmt_desc;
    fmt_desc.type = type;
    fmt_desc.index = 0;

    if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
        type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        CIX_VAAPI_DEBUG("Device supported output formats:\n");
    } else {
        CIX_VAAPI_DEBUG("Device supported capture formats:\n");
    }

    while (ioctl(fd_, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        CIX_VAAPI_DEBUG("    %s\n", fmt_desc.description);
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
    CIX_VAAPI_CHECK_RETURN_CODE(Open(dec_device.c_str()) == 0,
        -1, "Failed to open device %s\n", dec_device.c_str());

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ioctl(fd_, VIDIOC_G_FMT, &fmt);

    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    f.width = width;
    f.height = height;
    f.pixelformat = V4L2_PIX_FMT_H264;
    ioctl(fd_, VIDIOC_S_FMT, &fmt);

    fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd_, VIDIOC_G_FMT, &fmt);

    f.width = width;
    f.height = height;
    f.pixelformat = GetV4l2PixFmtFromVaFourcc(format);
    f.num_planes = GetNumPlanes(format);
    for (int i = 0; i < f.num_planes; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        f.plane_fmt[i].bytesperline = (((width * bpp) >> 3) + 63) & ~63; // Align to 64 bytes
        f.plane_fmt[i].sizeimage = 0;
    }
    ioctl(fd_, VIDIOC_S_FMT, &fmt);

    for (int i = 0; i < f.num_planes; i++) {
        lengths[i] = f.plane_fmt[i].sizeimage;
        strides[i] = f.plane_fmt[i].bytesperline;
    }

    Close();
    return 0;
}

VAStatus MediaDeviceV4l2Stateful::SyncBuffer(VABufferID id) {
    // Sync the buffer with the device
    auto buf = GetBufferPool()->GetBufferInfo(id);
    CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr,
        VA_STATUS_ERROR_INVALID_BUFFER,
        "Buffer %d not found during SyncBuffer\n", id);

    auto it = codecs.find(buf->ctx);
    if (it != codecs.end() && it->second)
        it->second->SyncBuffer(id);

    return VA_STATUS_SUCCESS;
}

VAStatus MediaDeviceV4l2Stateful::MapCodedBuffer(
    VABufferID buf_id,
    void **pbuf
) {
    const std::lock_guard<std::mutex> lock(mutex);
    // Sync buffer and then map
    CIX_VAAPI_CHECK_RETURN_CODE(SyncBuffer(buf_id) == VA_STATUS_SUCCESS,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Failed to sync coded buffer %d before map\n", buf_id);

    auto buf = GetBufferPool()->GetBufferInfo(buf_id);
    auto it = codecs.find(buf->ctx);
    auto codec = (it != codecs.end()) ? it->second : nullptr;
    CIX_VAAPI_CHECK_RETURN_CODE(codec != nullptr,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Coded buffer %d has no associated codec\n", buf_id);

    auto vbuf = codec->GetCapPort().GetBufferByUserData(buf_id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr,
        VA_STATUS_ERROR_OPERATION_FAILED,
        "Coded buffer %d not found in codec\n", buf_id);

    coded_buffer_segment.buf = (uint8_t *)vbuf->Map(0) + vbuf->GetDataOffset(0);
    coded_buffer_segment.size = vbuf->GetBytesUsed(0) - vbuf->GetDataOffset(0);
    coded_buffer_segment.bit_offset = 0;
    coded_buffer_segment.next = nullptr;
    *pbuf = &coded_buffer_segment;
    CIX_VAAPI_INFO("Mapped coded buffer %d: buf = %p, size = %d, bit offset = %d\n", buf_id,
        coded_buffer_segment.buf, coded_buffer_segment.size, coded_buffer_segment.bit_offset);

    return VA_STATUS_SUCCESS;
}

int32_t MediaDeviceV4l2Stateful::Open(const char* device_name) {
    CIX_VAAPI_INFO("Open device %s\n", device_name);
    fd_ = open(device_name, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        return -1;
    }

    return 0;
}

int32_t MediaDeviceV4l2Stateful::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    return 0;
}
