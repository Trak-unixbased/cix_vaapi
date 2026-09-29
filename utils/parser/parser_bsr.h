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

/**
 * @file parser_bsr.h
 * Bitstream reader, floor log2, exponential-Golomb and related VLC readers.
 */

#ifndef PARSER_BSR_H
#define PARSER_BSR_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#define CIX_IM_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CIX_IM_MIN(a, b) ((a) > (b) ? (b) : (a))

#define CIX_MK_TAG_U32(a, b, c, d) \
    ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#define CIX_ERR_TAG(a, b, c, d) (-(int)CIX_MK_TAG_U32(a, b, c, d))
#define CIX_ERR_INVALID_BITSTREAM CIX_ERR_TAG('I', 'N', 'D', 'A')

#ifdef __GNUC__
#    define CIX_CC_GNU_GE(x, y) \
        (__GNUC__ > (x) || __GNUC__ == (x) && __GNUC_MINOR__ >= (y))
#    define CIX_CC_GNU_LE(x, y) \
        (__GNUC__ < (x) || __GNUC__ == (x) && __GNUC_MINOR__ <= (y))
#else
#    define CIX_CC_GNU_GE(x, y) 0
#    define CIX_CC_GNU_LE(x, y) 0
#endif

#if CIX_CC_GNU_GE(3, 3) || defined(__clang__)
#    define cix_may_alias __attribute__((may_alias))
#else
#    define cix_may_alias
#endif

#ifndef cix_force_inline
#if CIX_CC_GNU_GE(3, 1)
#    define cix_force_inline __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#    define cix_force_inline __forceinline
#else
#    define cix_force_inline inline
#endif
#endif

#if CIX_CC_GNU_GE(2, 6) || defined(__clang__)
#    define cix_const_fn __attribute__((const))
#else
#    define cix_const_fn
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define cix_unused __attribute__((unused))
#else
#    define cix_unused
#endif

#if defined(ASSERT_LEVEL) && ASSERT_LEVEL > 1
#include <assert.h>
#define cix_dbg_assert2(cond) assert(cond)
#define cix_dbg_assert2_fpu() ((void)0)
#else
#define cix_dbg_assert2(cond) ((void)0)
#define cix_dbg_assert2_fpu() ((void)0)
#endif

union cix_unaligned_u64 {
    uint64_t w;
} __attribute__((packed)) cix_may_alias;
union cix_unaligned_u32 {
    uint32_t w;
} __attribute__((packed)) cix_may_alias;

#define CIX_RD_RAW_32(p) (((const union cix_unaligned_u32 *)(p))->w)
#define CIX_RD_RAW_64(p) (((const union cix_unaligned_u64 *)(p))->w)

#define CIX_BSWAP16_CONST(x) (((x) << 8 & 0xff00) | ((x) >> 8 & 0x00ff))
#define CIX_BSWAP32_CONST(x) (CIX_BSWAP16_CONST(x) << 16 | CIX_BSWAP16_CONST((x) >> 16))

#define CIX_RD_BE32(p) CIX_BSWAP32_CONST(CIX_RD_RAW_32(p))
#define CIX_RD_LE32(p) CIX_RD_RAW_32(p)

typedef struct CixBitReader {
    const uint8_t *bytes_ptr;
    const uint8_t *bytes_end;
    int bit_index;
    int bit_length;
    int bit_length_plus8;
} CixBitReader;

static inline unsigned int cix_br_read_u(CixBitReader *s, int n);
static inline void cix_br_skip(CixBitReader *s, int n);
static inline unsigned int cix_br_peek_u(CixBitReader *s, int n);

#define CIX_BR_CACHE_MIN_BITS 25

#define CIX_BR_OPEN_RAW(name, br)               \
    unsigned int name##_bit_index = (br)->bit_index; \
    unsigned int cix_unused name##_word

#define CIX_BR_OPEN(name, br) CIX_BR_OPEN_RAW(name, br)

#define CIX_BR_HAS_DATA(name, br) 1

#define CIX_BR_CLOSE(name, br) (br)->bit_index = name##_bit_index

