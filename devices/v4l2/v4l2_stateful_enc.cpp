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
#include <linux/videodev2.h>
#include <string.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <va/va.h>
#include "cix_va_compat.h"
#include "v4l2_stateful_enc.h"
#include "extractor_h264.h"
#include "extractor_hevc.h"
#include "dump.h"

#define SYNC_ENC_SURFACE_POLL_TIMEOUT_MS 1000
#define SYNC_ENC_SURFACE_MAX_RETRIES 15

V4l2StatefulEncoder::V4l2StatefulEncoder(
    const char *device_name,
    VAContextID context,
    uint32_t pic_width,
    uint32_t pic_height,
    enum v4l2_buf_type out_type,
    enum v4l2_buf_type cap_type,
    uint32_t out_format,
    uint32_t cap_format
) : V4l2Stateful(device_name, context, pic_width, pic_height, out_type, cap_type, out_format, cap_format),
    extractor(nullptr),
    slice_param_count(0),
    sps_count(0),
    sps_id(-1),
    first_slice(true),
    frame_count(0),
    frame_dump(nullptr),
    v4l2_rc_mode(-1)
{
    if (cap_format == V4L2_PIX_FMT_H264)
        extractor = new ExtractorH264();
    else if (cap_format == V4L2_PIX_FMT_HEVC)
        extractor = new ExtractorHEVC();

    if (extractor == nullptr) {
        CIX_VAAPI_ERROR("Unsupported encoder output format %s\n", GetFourccString(cap_format));
        SetValid(false);
    }

    frame_dump = DUMP_OpenFrameFile();
}

V4l2StatefulEncoder::~V4l2StatefulEncoder()
{
    delete extractor;
    extractor = nullptr;

    DUMP_Close(frame_dump);
}

int32_t V4l2StatefulEncoder::CreateBuffers()
{
    Port &port = GetCapPort();
    uint32_t num = GetNumBitstreamBuffers();
    return port.CreateBuffers(num, V4L2_MEMORY_MMAP) > 0 ? 0 : -1;
}

int32_t V4l2StatefulEncoder::AddSurfaces(uint32_t num)
{
    Port &port = GetOutPort();
    if (num == 0)
        num = V4L2_STATEFUL_CODEC_NUM_FRAME_BUFFERS;
    return port.CreateBuffers(num, V4L2_MEMORY_DMABUF) > 0 ? 0 : -1;
}

void V4l2StatefulEncoder::SetSeqParamBuffer(void *data, uint32_t size)
{
    if (data == nullptr || extractor == nullptr)
        return;

    extractor->SetSequenceParameter(data, size);

    if (!GetCapPort().IsStreamOn() && !GetOutPort().IsStreamOn()) {
        int32_t tier = GetV4l2Tier(extractor->GetTier(), extractor->GetLevel());
        if (tier >= 0 && SetV4l2Tier(tier) != 0)
            CIX_VAAPI_WARNING("SetTier(%d) failed\n", tier);

        int32_t level = GetV4l2Level(extractor->GetLevel());
        if (level >= 0 && SetV4l2Level(level) != 0)
            CIX_VAAPI_WARNING("SetLevel(%d) failed\n", level);

        uint32_t gop_size = extractor->GetGopSize();
        if (gop_size != (uint32_t)-1 && SetV4l2GopSize(gop_size) != 0)
            CIX_VAAPI_WARNING("SetGopSize(%u) failed\n", gop_size);

        uint32_t b_frames = extractor->GetBFrames();
        if (b_frames != 0 && SetV4l2BFrames(b_frames) != 0)
            CIX_VAAPI_WARNING("SetBFrames(%u) failed\n", b_frames);
    }
}

void V4l2StatefulEncoder::SetPicParamBuffer(void *data, uint32_t size, VABufferID id)
{
    (void)id;
    if (data && extractor)
        extractor->SetPictureParameter(data, size);
}

