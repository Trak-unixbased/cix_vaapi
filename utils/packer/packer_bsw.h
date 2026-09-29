/*
 * packer_bsw.h: sequential bit buffer writer for parameter NAL packing
 *
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

#ifndef CIX_VAAPI_PACKER_BSW_H
#define CIX_VAAPI_PACKER_BSW_H

#include <stdint.h>

#define CIX_BSW_INLINE_ALWAYS __attribute__((always_inline)) inline
#define CIX_BSW_MAY_ALIAS __attribute__((may_alias))
#define CIX_BSW_PTRW sizeof(void *)
#define CIX_BSW_LOAD_U32_AS_WORD(ptr) (((cix_bsw_u32_alias *)(ptr))->word)

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define CIX_BSW_HOST_BE 1
#else
#define CIX_BSW_HOST_BE 0
#endif

typedef union {
    uint32_t word;
    uint16_t halves[2];
    uint8_t bytes[4];
} CIX_BSW_MAY_ALIAS cix_bsw_u32_alias;

typedef struct {
    uint16_t bit_count;
    uint8_t span;
    uint8_t next_index;
} cix_bsw_vlc_large;

typedef struct cix_bsw_ctx {
    uint8_t *base;
    uint8_t *ptr;
    uint8_t *end;

    uintptr_t pending;
    int free_bits;
    int rate_distortion_bits;
} cix_bsw_ctx;

static CIX_BSW_INLINE_ALWAYS uint32_t cix_bsw_bswap32(uint32_t v)
{
    return (v << 24) + ((v << 8) & 0xff0000) + ((v >> 8) & 0xff00) + (v >> 24);
}

static CIX_BSW_INLINE_ALWAYS uint64_t cix_bsw_bswap64(uint64_t v)
{
    return cix_bsw_bswap32((uint32_t)(v >> 32)) + ((uint64_t)cix_bsw_bswap32((uint32_t)v) << 32);
}

static CIX_BSW_INLINE_ALWAYS uintptr_t cix_bsw_bswap_word(uintptr_t v)
{
    return CIX_BSW_PTRW == 8 ? cix_bsw_bswap64(v) : cix_bsw_bswap32((uint32_t)v);
}

static inline void cix_bsw_init(cix_bsw_ctx *w, void *data, int byte_len)
{
    int misalign = ((intptr_t)data & 3);
    w->ptr = w->base = (uint8_t *)data - misalign;
    w->end = (uint8_t *)data + byte_len;
    w->free_bits = (CIX_BSW_PTRW - misalign) * 8;
    if (misalign) {
        w->pending = cix_bsw_bswap32(CIX_BSW_LOAD_U32_AS_WORD(w->ptr));
        w->pending >>= (4 - misalign) * 8;
    } else
        w->pending = 0;
}

static inline int cix_bsw_bit_position(cix_bsw_ctx *w)
{
    return 8 * (w->ptr - w->base) + (int)(CIX_BSW_PTRW * 8) - w->free_bits;
}

/* Commit pending bits; stream may no longer be 32-bit aligned at ptr. */
static inline void cix_bsw_flush_bits(cix_bsw_ctx *w)
{
    CIX_BSW_LOAD_U32_AS_WORD(w->ptr) = cix_bsw_bswap32((uint32_t)(w->pending << (w->free_bits & 31)));
    w->ptr += CIX_BSW_PTRW - (w->free_bits >> 3);
    w->free_bits = (int)(CIX_BSW_PTRW * 8);
}

/* Inverse of cix_bsw_flush_bits: reload aligned word state from ptr. */
static inline void cix_bsw_resume_align(cix_bsw_ctx *w)
{
    int misalign = ((intptr_t)w->ptr & 3);
    if (misalign) {
        w->ptr = (uint8_t *)w->ptr - misalign;
        w->free_bits = (int)((CIX_BSW_PTRW - misalign) * 8);
        w->pending = cix_bsw_bswap32(CIX_BSW_LOAD_U32_AS_WORD(w->ptr));
        w->pending >>= (4 - misalign) * 8;
    }
}

static inline void cix_bsw_write(cix_bsw_ctx *w, int n_bits, uint32_t bits)
{
    if (CIX_BSW_PTRW == 8) {
        w->pending = (w->pending << n_bits) | bits;
        w->free_bits -= n_bits;
        if (w->free_bits <= 32) {
#if CIX_BSW_HOST_BE
            CIX_BSW_LOAD_U32_AS_WORD(w->ptr) = (uint32_t)(w->pending >> (32 - w->free_bits));
#else
            CIX_BSW_LOAD_U32_AS_WORD(w->ptr) = (uint32_t)cix_bsw_bswap_word(w->pending << w->free_bits);
#endif
            w->free_bits += 32;
            w->ptr += 4;
        }
    } else {
        if (n_bits < w->free_bits) {
            w->pending = (w->pending << n_bits) | bits;
            w->free_bits -= n_bits;
        } else {
            n_bits -= w->free_bits;
            w->pending = (w->pending << w->free_bits) | (bits >> n_bits);
            CIX_BSW_LOAD_U32_AS_WORD(w->ptr) = (uint32_t)cix_bsw_bswap_word(w->pending);
            w->ptr += 4;
            w->pending = bits;
            w->free_bits = 32 - n_bits;
        }
    }
}

