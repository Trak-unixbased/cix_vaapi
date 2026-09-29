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

#ifndef V4L2_STATEFUL_ENC_H_
#define V4L2_STATEFUL_ENC_H_

#include "v4l2_stateful.h"
#include "extractor.h"
#include "dump.h"


class V4l2StatefulEncoder
    : public V4l2Stateful {
public:
    V4l2StatefulEncoder(
        const char *device_name,
        VAContextID context,
        uint32_t pic_width,
        uint32_t pic_height,
        enum v4l2_buf_type out_type,
        enum v4l2_buf_type cap_type,
        uint32_t out_format,
        uint32_t cap_format
    );
    ~V4l2StatefulEncoder();

    virtual void CreateBuffers() override;
    virtual void AddSurfaces(uint32_t num) override;
    virtual void SetSeqParamBuffer(void *data, uint32_t size) override;
    virtual void SetPicParamBuffer(void *data, uint32_t size, VABufferID id) override;
    virtual void SetRateControl(void *data, uint32_t size) override;
    virtual void SetFrameRate(void *data, uint32_t size) override;
    virtual void SetSliceParamBuffer(void *data, uint32_t size, uint32_t num_elements) override;
    virtual void ResetFrameData() override;
    virtual int32_t InitializeBuffers(VABufferID render_target) override;
    virtual int32_t ProcessBuffers(VABufferID *buffers, int num_buffers) override;
    virtual int32_t Submit() override;
    virtual uint32_t FindV4l2BufferIdBySurfaceId(VASurfaceID surface) override;
    virtual Surface *GetSurfaceFromV4l2BufferId(uint32_t id) override;
    virtual bool IsEncoder() { return true; }
    virtual VABufferID GetCodedBufferID() override;
    virtual int32_t SyncBuffer(VABufferID id) override;
    virtual int32_t SetRateControlMode(uint32_t mode) override;
    virtual int32_t SetProfile(VAProfile profile) override;
    virtual Port &GetSurfacePort() override { return GetOutPort(); }

private:
    int32_t GetV4l2ProfileFromVA(VAProfile profile);
    int32_t GetV4l2Tier(uint8_t tier, uint8_t level);
    int32_t GetV4l2Level(uint8_t level);
    int32_t SetV4l2Tier(uint32_t tier);
    int32_t SetV4l2Level(uint32_t level);
    int32_t SetV4l2BitrateMode(int32_t rc_mode);
    int32_t SetV4l2Bitrate(uint32_t bitrate_bps);
    int32_t SetV4l2QpRange(uint32_t qp_min, uint32_t qp_max);
    int32_t SetV4l2Framerate(uint32_t fps_n, uint32_t fps_d);
    int32_t SetV4l2GopSize(uint32_t gop_size);
    int32_t SetV4l2BFrames(uint32_t b_frames);
    uint32_t SetQPI(uint32_t qp_i);
    uint32_t SetQPP(uint32_t qp_p);
    uint32_t SetQPB(uint32_t qp_b);
    int32_t CheckResolutionChange(uint32_t width, uint32_t height, uint32_t format);
    uint32_t slice_param_count;
    uint32_t sps_count;
    bool first_slice;
    DUMP_HANDLE frame_dump;
    uint64_t frame_count;
    int32_t sps_id;
    Extractor *extractor;
    int32_t v4l2_rc_mode;
};

#endif  // V4L2_STATEFUL_ENC_H_