void V4l2StatefulEncoder::SetSliceParamBuffer(void *data, uint32_t size, uint32_t num_elements)
{
    CIX_VAAPI_CHECK_RETURN(num_elements == 1,
        "Slice parameter buffer should contain only one element, got %u\n", num_elements);

    if (data == nullptr || extractor == nullptr)
        return;

    extractor->SetSliceParameter(data, size);

    if (v4l2_rc_mode == -1) {
        // For CQP mode, set QP for each slice based on the coding type
        uint32_t qp = extractor->GetQP();
        uint32_t coding_type = extractor->GetCodingType();
        if (frame_count == 0) {
            SetQPI(qp);
            SetQPP(qp);
            SetQPB(qp);
        } else if (coding_type == CODING_TYPE_I) {
            SetQPI(qp);
        } else if (coding_type == CODING_TYPE_P) {
            SetQPP(qp);
        } else if (coding_type == CODING_TYPE_B) {
            SetQPB(qp);
        }
    }
}

void V4l2StatefulEncoder::SetRateControl(void *data, uint32_t size)
{
    if (data == nullptr || size < sizeof(VAEncMiscParameterRateControl))
        return;

    VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)data;
    // 64-bit arithmetic: bits_per_second * target_percentage overflowed
    // 32 bits above ~43 Mbit/s. A percentage of 0 means "not set" (100 %).
    uint32_t percentage = rc->target_percentage ? rc->target_percentage : 100;
    if (percentage > 100)
        percentage = 100;
    uint64_t bitrate = (uint64_t)rc->bits_per_second * percentage / 100;
    if (bitrate > INT32_MAX)
        bitrate = INT32_MAX;
    uint32_t bitrate_bps = (uint32_t)bitrate;

    CIX_VAAPI_INFO("Set target bitrate to %u bps (bits_per_second = %u, target_percentage = %u)\n",
        bitrate_bps, rc->bits_per_second, rc->target_percentage);
    if (bitrate_bps != 0 && SetV4l2Bitrate(bitrate_bps) != 0)
        CIX_VAAPI_WARNING("SetTargetBitrate(%u) failed\n", bitrate_bps);

    if (rc->min_qp || rc->max_qp) {
        CIX_VAAPI_INFO("Set QP range to [%u, %u]\n", rc->min_qp, rc->max_qp);
        CIX_VAAPI_CHECK_RETURN(SetV4l2QpRange(rc->min_qp, rc->max_qp) == 0,
            "Failed to set QP range: qp_min = %u, qp_max = %u\n", rc->min_qp, rc->max_qp);
    }
}

void V4l2StatefulEncoder::SetFrameRate(void *data, uint32_t size)
{
    if (data == nullptr || size < sizeof(VAEncMiscParameterFrameRate))
        return;

    VAEncMiscParameterFrameRate *framerate = (VAEncMiscParameterFrameRate *)data;
    // VA-API: numerator in the low 16 bits, denominator in the high 16 bits,
    // a denominator of 0 meaning 1 (plain integer frame rate).
    uint32_t fps_d = (framerate->framerate >> 16) & 0xFFFF;
    uint32_t fps_n = framerate->framerate & 0xFFFF;
    if (fps_d == 0)
        fps_d = 1;
    CIX_VAAPI_INFO("Set frame rate to: %u/%u fps\n", fps_n, fps_d);
    if (fps_n != 0 && SetV4l2Framerate(fps_n, fps_d) != 0)
        CIX_VAAPI_WARNING("SetFrameRate(%u/%u) failed\n", fps_n, fps_d);
}

int32_t V4l2StatefulEncoder::CheckResolutionChange(uint32_t width, uint32_t height, uint32_t format)
{
    auto &port = GetOutPort();
    if (port.GetWidth() != width || port.GetHeight() != height || port.GetPixelFormat() != format) {
        CIX_VAAPI_CHECK_RETURN_CODE(frame_count == 0, -1,
            "Resolution change is not supported (%ux%u (%s) -> %ux%u (%s))\n",
            port.GetWidth(), port.GetHeight(), GetFourccString(port.GetPixelFormat()),
            width, height, GetFourccString(format));
        if (GetOutPort().UpdateResolution(width, height, format) != 0 ||
            GetCapPort().UpdateResolution(width, height, GetCapPort().GetPixelFormat()) != 0)
            return -1;
        CIX_VAAPI_INFO("Updated port format to %ux%u (%s)\n", width, height, GetFourccString(format));
    }

    return 0;
}

