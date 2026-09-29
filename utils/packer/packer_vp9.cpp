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

#include <cstring>
#include "packer_vp9.h"
#include "parser_bsr.h"
#include "log.h"

namespace {

/* VP9 spec section 6.2 (uncompressed_header) and Annex B (superframe). */
enum { VP9_FRAME_SYNC_CODE = 0x498342, VP9_CS_RGB = 7 };

static bool vp9_skip_color_config(CixBitReader *br, unsigned profile)
{
    if (profile >= 2) {
        if (cix_br_bits_left(br) < 1)
            return false;
        cix_br_skip(br, 1);
    }
    if (cix_br_bits_left(br) < 3)
        return false;
    unsigned color_space = cix_br_read_u(br, 3);
    if (color_space != VP9_CS_RGB) {
        if (cix_br_bits_left(br) < 1)
            return false;
        cix_br_skip(br, 1);
        if (profile == 1 || profile == 3) {
            if (cix_br_bits_left(br) < 3)
                return false;
            cix_br_skip(br, 3);
        }
    } else if (profile == 1 || profile == 3) {
        if (cix_br_bits_left(br) < 1)
            return false;
        cix_br_skip(br, 1);
    }
    return true;
}

static bool vp9_skip_frame_size(CixBitReader *br)
{
    if (cix_br_bits_left(br) < 32)
        return false;
    cix_br_skip(br, 16);
    cix_br_skip(br, 16);
    return true;
}

static bool vp9_skip_render_size(CixBitReader *br)
{
    if (cix_br_bits_left(br) < 1)
        return false;
    if (cix_br_read_flag(br)) {
        if (cix_br_bits_left(br) < 32)
            return false;
        cix_br_skip(br, 16);
        cix_br_skip(br, 16);
    }
    return true;
}

/* Parse refresh_frame_flags after frame_marker and profile (spec section 6.2). */
static bool vp9_parse_refresh_frame_flags_from_uncompressed_header(
    const uint8_t *data, uint32_t nbytes, uint8_t *out_refresh)
{
    CixBitReader br;
    if (!data || !out_refresh || nbytes == 0)
        return false;
    if (cix_br_init_bytes(&br, data, nbytes) != 0)
        return false;

    if (cix_br_bits_left(&br) < 2)
        return false;
    if (cix_br_read_u(&br, 2) != 2)
        return false;

    if (cix_br_bits_left(&br) < 2)
        return false;
    unsigned profile = cix_br_read_u(&br, 1);
    profile |= cix_br_read_u(&br, 1) << 1;
    if (profile == 3) {
        if (cix_br_bits_left(&br) < 1)
            return false;
        if (cix_br_read_flag(&br) != 0)
            return false;
    }

    if (cix_br_bits_left(&br) < 1)
        return false;
    if (cix_br_read_flag(&br)) {
        if (cix_br_bits_left(&br) < 3)
            return false;
        cix_br_skip(&br, 3);
        *out_refresh = 0;
        return true;
    }

    if (cix_br_bits_left(&br) < 3)
        return false;
    unsigned frame_type = cix_br_read_flag(&br);
    unsigned show_frame = cix_br_read_flag(&br);
    unsigned error_resilient = cix_br_read_flag(&br);

    if (frame_type == 0) {
        if (cix_br_bits_left(&br) < 24)
            return false;
        if (cix_br_read_u(&br, 24) != VP9_FRAME_SYNC_CODE)
            return false;
        if (!vp9_skip_color_config(&br, profile))
            return false;
        if (!vp9_skip_frame_size(&br))
            return false;
        if (!vp9_skip_render_size(&br))
            return false;
        *out_refresh = 0xff;
        return true;
    }

    unsigned intra_only = 0;
    if (!show_frame) {
        if (cix_br_bits_left(&br) < 1)
            return false;
        intra_only = cix_br_read_flag(&br);
    }
    if (!error_resilient) {
        if (cix_br_bits_left(&br) < 2)
            return false;
        cix_br_skip(&br, 2);
    }

    if (intra_only) {
        if (cix_br_bits_left(&br) < 24)
            return false;
        if (cix_br_read_u(&br, 24) != VP9_FRAME_SYNC_CODE)
            return false;
        if (profile > 0) {
            if (!vp9_skip_color_config(&br, profile))
                return false;
        }
        if (cix_br_bits_left(&br) < 8)
            return false;
        *out_refresh = (uint8_t)cix_br_read_u(&br, 8);
        return true;
    }

    if (cix_br_bits_left(&br) < 8)
        return false;
    *out_refresh = (uint8_t)cix_br_read_u(&br, 8);
    return true;
}

/* Lowest set bit in refresh_frame_flags (same idea as ff_ctz in vp9_raw_reorder.c). */
static unsigned vp9_refresh_lowest_slot(uint8_t refresh_frame_flags)
{
    for (unsigned i = 0; i < 8; i++) {
        if (refresh_frame_flags & (1u << i))
            return i;
    }
    return 0;
}

/* floor(log2(v)) for v >= 1; v == 0 yields 0 (unused for superframe size math). */
static unsigned vp9_floor_log2_u32(uint32_t v)
{
    unsigned r = 0;
    while (v > 1u) {
        v >>= 1u;
        r++;
    }
    return r;
}

/*
 * Annex B superframe index: marker, LE frame sizes, marker (see vp9_superframe.c
 * merge_superframe; mag = floor_log2(max size) >> 3).
 */
static void vp9_write_superframe_index_two_frames(
    uint8_t *idx, uint32_t frame0_bytes, uint32_t frame1_bytes)
{
    const uint32_t max_sz = frame0_bytes > frame1_bytes ? frame0_bytes : frame1_bytes;
    const unsigned mag = vp9_floor_log2_u32(max_sz ? max_sz : 1u) >> 3;
    const uint8_t marker = (uint8_t)(0xC0 + (mag << 3) + 1); /* 2 frames */
    uint8_t *p = idx;

    *p++ = marker;
    for (unsigned f = 0; f < 2u; f++) {
        const uint32_t fs = f == 0u ? frame0_bytes : frame1_bytes;
        for (unsigned b = 0; b < (unsigned)(mag + 1); b++)
            *p++ = (uint8_t)((fs >> (8u * b)) & 0xffu);
    }
    *p++ = marker;
}

/*
 * Minimal show_existing_frame: 16 bits zero-padded to two bytes (section 6.2;
 * same bit layout as vp9_raw_reorder.c synthetic display packet).
 */
static void vp9_write_show_existing_frame_2bytes(
    uint8_t *dst, unsigned profile, unsigned frame_to_show_map_idx)
{
    profile &= 3u;
    frame_to_show_map_idx &= 7u;

    uint32_t u = 2u;
    u = (u << 1) | (profile & 1u);
    u = (u << 1) | ((profile >> 1) & 1u);
    if (profile == 3u)
        u = (u << 1) | 0u;
    u = (u << 1) | 1u;
    u = (u << 3) | frame_to_show_map_idx;
    const int tb = 2 + 2 + 1 + 3 + (profile == 3u ? 1 : 0);
    u <<= (16 - tb);
    dst[0] = (uint8_t)(u >> 8);
    dst[1] = (uint8_t)(u & 0xffu);
}

} // namespace

