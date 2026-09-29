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

#ifndef PARSER_H264_H_
#define PARSER_H264_H_

#include "parser.h"
#include "slice.h"

class ParserH264 : public Parser {
public:
    ParserH264();
    ~ParserH264() {};
    virtual void ParseSliceHeader(void *data, uint32_t size);
    virtual int32_t GetPPSID() { return pic_parameter_set_id; };

private:
    H264SliceHeader slice_header;
    int32_t pic_parameter_set_id;
};

#endif  // PARSER_H264_H_