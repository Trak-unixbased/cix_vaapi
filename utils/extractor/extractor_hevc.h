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

#ifndef EXTRACTOR_HEVC_H_
#define EXTRACTOR_HEVC_H_

#include "device_common.h"
#include "extractor.h"

class ExtractorHEVC : public Extractor {
public:
    ExtractorHEVC();
    ~ExtractorHEVC() {};

    virtual void SetSequenceParameter(void *data, uint32_t size);
    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual uint32_t GetCodedBufferID();
    virtual uint32_t GetBitrate() override;
    virtual uint32_t GetFrameRateDenominator() override { return seq_param.vui_time_scale; }
    virtual uint32_t GetFrameRateNumerator() override { return seq_param.vui_num_units_in_tick; }
    virtual uint32_t GetGopSize() override { return seq_param.intra_period; }
    virtual uint32_t GetBFrames() override { return seq_param.ip_period - 1; }
    virtual uint32_t GetQP() override { return qp; }
    virtual uint32_t GetCodingType() override { return pic_param.pic_fields.bits.coding_type; }
    virtual uint8_t GetTier() override { return seq_param.general_tier_flag; }
    virtual uint8_t GetLevel() override { return seq_param.general_level_idc / 3; }

private:
    VAEncSequenceParameterBufferHEVC seq_param;
    VAEncPictureParameterBufferHEVC pic_param;
    VAEncSliceParameterBufferHEVC slc_param;
    uint32_t qp;
};

#endif  // EXTRACTOR_HEVC_H_