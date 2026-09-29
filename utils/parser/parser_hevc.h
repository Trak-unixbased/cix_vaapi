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

#ifndef PARSER_HEVC_H_
#define PARSER_HEVC_H_

#include <vector>
#include "parser.h"
#include "sequence.h"

class ParserHEVC : public Parser {
public:
    ParserHEVC();
    ~ParserHEVC() {};
    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void *ParseSPS(void *data, uint32_t size);
    virtual void ParseSliceHeader(void *data, uint32_t size);
    virtual int32_t GetSPSID(void *data, uint32_t size);
    virtual int32_t GetPPSID() { return slice_pic_parameter_set_id; };
    virtual bool GetOutputFlag() { return output_flag; };

private:
    VAPictureParameterBufferHEVC pic_param;
    std::vector<VASliceParameterBufferHEVC> slice_params;
    SequenceHeader seq_header;
    int32_t slice_pic_parameter_set_id;
    bool output_flag;
};

#endif  // PARSER_HEVC_H_