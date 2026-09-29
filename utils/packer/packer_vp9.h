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

#ifndef PACKER_VP9_H_
#define PACKER_VP9_H_

#include <vector>
#include <va/va.h>
#include <va/va_dec_vp9.h>
#include "packer.h"

/**
 * Builds the VP9 bitstream for one V4L2 OUTPUT buffer from VA slice data.
 *
 * Normally copies one elementary frame (vaapi_vp9). For non-displayed pictures
 * (show_frame == 0) that still refresh reference slots, CopySlice appends a
 * minimal show_existing_frame and Annex B superframe wrapping so decode can
 * surface the refreshed frame.
 */
class PackerVP9 : public Packer {
public:
    PackerVP9();
    ~PackerVP9() {}

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *GetPictureParameter() { return &pic_param; }
    virtual int32_t SetIQMatrix(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset);
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id);
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param);
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size);
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice);
    virtual int32_t CheckIntegrity();

private:
    VADecPictureParameterBufferVP9 pic_param;
    std::vector<VASliceParameterBufferVP9> slice_params;
    bool pic_valid;
};

#endif  // PACKER_VP9_H_