int32_t V4l2StatefulEncoder::InitializeBuffers(VABufferID render_target)
{
    CIX_VAAPI_CHECK_RETURN_CODE(extractor != nullptr, -1, "Encoder is not initialized\n");

    // Poll to get as many output/capture port buffers as possible
    if (frame_count > 0) {
        for (uint32_t i = 0; i < 2 * V4L2_STATEFUL_CODEC_NUM_FRAME_BUFFERS; i++) {
            if (Poll(POLLOUT | POLLIN, 0) <= 0)
                break;
        }
    }

    auto pool = GetSurfacePool();
    CIX_VAAPI_CHECK_RETURN_CODE(pool != nullptr, -1, "Encoder has no surface pool\n");

    // The surface pool stores VA FOURCCs, the port V4L2 pixel formats.
    uint32_t v4l2_format = VaFourccToV4l2PixFmt(pool->GetFormat());
    CIX_VAAPI_CHECK_RETURN_CODE(v4l2_format != 0, -1,
        "Unsupported encoder input format %s\n", GetFourccString(pool->GetFormat()));
    if (CheckResolutionChange(pool->GetWidth(), pool->GetHeight(), v4l2_format) != 0)
        return -1;

    if (GetCapPort().StreamOn() != 0 || GetOutPort().StreamOn() != 0)
        return -1;

    // For encoder, bind surface ID to surface buffer statically
    auto surface = pool->GetSurfaceByID(render_target);
    // There should be a surface bound to this surface ID when client fill frame data
    CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, -1,
        "Failed to get surface for render target %u\n", render_target);
    // Assume there is always a free V4L2 buffer mapped to render_target as
    // the buffer should have already been synced and filled by client.
    auto v4l2buf = GetOutPort().FindBufferByRenderTarget(render_target);
    if (v4l2buf == nullptr) {
        // If this surface hasn't been bound to a V4L2 buffer, get a free buffer and bind them now
        v4l2buf = GetOutPort().GetFreeBuffer();
        CIX_VAAPI_CHECK_RETURN_CODE(v4l2buf != nullptr, -1,
            "Failed to get free buffer from output port\n");
        CIX_VAAPI_INFO("Got free output buffer for frame %llu: id = %d\n",
            (unsigned long long)frame_count, v4l2buf->GetId());
        if (!v4l2buf->BindSurface(surface)) {
            v4l2buf->SetInUse(false);
            return -1;
        }
    } else if (v4l2buf->IsInQueue()) {
        CIX_VAAPI_ERROR("Surface %u is still being encoded\n", render_target);
        return -1;
    }
    v4l2buf->SetInUse(true);
    for (uint32_t i = 0; i < surface->GetNumPlanes(); i++) {
        v4l2buf->SetDataOffset(i, 0);
        v4l2buf->SetBytesUsed(i, surface->GetSize(i));
    }
    v4l2buf->SetUserData(extractor->GetCodedBufferID()); // This buffer ID will be assigned to the final coded buffer
    v4l2buf->SetField(V4L2_FIELD_NONE);
    OwnSurface(render_target);
    if (QueueBuffer(v4l2buf) != 0) {
        FreeSurface(render_target);
        v4l2buf->SetInUse(false);
        return -1;
    }

    // Dump the frame data for debugging. The planes share one DMA-BUF, so
    // dump the whole surface buffer instead of mapping each V4L2 plane.
    if (frame_dump && surface->GetBuffer()) {
        void *ptr = surface->GetBuffer()->Map();
        DUMP_Write(frame_dump, ptr, surface->GetBuffer()->GetSize());
    }

    return 0;
}

void V4l2StatefulEncoder::ResetFrameData()
{
    slice_param_count = 0;
    sps_count = 0;
    first_slice = true;
}

