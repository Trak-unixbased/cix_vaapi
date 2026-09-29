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

#ifndef PACKER_H264_H_
#define PACKER_H264_H_

#include "device_common.h"
#include "packer.h"
#include "slice.h"

class PackerH264 : public Packer {
public:
    PackerH264();
    ~PackerH264() {};

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *GetPictureParameter() { return &pic_param; }
    virtual int32_t SetIQMatrix(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset);
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id);
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param);
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size);
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice);

private:
    uint32_t AddStartCode(uint8_t *buf, bool short_start_code = false);
    uint32_t AddNaluHeader(uint8_t *buf, uint8_t type);
    void PackSPS();
    void PackPPS(int32_t sps_id, int32_t pps_id);
    void PackSliceHeader();
    uint32_t PackSliceHeaderPartial(H264SliceHeader *header, uint8_t nal_unit_type);
    uint32_t CopySliceData(uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
                           uint8_t *in, uint32_t in_size, uint32_t in_bit_offset);
    void PackScalingList(cix_bsw_ctx *s, int32_t index);
    VAPictureParameterBufferH264 pic_param;
    VAIQMatrixBufferH264 iq_matrix;
    std::vector<VASliceParameterBufferH264> slice_params;
    bool update_sps;
    bool update_pps;
    uint32_t pic_scaling_matrix_present_flag;
    uint32_t pic_scaling_list_present_flag;
    cix_bsw_ctx p;

    uint8_t scaling_list_4x4_flat[16];
    uint8_t scaling_list_8x8_flat[64];

    const uint8_t scaling_list_4x4_default[2][16] = {
        { 6, 13, 20, 28, 13, 20, 28, 32, 20, 28, 32, 37, 28, 32, 37, 42 }, // Default_4x4_Intra
        { 10, 14, 20, 24, 14, 20, 24, 27, 20, 24, 27, 30, 24, 27, 30, 34 } // Default_4x4_Inter
    };
    const uint8_t scaling_list_8x8_default[2][64] = {
        { 6, 10, 13, 16, 18, 23, 25, 27, 10, 11, 16, 18, 23, 25, 27, 29,
          13, 16, 18, 23, 25, 27, 29, 31, 16, 18, 23, 25, 27, 29, 31, 33,
          18, 23, 25, 27, 29, 31, 33, 36, 23, 25, 27, 29, 31, 33, 36, 38,
          25, 27, 29, 31, 33, 36, 38, 40, 27, 29, 31, 33, 36, 38, 40, 42 }, // Default_8x8_Intra
        { 9, 13, 15, 17, 19, 21, 22, 24, 13, 13, 17, 19, 21, 22, 24, 25,
          15, 17, 19, 21, 22, 24, 25, 27, 17, 19, 21, 22, 24, 25, 27, 28,
          19, 21, 22, 24, 25, 27, 28, 30, 21, 22, 24, 25, 27, 28, 30, 32,
          22, 24, 25, 27, 28, 30, 32, 33, 24, 25, 27, 28, 30, 32, 33, 35 }  // Default_8x8_Inter
    };

    const uint8_t *scaling_list_default[8] = {
        scaling_list_4x4_default[0],
        scaling_list_4x4_default[0],
        scaling_list_4x4_default[0],
        scaling_list_4x4_default[1],
        scaling_list_4x4_default[1],
        scaling_list_4x4_default[1],
        scaling_list_8x8_default[0],
        scaling_list_8x8_default[1]
    };

    const uint8_t zigzag_scan4[16] = {
        0,  1,  4,  8,  5,  2, 3,  6,  9,  12,  13, 10, 7, 11, 14, 15
    };

    const uint8_t zigzag_scan8[64] = {
        0,  1,  8,  16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
    };
};

#endif  // PACKER_H264_H_