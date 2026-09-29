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
#include "extractor_hevc.h"

ExtractorHEVC::ExtractorHEVC() :
    seq_param(),
    pic_param(),
    slc_param(),
    qp(26)
{
    SetFormat(EXTRACTOR_HEVC);
}

void ExtractorHEVC::SetSequenceParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncSequenceParameterBufferHEVC))
        seq_param = *(VAEncSequenceParameterBufferHEVC *)data;
    else
        CIX_VAAPI_ERROR("Invalid sequence parameter buffer size: %d\n", size);
}

void ExtractorHEVC::SetPictureParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncPictureParameterBufferHEVC))
        pic_param = *(VAEncPictureParameterBufferHEVC *)data;
    else
        CIX_VAAPI_ERROR("Invalid picture parameter buffer size: %d\n", size);
}

void ExtractorHEVC::SetSliceParameter(void *data, uint32_t size)
{
    if (size == sizeof(VAEncSliceParameterBufferHEVC)) {
        slc_param = *(VAEncSliceParameterBufferHEVC *)data;
        qp = pic_param.pic_init_qp + slc_param.slice_qp_delta;
    } else {
        CIX_VAAPI_ERROR("Invalid slice parameter buffer size: %d\n", size);
    }
}

uint32_t ExtractorHEVC::GetCodedBufferID()
{
    return pic_param.coded_buf;
}

uint32_t ExtractorHEVC::GetBitrate()
{
    return seq_param.bits_per_second;
}
