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

#ifndef V4L2_STATEFUL_DEC_H_
#define V4L2_STATEFUL_DEC_H_

#include "v4l2_stateful.h"
#include "packer.h"
#include "parser.h"
#include "dump.h"


struct DataBlock {
    void *buffer;
    uint32_t size;
    uint32_t offset;
    uint32_t id;
    DataBlock(void *buffer, uint32_t size, uint32_t offset, uint32_t id) :
        buffer(buffer), size(size), offset(offset), id(id) {}
};

class V4l2StatefulDecoder
    : public V4l2Stateful {
public:
    V4l2StatefulDecoder(
        const char *device_name,
        VAContextID context,
        uint32_t pic_width,
        uint32_t pic_height,
        enum v4l2_buf_type out_type,
        enum v4l2_buf_type cap_type,
        uint32_t out_format,
        uint32_t cap_format
    );
    ~V4l2StatefulDecoder();

    virtual void CreateBuffers() override;
    virtual void AddSurfaces(uint32_t num) override;
    virtual void SetPicParamBuffer(void *data, uint32_t size, VABufferID id) override;
    virtual int32_t SetIQMatrix(void *data, uint32_t size) override;
    virtual void SetSliceParamBuffer(void *data, uint32_t size, uint32_t num_elements) override;
    virtual void SetSliceDataBuffer(void *data, uint32_t size, VABufferID id) override;
    virtual void SetSpsBuffer(void *data, uint32_t size, VABufferID id) override;
    virtual void ResetFrameData() override;
    virtual int32_t InitializeBuffers(VABufferID render_target) override;
    virtual int32_t ProcessBuffers(VABufferID *buffers, int num_buffers) override;
    virtual int32_t Submit() override;
    virtual uint32_t FindV4l2BufferIdBySurfaceId(VASurfaceID surface) override;
    virtual Surface *GetSurfaceFromV4l2BufferId(uint32_t id) override;
    virtual Port &GetSurfacePort() override { return GetCapPort(); }

private:
    DataBlock *GetDataBlockByBufferId(uint32_t id);
    void RemoveDataBlockByBufferId(uint32_t id);
    Packer *packer;
    Parser *parser;
    Buffer bits_cache;
    Buffer data_cache;
    uint32_t data_offset;
    uint32_t slice_param_count;
    uint32_t slice_data_count;
    uint32_t sps_count;
    bool first_slice;
    std::vector<DataBlock> data_blocks;
    V4l2Buffer *buf;
    DUMP_HANDLE bits_dump;
    uint64_t frame_count;
    int32_t sps_id;
};

#endif  // V4L2_STATEFUL_DEC_H_