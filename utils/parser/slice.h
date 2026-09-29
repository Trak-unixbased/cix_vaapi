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

#ifndef SLICE_H_
#define SLICE_H_

#include <stdint.h>

#define H264_PROFILE_BASELINE 66
#define H264_PROFILE_MAIN 77
#define H264_PROFILE_HIGH 100
#define H264_PROFILE_HIGH10 110

#define H264_NALU_TYPE_NONIDR_SLICE 1
#define H264_NALU_TYPE_IDR_SLICE 5
#define H264_NALU_TYPE_SPS 7
#define H264_NALU_TYPE_PPS 8
#define H264_NALU_TYPE_AUD 9

#define H264_SLICE_TYPE_P 0
#define H264_SLICE_TYPE_B 1
#define H264_SLICE_TYPE_I 2

struct H264SliceHeader {
    uint32_t first_mb_in_slice;
    uint32_t slice_type;
    uint32_t pic_parameter_set_id;
    uint32_t frame_num;
    uint32_t field_pic_flag;
    uint32_t bottom_field_flag;
    uint32_t idr_pic_id;
    uint32_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt[2];
    uint32_t redundant_pic_cnt;
    uint32_t direct_spatial_mv_pred_flag;
    uint32_t num_ref_idx_active_override_flag;
    uint32_t num_ref_idx_l0_active_minus1;
    uint32_t num_ref_idx_l1_active_minus1;
    // uint32_t cabac_init_idc;
    // int32_t slice_qp_delta;
    // int32_t sp_for_switch_flag;
    // int32_t slice_qs_delta;
    // uint32_t disable_deblocking_filter_idc;
    // int32_t slice_alpha_c0_offset_div2;
    // int32_t slice_beta_offset_div2;
    // uint32_t slice_group_change_cycle;
    uint32_t bit_offset;
};
#endif  // SLICE_H_