PackerVP9::PackerVP9() : pic_valid(false)
{
    memset(&pic_param, 0, sizeof(pic_param));
}

void PackerVP9::SetPictureParameter(void *data, uint32_t size)
{
    if (!data || size != sizeof(VADecPictureParameterBufferVP9))
        return;
    pic_param = *(VADecPictureParameterBufferVP9 *)data;
    pic_valid = true;
}

int32_t PackerVP9::SetIQMatrix(void *data, uint32_t size)
{
    (void)data;
    (void)size;
    return 0;
}

void PackerVP9::SetSliceParameter(void *data, uint32_t size)
{
    slice_params.clear();
    uint32_t n = size / sizeof(VASliceParameterBufferVP9);
    if (!data || n == 0 || size % sizeof(VASliceParameterBufferVP9) != 0)
        return;
    VASliceParameterBufferVP9 *sp = (VASliceParameterBufferVP9 *)data;
    for (uint32_t i = 0; i < n; i++)
        slice_params.push_back(sp[i]);
}

void PackerVP9::PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset)
{
    (void)out;
    (void)out_size;
    (void)offset;
}

void PackerVP9::PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id)
{
    (void)out;
    (void)out_size;
    (void)offset;
    (void)sps_id;
    (void)pps_id;
}

void PackerVP9::PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param)
{
    (void)out;
    (void)out_size;
    (void)offset;
    (void)in;
    (void)in_size;
    (void)slice_param;
}

void PackerVP9::PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size)
{
    if (!in || !out || in_size > out_size)
        return;
    memcpy(out, in, in_size);
}