int32_t V4l2StatefulEncoder::ProcessBuffers(VABufferID *buffers, int num_buffers)
{
    (void)buffers;
    (void)num_buffers;
    if (extractor == nullptr)
        return -1;

    return 0;
}

int32_t V4l2StatefulEncoder::Submit()
{
    if (extractor == nullptr)
        return -1;

    Port &cap = GetCapPort();

    // Clear the in_use flag of the v4l2 buffer bound to the coded buffer
    // ID previously if there is any, so we can reuse it later.
    auto vbuf = cap.GetBufferByUserData(extractor->GetCodedBufferID());
    if (vbuf && !vbuf->IsInQueue())
        vbuf->SetInUse(false);

    // Enqueue all the free buffers to capture port.
    auto v4l2buf = cap.GetFreeBuffer();
    while (v4l2buf != nullptr) {
        v4l2buf->ClearData();
        v4l2buf->Map(0);
        if (QueueBuffer(v4l2buf) != 0) {
            v4l2buf->SetInUse(false);
            break;
        }
        v4l2buf = cap.GetFreeBuffer();
    }

    // There should be queued buffers now, otherwise report error.
    CIX_VAAPI_CHECK_RETURN_CODE(cap.GetQueuedBuffers() > 0, -1,
        "No queued buffer in capture port is not expected.\n");

    frame_count++;

    return 0;
}

uint32_t V4l2StatefulEncoder::FindV4l2BufferIdBySurfaceId(VASurfaceID surface_id) {
    auto &port = GetOutPort();
    auto vbuf = port.FindBufferByRenderTarget(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr, INVALID_BUFFER_ID,
        "Failed to find V4L2 buffer for surface %u\n", surface_id);
    return vbuf->GetId();
}

Surface *V4l2StatefulEncoder::GetSurfaceFromV4l2BufferId(uint32_t id) {
    auto &port = GetOutPort();
    auto vbuf = port.GetBuffer(id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr, nullptr,
        "Failed to find buffer %u\n", id);
    return vbuf->GetSurface();
}

VABufferID V4l2StatefulEncoder::GetCodedBufferID()
{
    if (extractor)
        return extractor->GetCodedBufferID();
    return VA_INVALID_ID;
}

int32_t V4l2StatefulEncoder::SyncBuffer(VABufferID id) {
    uint32_t retries = 0;
    CIX_VAAPI_INFO("Sync buffer: %u\n", id);

    // The loop used to exit as soon as a buffer carrying this ID was found,
    // even while it was still queued to the VPU, so vaMapBuffer() could read
    // an incomplete bitstream.
    while (true) {
        V4l2Buffer *vbuf = GetCapPort().GetBufferByUserData(id);
        if (vbuf && vbuf->IsReady())
            return 0;

        if (Poll(POLLPRI | POLLOUT | POLLIN, SYNC_ENC_SURFACE_POLL_TIMEOUT_MS) == -1) {
            if (GetFd() < 0 || ++retries >= SYNC_ENC_SURFACE_MAX_RETRIES) {
                CIX_VAAPI_ERROR("Failed to sync buffer %u\n", id);
                return -1;
            }
            CIX_VAAPI_WARNING("Encoder poll timeout, retry %u/%u\n",
                retries, SYNC_ENC_SURFACE_MAX_RETRIES);
        }
    }
}

int32_t V4l2StatefulEncoder::SetRateControlMode(uint32_t mode) {
    switch (mode) {
        case VA_RC_CBR:
            v4l2_rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
            break;
        case VA_RC_VBR:
            v4l2_rc_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR;
            break;
        case VA_RC_CQP:
            v4l2_rc_mode = -1;
            break;
        default:
            CIX_VAAPI_WARNING("Unsupported rate control mode: 0x%x\n", mode);
            return -1;
    }
    return SetV4l2BitrateMode(v4l2_rc_mode);
}

