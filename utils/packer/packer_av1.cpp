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

#include <cstring>
#include "packer_av1.h"
#include "sequence.h"
#include "log.h"

PackerAV1::PackerAV1()
    : pic_valid(false), td_obu_inserted(false), tiles_copied(0)
{
    memset(&pic_param, 0, sizeof(pic_param));
}

void PackerAV1::SetPictureParameter(void *data, uint32_t size)
{
    if (!data || size != sizeof(VADecPictureParameterBufferAV1))
        return;
    pic_param = *(VADecPictureParameterBufferAV1 *)data;
    pic_valid = true;
    td_obu_inserted = false;
    tiles_copied = 0;
    slice_params.clear();
}

int32_t PackerAV1::SetIQMatrix(void *data, uint32_t size)
{
    (void)data;
    (void)size;
    return 0;
}

void PackerAV1::SetSliceParameter(void *data, uint32_t size)
{
    uint32_t n = size / sizeof(VASliceParameterBufferAV1);
    if (!data || n == 0 || size % sizeof(VASliceParameterBufferAV1) != 0)
        return;
    VASliceParameterBufferAV1 *sp = (VASliceParameterBufferAV1 *)data;
    for (uint32_t i = 0; i < n; i++)
        slice_params.push_back(sp[i]);
}

uint32_t PackerAV1::SaveSliceData(void *dst, void *data, uint32_t size)
{
    if (!dst || !data || size == 0)
        return 0;

    auto slice_param = slice_params.back();
    if (size < slice_param.slice_data_size)
        return 0;

    memcpy(dst, (uint8_t *)data + slice_param.slice_data_offset, slice_param.slice_data_size);
    return slice_param.slice_data_size;
}

void PackerAV1::PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset)
{
    (void)out_size;

    if (!td_obu_inserted) {
        uint8_t *data = (uint8_t *)out + offset;
        /* OBU_TEMPORAL_DELIMITER, obu_size = 0 */
        data[0] = 0x12;
        data[1] = 0x00;
        offset += 2;
        td_obu_inserted = true;
    }
}

uint32_t PackerAV1::CopySPS(void *out, uint32_t out_size, void *seq_header)
{
    if (!out || out_size == 0 || !seq_header)
        return 0;

    const SequenceHeader *sps = (const SequenceHeader *)seq_header;
    if (!sps->data || sps->size == 0)
        return 0;
    if (sps->size > out_size) {
        CIX_VAAPI_WARNING("AV1: sequence buffer (%u B) exceeds output space (%u B)\n",
            sps->size, out_size);
        return 0;
    }

    memcpy(out, sps->data, sps->size);
    return sps->size;
}

uint32_t PackerAV1::CopyPPS(void *out, uint32_t out_size, void *in, uint32_t in_size)
{
    if (!out || out_size == 0 || !in || in_size == 0)
        return 0;
    memcpy(out, in, in_size);
    return in_size;
}

void PackerAV1::PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id)
{
    (void)out;
    (void)out_size;
    (void)offset;
    (void)sps_id;
    (void)pps_id;
}

void PackerAV1::PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param)
{
    (void)out;
    (void)out_size;
    (void)offset;
    (void)in;
    (void)in_size;
    (void)slice_param;
}

void PackerAV1::PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size)
{
    if (!in || !out || in_size > out_size)
        return;
    memcpy(out, in, in_size);
}

uint32_t PackerAV1::PackRepeatFrame(void *buffer, uint32_t buffer_size)
{
#if (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
    if (!pic_valid || !buffer || buffer_size == 0)
        return 0;
    if (pic_param.pic_info_fields.bits.show_frame == 1)
        return 0;
    uint8_t *data = (uint8_t *)buffer;
    data[0] = 0x1A; /* OBU_FRAME_HEADER */
    data[1] = 0x01; /* obu_size */
    data[2] = 0x80; /* show_existing_frame */

    const VADecPictureParameterBufferAV1Ext *ext =
        va_get_dec_picture_parameter_buffer_av1_ext_buffer(&pic_param);
    if (!ext)
        return 0;

    const uint8_t refresh_frame_flags = ext->u.buffer.refresh_frame_flags;
    if (refresh_frame_flags == 0)
        return 0;

    uint8_t frame_to_show_map_idx = 0;
    for (int i = 0; i < 8; i++) {
        if (refresh_frame_flags & (1 << i)) {
            frame_to_show_map_idx = i;
            break;
        }
    }

    data[2] |= frame_to_show_map_idx << 4;
    data[2] |= 0x08; /* trailing bits */
    return 3;
#else
    (void)buffer;
    (void)buffer_size;
    return 0;
#endif  // (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
}

uint32_t PackerAV1::CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice)
{
    (void)first_slice;

    if (!out || !in || tiles_copied >= slice_params.size())
        return 0;

    uint32_t copy_len = 0;
    uint32_t num_tiles = pic_param.tile_cols * pic_param.tile_rows;
    bool is_last_tile = tiles_copied + 1 >= num_tiles;
    if (!is_last_tile) {
        const VADecPictureParameterBufferAV1Ext *ext =
            va_get_dec_picture_parameter_buffer_av1_ext_buffer(&pic_param);
        if (!ext)
            return 0;

        uint8_t *data = (uint8_t *)out;
        for (int i = 0; i < ext->u.buffer.tile_size_bytes; i++)
            *(data++) = ((in_size - 1) >> (8 * i)) & 0xFF;
        copy_len += ext->u.buffer.tile_size_bytes;

        if (in_size > out_size - copy_len) {
            CIX_VAAPI_WARNING("AV1: tile payload exceeds output space (need %u, left %u)\n",
                in_size, out_size - copy_len);
            return 0;
        }
        memcpy(data, (uint8_t *)in, in_size);
        copy_len += in_size;
        tiles_copied++;
    } else {
        if (in_size > out_size) {
            CIX_VAAPI_WARNING("AV1: tile payload exceeds output plane (%u > %u)\n",
                in_size, out_size);
            return 0;
        }
        memcpy(out, (uint8_t *)in, in_size);
        copy_len += in_size;
        tiles_copied++;

        copy_len += PackRepeatFrame((uint8_t *)out + copy_len, out_size - copy_len);
    }

    return copy_len;
}

int32_t PackerAV1::CheckIntegrity()
{
    if (!pic_valid) {
        CIX_VAAPI_WARNING("AV1: missing picture parameters\n");
        return -1;
    }
    if (slice_params.empty()) {
        CIX_VAAPI_WARNING("AV1: missing slice (tile) parameters\n");
        return -1;
    }

    for (size_t i = 0; i < slice_params.size(); i++) {
        const VASliceParameterBufferAV1 &sp = slice_params[i];
        if (sp.slice_data_size == 0) {
            CIX_VAAPI_WARNING("AV1: invalid tile %zu slice sizes\n", i);
            return -1;
        }
    }

    if (tiles_copied != slice_params.size()) {
        CIX_VAAPI_WARNING("AV1: expected %zu tile buffers, got %u\n",
            slice_params.size(), tiles_copied);
        return -1;
    }

    return 0;
}
