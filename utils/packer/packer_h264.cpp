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

#include <cstdio>
#include <cstring>
#include <va/va_backend.h>
#include "packer_h264.h"
#include "slice.h"


#define cix_bsw_put_ue cix_bsw_put_ue_wide

PackerH264::PackerH264() :
    update_sps(false),
    update_pps(false),
    pic_scaling_matrix_present_flag(0),
    pic_scaling_list_present_flag(0)
{
    memset(&pic_param, 0xFF, sizeof(VAPictureParameterBufferH264));
    memset(&iq_matrix, 0, sizeof(VAIQMatrixBufferH264));
    memset(scaling_list_4x4_flat, 16, sizeof(scaling_list_4x4_flat));
    memset(scaling_list_8x8_flat, 16, sizeof(scaling_list_8x8_flat));
}

void PackerH264::SetPictureParameter(void *data, uint32_t size)
{
    if (size != sizeof(VAPictureParameterBufferH264))
        return;

    VAPictureParameterBufferH264 *new_param = (VAPictureParameterBufferH264 *)data;

    update_sps |= (pic_param.seq_fields.value != new_param->seq_fields.value);
    update_sps |= (pic_param.picture_width_in_mbs_minus1 != new_param->picture_width_in_mbs_minus1);
    update_sps |= (pic_param.picture_height_in_mbs_minus1 != new_param->picture_height_in_mbs_minus1);
    update_sps |= (pic_param.bit_depth_luma_minus8 != new_param->bit_depth_luma_minus8);
    update_sps |= (pic_param.bit_depth_chroma_minus8 != new_param->bit_depth_chroma_minus8);
    update_sps |= (pic_param.num_ref_frames != new_param->num_ref_frames);

    // update_pps = pic_param.pic_fields.bits.entropy_coding_mode_flag != new_param->pic_fields.bits.entropy_coding_mode_flag;
    // update_pps |= pic_param.pic_fields.bits.pic_order_present_flag != new_param->pic_fields.bits.pic_order_present_flag;
    // update_pps |= pic_param.pic_fields.bits.weighted_pred_flag != new_param->pic_fields.bits.weighted_pred_flag;
    // update_pps |= pic_param.pic_fields.bits.weighted_bipred_idc != new_param->pic_fields.bits.weighted_bipred_idc;
    // update_pps |= pic_param.pic_init_qp_minus26 != new_param->pic_init_qp_minus26;
    // update_pps |= pic_param.pic_init_qs_minus26 != new_param->pic_init_qs_minus26;
    // update_pps |= pic_param.chroma_qp_index_offset != new_param->chroma_qp_index_offset;
    // update_pps |= pic_param.pic_fields.bits.deblocking_filter_control_present_flag != new_param->pic_fields.bits.deblocking_filter_control_present_flag;
    // update_pps |= pic_param.pic_fields.bits.constrained_intra_pred_flag != new_param->pic_fields.bits.constrained_intra_pred_flag;
    // update_pps |= pic_param.pic_fields.bits.redundant_pic_cnt_present_flag != new_param->pic_fields.bits.redundant_pic_cnt_present_flag;
    // update_pps |= pic_param.pic_fields.bits.transform_8x8_mode_flag != new_param->pic_fields.bits.transform_8x8_mode_flag;
    // update_pps |= pic_param.second_chroma_qp_index_offset != new_param->second_chroma_qp_index_offset;
    update_pps = true; // always update PPS for num_ref_idx_l0_active_minus1 and num_ref_idx_l1_active_minus1

    pic_param = *(VAPictureParameterBufferH264 *)data;

    // TODO: handle this unsupported error
    CIX_VAAPI_CHECK_RETURN(pic_param.seq_fields.bits.chroma_format_idc == 1,
        "chroma_format_idc %d not supported\n", pic_param.seq_fields.bits.chroma_format_idc);
}

