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
#include <va/va_backend.h>
#include "extractor_h264.h"
#include "slice.h"

#define SLICE_TYPE_I 2
#define SLICE_TYPE_P 0
#define SLICE_TYPE_B 1

ExtractorH264::ExtractorH264() :
    seq_param(),
    pic_param(),
    slc_param(),
    qp(26)
{
    SetFormat(EXTRACTOR_H264);
}

void ExtractorH264::SetSequenceParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncSequenceParameterBufferH264))
        seq_param = *(VAEncSequenceParameterBufferH264 *)data;
    else
        CIX_VAAPI_ERROR("Invalid sequence parameter buffer size: %d\n", size);
}

void ExtractorH264::SetPictureParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncPictureParameterBufferH264))
        pic_param = *(VAEncPictureParameterBufferH264 *)data;
    else
        CIX_VAAPI_ERROR("Invalid picture parameter buffer size: %d\n", size);
}

void ExtractorH264::SetSliceParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncSliceParameterBufferH264)) {
        slc_param = *(VAEncSliceParameterBufferH264 *)data;
        qp = pic_param.pic_init_qp + slc_param.slice_qp_delta;
    } else {
        CIX_VAAPI_ERROR("Invalid slice parameter buffer size: %d\n", size);
    }
}

uint32_t ExtractorH264::GetCodedBufferID()
{
    return pic_param.coded_buf;
}

uint32_t ExtractorH264::GetBitrate()
{
    return seq_param.bits_per_second;
}

uint32_t ExtractorH264::GetCodingType()
{
    uint32_t slice_type = slc_param.slice_type >= 5 ?
                slc_param.slice_type - 5 : slc_param.slice_type;

    if (slice_type == SLICE_TYPE_I)
        return CODING_TYPE_I;
    else if (slice_type == SLICE_TYPE_P)
        return CODING_TYPE_P;
    else if (slice_type == SLICE_TYPE_B)
        return CODING_TYPE_B;
    else
        return CODING_TYPE_I;  // default to I frame for unknown slice type
}
