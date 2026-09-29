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

#ifndef EXTRACTOR_H264_H_
#define EXTRACTOR_H264_H_

#include "device_common.h"
#include "extractor.h"
#include "slice.h"

class ExtractorH264 : public Extractor {
public:
    ExtractorH264();
    ~ExtractorH264() {};

    virtual void SetSequenceParameter(void *data, uint32_t size);
    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual uint32_t GetCodedBufferID();
    virtual uint32_t GetBitrate() override;
    virtual uint32_t GetFrameRateDenominator() override { return seq_param.time_scale; }
    virtual uint32_t GetFrameRateNumerator() override { return seq_param.num_units_in_tick; }
    virtual uint32_t GetGopSize() override { return seq_param.intra_period; }
    virtual uint32_t GetBFrames() override { return seq_param.ip_period - 1; }
    virtual uint32_t GetQP() override { return qp; }
    virtual uint32_t GetCodingType() override;
    virtual uint8_t GetLevel() override { return seq_param.level_idc; }

private:
    VAEncSequenceParameterBufferH264 seq_param;
    VAEncPictureParameterBufferH264 pic_param;
    VAEncSliceParameterBufferH264 slc_param;
    uint32_t qp;
};

#endif  // EXTRACTOR_H264_H_