int32_t PackerH264::SetIQMatrix(void *data, uint32_t size)
{
    CIX_VAAPI_CHECK_RETURN_CODE(size == sizeof(VAIQMatrixBufferH264), -1,
        "Invalid IQ matrix size: %d\n", size);


    iq_matrix = *(VAIQMatrixBufferH264 *)data;
    int nonflat_flags = 0;

    // Check if the scaling list is flat
    pic_scaling_matrix_present_flag = 0;
    pic_scaling_list_present_flag = 0;
    for (int i = 0; i < 6; i++) {
        if (std::memcmp(scaling_list_4x4_flat, iq_matrix.ScalingList4x4[i], 16))
            nonflat_flags |= 1 << i;
    }

    for (int i = 0; i < 2; i++) {
        if (std::memcmp(&scaling_list_8x8_flat, iq_matrix.ScalingList8x8[i], 64))
            nonflat_flags |= 1 << (i+6);
    }

    if (nonflat_flags == 0) {
        CIX_VAAPI_INFO("Got flat scaling list\n");
        return 0;
    }

    // Check if the scaling list is default
    pic_scaling_matrix_present_flag = 1;
    for (int i = 0; i < 6; i++) {
        const uint8_t *scaling_list_default = (i == 0 || i == 3) ? 
            scaling_list_4x4_default[(i+1)>>2] : iq_matrix.ScalingList4x4[i-1];
        if (std::memcmp(scaling_list_default, iq_matrix.ScalingList4x4[i], 16))
            pic_scaling_list_present_flag |= 1 << i;
    }

    if (std::memcmp(&scaling_list_8x8_default[0], iq_matrix.ScalingList8x8[0], 64))
        pic_scaling_list_present_flag |= 1 << 6;

    if (std::memcmp(&scaling_list_8x8_default[1], iq_matrix.ScalingList8x8[1], 64))
        pic_scaling_list_present_flag |= 1 << 7;

    if (pic_scaling_list_present_flag == 0)
        CIX_VAAPI_INFO("Got default scaling list\n");
    else
        CIX_VAAPI_INFO("Got customized scaling list: 0x%x\n", pic_scaling_list_present_flag);

    return 0;
}

void PackerH264::SetSliceParameter(void *data, uint32_t size)
{
    uint32_t num = size / sizeof(VASliceParameterBufferH264);
    if (num == 0 || size % sizeof(VASliceParameterBufferH264) != 0)
        return;

        VASliceParameterBufferH264 *slice_param = (VASliceParameterBufferH264 *)data;
    for (int i = 0; i < num; i++) {
        slice_params.emplace_back(slice_param[i]);
        CIX_VAAPI_INFO("Got slice parameter: slice_data_size = %d, slice_data_offset = %d, slice_data_bit_offset = %d\n",
            slice_param[i].slice_data_size, slice_param[i].slice_data_offset, slice_param[i].slice_data_bit_offset);
    }
}

void PackerH264::PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset)
{
    if (!update_sps)
        return;

    void *cache;
    uint32_t cache_size;
    offset += AddStartCode((uint8_t *)out + offset);
    offset += AddNaluHeader((uint8_t *)out + offset, H264_NALU_TYPE_SPS);
    GetCache(&cache, &cache_size);
    cix_bsw_init(&p, (uint8_t *)cache, cache_size);
    PackSPS();

    uint32_t bytes = cix_bsw_bit_position(&p) >> 3;
    offset += AddEmulationPrevention((uint8_t *)out + offset, out_size - offset,
                                     (uint8_t *)cache, bytes);
    update_sps = false;
}

void PackerH264::PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id)
{
    if (!update_pps)
        return;

    void *cache;
    uint32_t cache_size;
    offset += AddStartCode((uint8_t *)out + offset);
    offset += AddNaluHeader((uint8_t *)out + offset, H264_NALU_TYPE_PPS);
    GetCache(&cache, &cache_size);
    cix_bsw_init(&p, (uint8_t *)cache, cache_size);
    PackPPS(0, pps_id);

    uint32_t bytes = cix_bsw_bit_position(&p) >> 3;
    offset += AddEmulationPrevention((uint8_t *)out + offset, out_size - offset,
                                     (uint8_t *)cache, bytes);
    update_pps = false;
}

