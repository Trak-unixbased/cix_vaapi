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

#include <cstdio>
#include <cstring>
#include <va/va_backend.h>
#include "packer_hevc.h"
#include "slice.h"
#include "sequence.h"

#define HEVC_NALU_TYPE_NONIDR_SLICE 1
#define HEVC_NALU_TYPE_IDR_SLICE 19
#define HEVC_NALU_TYPE_VPS 32
#define HEVC_NALU_TYPE_SPS 33
#define HEVC_NALU_TYPE_PPS 34
#define HEVC_NALU_TYPE_AUD 35

#define HEVC_PROFILE_MAIN 1
#define HEVC_PROFILE_MAIN10 2

#define cix_bsw_put_ue cix_bsw_put_ue_wide

PackerHEVC::PackerHEVC() :
    update_vps(true),
    update_sps(false),
    update_pps(false),
    update_scaling_list(false),
    sps_inserted(false),
    custom_scaling_lists(0)
{
    memset(&pic_param, 0xFF, sizeof(VAPictureParameterBufferHEVC));
    memset(&iq_matrix, 0, sizeof(VAIQMatrixBufferHEVC));
}

void PackerHEVC::DetectPPSChange(VAPictureParameterBufferHEVC *new_param)
{
    update_pps |= pic_param.slice_parsing_fields.bits.dependent_slice_segments_enabled_flag != new_param->slice_parsing_fields.bits.dependent_slice_segments_enabled_flag;
    update_pps |= pic_param.slice_parsing_fields.bits.output_flag_present_flag != new_param->slice_parsing_fields.bits.output_flag_present_flag;
    update_pps |= pic_param.num_extra_slice_header_bits != new_param->num_extra_slice_header_bits;
    update_pps |= pic_param.pic_fields.bits.sign_data_hiding_enabled_flag != new_param->pic_fields.bits.sign_data_hiding_enabled_flag;
    update_pps |= pic_param.slice_parsing_fields.bits.cabac_init_present_flag != new_param->slice_parsing_fields.bits.cabac_init_present_flag;
    update_pps |= pic_param.num_ref_idx_l0_default_active_minus1 != new_param->num_ref_idx_l0_default_active_minus1;
    update_pps |= pic_param.num_ref_idx_l1_default_active_minus1 != new_param->num_ref_idx_l1_default_active_minus1;
    update_pps |= pic_param.init_qp_minus26 != new_param->init_qp_minus26;
    update_pps |= pic_param.pic_fields.bits.constrained_intra_pred_flag != new_param->pic_fields.bits.constrained_intra_pred_flag;
    update_pps |= pic_param.pic_fields.bits.transform_skip_enabled_flag != new_param->pic_fields.bits.transform_skip_enabled_flag;
    update_pps |= pic_param.pic_fields.bits.cu_qp_delta_enabled_flag != new_param->pic_fields.bits.cu_qp_delta_enabled_flag;
    if (pic_param.pic_fields.bits.cu_qp_delta_enabled_flag)
        update_pps |= pic_param.diff_cu_qp_delta_depth != new_param->diff_cu_qp_delta_depth;
    update_pps |= pic_param.pps_cb_qp_offset != new_param->pps_cb_qp_offset;
    update_pps |= pic_param.pps_cr_qp_offset != new_param->pps_cr_qp_offset;
    update_pps |= pic_param.slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag != new_param->slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag;
    update_pps |= pic_param.pic_fields.bits.weighted_pred_flag != new_param->pic_fields.bits.weighted_pred_flag;
    update_pps |= pic_param.pic_fields.bits.weighted_bipred_flag != new_param->pic_fields.bits.weighted_bipred_flag;
    update_pps |= pic_param.pic_fields.bits.transquant_bypass_enabled_flag != new_param->pic_fields.bits.transquant_bypass_enabled_flag;
    update_pps |= pic_param.pic_fields.bits.tiles_enabled_flag != new_param->pic_fields.bits.tiles_enabled_flag;
    update_pps |= pic_param.pic_fields.bits.entropy_coding_sync_enabled_flag != new_param->pic_fields.bits.entropy_coding_sync_enabled_flag;
    if (pic_param.pic_fields.bits.tiles_enabled_flag) {
        update_pps |= pic_param.num_tile_columns_minus1 != new_param->num_tile_columns_minus1;
        update_pps |= pic_param.num_tile_rows_minus1 != new_param->num_tile_rows_minus1;
        for (int i = 0; i < pic_param.num_tile_columns_minus1; i++)
            update_pps |= pic_param.column_width_minus1[i] != new_param->column_width_minus1[i];
        for (int i = 0; i < pic_param.num_tile_rows_minus1; i++)
            update_pps |= pic_param.row_height_minus1[i] != new_param->row_height_minus1[i];
        update_pps |= pic_param.pic_fields.bits.loop_filter_across_tiles_enabled_flag != new_param->pic_fields.bits.loop_filter_across_tiles_enabled_flag;
    }
    update_pps |= pic_param.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag != new_param->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag;
    update_pps |= pic_param.slice_parsing_fields.bits.deblocking_filter_override_enabled_flag != new_param->slice_parsing_fields.bits.deblocking_filter_override_enabled_flag;
    update_pps |= pic_param.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag != new_param->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag;
    if (!pic_param.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag) {
        update_pps |= pic_param.pps_beta_offset_div2 != new_param->pps_beta_offset_div2;
        update_pps |= pic_param.pps_tc_offset_div2 != new_param->pps_tc_offset_div2;
    }
    update_pps |= pic_param.slice_parsing_fields.bits.lists_modification_present_flag != new_param->slice_parsing_fields.bits.lists_modification_present_flag;
    update_pps |= pic_param.log2_parallel_merge_level_minus2 != new_param->log2_parallel_merge_level_minus2;
    update_pps |= pic_param.slice_parsing_fields.bits.slice_segment_header_extension_present_flag != new_param->slice_parsing_fields.bits.slice_segment_header_extension_present_flag;
    CIX_VAAPI_DEBUG("PPS change detected: %d\n", update_pps);
}

