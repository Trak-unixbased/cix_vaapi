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

#ifndef PACKER_H_
#define PACKER_H_

#include <stdint.h>
#include "packer_bsw.h"

// Minimum room kept free in the bitstream buffer before a parameter set
// (VPS/SPS/PPS) is generated into it.
#define PACKER_MIN_NALU_SPACE 64

class Packer {
public:
    Packer();
    // Virtual: packers are deleted through a Packer pointer, the derived
    // destructors (and their std::vector members) were never run.
    virtual ~Packer() {}

    virtual void SetPictureParameter(void *data, uint32_t size) = 0;
    virtual void *GetPictureParameter() = 0;
    virtual int32_t SetIQMatrix(void *data, uint32_t size) = 0;
    virtual void SetSliceParameter(void *data, uint32_t size) = 0;
    virtual uint32_t SaveSliceData(void *dst, void *data, uint32_t size);
    virtual void PackVpsNalu(void *out, uint32_t out_size, uint32_t &offset) { (void)out; (void)out_size; (void)offset; }
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset) = 0;
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id) = 0;
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param) = 0;
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size) = 0;
    virtual uint32_t CopySPS(void *out, uint32_t out_size, void *seq_header) { (void)out; (void)out_size; (void)seq_header; return 0; }
    virtual uint32_t CopyPPS(void *out, uint32_t out_size, void *in, uint32_t in_size) { (void)out; (void)out_size; (void)in; (void)in_size; return 0; }
    // Returns the number of bytes written, 0 when the slice does not fit.
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice) = 0;
    virtual int32_t CheckIntegrity() { return 0; }
    // Inserts emulation prevention bytes. Never writes more than out_size bytes.
    uint32_t AddEmulationPrevention(uint8_t *out, uint32_t out_size, uint8_t *in, uint32_t in_size);
    void SetCache(void *data, uint32_t size) { cache = data; cache_size = size; }
    void GetCache(void **data, uint32_t *size) { *data = cache; *size = cache_size; }
    uint32_t CopyBits(
        uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
        uint8_t *in, uint32_t in_size, uint32_t in_bit_offset);
    uint32_t CopyBitsRaw(
        uint8_t *out, uint32_t out_size, uint32_t out_bit_offset,
        uint8_t *in, uint32_t in_size, uint32_t in_bit_offset);

protected:
    // True when a parameter set can be generated at "offset".
    static bool HasRoom(uint32_t out_size, uint32_t offset) {
        return offset < out_size && out_size - offset >= PACKER_MIN_NALU_SPACE;
    }

private:
    void *cache;
    uint32_t cache_size;
};

#endif  // PACKER_H_