void PackerH264::PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_header)
{
    H264SliceHeader *param = (H264SliceHeader *)slice_header;
    void *cache;
    uint32_t cache_size;
    offset += AddStartCode((uint8_t *)out + offset);
    ((uint8_t *)out)[offset++] = ((uint8_t *)in)[0]; // NALU header

    // pack slice header and copy slice data to cache first, and then add emulation prevention bytes and write to out buffer
    GetCache(&cache, &cache_size);
    cix_bsw_init(&p, (uint8_t *)cache, cache_size);
    uint8_t nal_unit_type = ((uint8_t *)in)[0] & 0x1f;
    uint32_t out_bit_offset = PackSliceHeaderPartial((H264SliceHeader *)slice_header, nal_unit_type);
    uint32_t in_bit_offset = ((H264SliceHeader *)slice_header)->bit_offset;
    uint32_t bytes = CopySliceData((uint8_t *)cache, cache_size, out_bit_offset,
                                (uint8_t *)in, in_size, in_bit_offset);
    offset += AddEmulationPrevention((uint8_t *)out + offset, out_size - offset,
                                     (uint8_t *)cache, bytes);
}

void PackerH264::PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size)
{
}

uint32_t PackerH264::CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice)
{
    uint32_t offset = 0;
    offset += AddStartCode((uint8_t *)out, !first_slice);
    memcpy((uint8_t *)out + offset, in, in_size);
    offset += in_size;
    slice_params.clear();
    return offset;
}

uint32_t PackerH264::AddStartCode(uint8_t *buf, bool short_start_code)
{
    buf[0] = 0;
    buf[1] = 0;
    if (short_start_code) {
        buf[2] = 1;
        return 3;
    } else {
        buf[2] = 0;
        buf[3] = 1;
        return 4;
    }
}

uint32_t PackerH264::AddNaluHeader(uint8_t *buf, uint8_t type)
{
    cix_bsw_ctx *s = &p;
    uint8_t ref_idc = type == H264_NALU_TYPE_SPS || type == H264_NALU_TYPE_PPS ?
        3 : pic_param.pic_fields.bits.reference_pic_flag;

    buf[0] = ref_idc << 5 | type;

    return 1;
}