void PackerHEVC::SetPictureParameter(void *data, uint32_t size)
{
    if (size != sizeof(VAPictureParameterBufferHEVC))
        return;

    DetectPPSChange((VAPictureParameterBufferHEVC *)data);
    if (update_pps)
        pic_param = *(VAPictureParameterBufferHEVC *)data;

    // TODO: handle this unsupported error
    CIX_VAAPI_CHECK_RETURN(pic_param.pic_fields.bits.chroma_format_idc == 1,
        "chroma_format_idc %d not supported\n", pic_param.pic_fields.bits.chroma_format_idc);
}

void PackerHEVC::PrintScalingList()
{
    CIX_VAAPI_INFO("Scaling List:\n");
    for (int size_id = 0; size_id < BLOCK_SIZE_NUM; size_id++) {
        int num_lists = (size_id < BLOCK_SIZE_32x32) ? 6 : 2;
        for (int list_id = 0; list_id < num_lists; list_id++) {
            CIX_VAAPI_INFO(" SizeID %d ListID %d:\n", size_id, list_id);
            const uint8_t *list = nullptr;
            const int line_size = (size_id == BLOCK_SIZE_4x4) ? 4 : 8;
            if (size_id == BLOCK_SIZE_4x4) {
                list = iq_matrix.ScalingList4x4[list_id];
            } else if (size_id == BLOCK_SIZE_8x8) {
                list = iq_matrix.ScalingList8x8[list_id];
            } else if (size_id == BLOCK_SIZE_16x16) {
                list = iq_matrix.ScalingList16x16[list_id];
            } else if (size_id == BLOCK_SIZE_32x32) {
                list = iq_matrix.ScalingList32x32[list_id];
            }
            int length = (size_id == BLOCK_SIZE_4x4) ? 16 : 64;
            for (int i = 0; i < length; i++) {
                fprintf(stderr, " %3d", list[i]);
                if ((i + 1) % line_size == 0) {
                    fprintf(stderr, "\n");
                    fflush(stderr);
                }
            }
        }
    }
}