uint32_t PackerVP9::CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice)
{
    (void)first_slice;
    if (!out || !in || slice_params.empty())
        return 0;

    const VASliceParameterBufferVP9 &sp = slice_params[0];
    uint32_t nbytes = sp.slice_data_size;
    if (nbytes > out_size) {
        CIX_VAAPI_WARNING("VP9 frame size %u exceeds output plane %u\n", nbytes, out_size);
        return 0;
    }
    if (nbytes > in_size) {
        CIX_VAAPI_WARNING("VP9 slice_data_size %u exceeds slice buffer %u\n", nbytes, in_size);
        return 0;
    }

    /* One coded frame per VA slice (vaapi_vp9 / elementary VP9). */
    memcpy(out, in, nbytes);

    uint8_t rff = 0;
    if (!vp9_parse_refresh_frame_flags_from_uncompressed_header(
            static_cast<const uint8_t *>(in), nbytes, &rff))
        CIX_VAAPI_WARNING("VP9: failed to parse refresh_frame_flags (uncompressed header section 6.2)\n");

    uint32_t total = nbytes;

    /* show_frame==0: append show_existing + superframe index so the ref can be shown. */
    if (pic_valid && pic_param.pic_fields.bits.show_frame == 0 && rff != 0) {
        const unsigned slot = vp9_refresh_lowest_slot(rff);
        const uint32_t sz_repeat = 2u;
        const uint32_t max_sz = nbytes > sz_repeat ? nbytes : sz_repeat;
        const unsigned mag = vp9_floor_log2_u32(max_sz ? max_sz : 1u) >> 3;
        const uint32_t idx_sz = 2u + (uint32_t)(mag + 1u) * 2u;
        const uint32_t total_needed = nbytes + sz_repeat + idx_sz;

        if (total_needed > out_size) {
            CIX_VAAPI_WARNING(
                "VP9: no space for superframe (need %u B incl. Annex B index, have %u)\n",
                total_needed, out_size);
        } else {
            uint8_t *const base = static_cast<uint8_t *>(out);
            vp9_write_show_existing_frame_2bytes(base + nbytes, pic_param.profile, slot);
            vp9_write_superframe_index_two_frames(base + nbytes + sz_repeat, nbytes, sz_repeat);
            total = total_needed;
        }
    }

    return total;
}

int32_t PackerVP9::CheckIntegrity()
{
    if (!pic_valid) {
        CIX_VAAPI_WARNING("VP9: missing picture parameters\n");
        return -1;
    }
    if (slice_params.empty()) {
        CIX_VAAPI_WARNING("VP9: missing slice parameters\n");
        return -1;
    }

    const VASliceParameterBufferVP9 &sp = slice_params[0];
    if (sp.slice_data_size == 0) {
        CIX_VAAPI_WARNING("VP9: slice_data_size is zero\n");
        return -1;
    }

    if (sp.slice_data_offset > sp.slice_data_size) {
        CIX_VAAPI_WARNING("VP9: slice_data_offset %u > slice_data_size %u\n",
            sp.slice_data_offset, sp.slice_data_size);
        return -1;
    }

    if (pic_param.frame_header_length_in_bytes > sp.slice_data_size) {
        CIX_VAAPI_WARNING("VP9: frame_header_length_in_bytes %u > slice_data_size %u\n",
            pic_param.frame_header_length_in_bytes, sp.slice_data_size);
        return -1;
    }

    /*
     * After the uncompressed header, VP9 stores first_partition_size in 16 bits,
     * then the arith-coded compressed header (see libavcodec/vp9.c after
     * read_uncompressed_header). Cross-check against the VA picture buffer.
     */
    uint32_t hdr_end = sp.slice_data_offset ? sp.slice_data_offset
                                            : pic_param.frame_header_length_in_bytes;
    if (hdr_end + 2 > sp.slice_data_size) {
        CIX_VAAPI_WARNING("VP9: no room for first_partition_size after header (hdr_end=%u size=%u)\n",
            hdr_end, sp.slice_data_size);
        return -1;
    }

    if (sp.slice_data_offset != 0 &&
        sp.slice_data_offset != pic_param.frame_header_length_in_bytes) {
        CIX_VAAPI_WARNING("VP9: slice_data_offset %u != frame_header_length_in_bytes %u\n",
            sp.slice_data_offset, pic_param.frame_header_length_in_bytes);
    }

    if (hdr_end + 2u + pic_param.first_partition_size > sp.slice_data_size) {
        CIX_VAAPI_WARNING("VP9: header + first_partition exceeds frame (hdr_end=%u fps=%u total=%u)\n",
            hdr_end, (unsigned)pic_param.first_partition_size, sp.slice_data_size);
        return -1;
    }

    return 0;
}