void PackerH264::PackSPS()
{
    cix_bsw_ctx *s = &p;
    bool is_8bit = pic_param.bit_depth_luma_minus8 == 0 && pic_param.bit_depth_chroma_minus8 == 0;

    cix_bsw_write(s, 8, is_8bit ? H264_PROFILE_HIGH : H264_PROFILE_HIGH10); // profile_idc
    cix_bsw_write(s, 8, 0); // set all the constraint_setx_flag to 0
    cix_bsw_write(s, 8, 61); // level_idc
    cix_bsw_put_ue(s, 0); // seq_parameter_set_id
    // assume high or high 10 profile, so always have following fields in sps
    cix_bsw_put_ue(s, 1); // chroma_format_idc = 1, 4:2:0
    cix_bsw_put_ue(s, pic_param.bit_depth_luma_minus8);
    cix_bsw_put_ue(s, pic_param.bit_depth_chroma_minus8);
    cix_bsw_write_bit(s, 0); // qpprime_y_zero_transform_bypass_flag should be 0 for BP, MP, HP and HP10
    // Scaling list is put in PPS, so skip it in SPS
    cix_bsw_write_bit(s, 0); // seq_scaling_matrix_present_flag
    cix_bsw_put_ue(s, pic_param.seq_fields.bits.log2_max_frame_num_minus4);
    cix_bsw_put_ue(s, pic_param.seq_fields.bits.pic_order_cnt_type);
    if (pic_param.seq_fields.bits.pic_order_cnt_type == 0)
        cix_bsw_put_ue(s, pic_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
    else if (pic_param.seq_fields.bits.pic_order_cnt_type == 1) {
        cix_bsw_write_bit(s, pic_param.seq_fields.bits.delta_pic_order_always_zero_flag);
        // TODO: The following fields are not available in VAPictureParameterBufferH264, need to be inferred from other fields
        // cix_bsw_write_se(s, pic_param.seq_fields.bits.offset_for_non_ref_pic);
        // cix_bsw_write_se(s, pic_param.seq_fields.bits.offset_for_top_to_bottom_field);
        // cix_bsw_put_ue(s, pic_param.num_ref_frames_in_pic_order_cnt_cycle);
        // for( int i = 0; i < pic_param.num_ref_frames_in_pic_order_cnt_cycle; i++ )
        //     cix_bsw_write_se(s, pic_param.offset_for_ref_frame[i]);
    }
    cix_bsw_put_ue(s, pic_param.num_ref_frames);
    cix_bsw_write_bit(s, pic_param.seq_fields.bits.gaps_in_frame_num_value_allowed_flag);
    cix_bsw_put_ue(s, pic_param.picture_width_in_mbs_minus1);
    cix_bsw_put_ue(s, pic_param.picture_height_in_mbs_minus1);
    cix_bsw_write_bit(s, pic_param.seq_fields.bits.frame_mbs_only_flag);
    if (!pic_param.seq_fields.bits.frame_mbs_only_flag)
        cix_bsw_write_bit(s, pic_param.seq_fields.bits.mb_adaptive_frame_field_flag);
    cix_bsw_write_bit(s, pic_param.seq_fields.bits.direct_8x8_inference_flag);
    cix_bsw_write_bit(s, 0); // frame_cropping_flag
    // Frame cropping is handled by client, so skip it here
    cix_bsw_write_bit(s, 0); // vui_parameters_present_flag
    // VUI is not critical for decoding, so skip it here
    cix_bsw_write_rbsp_trailing(s);
    cix_bsw_flush_bits(s);
}

void PackerH264::PackScalingList(cix_bsw_ctx *s, int32_t index)
{
    uint8_t *list = iq_matrix.ScalingList4x4[index];
    const uint8_t *zigzag = zigzag_scan4;
    int len = 16;

    if (index >= 6) {
        list = iq_matrix.ScalingList8x8[index-6];
        zigzag = zigzag_scan8;
        len = 64;
    }

    if (memcmp(list, scaling_list_default[index], len)) {
        int run;

        for (run = len; run > 1; run--)
            if (list[zigzag[run-1]] != list[zigzag[run-2]])
                break;

        if (run < len && len - run < cix_bsw_se_bit_length((int8_t)-list[zigzag[run]]))
            run = len;

        for (int j = 0; j < run; j++)
            cix_bsw_write_se(s, (int8_t)(list[zigzag[j]] - (j>0 ? list[zigzag[j-1]] : 8)));

        if (run < len)
            cix_bsw_write_se(s, (int8_t)-list[zigzag[run]]);
    } else {
        cix_bsw_write_se(s, -8);
    }
}

void PackerH264::PackPPS(int32_t sps_id, int32_t pps_id)
{
    cix_bsw_ctx *s = &p;
    int qp_bd_offset = 6 * pic_param.bit_depth_luma_minus8;


    cix_bsw_put_ue(s, pps_id); // pic_parameter_set_id, // TODO: should be from slice header
    cix_bsw_put_ue(s, 0); // seq_parameter_set_id, always use 0 as it's up to date
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.entropy_coding_mode_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.pic_order_present_flag); // bottom_field_pic_order_in_frame_present_flag
    cix_bsw_put_ue(s, 0); // num_slice_groups_minus1
    // Skip FMO related fields as it's not supported
    cix_bsw_put_ue(s, slice_params[0].num_ref_idx_l0_active_minus1);
    cix_bsw_put_ue(s, slice_params[0].num_ref_idx_l1_active_minus1);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.weighted_pred_flag);
    cix_bsw_write(s, 2, pic_param.pic_fields.bits.weighted_bipred_idc);
    cix_bsw_write_se(s, pic_param.pic_init_qp_minus26 - qp_bd_offset);
    cix_bsw_write_se(s, pic_param.pic_init_qs_minus26 - qp_bd_offset);
    cix_bsw_write_se(s, pic_param.chroma_qp_index_offset);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.deblocking_filter_control_present_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.constrained_intra_pred_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.redundant_pic_cnt_present_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.transform_8x8_mode_flag);
    cix_bsw_write_bit(s, pic_scaling_matrix_present_flag);
    if (pic_scaling_matrix_present_flag) {
        int num_list = pic_param.pic_fields.bits.transform_8x8_mode_flag ? 8 : 6;
        for (int i = 0; i < num_list; i++) {
            uint32_t scaling_list_present = (pic_scaling_list_present_flag >> i) & 1;
            cix_bsw_write_bit(s, scaling_list_present);
            if (scaling_list_present)
                PackScalingList(s, i);
        }
    }
    cix_bsw_write_se(s, pic_param.second_chroma_qp_index_offset);
    cix_bsw_write_rbsp_trailing(s);
    cix_bsw_flush_bits(s);
}

