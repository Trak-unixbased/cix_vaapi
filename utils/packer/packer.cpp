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

#include <string.h>
#include "packer.h"
#include "log.h"

Packer::Packer() :
    cache(nullptr),
    cache_size(0)
{
}

uint32_t Packer::AddEmulationPrevention(uint8_t *out, uint32_t out_size, uint8_t *in, uint32_t in_size)
{
    uint32_t i = 0;
    uint32_t j = 0;
    while (i+2 < in_size) {
        if (in[i] == 0 && in[i + 1] == 0 &&
            (in[i + 2] == 0x00 || in[i + 2] == 0x01 || in[i + 2] == 0x02 || in[i + 2] == 0x03)) {
            out[j++] = in[i++];
            out[j++] = in[i++];
            out[j++] = 0x03; // insert emulation prevention byte
        } else {
            out[j++] = in[i++];
        }
    }

    while (i < in_size)
        out[j++] = in[i++];

    return j;
}

uint32_t Packer::SaveSliceData(void *dst, void *data, uint32_t size)
{
    if (!dst || !data || size == 0)
        return 0;

    memcpy(dst, data, size);
    return size;
}

uint32_t Packer::CopyBits(
    uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
    uint8_t *in, uint32_t in_size, uint32_t in_bit_offset)
{
    CIX_VAAPI_DEBUG("Copy bits: out_size = %d, out_bit_offset = %d, in_size = %d, in_bit_offset = %d\n",
        out_size, out_bit_offset, in_size, in_bit_offset);
    //! TODO: to optimize for performance
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
        uint32_t copy_bits = (in_size << 3) - in_bit_offset;
        if (copy_bits <= 8 - dst_bit_offset) {
            src_word <<= src_bit_offset;
            src_word >>= (8 + dst_bit_offset);
            src_word &= (0xff >> (8 - copy_bits - dst_bit_offset)); // add cabac_alignment_one_bit(s)
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
            copy_bits -= (8 - dst_bit_offset);

            copy_bytes = (copy_bits + 7) >> 3;

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
            shift_bits = (copy_bits & 7);
            if (shift_bits == 0)
                shift_bits = 8;
            dst[copy_bytes-1] |= 0xff >> shift_bits;

            dst += copy_bytes;
        }
    }

    return (uint32_t)(dst - out);
}

// Copy bits from `in` (starting at `in_bit_offset`) to `out` (starting at `out_bit_offset`)
// without the H.264-specific cabac_alignment_one_bit trailing fill. Bits in the final
// destination byte that fall outside the copied range are cleared to 0 so the destination
// buffer remains a faithful bit-for-bit continuation of the source. Use this for HEVC SPS
// rewriting where appending 1-bits would corrupt rbsp_trailing_bits / following fields.
uint32_t Packer::CopyBitsRaw(
    uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
    uint8_t *in, uint32_t in_size, uint32_t in_bit_offset)
{
    CIX_VAAPI_DEBUG("Copy bits raw: out_size = %d, out_bit_offset = %d, in_size = %d, in_bit_offset = %d\n",
        out_size, out_bit_offset, in_size, in_bit_offset);

    uint32_t total_in_bits = in_size << 3;
    if (in_bit_offset >= total_in_bits)
        return (out_bit_offset + 7) >> 3;

    uint32_t copy_bits = total_in_bits - in_bit_offset;
    uint32_t out_bit_end = out_bit_offset + copy_bits;
    uint32_t out_byte_end = (out_bit_end + 7) >> 3;
    if (out_byte_end > out_size) {
        CIX_VAAPI_ERROR("CopyBitsRaw: destination too small: need %u bytes, have %u\n",
            out_byte_end, out_size);
        return 0;
    }

    for (uint32_t i = 0; i < copy_bits; i++) {
        uint32_t src_bit_index = in_bit_offset + i;
        uint32_t dst_bit_index = out_bit_offset + i;
        uint8_t bit = (in[src_bit_index >> 3] >> (7 - (src_bit_index & 7))) & 1;
        uint8_t dst_mask = (uint8_t)(1u << (7 - (dst_bit_index & 7)));
        if (bit)
            out[dst_bit_index >> 3] |= dst_mask;
        else
            out[dst_bit_index >> 3] &= (uint8_t)~dst_mask;
    }

    // Clear any bits past the last copied bit in the final byte (avoid trailing garbage).
    uint32_t tail_bits = out_bit_end & 7;
    if (tail_bits != 0) {
        uint8_t keep_mask = (uint8_t)(0xff << (8 - tail_bits));
        out[out_byte_end - 1] &= keep_mask;
    }

    return out_byte_end;
}
