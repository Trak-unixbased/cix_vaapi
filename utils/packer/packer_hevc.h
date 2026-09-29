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

#ifndef PACKER_HEVC_H_
#define PACKER_HEVC_H_

#include "device_common.h"
#include "packer.h"

class PackerHEVC : public Packer {
public:
    PackerHEVC();
    ~PackerHEVC() {};

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *GetPictureParameter() { return &pic_param; }
    virtual int32_t SetIQMatrix(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual void PackVpsNalu(void *out, uint32_t out_size, uint32_t &offset);
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset);
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id);
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param);
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size);
    virtual uint32_t CopySPS(void *out, uint32_t out_size, void *seq_header);
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice);
    virtual int32_t CheckIntegrity();

private:
    uint32_t AddStartCode(uint8_t *buf, bool short_start_code = false);
    uint32_t AddNaluHeader(uint8_t *buf, uint8_t type);
    void PackProfileTierLevel();
    void PackVPS();
    void PackSPS();
    void PackPPS(int32_t sps_id, int32_t pps_id);
    void PackSliceHeader();
    void PackScalingList(cix_bsw_ctx *s);
    const uint8_t *GetDefaultList(int32_t size_id, int32_t list_id);
    int32_t GetPredListId(int32_t size_id, int32_t list_id);
    void DetectPPSChange(VAPictureParameterBufferHEVC *new_param);
    void PrintScalingList();
    VAPictureParameterBufferHEVC pic_param;
    VAIQMatrixBufferHEVC iq_matrix;
    std::vector<VASliceParameterBufferHEVC> slice_params;
    bool update_vps;
    bool update_sps;
    bool update_pps;
    bool update_scaling_list;
    bool sps_inserted;
    int32_t last_sps_id;
    int32_t last_pps_id;
    uint32_t custom_scaling_lists;
    cix_bsw_ctx p;

    enum BlockSize {
        BLOCK_SIZE_4x4 = 0,
        BLOCK_SIZE_8x8 = 1,
        BLOCK_SIZE_16x16 = 2,
        BLOCK_SIZE_32x32 = 3,
        BLOCK_SIZE_NUM = 4
    };

    const uint8_t scaling_list_4x4_default[16] = {
        16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
    };
    const uint8_t scaling_list_intra_default[64] = {
        16, 16, 16, 16, 17, 18, 21, 24,
        16, 16, 16, 16, 17, 19, 22, 25,
        16, 16, 17, 18, 20, 22, 25, 29,
        16, 16, 18, 21, 24, 27, 31, 36,
        17, 17, 20, 24, 30, 35, 41, 47,
        18, 19, 22, 27, 35, 44, 54, 65,
        21, 22, 25, 31, 41, 54, 70, 88,
        24, 25, 29, 36, 47, 65, 88, 115
    };
    const uint8_t scaling_list_inter_default[64] = {
        16, 16, 16, 16, 17, 18, 20, 24,
        16, 16, 16, 17, 18, 20, 24, 25,
        16, 16, 17, 18, 20, 24, 25, 28,
        16, 17, 18, 20, 24, 25, 28, 33,
        17, 18, 20, 24, 25, 28, 33, 41,
        18, 20, 24, 25, 28, 33, 41, 54,
        20, 24, 25, 28, 33, 41, 54, 71,
        24, 25, 28, 33, 41, 54, 71, 91
    };
    const uint8_t scaling_list_dc_16x16_default[6] = {
        16, 16, 16, 16, 16, 16
    };
    const uint8_t scaling_list_dc_32x32_default[2] = {
        16, 16
    };
    // const uint8_t scaling_list_flat[64] = {
    //     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    //     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    //     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    //     16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
    // };
    uint8_t *scaling_list[4] = {
        iq_matrix.ScalingList4x4[0],
        iq_matrix.ScalingList8x8[0],
        iq_matrix.ScalingList16x16[0],
        iq_matrix.ScalingList32x32[0]
    };

    const uint8_t zigzag_scan4[16] = {
        0,  4,  1,  8,  5,  2, 12,  9,  6,  3,  13, 10, 7, 14, 11, 15
    };

    const uint8_t zigzag_scan8[64] = {
        0,   8,  1, 16,  9,  2, 24, 17, 10,  3, 32, 25, 18, 11,  4, 40,
        33, 26, 19, 12,  5, 48, 41, 34, 27, 20, 13,  6, 56, 49, 42, 35,
        28, 21, 14,  7, 57, 50, 43, 36, 29, 22, 15, 58, 51, 44, 37, 30,
        23, 59, 52, 45, 38, 31, 60, 53, 46, 39, 61, 54, 47, 62, 55, 63
    };
};

#endif  // PACKER_HEVC_H_