void PackerH264::PackSliceHeader()
{
    cix_bsw_ctx *s = &p;
    auto slice_param = slice_params[0];

    cix_bsw_put_ue(s, slice_param.first_mb_in_slice);
    cix_bsw_put_ue(s, slice_param.slice_type);
    cix_bsw_put_ue(s, 0); // pic_parameter_set_id
    // separate colour plane is not supported
    cix_bsw_put_ue(s, pic_param.frame_num);
    if (!pic_param.seq_fields.bits.frame_mbs_only_flag) {
        cix_bsw_write_bit(s, pic_param.pic_fields.bits.field_pic_flag);
        if (pic_param.pic_fields.bits.field_pic_flag) {
            uint32_t bottom_field_flag = !!(pic_param.CurrPic.flags & VA_PICTURE_H264_BOTTOM_FIELD);
            cix_bsw_write_bit(s, bottom_field_flag);
        }
    }
    // TODO: add idr_pic_id
    if (pic_param.seq_fields.bits.pic_order_cnt_type == 0) {
        // TODO: add pic_order_cnt_lsb
        // cix_bsw_put_ue(s, slice_param.pic_order_cnt_lsb);
        // if( pic_param.pic_fields.bits.pic_order_present_flag && !pic_param.pic_fields.bits.field_pic_flag )
        //     cix_bsw_write_se(s, slice_param.delta_pic_order_cnt_bottom);
    }

    if (pic_param.seq_fields.bits.pic_order_cnt_type == 1 &&
        !pic_param.seq_fields.bits.delta_pic_order_always_zero_flag) {
        // TODO: delta_pic_order_cnt[ 0 ] and delta_pic_order_cnt[ 1 ]
        // cix_bsw_write_se(s, slice_param.delta_pic_order_cnt[0]);
        // if( pic_param.pic_fields.bits.pic_order_present_flag && !pic_param.pic_fields.bits.field_pic_flag )
        //     cix_bsw_write_se(s, slice_param.delta_pic_order_cnt[1]);
    }

    // TODO: to add redundant_pic_cnt
    // if (pic_param.pic_fields.bits.redundant_pic_cnt_present_flag)
    //     cix_bsw_put_ue(s, slice_param.redundant_pic_cnt);

    if (slice_param.slice_type%5 == H264_SLICE_TYPE_B)
        cix_bsw_write_bit(s, slice_param.direct_spatial_mv_pred_flag);

    if (slice_param.slice_type%5 == H264_SLICE_TYPE_P || slice_param.slice_type%5 == H264_SLICE_TYPE_B) {
        cix_bsw_write_bit(s, 1); // num_ref_idx_active_override_flag, always override
        cix_bsw_put_ue(s, slice_param.num_ref_idx_l0_active_minus1);
        if (slice_param.slice_type%5 == H264_SLICE_TYPE_B)
            cix_bsw_put_ue(s, slice_param.num_ref_idx_l1_active_minus1);
    }
    // TODO: ref_pic_list_modification()
    if ((pic_param.pic_fields.bits.weighted_pred_flag && slice_param.slice_type%5 == H264_SLICE_TYPE_P) ||
        (pic_param.pic_fields.bits.weighted_bipred_idc && slice_param.slice_type%5 == H264_SLICE_TYPE_B)) {
        // TODO: pred_weight_table()
        cix_bsw_put_ue(s, slice_param.luma_log2_weight_denom);
        if (pic_param.seq_fields.bits.chroma_format_idc != 0)
            cix_bsw_put_ue(s, slice_param.chroma_log2_weight_denom);
        for (int i = 0; i <= slice_param.num_ref_idx_l0_active_minus1; i++) {
            cix_bsw_write_bit(s, slice_param.luma_weight_l0_flag);
            if (slice_param.luma_weight_l0_flag) {
                cix_bsw_write_se(s, slice_param.luma_weight_l0[i]);
                cix_bsw_write_se(s, slice_param.luma_offset_l0[i]);
            }
            if (pic_param.seq_fields.bits.chroma_format_idc != 0) {
                cix_bsw_write_bit(s, slice_param.chroma_weight_l0_flag);
                if (slice_param.chroma_weight_l0_flag) {
                    for (int j = 0; j < 2; j++) {
                        cix_bsw_write_se(s, slice_param.chroma_weight_l0[i][j]);
                        cix_bsw_write_se(s, slice_param.chroma_offset_l0[i][j]);
                    }
                }
            }
        }
        if (slice_param.slice_type%5 == H264_SLICE_TYPE_B) {
            for (int i = 0; i <= slice_param.num_ref_idx_l1_active_minus1; i++) {
                cix_bsw_write_bit(s, slice_param.luma_weight_l1_flag);
                if (slice_param.luma_weight_l1_flag) {
                    cix_bsw_write_se(s, slice_param.luma_weight_l1[i]);
                    cix_bsw_write_se(s, slice_param.luma_offset_l1[i]);
                }
                if (pic_param.seq_fields.bits.chroma_format_idc != 0) {
                    cix_bsw_write_bit(s, slice_param.chroma_weight_l1_flag);
                    if (slice_param.chroma_weight_l1_flag) {
                        for (int j = 0; j < 2; j++) {
                            cix_bsw_write_se(s, slice_param.chroma_weight_l1[i][j]);
                            cix_bsw_write_se(s, slice_param.chroma_offset_l1[i][j]);
                        }
                    }
                }
            }
        }
    }

    if (pic_param.pic_fields.bits.reference_pic_flag) {
        // TODO: dec_ref_pic_marking()
    }

    if (pic_param.pic_fields.bits.entropy_coding_mode_flag && slice_param.slice_type%5 != H264_SLICE_TYPE_I) {
        cix_bsw_put_ue(s, slice_param.cabac_init_idc);
    }
    // skip SP/SI related fields as not supported
    cix_bsw_write_se(s, slice_param.slice_qp_delta);
    if (pic_param.pic_fields.bits.deblocking_filter_control_present_flag) {
        cix_bsw_put_ue(s, slice_param.disable_deblocking_filter_idc);
        if (slice_param.disable_deblocking_filter_idc != 1) {
            cix_bsw_write_se(s, slice_param.slice_alpha_c0_offset_div2);
            cix_bsw_write_se(s, slice_param.slice_beta_offset_div2);
        }
    }
    // skip FMO related fields as not supported
}