int32_t PackerHEVC::SetIQMatrix(void *data, uint32_t size)
{
    CIX_VAAPI_CHECK_RETURN_CODE(size == sizeof(VAIQMatrixBufferHEVC), -1,
        "Invalid IQ matrix size: %d\n", size);

    if (!pic_param.pic_fields.bits.scaling_list_enabled_flag) {
        CIX_VAAPI_DEBUG("Scaling list disabled in PPS, ignore IQ matrix\n");
        return 0;
    }

    size -= sizeof(iq_matrix.va_reserved);
    if (!update_pps && !std::memcmp(data, &iq_matrix, size)) {
        CIX_VAAPI_DEBUG("IQ matrix not changed: %d\n", update_pps);
        return 0;
    }

    update_pps = true; // update PPS as scaling list changed

    iq_matrix = *(VAIQMatrixBufferHEVC *)data;

    custom_scaling_lists = 0;
    for (int i = 0; i < 6; i++) {
        if (std::memcmp(scaling_list_4x4_default, iq_matrix.ScalingList4x4[i], 16))
            custom_scaling_lists |= 1 << i;
    }

    for (int i = 0; i < 6; i++) {
        const uint8_t *scaling_list_default = (i < 3) ?
            scaling_list_intra_default : scaling_list_inter_default;
        if (std::memcmp(scaling_list_default, iq_matrix.ScalingList8x8[i], 64))
            custom_scaling_lists |= 1 << (i+6);
    }

    for (int i = 0; i < 6; i++) {
        const uint8_t *scaling_list_default = (i < 3) ?
            scaling_list_intra_default : scaling_list_inter_default;
        if (std::memcmp(scaling_list_default, iq_matrix.ScalingList16x16[i], 64))
            custom_scaling_lists |= 1 << (i+12);
    }

    if (std::memcmp(scaling_list_intra_default, iq_matrix.ScalingList32x32[0], 64))
        custom_scaling_lists |= 1 << 18;
    if (std::memcmp(scaling_list_inter_default, iq_matrix.ScalingList32x32[1], 64))
        custom_scaling_lists |= 1 << 19;

    if (custom_scaling_lists == 0) {
        CIX_VAAPI_INFO("Got default scaling list\n");
    } else {
        CIX_VAAPI_INFO("Got custom scaling list: 0x%x\n", custom_scaling_lists);
        if (log_level >= LOG_LEVEL_DEBUG)
            PrintScalingList();
    }

    return 0;
}

void PackerHEVC::SetSliceParameter(void *data, uint32_t size)
{
    uint32_t num = size / sizeof(VASliceParameterBufferHEVC);
    if (num == 0 || size % sizeof(VASliceParameterBufferHEVC) != 0)
        return;

    VASliceParameterBufferHEVC *slice_param = (VASliceParameterBufferHEVC *)data;
    for (int i = 0; i < num; i++) {
        slice_params.emplace_back(slice_param[i]);
        CIX_VAAPI_INFO("Got slice parameter: slice_data_size = %d, slice_data_offset = %d, slice_data_byte_offset = %d\n",
            slice_param[i].slice_data_size, slice_param[i].slice_data_offset, slice_param[i].slice_data_byte_offset);
    }
}

void PackerHEVC::PackVpsNalu(void *out, uint32_t out_size, uint32_t &offset)
{
    if (!update_vps)
        return;

    void *cache;
    uint32_t cache_size;
    offset += AddStartCode((uint8_t *)out + offset);
    offset += AddNaluHeader((uint8_t *)out + offset, HEVC_NALU_TYPE_VPS);
    GetCache(&cache, &cache_size);
    cix_bsw_init(&p, (uint8_t *)cache, cache_size);
    PackVPS();

    uint32_t bytes = cix_bsw_bit_position(&p) >> 3;
    offset += AddEmulationPrevention((uint8_t *)out + offset, out_size - offset,
                                     (uint8_t *)cache, bytes);
    update_vps = false;
}

void PackerHEVC::PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset)
{
}

void PackerHEVC::PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id)
{
    if (!update_pps && last_sps_id == sps_id && last_pps_id == pps_id)
        return;

    void *cache;
    uint32_t cache_size;
    offset += AddStartCode((uint8_t *)out + offset);
    offset += AddNaluHeader((uint8_t *)out + offset, HEVC_NALU_TYPE_PPS);
    GetCache(&cache, &cache_size);
    cix_bsw_init(&p, (uint8_t *)cache, cache_size);
    PackPPS(sps_id, pps_id);

    uint32_t bytes = cix_bsw_bit_position(&p) >> 3;
    offset += AddEmulationPrevention((uint8_t *)out + offset, out_size - offset,
                                     (uint8_t *)cache, bytes);
    update_pps = false;
}

void PackerHEVC::PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_header)
{
}

void PackerHEVC::PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size)
{
}

