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

#ifndef EXTRACTOR_H_
#define EXTRACTOR_H_

#include <stdint.h>

#define CODING_TYPE_I 1
#define CODING_TYPE_P 2
#define CODING_TYPE_B 3

#define EXTRACTOR_H264 0
#define EXTRACTOR_HEVC 1

class Extractor {
public:
    Extractor();
    ~Extractor() {};

    virtual void SetSequenceParameter(void *data, uint32_t size) {};
    virtual void SetPictureParameter(void *data, uint32_t size) = 0;
    virtual void SetSliceParameter(void *data, uint32_t size) = 0;
    virtual uint32_t GetCodedBufferID() = 0;
    virtual uint32_t GetBitrate() { return 0; }
    virtual uint32_t GetFrameRateDenominator() { return 1; }
    virtual uint32_t GetFrameRateNumerator() { return 30; }
    virtual uint32_t GetGopSize() { return -1; }
    virtual uint32_t GetBFrames() { return 0; }
    virtual uint32_t GetQP() { return 26; }
    virtual uint32_t GetCodingType() { return CODING_TYPE_I; }
    virtual uint8_t GetTier() { return 0; }
    virtual uint8_t GetLevel() { return 0; }
    void SetFormat(uint32_t fmt) { format = fmt; }
    uint32_t GetFormat() { return format; }

private:
    void *cache;
    uint32_t cache_size;
    uint32_t format;
};

#endif  // EXTRACTOR_H_