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

#ifndef PACKER_AV1_H_
#define PACKER_AV1_H_

#include <va/va.h>
#include <va/va_dec_av1.h>
#include <vector>
#include "packer.h"

/**
 * Assembles one AV1 temporal unit for V4L2 OUTPUT: temporal delimiter OBU
 * (PackSpsNalu), sequence OBU bytes from the client (CopySPS), frame-header OBU
 * from the VA picture extension (CopyPPS), tile payloads with size prefixes where
 * required (CopySlice), optional show_existing_frame OBU (PackRepeatFrame).
 */
class PackerAV1 : public Packer {
public:
    PackerAV1();
    ~PackerAV1() {}

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *GetPictureParameter() { return &pic_param; }
    virtual int32_t SetIQMatrix(void *data, uint32_t size);
    virtual void SetSliceParameter(void *data, uint32_t size);
    virtual uint32_t SaveSliceData(void *dst, void *data, uint32_t size);
    virtual void PackSpsNalu(void *out, uint32_t out_size, uint32_t &offset);
    virtual void PackPpsNalu(void *out, uint32_t out_size, uint32_t &offset, int32_t sps_id, int32_t pps_id);
    virtual void PackSliceNalu(void *out, uint32_t out_size, uint32_t &offset, void *in, uint32_t in_size, void *slice_param);
    virtual void PackFrame(void *in, uint32_t in_size, void *out, uint32_t out_size);
    virtual uint32_t CopySPS(void *out, uint32_t out_size, void *seq_header);
    virtual uint32_t CopyPPS(void *out, uint32_t out_size, void *in, uint32_t in_size);
    virtual uint32_t CopySlice(void *out, uint32_t out_size, void *in, uint32_t in_size, bool first_slice);
    virtual int32_t CheckIntegrity();

private:
    uint32_t PackRepeatFrame(void *buffer, uint32_t buffer_size);

    VADecPictureParameterBufferAV1 pic_param;
    std::vector<VASliceParameterBufferAV1> slice_params;
    bool pic_valid;
    bool td_obu_inserted;
    uint32_t tiles_copied;
};

#endif  // PACKER_AV1_H_