int32_t V4l2StatefulEncoder::SetProfile(VAProfile profile) {
    int32_t v4l2_profile = GetV4l2ProfileFromVA(profile);

    if (v4l2_profile == -1 || extractor == nullptr)
        return -1;

    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_PROFILE, v4l2_profile);
        case EXTRACTOR_HEVC:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, v4l2_profile);
    }

    return 0;
}

int32_t V4l2StatefulEncoder::SetV4l2Tier(uint32_t tier) {
    return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_TIER, tier);
}

int32_t V4l2StatefulEncoder::SetV4l2Level(uint32_t level) {
    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_LEVEL, level);
        case EXTRACTOR_HEVC:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_LEVEL, level);
    }

    return 0;
}

int32_t V4l2StatefulEncoder::SetV4l2BitrateMode(int32_t rc_mode) {
    int32_t rc_enabled = rc_mode != -1 ? 1 : 0;
    CIX_VAAPI_CHECK_RETURN_CODE(
        SetV4l2Control(V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, rc_enabled) == 0, -1,
        "Failed to set frame rate control enable: %d\n", rc_enabled);

    if (rc_enabled)
        return SetV4l2Control(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, rc_mode);

    return 0;
}

int32_t V4l2StatefulEncoder::SetV4l2Bitrate(uint32_t bitrate_bps) {
    return SetV4l2Control(V4L2_CID_MPEG_VIDEO_BITRATE, (int32_t)bitrate_bps);
}

int32_t V4l2StatefulEncoder::SetV4l2QpRange(uint32_t qp_min, uint32_t qp_max) {
    int32_t ret = 0;
    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            if (qp_min)
                ret |= SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_MIN_QP, (int32_t)qp_min);
            if (qp_max)
                ret |= SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_MAX_QP, (int32_t)qp_max);
            break;
        case EXTRACTOR_HEVC:
            if (qp_min)
                ret |= SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP, (int32_t)qp_min);
            if (qp_max)
                ret |= SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP, (int32_t)qp_max);
            break;
        default:
            CIX_VAAPI_WARNING("Unsupported format (%u) for setting QP range: [%u, %u]\n",
                extractor->GetFormat(), qp_min, qp_max);
            return -1;
    }
    return ret;
}

int32_t V4l2StatefulEncoder::SetV4l2Framerate(uint32_t fps_n, uint32_t fps_d) {
    struct v4l2_streamparm streamparm;

    memset(&streamparm, 0x00, sizeof(struct v4l2_streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    streamparm.parm.output.timeperframe.numerator = fps_d;
    streamparm.parm.output.timeperframe.denominator = fps_n;
    return SetV4l2Parm(&streamparm);
}

int32_t V4l2StatefulEncoder::SetV4l2GopSize(uint32_t gop_size) {
    return SetV4l2Control(V4L2_CID_MPEG_VIDEO_GOP_SIZE, (int32_t)gop_size);
}

int32_t V4l2StatefulEncoder::SetV4l2BFrames(uint32_t b_frames) {
    return SetV4l2Control(V4L2_CID_MPEG_VIDEO_B_FRAMES, (int32_t)b_frames);
}

int32_t V4l2StatefulEncoder::SetQPI(uint32_t qp_i) {
    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, (int32_t)qp_i);
        case EXTRACTOR_HEVC:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP, (int32_t)qp_i);
        default:
            CIX_VAAPI_WARNING("Unsupported format (%u) for setting QP I: %u\n", extractor->GetFormat(), qp_i);
            return -1;
    }
}

int32_t V4l2StatefulEncoder::SetQPP(uint32_t qp_p) {
    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, (int32_t)qp_p);
        case EXTRACTOR_HEVC:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP, (int32_t)qp_p);
        default:
            CIX_VAAPI_WARNING("Unsupported format (%u) for setting QP P: %u\n", extractor->GetFormat(), qp_p);
            return -1;
    }
}