uint32_t PackerHEVC::CopySPS(void *out, uint32_t out_size, void *seq_hdr)
{
    CIX_VAAPI_CHECK_RETURN_CODE(seq_hdr != nullptr, 0,
        "Invalid sequence header pointer\n");

    SequenceHeader *seq_header = (SequenceHeader *)seq_hdr;
    uint8_t *in = (uint8_t *)seq_header->data;
    uint32_t in_size = seq_header->size;
    uint32_t offset = 0;
    offset += AddStartCode((uint8_t *)out, false);
    uint8_t *buf = (uint8_t *)out + offset;
    if (seq_header->has_cropping) {
        CIX_VAAPI_INFO("Remove conformance window info from SPS\n");
        // Copy the bits before conformance_window_flag
        void *cache;
        uint32_t cache_size;
        uint32_t bytes = seq_header->bit_offset.conformance_window_flag >> 3;
        GetCache(&cache, &cache_size);
        memcpy(cache, in, bytes);
        // Repack conformance_window_flag as 0
        uint32_t bits = seq_header->bit_offset.conformance_window_flag & 7;
        *((uint8_t *)cache + bytes) = bits == 0 ? 0 : (in[bytes] & ~(0xff >> bits));
        // Copy the remaining bits from bit_depth_luma_minus8 onward, without the H.264
        // cabac_alignment_one_bit trailing fill that the generic CopyBits would inject.
        int bit_offset = seq_header->bit_offset.conformance_window_flag + 1;
        in_size = CopyBitsRaw((uint8_t *)cache, cache_size, bit_offset,
                              in, in_size, seq_header->bit_offset.bit_depth_luma_minus8);
        in = (uint8_t *)cache;
    }
    offset += AddEmulationPrevention((uint8_t *)buf, out_size - offset,
                                     (uint8_t *)in, in_size);
    // Set VPS id to 0
    buf[2] &= 0x0f; // The first 16-bits is NALU header, and then 4-bit sps_video_parameter_set_id
    // *((uint8_t *)out + offset) = 0x80;
    // offset++;
    sps_inserted = true;
    return offset;
}

uint32_t PackerHEVC::CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice)
{
    uint32_t offset = 0;
    offset += AddStartCode((uint8_t *)out, !first_slice);
    memcpy((uint8_t *)out + offset, in, in_size);
    offset += in_size;
    slice_params.clear();
    return offset;
}

int32_t PackerHEVC::CheckIntegrity()
{
    if (!sps_inserted) {
        CIX_VAAPI_ERROR("SPS is missing\n");
        return -1;
    }

    return 0;
}

uint32_t PackerHEVC::AddStartCode(uint8_t *buf, bool short_start_code)
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

uint32_t PackerHEVC::AddNaluHeader(uint8_t *buf, uint8_t type)
{
    buf[0] = type << 1;
    buf[1] = 1;

    return 2;
}

void PackerHEVC::PackProfileTierLevel()
{
    cix_bsw_ctx *s = &p;
    bool is_8bit = pic_param.bit_depth_luma_minus8 == 0 && pic_param.bit_depth_chroma_minus8 == 0;

    cix_bsw_write(s, 2, 0); // general_profile_space
    cix_bsw_write_bit(s, 0); // general_tier_flag
    if (is_8bit) {
        cix_bsw_write(s, 5, HEVC_PROFILE_MAIN); // general_profile_idc
        cix_bsw_write(s, 32, 0x60000000); // general_profile_compatibility_flag[ j ]
    } else {
        cix_bsw_write(s, 5, HEVC_PROFILE_MAIN10); // general_profile_idc
        cix_bsw_write(s, 32, 0x20000000); // general_profile_compatibility_flag[ j ]
    }
    cix_bsw_write_bit(s, 0); // general_progressive_source_flag
    cix_bsw_write_bit(s, 0); // general_interlaced_source_flag
    cix_bsw_write_bit(s, 0); // general_non_packed_constraint_flag
    cix_bsw_write_bit(s, 0); // general_frame_only_constraint_flag
    cix_bsw_write(s, 7, 0); // general_reserved_zero_7bits
    cix_bsw_write_bit(s, 0); // general_one_picture_only_constraint_flag
    cix_bsw_write(s, 35, 0); // general_reserved_zero_35bits
    cix_bsw_write_bit(s, 0); // general_inbld_flag
    cix_bsw_write(s, 8, 186); // general_level_idc, 186 means level 6.2
}

