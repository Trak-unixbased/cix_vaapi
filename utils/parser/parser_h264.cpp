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
#include "parser_h264.h"
#include "parser_bsr.h"

#define H264_NALU_HEADER_BITS 8

ParserH264::ParserH264() : Parser(),
    pic_parameter_set_id(-1)
{
}

void ParserH264::ParseSliceHeader(void *data, uint32_t size)
{
    CIX_VAAPI_CHECK_RETURN(data != nullptr && size > 0,
        "Invalid data buffer: data = %p, size = %d\n", data, size);

    CixBitReader gb;
    int ret = cix_br_init_bytes(&gb, (uint8_t *)data, size);
    CIX_VAAPI_CHECK_RETURN(ret >= 0, "Failed to initialize stream reader.\n");
    cix_br_skip_bits(&gb, H264_NALU_HEADER_BITS);
    cix_eg_read_ue(&gb); // first_mb_in_slice
    cix_eg_read_ue(&gb); // slice_type
    pic_parameter_set_id = cix_eg_read_ue(&gb);
}