int32_t V4l2StatefulEncoder::SetQPB(uint32_t qp_b) {
    switch (extractor->GetFormat()) {
        case EXTRACTOR_H264:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP, (int32_t)qp_b);
        case EXTRACTOR_HEVC:
            return SetV4l2Control(V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP, (int32_t)qp_b);
        default:
            CIX_VAAPI_WARNING("Unsupported format (%u) for setting QP B: %u\n", extractor->GetFormat(), qp_b);
            return -1;
    }
}

int32_t V4l2StatefulEncoder::GetV4l2ProfileFromVA(VAProfile profile)
{
    switch (profile) {
        case VAProfileH264ConstrainedBaseline:
            return V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE;
        case VAProfileH264Main:
            return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
        case VAProfileH264High:
            return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
#if VA_CHECK_VERSION(1, 18, 0)
        case VAProfileH264High10:
            return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
#endif
        case VAProfileHEVCMain:
            return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;
        case VAProfileHEVCMain10:
            return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10;
        case VAProfileVP9Profile0:
            return V4L2_MPEG_VIDEO_VP9_PROFILE_0;
        case VAProfileVP9Profile1:
            return V4L2_MPEG_VIDEO_VP9_PROFILE_1;
        case VAProfileVP9Profile2:
            return V4L2_MPEG_VIDEO_VP9_PROFILE_2;
        case VAProfileVP8Version0_3:
            return V4L2_MPEG_VIDEO_VP8_PROFILE_0;
        default:
            CIX_VAAPI_WARNING("Unsupported VA profile: %d\n", profile);
            return -1;
    }
}

int32_t V4l2StatefulEncoder::GetV4l2Tier(uint8_t tier, uint8_t level)
{
    if (extractor == nullptr || extractor->GetFormat() != EXTRACTOR_HEVC)
        return -1;

    switch (tier) {
        case 0:
            return V4L2_MPEG_VIDEO_HEVC_TIER_MAIN;
        case 1:
            if (level < 40) // Force main tier for level < 4.0 as defined by HEVC spec.
                return V4L2_MPEG_VIDEO_HEVC_TIER_MAIN;
            else
                return V4L2_MPEG_VIDEO_HEVC_TIER_HIGH;
        default:
            CIX_VAAPI_WARNING("Unsupported HEVC tier: %u\n", tier);
            return -1;
    }
}

int32_t V4l2StatefulEncoder::GetV4l2Level(uint8_t level)
{
    if (extractor == nullptr)
        return -1;

    if (extractor->GetFormat() == EXTRACTOR_H264) {
        switch (level) {
            case 10:
                return V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
            case 9:
                return V4L2_MPEG_VIDEO_H264_LEVEL_1B;
            case 11:
                return V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
            case 12:
                return V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
            case 13:
                return V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
            case 20:
                return V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
            case 21:
                return V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
            case 22:
                return V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
            case 30:
                return V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
            case 31:
                return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
            case 32:
                return V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
            case 40:
                return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
            case 41:
                return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
            case 42:
                return V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
            case 50:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
            case 51:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
            case 52:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_1 + 1;  // V4L2_MPEG_VIDEO_H264_LEVEL_5_2
            case 60:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_1 + 2;  // V4L2_MPEG_VIDEO_H264_LEVEL_6_0
            case 61:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_1 + 3;  // V4L2_MPEG_VIDEO_H264_LEVEL_6_1
            case 62:
                return V4L2_MPEG_VIDEO_H264_LEVEL_5_1 + 4;  // V4L2_MPEG_VIDEO_H264_LEVEL_6_2
            default:
                CIX_VAAPI_WARNING("Unsupported H264 level: %u\n", level);
                return -1;
        }
    } else if (extractor->GetFormat() == EXTRACTOR_HEVC) {
        switch (level) {
            case 10:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_1;
            case 20:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_2;
            case 21:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1;
            case 30:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_3;
            case 31:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1;
            case 40:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_4;
            case 41:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1;
            case 50:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_5;
            case 51:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1;
            case 52:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2;
            case 60:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_6;
            case 61:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1;
            case 62:
                return V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2;
            default:
                CIX_VAAPI_WARNING("Unsupported HEVC level: %u\n", level);
                return -1;
        }
    }

    return -1;
}