void PackerHEVC::PackVPS()
{
    cix_bsw_ctx *s = &p;
    bool is_8bit = pic_param.bit_depth_luma_minus8 == 0 && pic_param.bit_depth_chroma_minus8 == 0;

    cix_bsw_write(s, 4, 0); // vps_video_parameter_set_id
    cix_bsw_write_bit(s, 1); // vps_base_layer_internal_flag
    cix_bsw_write_bit(s, 1); // vps_base_layer_available_flag
    cix_bsw_write(s, 6, 0); // vps_max_layers_minus1
    cix_bsw_write(s, 3, 0); // vps_max_sub_layers_minus1
    cix_bsw_write_bit(s, 1); // vps_temporal_id_nesting_flag
    cix_bsw_write(s, 16, 0xffff); // vps_reserved_0xffff_16bits
    PackProfileTierLevel();
    cix_bsw_write_bit(s, 0); // vps_sub_layer_ordering_info_present_flag
    cix_bsw_put_ue(s, pic_param.sps_max_dec_pic_buffering_minus1); // vps_max_dec_pic_buffering_minus1[0]
    cix_bsw_put_ue(s, pic_param.sps_max_dec_pic_buffering_minus1); // vps_num_reorder_pics[0]
    cix_bsw_put_ue(s, 0); // vps_max_latency_increase_plus1[0]
    cix_bsw_write(s, 6, 0); // vps_max_layer_id
    cix_bsw_put_ue(s, 0); // vps_num_layer_sets_minus1
    cix_bsw_write_bit(s, 0); // vps_timing_info_present_flag
    cix_bsw_write_bit(s, 0); // vps_extension_flag
    cix_bsw_write_rbsp_trailing(s);
    cix_bsw_flush_bits(s);
}

void PackerHEVC::PackSPS()
{
    cix_bsw_ctx *s = &p;

    cix_bsw_write(s, 4, 0); // sps_video_parameter_set_id
    cix_bsw_write_bit(s, 0); // sps_max_sub_layers_minus1
    cix_bsw_write_bit(s, 1); // sps_temporal_id_nesting_flag
    PackProfileTierLevel();
    cix_bsw_put_ue(s, 0); // sps_seq_parameter_set_id
    cix_bsw_put_ue(s, 1); // chroma_format_idc = 1, 4:2:0
    cix_bsw_put_ue(s, pic_param.pic_width_in_luma_samples);
    cix_bsw_put_ue(s, pic_param.pic_height_in_luma_samples);
    cix_bsw_write_bit(s, 0); // conformance_window_flag
    cix_bsw_put_ue(s, pic_param.bit_depth_luma_minus8);
    cix_bsw_put_ue(s, pic_param.bit_depth_chroma_minus8);
    cix_bsw_put_ue(s, pic_param.log2_max_pic_order_cnt_lsb_minus4);
    cix_bsw_write_bit(s, 0); // sps_sub_layer_ordering_info_present_flag
    cix_bsw_put_ue(s, pic_param.sps_max_dec_pic_buffering_minus1);
    cix_bsw_put_ue(s, pic_param.sps_max_dec_pic_buffering_minus1); // sps_max_num_reorder_pics[0] // TODO: double check
    cix_bsw_put_ue(s, 0); // sps_max_latency_increase_plus1[0] // TODO: double check
    cix_bsw_put_ue(s, pic_param.log2_min_luma_coding_block_size_minus3);
    cix_bsw_put_ue(s, pic_param.log2_diff_max_min_luma_coding_block_size);
    cix_bsw_put_ue(s, pic_param.log2_min_transform_block_size_minus2);
    cix_bsw_put_ue(s, pic_param.log2_diff_max_min_transform_block_size);
    cix_bsw_put_ue(s, pic_param.max_transform_hierarchy_depth_inter);
    cix_bsw_put_ue(s, pic_param.max_transform_hierarchy_depth_intra);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.scaling_list_enabled_flag);
    if (pic_param.pic_fields.bits.scaling_list_enabled_flag)
        cix_bsw_write_bit(s, 0); // sps_scaling_list_data_present_flag, disable in SPS as it can be enabled in PPS
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.amp_enabled_flag);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.pcm_enabled_flag);
    if (pic_param.pic_fields.bits.pcm_enabled_flag) {
        cix_bsw_write(s, 4, pic_param.pcm_sample_bit_depth_luma_minus1);
        cix_bsw_write(s, 4, pic_param.pcm_sample_bit_depth_chroma_minus1);
        cix_bsw_put_ue(s, pic_param.log2_min_pcm_luma_coding_block_size_minus3);
        cix_bsw_put_ue(s, pic_param.log2_diff_max_min_pcm_luma_coding_block_size);
        cix_bsw_write_bit(s, pic_param.pic_fields.bits.pcm_loop_filter_disabled_flag);
    }
    cix_bsw_put_ue(s, pic_param.num_short_term_ref_pic_sets);
    for (int i = 0; i < pic_param.num_short_term_ref_pic_sets; i++) {
        // TODO: st_ref_pic_set(i)
    }
    cix_bsw_write_bit(s, 0); //pic_param.slice_parsing_fields.bits.long_term_ref_pics_present_flag);
    if (pic_param.slice_parsing_fields.bits.long_term_ref_pics_present_flag) {
        CIX_VAAPI_WARNING("long_term_ref_pics_present_flag = 1 is not supported\n");
        cix_bsw_put_ue(s, pic_param.num_long_term_ref_pic_sps);
        for (int i = 0; i < pic_param.num_long_term_ref_pic_sps; i++) {
            // TODO: lt_ref_pic_poc_lsb_sps[i]
            // TODO: used_by_curr_pic_sps_flag[i]
        }
    }
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.strong_intra_smoothing_enabled_flag);
    cix_bsw_write_bit(s, 0); // vui_parameters_present_flag, ignore vui which is not critical for decoding
    cix_bsw_write_bit(s, 0); // sps_extension_present_flag, ignore sps extension which is not critical for decoding
    cix_bsw_write_rbsp_trailing(s);
    cix_bsw_flush_bits(s);
}

