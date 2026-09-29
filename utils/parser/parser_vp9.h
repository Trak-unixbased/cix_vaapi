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

#ifndef PARSER_VP9_H_
#define PARSER_VP9_H_

#include <va/va.h>
#include <va/va_dec_vp9.h>
#include "parser.h"
#include "parser_bsr.h"

/** Slice header sanity vs. picture params; show_existing follow-on is built in PackerVP9. */
class ParserVP9 : public Parser {
public:
    ParserVP9();
    ~ParserVP9() {}

    virtual void SetPictureParameter(void *data, uint32_t size);
    virtual void ParseSliceHeader(void *data, uint32_t size);

private:
    VADecPictureParameterBufferVP9 pic_param;
    bool pic_valid;
};

#endif  // PARSER_VP9_H_
