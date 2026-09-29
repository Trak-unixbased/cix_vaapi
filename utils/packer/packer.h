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

#ifndef PACKER_H_
#define PACKER_H_

#include <stdint.h>
#include "packer_bsw.h"

class Packer {
public:
    Packer();
    ~Packer() {};

    virtual void SetPictureParameter(void *data, uint32_t size) = 0;
    virtual void *GetPictureParameter() = 0;
    virtual int32_t SetIQMatrix(void *data, uint32_t size) = 0;
    virtual void SetSliceParameter(void *data, uint32_t size) = 0;
    virtual uint32_t SaveSliceData(void *dst, void *data, uint32_t size);
    virtual void PackVpsNalu(void *out, uint32_t out_size, uint32_t &offset) {};
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset) = 0;
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id) = 0;
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param) = 0;
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size) = 0;
    virtual uint32_t CopySPS(void *out, uint32_t out_size, void *seq_header) { return 0; };
    virtual uint32_t CopyPPS(void *out, uint32_t out_size, void *in, uint32_t in_size) { return 0; };
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice) = 0;
    virtual int32_t CheckIntegrity() { return 0; };
    uint32_t AddEmulationPrevention(uint8_t *out, uint32_t out_size, uint8_t *in, uint32_t in_size);
    void SetCache(void *data, uint32_t size) { cache = data; cache_size = size; }
    void GetCache(void **data, uint32_t *size) { *data = cache; *size = cache_size; }
    uint32_t CopyBits(
        uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
        uint8_t *in, uint32_t in_size, uint32_t in_bit_offset);
    uint32_t CopyBitsRaw(
        uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
        uint8_t *in, uint32_t in_size, uint32_t in_bit_offset);

private:
    void *cache;
    uint32_t cache_size;
};

#endif  // PACKER_H_