const uint8_t *PackerHEVC::GetDefaultList(int size_id, int list_id)
{
    if (size_id == BLOCK_SIZE_4x4)
        return scaling_list_4x4_default;
    else if (size_id == BLOCK_SIZE_8x8 || size_id == BLOCK_SIZE_16x16)
        return list_id < 3 ? scaling_list_intra_default : scaling_list_inter_default;
    else
        return list_id < 1 ? scaling_list_intra_default : scaling_list_inter_default;
}

int32_t PackerHEVC::GetPredListId(int size_id, int list_id)
{
    for (int pred_list_id = list_id; pred_list_id >= 0; pred_list_id--) {
        // Check DC value first
        if (size_id >= BLOCK_SIZE_16x16 && pred_list_id != list_id) {
            uint8_t *scaling_list_dc = (size_id == BLOCK_SIZE_32x32) ?
                iq_matrix.ScalingListDC32x32 : iq_matrix.ScalingListDC16x16;
            if (scaling_list_dc[list_id] != scaling_list_dc[pred_list_id])
                continue;
        }
        // Then check matrix values
        int32_t size = (size_id == BLOCK_SIZE_4x4) ? 16 : 64;
        const uint8_t *pred_list = pred_list_id == list_id ?
            GetDefaultList(size_id, list_id) : &scaling_list[size_id][pred_list_id*size];
        if (!memcmp(&scaling_list[size_id][list_id*size], pred_list, size))
            return pred_list_id;
    }

    return -1;
}

void PackerHEVC::PackScalingList(cix_bsw_ctx *s)
{
    for (int size_id = 0; size_id < BLOCK_SIZE_NUM; size_id++) {
        const uint8_t *scan = zigzag_scan8;
        int32_t coef_num = 64;
        uint8_t *list_dc = iq_matrix.ScalingListDC16x16;
        int list_num = 6;
        if (size_id == BLOCK_SIZE_32x32) {
            list_dc = iq_matrix.ScalingListDC32x32;
            list_num = 2;
        } else if (size_id == BLOCK_SIZE_4x4) {
            scan = zigzag_scan4;
            coef_num = 16;
        }

        for (int list_id = 0; list_id < list_num; list_id++) {
            int pred_list_id = GetPredListId(size_id, list_id);
            cix_bsw_write_bit(s, pred_list_id < 0); // scaling_list_pred_mode_flag
            if (pred_list_id >= 0) {
                cix_bsw_put_ue(s, list_id - pred_list_id); // scaling_list_pred_matrix_id_delta
                continue;
            }

            int32_t next_coef = 8;
            uint8_t *list = &scaling_list[size_id][list_id*coef_num];
            if (size_id > BLOCK_SIZE_8x8) {
                cix_bsw_write_se(s, list_dc[list_id] - 8); // scaling_list_dc_coef_minus8
                next_coef = list_dc[list_id];
            }

            for (int i = 0; i < coef_num; i++) {
                int data = list[scan[i]] - next_coef;
                if (data < -128)
                    data += 256;
                else if (data > 127)
                    data -= 256;
                next_coef = (next_coef + data + 256) & 0xFF;
                cix_bsw_write_se(s, data); // scaling_list_delta_coef
            }
        }
    }
}