uint32_t PackerH264::PackSliceHeaderPartial(H264SliceHeader *header, uint8_t nal_unit_type)
{
    cix_bsw_ctx *s = &p;
    uint32_t bits = 0;
    auto slice_param = slice_params[0];

    cix_bsw_put_ue(s, header->first_mb_in_slice);
    cix_bsw_put_ue(s, header->slice_type);
    cix_bsw_put_ue(s, header->pic_parameter_set_id);
    cix_bsw_write(s, pic_param.seq_fields.bits.log2_max_frame_num_minus4 + 4,
        header->frame_num);
    CIX_VAAPI_DEBUG("frame_num = %d\n", header->frame_num);
    if (!pic_param.seq_fields.bits.frame_mbs_only_flag) {
        cix_bsw_write_bit(s, header->field_pic_flag);
        if (header->field_pic_flag) {
            cix_bsw_write_bit(s, header->bottom_field_flag);
        }
    }
    if (nal_unit_type == H264_NALU_TYPE_IDR_SLICE)
        cix_bsw_put_ue(s, header->idr_pic_id);
    if (pic_param.seq_fields.bits.pic_order_cnt_type == 0) {
        cix_bsw_write(s,
            pic_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 + 4,
            header->pic_order_cnt_lsb);
        if (pic_param.pic_fields.bits.pic_order_present_flag && !header->field_pic_flag)
            cix_bsw_write_se(s, header->delta_pic_order_cnt_bottom);
    }
    if (pic_param.seq_fields.bits.pic_order_cnt_type == 1 &&
        !pic_param.seq_fields.bits.delta_pic_order_always_zero_flag) {
        cix_bsw_write_se(s, header->delta_pic_order_cnt[0]);
        if (header->delta_pic_order_cnt[1])
            cix_bsw_write_se(s, header->delta_pic_order_cnt[1]);
    }
    if (pic_param.pic_fields.bits.redundant_pic_cnt_present_flag)
        cix_bsw_put_ue(s, header->redundant_pic_cnt);
    if (header->slice_type % 5 == H264_SLICE_TYPE_B)
        cix_bsw_write_bit(s, header->direct_spatial_mv_pred_flag);
    if (header->slice_type % 5 == H264_SLICE_TYPE_P ||
        header->slice_type % 5 == H264_SLICE_TYPE_B) {
        cix_bsw_write_bit(s, 1); // num_ref_idx_active_override_flag, always override
        cix_bsw_put_ue(s, slice_param.num_ref_idx_l0_active_minus1);
        if (header->slice_type % 5 == H264_SLICE_TYPE_B)
            cix_bsw_put_ue(s, slice_param.num_ref_idx_l1_active_minus1);
    }
    bits = cix_bsw_bit_position(s);
    cix_bsw_flush_bits(s);
    return bits;
}

