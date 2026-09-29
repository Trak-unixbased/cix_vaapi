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

#ifndef PARSER_AV1_H_
#define PARSER_AV1_H_

#include <va/va.h>
#include <va/va_dec_av1.h>
#include "parser.h"
#include "sequence.h"

/**
 * AV1 picture metadata for the V4L2 bitstream path.
 * ParseSPS records raw sequence OBU bytes in SequenceHeader for PackerAV1::CopySPS.
 * GetPpsBuffer returns frame-header OBU bytes when av1_seq uses VA_DEC_PIC_PARAM_AV1_EXT_MAGIC_BUFFER.
 */
class ParserAV1 : public Parser {
public:
    ParserAV1();
    ~ParserAV1() {}

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *ParseSPS(void *data, uint32_t size);
    virtual const void *GetPpsBuffer(uint32_t &size);
    virtual void ParseSliceHeader(void *data, uint32_t size);
    virtual VASurfaceID GetRenderTargetSurfaceId(VASurfaceID render_target) const override;
    virtual void ResetPictureState() override;

private:
    VADecPictureParameterBufferAV1 pic_param;
    SequenceHeader seq_header;
    bool pic_valid;
};

#endif  // PARSER_AV1_H_