void PackerHEVC::PackPPS(int32_t sps_id, int32_t pps_id)
{
    cix_bsw_ctx *s = &p;

    cix_bsw_put_ue(s, pps_id); // pps_pic_parameter_set_id
    cix_bsw_put_ue(s, sps_id); // pps_seq_parameter_set_id
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.dependent_slice_segments_enabled_flag);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.output_flag_present_flag);
    cix_bsw_write(s, 3, pic_param.num_extra_slice_header_bits);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.sign_data_hiding_enabled_flag);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.cabac_init_present_flag);
    cix_bsw_put_ue(s, pic_param.num_ref_idx_l0_default_active_minus1);
    cix_bsw_put_ue(s, pic_param.num_ref_idx_l1_default_active_minus1);
    cix_bsw_write_se(s, pic_param.init_qp_minus26);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.constrained_intra_pred_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.transform_skip_enabled_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.cu_qp_delta_enabled_flag);
    if (pic_param.pic_fields.bits.cu_qp_delta_enabled_flag)
        cix_bsw_put_ue(s, pic_param.diff_cu_qp_delta_depth);
    cix_bsw_write_se(s, pic_param.pps_cb_qp_offset);
    cix_bsw_write_se(s, pic_param.pps_cr_qp_offset);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.pps_slice_chroma_qp_offsets_present_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.weighted_pred_flag );
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.weighted_bipred_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.transquant_bypass_enabled_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.tiles_enabled_flag);
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.entropy_coding_sync_enabled_flag);
    if (pic_param.pic_fields.bits.tiles_enabled_flag) {
        cix_bsw_put_ue(s, pic_param.num_tile_columns_minus1);
        cix_bsw_put_ue(s, pic_param.num_tile_rows_minus1);
        cix_bsw_write_bit(s, 0); // uniform_spacing_flag
        for (int i = 0; i < pic_param.num_tile_columns_minus1; i++)
            cix_bsw_put_ue(s, pic_param.column_width_minus1[i]);
        for (int i = 0; i < pic_param.num_tile_rows_minus1; i++)
            cix_bsw_put_ue(s, pic_param.row_height_minus1[i]);
        cix_bsw_write_bit(s, pic_param.pic_fields.bits.loop_filter_across_tiles_enabled_flag);
    }
    cix_bsw_write_bit(s, pic_param.pic_fields.bits.pps_loop_filter_across_slices_enabled_flag);
    cix_bsw_write_bit(s, 1); // deblocking_filter_control_present_flag = 1
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.deblocking_filter_override_enabled_flag);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag);
    if (!pic_param.slice_parsing_fields.bits.pps_disable_deblocking_filter_flag) {
        cix_bsw_write_se(s, pic_param.pps_beta_offset_div2);
        cix_bsw_write_se(s, pic_param.pps_tc_offset_div2);
    }
    // cix_bsw_write_bit(s, pic_param.pic_fields.bits.scaling_list_enabled_flag);
    if (pic_param.pic_fields.bits.scaling_list_enabled_flag) {
        cix_bsw_write_bit(s, custom_scaling_lists != 0); // pps_scaling_list_data_present_flag
        if (custom_scaling_lists)
            PackScalingList(s);
        // custom_scaling_lists = 0;
    } else {
        cix_bsw_write_bit(s, 0); // pps_scaling_list_data_present_flag
    }
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.lists_modification_present_flag);
    cix_bsw_put_ue(s, pic_param.log2_parallel_merge_level_minus2);
    cix_bsw_write_bit(s, pic_param.slice_parsing_fields.bits.slice_segment_header_extension_present_flag);
    cix_bsw_write_bit(s, 0); // pps_extension_present_flag
    cix_bsw_write_rbsp_trailing(s);
    cix_bsw_flush_bits(s);
}

void PackerHEVC::PackSliceHeader()
{
}

