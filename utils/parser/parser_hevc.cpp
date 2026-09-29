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
#include "parser_hevc.h"
#include "parser_bsr.h"

#define HEVC_NALU_HEADER_BITS 16
#define HEVC_NALU_TYPE_SPS 33
#define HEVC_NALU_TYPE_BLA_W_LP 16
#define HEVC_NALU_TYPE_RSV_IRAP_VCL23 23
#define HEVC_SPS_BUFFER_SIZE 4096

ParserHEVC::ParserHEVC() : Parser(),
    output_flag(true),
    slice_pic_parameter_set_id(-1)
{
    memset(&seq_header, 0, sizeof(SequenceHeader));
    seq_header.data = malloc(HEVC_SPS_BUFFER_SIZE);
    CIX_VAAPI_CHECK_RETURN(seq_header.data != nullptr,
        "Failed to allocate memory for HEVC sequence header buffer.\n");
}

void ParserHEVC::SetPictureParameter(void *data, uint32_t size)
{
    if (size != sizeof(VAPictureParameterBufferHEVC))
        return;

    pic_param = *(VAPictureParameterBufferHEVC *)data;
}

void *ParserHEVC::ParseSPS(void *data, uint32_t size)
{
    int32_t value = 0;

    CIX_VAAPI_CHECK_RETURN_NULL(data != nullptr && size > 0,
        "Invalid data buffer: data = %p, size = %d\n", data, size);

    if (seq_header.data && !memcmp(seq_header.data, data, size) && seq_header.size == size)
        return &seq_header;

    CixBitReader gb;
    int ret = cix_br_init_bytes(&gb, (uint8_t *)data, size);
    CIX_VAAPI_CHECK_RETURN_NULL(ret >= 0, "Failed to initialize stream reader.\n");
    cix_br_skip_bits(&gb, 1); // forbidden_zero_bit
    int nal_unit_type = cix_br_read_u(&gb, 6);
    CIX_VAAPI_CHECK_RETURN_NULL(nal_unit_type == HEVC_NALU_TYPE_SPS,
        "Not an SPS NAL unit (got %d).\n", nal_unit_type);
    cix_br_skip_bits(&gb, 6); // nuh_layer_id
    cix_br_skip_bits(&gb, 3); // nuh_temporal_id_plus1

    cix_br_skip_bits(&gb, 4); // sps_video_parameter_set_id
    CIX_VAAPI_CHECK_RETURN_NULL(cix_br_read_u(&gb, 3) == 0,
        "sps_max_sub_layers_minus1 > 0 is not expected.\n");
    cix_br_skip_bits(&gb, 1); // sps_temporal_id_nesting_flag

    // profile_tier_level( 1, sps_max_sub_layers_minus1 )
    cix_br_skip_bits(&gb, 2); // general_profile_space
    cix_br_skip_bits(&gb, 1); // general_tier_flag
    cix_br_skip_bits(&gb, 5); // general_profile_idc
    cix_br_skip_bits(&gb, 32); // general_profile_compatibility_flag[ j ]
    cix_br_skip_bits(&gb, 1); // general_progressive_source_flag
    cix_br_skip_bits(&gb, 1); // general_interlaced_source_flag
    cix_br_skip_bits(&gb, 1); // general_non_packed_constraint_flag
    cix_br_skip_bits(&gb, 1); // general_frame_only_constraint_flag
    cix_br_skip_bits(&gb, 7); // general_reserved_zero_7bits
    cix_br_skip_bits(&gb, 1); // general_one_picture_only_constraint_flag
    cix_br_skip_bits(&gb, 35); // general_reserved_zero_35bits
    cix_br_skip_bits(&gb, 1); // general_inbld_flag
    cix_br_skip_bits(&gb, 8); // general_level_idc

    seq_header.sps_seq_parameter_set_id = cix_eg_read_ue(&gb);
    value = cix_eg_read_ue(&gb); // chroma_format_idc
    CIX_VAAPI_CHECK_RETURN_NULL(value < 2, "Unsupported chroma_format_idc %d.\n", value);
    cix_eg_read_ue(&gb); // pic_width_in_luma_samples
    cix_eg_read_ue(&gb); // pic_height_in_luma_samples

    seq_header.bit_offset.conformance_window_flag = gb.bit_index;
    seq_header.has_cropping = false;
    if (cix_br_read_flag(&gb)) { // conformance_window_flag
        seq_header.has_cropping |= cix_eg_read_ue(&gb) != 0; // conf_win_left_offset
        seq_header.has_cropping |= cix_eg_read_ue(&gb) != 0; // conf_win_right_offset
        seq_header.has_cropping |= cix_eg_read_ue(&gb) != 0; // conf_win_top_offset
        seq_header.has_cropping |= cix_eg_read_ue(&gb) != 0; // conf_win_bottom_offset
    }

    seq_header.bit_offset.bit_depth_luma_minus8 = gb.bit_index;
    seq_header.size = size;
    if (seq_header.data && size <= HEVC_SPS_BUFFER_SIZE)
        memcpy(seq_header.data, data, size);
    else
        seq_header.data = data;

    return &seq_header;
}

int32_t ParserHEVC::GetSPSID(void *data, uint32_t size)
{
    ParseSPS(data, size);
    return seq_header.sps_seq_parameter_set_id;
}

void ParserHEVC::ParseSliceHeader(void *data, uint32_t size)
{
    CIX_VAAPI_CHECK_RETURN(data != nullptr && size > 0,
        "Invalid data buffer: data = %p, size = %d\n", data, size);

    CixBitReader gb;
    int ret = cix_br_init_bytes(&gb, (uint8_t *)data, size);
    CIX_VAAPI_CHECK_RETURN(ret >= 0, "Failed to initialize stream reader.\n");
    cix_br_skip_bits(&gb, 1); // forbidden_zero_bit
    int nal_unit_type = cix_br_read_u(&gb, 6);
    cix_br_skip_bits(&gb, 6); // nuh_layer_id
    cix_br_skip_bits(&gb, 3); // nuh_temporal_id_plus1

    int first_slice_segment_in_pic_flag = cix_br_read_flag(&gb);
    CIX_VAAPI_CHECK_RETURN(first_slice_segment_in_pic_flag,
        "Not the first slice segment in picture.\n");

    if (nal_unit_type >= HEVC_NALU_TYPE_BLA_W_LP &&
        nal_unit_type <= HEVC_NALU_TYPE_RSV_IRAP_VCL23)
        cix_br_skip_bits(&gb, 1); // no_output_of_prior_pics_flag

    slice_pic_parameter_set_id = cix_eg_read_ue(&gb);

    cix_br_skip_bits(&gb, pic_param.num_extra_slice_header_bits); // slice_reserved_flag[ i ]
    cix_eg_read_ue(&gb); // slice_type
    if (pic_param.slice_parsing_fields.bits.output_flag_present_flag) {
        output_flag = cix_br_read_flag(&gb); // pic_output_flag
        if (!output_flag)
            CIX_VAAPI_INFO("Got a picture with pic_output_flag = 0\n");
    }
}
