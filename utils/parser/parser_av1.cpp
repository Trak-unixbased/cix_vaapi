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
#include "parser_av1.h"

ParserAV1::ParserAV1() : pic_valid(false)
{
    memset(&pic_param, 0, sizeof(pic_param));
    memset(&seq_header, 0, sizeof(seq_header));
}

VASurfaceID ParserAV1::GetRenderTargetSurfaceId(VASurfaceID render_target) const
{
    if (!pic_valid)
        return render_target;
    if (pic_param.film_grain_info.film_grain_info_fields.bits.apply_grain)
        return pic_param.current_display_picture;
    return pic_param.current_frame;
}

void ParserAV1::ResetPictureState()
{
    pic_valid = false;
}

void ParserAV1::SetPictureParameter(void *data, uint32_t size)
{
    if (!data || size != sizeof(VADecPictureParameterBufferAV1))
        return;
    pic_param = *(VADecPictureParameterBufferAV1 *)data;
    pic_valid = true;
}

void *ParserAV1::ParseSPS(void *data, uint32_t size)
{
    if (!data || size == 0)
        return nullptr;

    seq_header.data = data;
    seq_header.size = size;
    seq_header.sps_seq_parameter_set_id = 0;
    seq_header.has_cropping = false;
    memset(&seq_header.bit_offset, 0, sizeof(seq_header.bit_offset));
    return &seq_header;
}

const void *ParserAV1::GetPpsBuffer(uint32_t &size)
{
    size = 0;
#if (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
    if (!pic_valid)
        return nullptr;
    const VADecPictureParameterBufferAV1Ext *ext =
        va_get_dec_picture_parameter_buffer_av1_ext_buffer(&pic_param);
    if (!ext)
        return nullptr;
    const uint32_t n = ext->u.buffer.buffer_size;
    if (n == 0)
        return nullptr;
    size = n;
    return (const void *)ext->u.buffer.buffer_address;
#else
    return nullptr;
#endif  // (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
}

void ParserAV1::ParseSliceHeader(void *data, uint32_t size)
{
    (void)data;
    (void)size;
}