uint32_t PackerH264::CopySliceData(
    uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
    uint8_t *in, uint32_t in_size, uint32_t in_bit_offset)
{
    CIX_VAAPI_DEBUG("Copy slice data: out_size = %d, out_bit_offset = %d, in_size = %d, in_bit_offset = %d\n",
        out_size, out_bit_offset, in_size, in_bit_offset);
    //! TODO: to optimize for performance
    auto slice_param = slice_params[0];
    uint32_t dst_bit_offset = out_bit_offset & 7;
    uint32_t src_bit_offset = in_bit_offset & 7;
    uint32_t dst_byte_offset = out_bit_offset >> 3;
    uint32_t src_byte_offset = in_bit_offset >> 3;
    uint8_t *dst = out + dst_byte_offset;
    uint8_t *src = in + src_byte_offset;
    uint32_t copy_bytes = in_size - src_byte_offset;

    if (dst_bit_offset == src_bit_offset) {
        uint8_t dst_mask = 0xff << (8 - dst_bit_offset);
        uint8_t src_mask = 0xff >> src_bit_offset;
        dst[0] = (dst[0] & dst_mask) | (src[0] & src_mask);
        copy_bytes--;

        memcpy(dst + 1, src + 1, copy_bytes);
        dst += copy_bytes + 1;
    } else {
        // Otherwise, need to handle the cabac_alignment_one_bit in the beginning of slice data
        // First, copy remaining bits of slice header
        uint8_t dst_mask = 0xff << (8 - dst_bit_offset);
        uint8_t src_mask = 0xff >> src_bit_offset;
        uint16_t src_word = (((uint16_t)src[0] & src_mask) << 8)+ src[1];
        uint32_t remaining_header_bits = slice_param.slice_data_bit_offset - in_bit_offset;
        if (remaining_header_bits <= 8 - dst_bit_offset) {
            src_word <<= src_bit_offset;
            src_word >>= (8 + dst_bit_offset);
            src_word &= (0xff >> (8 - remaining_header_bits - dst_bit_offset)); // add cabac_alignment_one_bit(s)
            dst[0] = (dst[0] & dst_mask) | (uint8_t)src_word;
            dst++;
        } else {
            // Handle the first byte, to make dst byte aligned
            if (dst_bit_offset > src_bit_offset)
                src_word >>= (dst_bit_offset - src_bit_offset);
            else
                src_word <<= (src_bit_offset - dst_bit_offset);
            src_word >>= 8;
            dst[0] = (dst[0] & dst_mask) | (uint8_t)src_word;
            dst++;
            remaining_header_bits -= (8 - dst_bit_offset);

            copy_bytes = (remaining_header_bits + 7) >> 3;

            // Then the remaining bytes
            int32_t shift_bits = dst_bit_offset - src_bit_offset;
            if (dst_bit_offset < src_bit_offset) {
                copy_bytes--;
                shift_bits += 8;
                src++;
            }

            for (uint32_t i = 0; i < copy_bytes; i++) {
                src_word = (src[i] << 8) + src[i + 1];
                src_word >>= shift_bits;
                src_word &= 0xff;
                dst[i] = (uint8_t)src_word;
            }

            // Fill cabac_alignment_one_bit until byte aligned
            shift_bits = (remaining_header_bits & 7);
            if (shift_bits == 0)
                shift_bits = 8;
            dst[copy_bytes-1] |= 0xff >> shift_bits;

            dst += copy_bytes;
        }

        // Copy slice data
        uint32_t slice_header_size = ((slice_param.slice_data_bit_offset + 7) >> 3);
        src = in + slice_header_size;
        copy_bytes = in_size - slice_header_size;
        memcpy(dst, src, copy_bytes);
        dst += copy_bytes;
    }

    return (uint32_t)(dst - out);
}

