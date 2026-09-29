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

#ifndef PARSER_H_
#define PARSER_H_

#include <stdint.h>
#include <va/va_backend.h>
#include "log.h"

class Parser {
public:
    Parser() {};
    ~Parser() {};
    virtual void SetPictureParameter(void *data, uint32_t size) {};
    virtual int32_t GetSPSID(void *data, uint32_t size) { return -1; };
    virtual int32_t GetPPSID() { return -1; };
    virtual bool GetOutputFlag() { return true; };
    virtual void ParseSliceHeader(void *data, uint32_t size) {}
    virtual void *ParseSPS(void *data, uint32_t size) { return nullptr; }
    virtual const void *GetPpsBuffer(uint32_t &size) { size = 0; return nullptr; }
    /** Surface the decoder writes / capture port binds (VA picture target). */
    virtual VASurfaceID GetRenderTargetSurfaceId(VASurfaceID render_target) const { return render_target; }
    /** Clears per-picture parser state at end of frame (before next BeginPicture). */
    virtual void ResetPictureState() {}
    uint32_t RemoveEmulationPrevention(uint8_t *data, uint32_t size);
};

#endif  // PARSER_H_