/* Two 16-bit writes; avoids extra branch in cix_bsw_write for 32-bit payloads. */
static inline void cix_bsw_write_u32_pair(cix_bsw_ctx *w, uint32_t bits)
{
    cix_bsw_write(w, 16, bits >> 16);
    cix_bsw_write(w, 16, bits);
}

static inline void cix_bsw_write_bit(cix_bsw_ctx *w, uint32_t one_bit)
{
    w->pending <<= 1;
    w->pending |= one_bit;
    w->free_bits--;
    if (w->free_bits == (int)(CIX_BSW_PTRW * 8 - 32)) {
        CIX_BSW_LOAD_U32_AS_WORD(w->ptr) = cix_bsw_bswap32((uint32_t)w->pending);
        w->ptr += 4;
        w->free_bits = (int)(CIX_BSW_PTRW * 8);
    }
}

static inline void cix_bsw_align_fill_zero(cix_bsw_ctx *w)
{
    cix_bsw_write(w, w->free_bits & 7, 0);
    cix_bsw_flush_bits(w);
}

static inline void cix_bsw_align_fill_one(cix_bsw_ctx *w)
{
    cix_bsw_write(w, w->free_bits & 7, (1 << (w->free_bits & 7)) - 1);
    cix_bsw_flush_bits(w);
}

static inline void cix_bsw_align_one_zero(cix_bsw_ctx *w)
{
    if (w->free_bits & 7)
        cix_bsw_write(w, w->free_bits & 7, 1u << ((w->free_bits & 7) - 1));
    cix_bsw_flush_bits(w);
}

static const uint8_t cix_bsw_ue_len_lut[256] = {
    1,  1,  3,  3,  5,  5,  5,  5,  7,  7,  7,  7,  7,  7,  7,  7,
    9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
};

static inline void cix_bsw_put_ue_wide(cix_bsw_ctx *w, unsigned int val)
{
    int len = 0;
    int tmp = (int)++val;
    if (tmp >= 0x10000) {
        len = 32;
        tmp >>= 16;
    }
    if (tmp >= 0x100) {
        len += 16;
        tmp >>= 8;
    }
    len += cix_bsw_ue_len_lut[tmp];
    cix_bsw_write(w, len >> 1, 0);
    cix_bsw_write(w, (len >> 1) + 1, (uint32_t)val);
}

/* Only correct when val+1 fits the lookup table (value under 255). */
static inline void cix_bsw_put_ue_fast(cix_bsw_ctx *w, int val)
{
    cix_bsw_write(w, cix_bsw_ue_len_lut[val + 1], (uint32_t)(val + 1));
}

static inline void cix_bsw_write_se(cix_bsw_ctx *w, int val)
{
    int len = 0;
    int tmp = 1 - val * 2;
    if (tmp < 0)
        tmp = val * 2;
    val = tmp;

    if (tmp >= 0x100) {
        len = 16;
        tmp >>= 8;
    }
    len += cix_bsw_ue_len_lut[tmp];
    cix_bsw_write(w, len, (uint32_t)val);
}

static inline void cix_bsw_put_te(cix_bsw_ctx *w, int range, int val)
{
    if (range == 1)
        cix_bsw_write_bit(w, (uint32_t)(1 ^ val));
    else
        cix_bsw_put_ue_fast(w, val);
}

static inline void cix_bsw_write_rbsp_trailing(cix_bsw_ctx *w)
{
    cix_bsw_write_bit(w, 1);
    cix_bsw_write(w, w->free_bits & 7, 0);
}

static CIX_BSW_INLINE_ALWAYS int cix_bsw_ue_bit_length(unsigned int val)
{
    return cix_bsw_ue_len_lut[val + 1];
}

static CIX_BSW_INLINE_ALWAYS int cix_bsw_ue_bit_length_wide(unsigned int val)
{
    if (val < 255)
        return cix_bsw_ue_len_lut[val + 1];
    else
        return cix_bsw_ue_len_lut[(val + 1) >> 8] + 16;
}

static CIX_BSW_INLINE_ALWAYS int cix_bsw_se_bit_length(int val)
{
    int tmp = 1 - val * 2;
    if (tmp < 0)
        tmp = val * 2;
    if (tmp < 256)
        return cix_bsw_ue_len_lut[tmp];
    else
        return cix_bsw_ue_len_lut[tmp >> 8] + 16;
}

static CIX_BSW_INLINE_ALWAYS int cix_bsw_te_bit_length(int range, int val)
{
    if (range == 1)
        return 1;
    else
        return cix_bsw_ue_len_lut[val + 1];
}

#endif /* CIX_VAAPI_PACKER_BSW_H */