#define CIX_BR_REFILL_LE(name, br)                                        \
    name##_word = CIX_RD_LE32((br)->bytes_ptr + (name##_bit_index >> 3)) >> \
                 (name##_bit_index & 7)

#define CIX_BR_REFILL_BE(name, br)                                        \
    name##_word = CIX_RD_BE32((br)->bytes_ptr + (name##_bit_index >> 3)) << \
                  (name##_bit_index & 7)

#define CIX_BR_REFILL(name, br) CIX_BR_REFILL_BE(name, br)

#define CIX_BR_CACHE_DROP(name, br, num) (name##_word <<= (num))

#define CIX_BR_IDX_ADVANCE(name, br, num) (name##_bit_index += (num))

#define CIX_BR_BITS_LEFT(name, br) \
    ((int)((br)->bit_length - name##_bit_index))

#define CIX_BR_SKIP_IN_CACHE(name, br, num) \
    do {                                    \
        CIX_BR_CACHE_DROP(name, br, num);   \
        CIX_BR_IDX_ADVANCE(name, br, num);  \
    } while (0)

#define CIX_BR_COMMIT_SKIP(name, br, num) CIX_BR_IDX_ADVANCE(name, br, num)

#define CIX_BR_PEEK_U_LE(name, br, num) cix_zero_extend(name##_word, num)
#define CIX_BR_PEEK_S_LE(name, br, num) cix_sign_extend(name##_word, num)

#define CIX_BR_PEEK_U_BE(name, br, num) cix_u32_top_bits(name##_word, num)
#define CIX_BR_PEEK_S_BE(name, br, num) cix_s32_top_bits(name##_word, num)

#define CIX_BR_PEEK_U(name, br, num) CIX_BR_PEEK_U_BE(name, br, num)
#define CIX_BR_PEEK_S(name, br, num) CIX_BR_PEEK_S_BE(name, br, num)

#define CIX_BR_CACHE_VAL(name, br) ((uint32_t)name##_word)

#define cix_u32_top_bits(a, s) (((uint32_t)(a)) >> (32 - (s)))
#define cix_s32_top_bits(a, s) (((int32_t)(a)) >> (32 - (s)))

#ifndef cix_sign_extend
static inline cix_const_fn int cix_sign_extend(int val, unsigned bits)
{
    unsigned shift = 8 * sizeof(int) - bits;
    union {
        unsigned u;
        int s;
    } v = {(unsigned)val << shift};
    return v.s >> shift;
}
#endif

#ifndef cix_zero_extend
static inline cix_const_fn unsigned cix_zero_extend(unsigned val, unsigned bits)
{
    return (val << ((8 * sizeof(int)) - bits)) >> ((8 * sizeof(int)) - bits);
}
#endif

static cix_force_inline cix_const_fn uint32_t cix_bswap32(uint32_t x)
{
    return CIX_BSWAP32_CONST(x);
}

static inline cix_const_fn uint64_t cix_bswap64(uint64_t x)
{
    return (uint64_t)cix_bswap32(x) << 32 | cix_bswap32(x >> 32);
}

static inline int cix_br_bit_pos(const CixBitReader *s)
{
    return s->bit_index;
}

static inline void cix_br_skip_bits(CixBitReader *s, int n)
{
    s->bit_index += n;
}

static inline int cix_br_read_xbits_be(CixBitReader *s, int n)
{
    register int sign;
    register int32_t word;
    CIX_BR_OPEN(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL(re, s);
    word = CIX_BR_CACHE_VAL(re, s);
    sign = ~word >> 31;
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
    return (cix_u32_top_bits(sign ^ word, n) ^ sign) - sign;
}

static inline int cix_br_read_xbits_le(CixBitReader *s, int n)
{
    register int sign;
    register int32_t word;
    CIX_BR_OPEN(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL_LE(re, s);
    word = CIX_BR_CACHE_VAL(re, s);
    sign = cix_sign_extend(~word, n) >> 31;
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
    return (cix_zero_extend(sign ^ word, n) ^ sign) - sign;
}

static inline int cix_br_read_s(CixBitReader *s, int n)
{
    register int tmp;
    CIX_BR_OPEN(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL(re, s);
    tmp = CIX_BR_PEEK_S(re, s, n);
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
    return tmp;
}

static inline unsigned int cix_br_read_u(CixBitReader *s, int n)
{
    register unsigned int tmp;
    CIX_BR_OPEN(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL(re, s);
    tmp = CIX_BR_PEEK_U(re, s, n);
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
    cix_dbg_assert2(tmp < UINT64_C(1) << n);
    return tmp;
}

static cix_force_inline int cix_br_read_u_z(CixBitReader *s, int n)
{
    return n ? cix_br_read_u(s, n) : 0;
}

static inline unsigned int cix_br_read_u_le(CixBitReader *s, int n)
{
    register int tmp;
    CIX_BR_OPEN(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL_LE(re, s);
    tmp = CIX_BR_PEEK_U_LE(re, s, n);
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
    return tmp;
}

static inline unsigned int cix_br_peek_u(CixBitReader *s, int n)
{
    register unsigned int tmp;
    CIX_BR_OPEN_RAW(re, s);
    cix_dbg_assert2(n > 0 && n <= 25);
    CIX_BR_REFILL(re, s);
    tmp = CIX_BR_PEEK_U(re, s, n);
    return tmp;
}

static inline void cix_br_skip(CixBitReader *s, int n)
{
    CIX_BR_OPEN(re, s);
    CIX_BR_COMMIT_SKIP(re, s, n);
    CIX_BR_CLOSE(re, s);
}

static inline unsigned int cix_br_read_flag(CixBitReader *s)
{
    unsigned int idx = s->bit_index;
    uint8_t b = s->bytes_ptr[idx >> 3];
    b <<= idx & 7;
    b >>= 8 - 1;
    idx++;
    s->bit_index = idx;
    return b;
}

static inline unsigned int cix_br_peek_flag(CixBitReader *s)
{
    return cix_br_peek_u(s, 1);
}

static inline void cix_br_skip_one(CixBitReader *s)
{
    cix_br_skip(s, 1);
}

static inline unsigned int cix_br_read_u_wide(CixBitReader *s, int n)
{
    cix_dbg_assert2(n >= 0 && n <= 32);
    if (!n) {
        return 0;
    } else if (n <= CIX_BR_CACHE_MIN_BITS) {
        return cix_br_read_u(s, n);
    } else {
        unsigned ret = cix_br_read_u(s, 16) << (n - 16);
        return ret | cix_br_read_u(s, n - 16);
    }
}

static inline uint64_t cix_br_read_u64(CixBitReader *s, int n)
{
    if (n <= 32) {
        return cix_br_read_u_wide(s, n);
    } else {
        uint64_t ret = (uint64_t)cix_br_read_u_wide(s, n - 32) << 32;
        return ret | cix_br_read_u_wide(s, 32);
    }
}

static inline int cix_br_read_s_wide(CixBitReader *s, int n)
{
    if (!n)
        return 0;
    return cix_sign_extend(cix_br_read_u_wide(s, n), n);
}

static inline unsigned int cix_br_peek_u_wide(CixBitReader *s, int n)
{
    if (n <= CIX_BR_CACHE_MIN_BITS) {
        return cix_br_peek_u(s, n);
    } else {
        CixBitReader copy = *s;
        return cix_br_read_u_wide(&copy, n);
    }
}

static inline int cix_br_init_ex(CixBitReader *s, const uint8_t *buffer,
                                 int bit_size, int is_le)
{
    int byte_count;
    int err = 0;
    (void)is_le;

    if (bit_size >= INT_MAX - CIX_IM_MAX(7, 64 * 8) || bit_size < 0 ||
        !buffer) {
        bit_size = 0;
        buffer = NULL;
        err = CIX_ERR_INVALID_BITSTREAM;
    }

    byte_count = (bit_size + 7) >> 3;

    s->bytes_ptr = buffer;
    s->bit_length = bit_size;
    s->bit_length_plus8 = bit_size + 8;
    s->bytes_end = buffer + byte_count;
    s->bit_index = 0;

    return err;
}

static inline int cix_br_init_bits(CixBitReader *s, const uint8_t *buffer,
                                   int bit_size)
{
    return cix_br_init_ex(s, buffer, bit_size, 0);
}

static inline int cix_br_init_bytes(CixBitReader *s, const uint8_t *buffer,
                                    int byte_size)
{
    if (byte_size > INT_MAX / 8 || byte_size < 0)
        byte_size = -1;
    return cix_br_init_bits(s, buffer, byte_size * 8);
}

static inline int cix_br_init_bytes_le(CixBitReader *s, const uint8_t *buffer,
                                       int byte_size)
{
    if (byte_size > INT_MAX / 8 || byte_size < 0)
        byte_size = -1;
    return cix_br_init_ex(s, buffer, byte_size * 8, 1);
}

static inline const uint8_t *cix_br_align_byte(CixBitReader *s)
{
    int n = -cix_br_bit_pos(s) & 7;
    if (n)
        cix_br_skip(s, n);
    return s->bytes_ptr + (s->bit_index >> 3);
}

static inline int cix_br_decode_trinary_012(CixBitReader *gb)
{
    int n;
    n = cix_br_read_flag(gb);
    if (n == 0)
        return 0;
    else
        return cix_br_read_flag(gb) + 1;
}

static inline int cix_br_decode_trinary_210(CixBitReader *gb)
{
    if (cix_br_read_flag(gb))
        return 0;
    else
        return 2 - cix_br_read_flag(gb);
}

static inline int cix_br_bits_left(CixBitReader *gb)
{
    return gb->bit_length - cix_br_bit_pos(gb);
}

static inline int cix_br_skip_ff_data_units(CixBitReader *gb)
{
    if (cix_br_bits_left(gb) <= 0)
        return CIX_ERR_INVALID_BITSTREAM;

    while (cix_br_read_flag(gb)) {
        cix_br_skip(gb, 8);
        if (cix_br_bits_left(gb) <= 0)
            return CIX_ERR_INVALID_BITSTREAM;
    }

    return 0;
}

extern const uint8_t cix_log2_lut[256];

#ifndef cix_log2_floor
#    define cix_log2_floor cix_log2_floor_inline
static cix_force_inline cix_const_fn int cix_log2_floor_inline(unsigned int v)
{
    int n = 0;
    if (v & 0xffff0000) {
        v >>= 16;
        n += 16;
    }
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += cix_log2_lut[v];
    return n;
}
#endif

#ifndef cix_log2_floor_u16
#    define cix_log2_floor_u16 cix_log2_floor_u16_inline
static cix_force_inline cix_const_fn int
cix_log2_floor_u16_inline(unsigned int v)
{
    int n = 0;
    if (v & 0xff00) {
        v >>= 8;
        n += 8;
    }
    n += cix_log2_lut[v];
    return n;
}
#endif

#if CIX_CC_GNU_GE(3, 4)
#    ifndef cix_parity_u32
#        define cix_parity_u32 __builtin_parity
#    endif
#endif

#define CIX_EG_INVALID 0x80000000

#define cix_golomb_suint unsigned
#define cix_golomb_u32 uint32_t

extern const uint8_t cix_exp_golomb_len_tab[512];
extern const uint8_t cix_exp_golomb_ue_tab[512];
extern const int8_t cix_exp_golomb_se_tab[512];

extern const uint8_t cix_interleaved_eg_len_tab[256];
extern const uint8_t cix_interleaved_eg_ue_tab[256];
extern const int8_t cix_interleaved_eg_se_tab[256];
extern const uint8_t cix_interleaved_dirac_aux_tab[256];

/**
 * Read an unsigned Exp-Golomb code in the range 0 to 8190.
 *
 * @returns the read value or a negative error code.
 */
static inline int cix_eg_read_ue(CixBitReader *br)
{
    unsigned int word;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    if (word >= (1 << 27)) {
        word >>= 32 - 9;
        CIX_BR_COMMIT_SKIP(re, br, cix_exp_golomb_len_tab[word]);
        CIX_BR_CLOSE(re, br);

        return cix_exp_golomb_ue_tab[word];
    } else {
        int lead = 2 * cix_log2_floor(word) - 31;
        CIX_BR_COMMIT_SKIP(re, br, 32 - lead);
        CIX_BR_CLOSE(re, br);
        if (lead < 7)
            return CIX_ERR_INVALID_BITSTREAM;
        word >>= lead;
        word--;

        return word;
    }
}

/**
 * Read an unsigned Exp-Golomb code in the range 0 to UINT32_MAX-1.
 */
static inline unsigned cix_eg_read_ue_wide(CixBitReader *br)
{
    unsigned buf, lead;

    buf = cix_br_peek_u_wide(br, 32);
    lead = 31 - cix_log2_floor(buf);
    cix_br_skip_bits(br, lead);

    return cix_br_read_u_wide(br, lead + 1) - 1;
}

/**
 * Read unsigned exp golomb code, constrained to a max of 31.
 * If the value encountered is not in 0..31, the return value
 * is outside the range 0..30.
 */
static inline int cix_eg_read_ue_lte31(CixBitReader *br)
{
    unsigned int word;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    word >>= 32 - 9;
    CIX_BR_COMMIT_SKIP(re, br, cix_exp_golomb_len_tab[word]);
    CIX_BR_CLOSE(re, br);

    return cix_exp_golomb_ue_tab[word];
}

static inline unsigned cix_eg_read_ue_interleaved(CixBitReader *br)
{
    uint32_t word;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    if (word & 0xAA800000) {
        word >>= 32 - 8;
        CIX_BR_COMMIT_SKIP(re, br, cix_interleaved_eg_len_tab[word]);
        CIX_BR_CLOSE(re, br);

        return cix_interleaved_eg_ue_tab[word];
    } else {
        unsigned ret = 1;

        do {
            word >>= 32 - 8;
            CIX_BR_COMMIT_SKIP(
                re, br,
                CIX_IM_MIN(cix_interleaved_eg_len_tab[word], 8));

            if (cix_interleaved_eg_len_tab[word] != 9) {
                ret <<= (cix_interleaved_eg_len_tab[word] - 1) >> 1;
                ret |= cix_interleaved_dirac_aux_tab[word];
                break;
            }
            ret = (ret << 4) | cix_interleaved_dirac_aux_tab[word];
            CIX_BR_REFILL(re, br);
            word = CIX_BR_CACHE_VAL(re, br);
        } while (ret < 0x8000000U && CIX_BR_HAS_DATA(re, br));

        CIX_BR_CLOSE(re, br);
        return ret - 1;
    }
}

static inline int cix_eg_read_te0(CixBitReader *br, int range)
{
    cix_dbg_assert2(range >= 1);

    if (range == 1)
        return 0;
    else if (range == 2)
        return cix_br_read_flag(br) ^ 1;
    else
        return cix_eg_read_ue(br);
}

static inline int cix_eg_read_te(CixBitReader *br, int range)
{
    cix_dbg_assert2(range >= 1);

    if (range == 2)
        return cix_br_read_flag(br) ^ 1;
    else
        return cix_eg_read_ue(br);
}

static inline int cix_eg_read_se(CixBitReader *br)
{
    unsigned int word;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    if (word >= (1 << 27)) {
        word >>= 32 - 9;
        CIX_BR_COMMIT_SKIP(re, br, cix_exp_golomb_len_tab[word]);
        CIX_BR_CLOSE(re, br);

        return cix_exp_golomb_se_tab[word];
    } else {
        int lead = cix_log2_floor(word), sign;
        CIX_BR_COMMIT_SKIP(re, br, 31 - lead);
        CIX_BR_REFILL(re, br);
        word = CIX_BR_CACHE_VAL(re, br);

        word >>= lead;

        CIX_BR_COMMIT_SKIP(re, br, 32 - lead);
        CIX_BR_CLOSE(re, br);

        sign = -(word & 1);
        word = ((word >> 1) ^ sign) - sign;

        return word;
    }
}

static inline int cix_eg_read_se_wide(CixBitReader *br)
{
    unsigned int buf = cix_eg_read_ue_wide(br);
    int sign = (buf & 1) - 1;
    return ((buf >> 1) ^ sign) + 1;
}

static inline int cix_eg_read_se_interleaved(CixBitReader *br)
{
    unsigned int word;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    if (word & 0xAA800000) {
        word >>= 32 - 8;
        CIX_BR_COMMIT_SKIP(re, br, cix_interleaved_eg_len_tab[word]);
        CIX_BR_CLOSE(re, br);

        return cix_interleaved_eg_se_tab[word];
    } else {
        int lead;
        CIX_BR_COMMIT_SKIP(re, br, 8);
        CIX_BR_REFILL(re, br);
        word |= 1 | (CIX_BR_CACHE_VAL(re, br) >> 8);

        if ((word & 0xAAAAAAAA) == 0)
            return CIX_EG_INVALID;

        for (lead = 31; (word & 0x80000000) == 0; lead--)
            word = (word << 2) - ((word << lead) >> (lead - 1)) + (word >> 30);

        CIX_BR_COMMIT_SKIP(re, br, 63 - 2 * lead - 8);
        CIX_BR_CLOSE(re, br);

        return (signed)(((((word << lead) >> lead) - 1) ^ -(word & 0x1)) + 1) >>
               1;
    }
}

static inline int cix_eg_dirac_read_se(CixBitReader *br)
{
    uint32_t ret = cix_eg_read_ue_interleaved(br);

    if (ret) {
        int sign = -cix_br_read_flag(br);
        ret = (ret ^ sign) - sign;
    }

    return ret;
}

static inline int cix_rice_read_u(CixBitReader *br, int k, int limit,
                                  int esc_len)
{
    unsigned int word;
    int lead;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    lead = cix_log2_floor(word);

    if (lead > 31 - limit) {
        cix_dbg_assert2(lead >= k);
        word >>= lead - k;
        word += (30U - lead) << k;
        CIX_BR_COMMIT_SKIP(re, br, 32 + k - lead);
        CIX_BR_CLOSE(re, br);

        return word;
    } else {
        CIX_BR_COMMIT_SKIP(re, br, limit);
        CIX_BR_REFILL(re, br);

        word = CIX_BR_PEEK_U(re, br, esc_len);

        CIX_BR_COMMIT_SKIP(re, br, esc_len);
        CIX_BR_CLOSE(re, br);

        return word + limit - 1;
    }
}

static inline int cix_rice_read_u_jpegls(CixBitReader *br, int k, int limit,
                                         int esc_len)
{
    unsigned int word;
    int lead;

    CIX_BR_OPEN(re, br);
    CIX_BR_REFILL(re, br);
    word = CIX_BR_CACHE_VAL(re, br);

    lead = cix_log2_floor(word);

    cix_dbg_assert2(k <= 31);

    if (lead - k >= 32 - CIX_BR_CACHE_MIN_BITS +
                       (CIX_BR_CACHE_MIN_BITS == 32) &&
        32 - lead < limit) {
        word >>= lead - k;
        word += (30U - lead) << k;
        CIX_BR_COMMIT_SKIP(re, br, 32 + k - lead);
        CIX_BR_CLOSE(re, br);

        return word;
    } else {
        int i;
        for (i = 0; i + CIX_BR_CACHE_MIN_BITS <= limit &&
                    CIX_BR_PEEK_U(re, br, CIX_BR_CACHE_MIN_BITS) == 0;
             i += CIX_BR_CACHE_MIN_BITS) {
            if (br->bit_length <= re_bit_index) {
                CIX_BR_CLOSE(re, br);
                return -1;
            }
            CIX_BR_COMMIT_SKIP(re, br, CIX_BR_CACHE_MIN_BITS);
            CIX_BR_REFILL(re, br);
        }
        for (; i < limit && CIX_BR_PEEK_U(re, br, 1) == 0; i++) {
            CIX_BR_SKIP_IN_CACHE(re, br, 1);
        }
        CIX_BR_COMMIT_SKIP(re, br, 1);
        CIX_BR_REFILL(re, br);

        if (i < limit - 1) {
            if (k) {
                if (k > CIX_BR_CACHE_MIN_BITS - 1) {
                    word = CIX_BR_PEEK_U(re, br, 16) << (k - 16);
                    CIX_BR_COMMIT_SKIP(re, br, 16);
                    CIX_BR_REFILL(re, br);
                    word |= CIX_BR_PEEK_U(re, br, k - 16);
                    CIX_BR_COMMIT_SKIP(re, br, k - 16);
                } else {
                    word = CIX_BR_PEEK_U(re, br, k);
                    CIX_BR_COMMIT_SKIP(re, br, k);
                }
            } else {
                word = 0;
            }

            word += ((cix_golomb_suint)i << k);
        } else if (i == limit - 1) {
            word = CIX_BR_PEEK_U(re, br, esc_len);
            CIX_BR_COMMIT_SKIP(re, br, esc_len);

            word++;
        } else {
            word = -1;
        }
        CIX_BR_CLOSE(re, br);
        return word;
    }
}

static inline int cix_rice_read_s(CixBitReader *br, int k, int limit,
                                  int esc_len)
{
    unsigned v = cix_rice_read_u(br, k, limit, esc_len);
    return (v >> 1) ^ -(v & 1);
}

static inline int cix_rice_read_s_flac(CixBitReader *br, int k, int limit,
                                       int esc_len)
{
    unsigned v = cix_rice_read_u_jpegls(br, k, limit, esc_len);
    return (v >> 1) ^ -(v & 1);
}

static inline unsigned int cix_rice_read_u_shorten(CixBitReader *br, int k)
{
    return cix_rice_read_u_jpegls(br, k, INT_MAX, 0);
}

static inline int cix_rice_read_s_shorten(CixBitReader *br, int k)
{
    int uvar = cix_rice_read_u_jpegls(br, k + 1, INT_MAX, 0);
    return (uvar >> 1) ^ -(uvar & 1);
}

#endif /* PARSER_BSR_H */
