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
#include "parser_vp9.h"
#include "log.h"

ParserVP9::ParserVP9() : pic_valid(false)
{
    memset(&pic_param, 0, sizeof(pic_param));
}

void ParserVP9::SetPictureParameter(void *data, uint32_t size)
{
    if (!data || size != sizeof(VADecPictureParameterBufferVP9))
        return;
    pic_param = *(VADecPictureParameterBufferVP9 *)data;
    pic_valid = true;
}

void ParserVP9::ParseSliceHeader(void *data, uint32_t size)
{
    if (!data || size < 2)
        return;

    CixBitReader br;
    if (cix_br_init_bytes(&br, (const uint8_t *)data, size) != 0)
        return;

    /* decode_frame_header() / libavcodec/vp9.c */
    unsigned int marker = cix_br_read_u(&br, 2);
    if (marker != 2) {
        CIX_VAAPI_WARNING("VP9: invalid frame marker %u\n", marker);
        return;
    }

    unsigned int profile = cix_br_read_u(&br, 1);
    profile |= cix_br_read_u(&br, 1) << 1;
    if (profile == 3)
        profile += cix_br_read_u(&br, 1);

    if (pic_valid && profile != pic_param.profile) {
        CIX_VAAPI_WARNING("VP9: profile mismatch (bitstream %u vs VA %u)\n",
            profile, (unsigned)pic_param.profile);
    }
}
