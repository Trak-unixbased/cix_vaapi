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

#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <linux/videodev2.h>
#include <string.h>
#include <poll.h>
#include <sys/ioctl.h>
#include "mvx-v4l2-controls.h"
#include "v4l2_stateful_dec.h"
#include "packer_h264.h"
#include "parser_h264.h"
#include "packer_hevc.h"
#include "parser_hevc.h"
#include "packer_vp9.h"
#include "parser_vp9.h"
#include "packer_av1.h"
#include "parser_av1.h"

#define CIX_VA_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CIX_VA_MIN_CACHE_SIZE (1024 * 1024)

V4l2StatefulDecoder::V4l2StatefulDecoder(
    const char *device_name,
    VAContextID context,
    uint32_t pic_width,
    uint32_t pic_height,
    enum v4l2_buf_type out_type,
    enum v4l2_buf_type cap_type,
    uint32_t out_format,
    uint32_t cap_format
) : V4l2Stateful(device_name, context, pic_width, pic_height, out_type, cap_type, out_format, cap_format),
    packer(nullptr),
    parser(nullptr),
    bits_cache(CIX_VA_MAX(CIX_VA_MIN_CACHE_SIZE, pic_width * pic_height)),
    data_cache(CIX_VA_MAX(CIX_VA_MIN_CACHE_SIZE, pic_width * pic_height)),
    data_offset(0),
    slice_param_count(0),
    slice_data_count(0),
    sps_count(0),
    sps_id(-1),
    first_slice(true),
    frame_count(0),
    bits_dump(nullptr),
    buf(nullptr)
{
    if (out_format == V4L2_PIX_FMT_H264) {
        packer = new PackerH264();
        parser = new ParserH264();
    } else if (out_format == V4L2_PIX_FMT_HEVC) {
        packer = new PackerHEVC();
        parser = new ParserHEVC();
    } else if (out_format == V4L2_PIX_FMT_VP9) {
        packer = new PackerVP9();
        parser = new ParserVP9();
    } else if (out_format == V4L2_PIX_FMT_AV1) {
        packer = new PackerAV1();
        parser = new ParserAV1();
    }

    if (packer)
        packer->SetCache(bits_cache.GetPtr(), bits_cache.GetSize());

    bits_dump = DUMP_OpenBitsFile();
    SetV4l2Control(V4L2_CID_MVE_VIDEO_FRAME_REORDERING, 0);
}

V4l2StatefulDecoder::~V4l2StatefulDecoder()
{
    if (packer) {
        delete packer;
        packer = nullptr;
    }

    if (parser) {
        delete parser;
        parser = nullptr;
    }

    Drain();

    DUMP_Close(bits_dump);
}

void V4l2StatefulDecoder::CreateBuffers()
{
    Port &port = GetOutPort();
    uint32_t num = GetNumBitstreamBuffers();
    port.CreateBuffers(num, V4L2_MEMORY_MMAP);
    port.StreamOn();
}

void V4l2StatefulDecoder::AddSurfaces(uint32_t num)
{
    Port &port = GetCapPort();
    port.SetSurfacePool(GetSurfacePool());
    port.CreateBuffers(num ? num : 32, V4L2_MEMORY_DMABUF);
    port.StreamOn();
}

void V4l2StatefulDecoder::SetPicParamBuffer(void *data, uint32_t size, VABufferID id)
{
    if (data && size) {
        if (packer)
            packer->SetPictureParameter(data, size);
        if (parser)
            parser->SetPictureParameter(data, size);
    }

    if (!parser)
        return;

    uint32_t pps_size = 0;
    const void *pps = parser->GetPpsBuffer(pps_size);
    if (!pps || pps_size == 0)
        return;

    RemoveDataBlockByBufferId(id);

    uint8_t *cache_base = (uint8_t *)data_cache.GetPtr();
    const uint32_t cache_sz = data_cache.GetSize();
    if (data_offset + pps_size > cache_sz) {
        CIX_VAAPI_WARNING("Picture PPS (ext) %u B exceeds data cache space (left %u)\n",
            pps_size, cache_sz > data_offset ? cache_sz - data_offset : 0);
        return;
    }

    memcpy(cache_base + data_offset, pps, pps_size);
    CIX_VAAPI_INFO("Copy picture PPS buffer %d: offset = %u, size = %u\n",
        id, data_offset, pps_size);
    data_blocks.emplace_back(cache_base, pps_size, data_offset, id);
    data_offset += pps_size;
}

int32_t V4l2StatefulDecoder::SetIQMatrix(void *data, uint32_t size)
{
    if (packer && data && size) {
        CIX_VAAPI_CHECK_RETURN_CODE(packer->SetIQMatrix(data, size) == 0, -1,
            "Failed to set IQ matrix\n");
    }

    return 0;
}

void V4l2StatefulDecoder::SetSliceParamBuffer(void *data, uint32_t size, uint32_t num_elements)
{
    if (packer)
        packer->SetSliceParameter(data, size * num_elements);
    slice_param_count += num_elements;
}

void V4l2StatefulDecoder::SetSliceDataBuffer(void *data, uint32_t size, VABufferID id)
{
    if (!packer)
        return;

    uint8_t *buf = (uint8_t *)data_cache.GetPtr();
    const uint32_t written = packer->SaveSliceData(buf + data_offset, data, size);

    data_blocks.emplace_back(buf, written, data_offset, id);
    data_offset += written;
    slice_data_count++;
}

void V4l2StatefulDecoder::SetSpsBuffer(void *data, uint32_t size, VABufferID id)
{
    uint8_t *buf = (uint8_t *)data_cache.GetPtr();
    memcpy(buf + data_offset, data, size);
    CIX_VAAPI_INFO("Copy SPS buffer %d: offset = %d, size = %d\n",
        id, data_offset, size);
    data_blocks.emplace_back(buf, size, data_offset, id);
    data_offset += size;
    sps_count++;
}

int32_t V4l2StatefulDecoder::InitializeBuffers(VABufferID render_target)
{
    VASurfaceID surface = (VASurfaceID)render_target;
    if (parser)
        surface = parser->GetRenderTargetSurfaceId(surface);

    // poll to get as many output/capture port buffers as possible
    while (frame_count > 0 && Poll(POLLOUT | POLLIN, 0) == 0);

    buf = GetOutPort().GetFreeBuffer();
    if (buf == nullptr) {
        CIX_VAAPI_WARNING("Failed to get free buffer from output port\n");

        while (buf == nullptr && Poll(POLLOUT, 100))
            buf = GetOutPort().GetFreeBuffer();

        CIX_VAAPI_CHECK_RETURN_CODE(buf != nullptr, -1,
            "Failed to get free buffer from output port\n");
    }

    CIX_VAAPI_INFO("Get free buffer for frame %d: id = %d\n",
        frame_count, buf->GetId());
    buf->SetBytesUsed(0, 0);
    buf->SetUserData(surface);

    // Find the render target buffer and clear its in_use flag
    auto v4l2buf = GetCapPort().FindBufferByRenderTarget(surface);
    if (v4l2buf && v4l2buf->IsReady())
        v4l2buf->SetInUse(false);

    // If not found and the render target is enqueued before, it means client
    // dropped this frame. Need to wait for VPU to dequeue  this frame buffer
    // and reuse it. Otherwise, there will be more than one frame buffers with
    // same render target ID, which is not expected.
    auto spool = GetSurfacePool();
    spool->DestroyUnusedSurface(surface);
    if (!spool->GetPicOutput(surface))
        return 0;
    if (v4l2buf == nullptr && CheckSurfaceOwnership(surface)) {
        SyncSurface(surface);
        v4l2buf = GetCapPort().FindBufferByRenderTarget(surface);
        CIX_VAAPI_CHECK_RETURN_CODE(v4l2buf != nullptr, -1,
            "Failed to find buffer of render target %d\n", surface);
        v4l2buf->SetInUse(false);
    }

    // This render target is owned by the decoder now
    OwnSurface(surface);

    // Clear the surface ID of this surface so it can be reused.
    auto surf = spool->GetSurfaceByID(surface);
    if (surf)
        surf->SetId(VA_INVALID_SURFACE);

    // Then get a free buffer from capture port and queue it
    v4l2buf = GetCapPort().GetFreeBuffer();

    // There should be always a free buffer in capture port as we just cleared one
    CIX_VAAPI_CHECK_RETURN_CODE(v4l2buf != nullptr, -1,
        "Failed to get free buffer from capture port\n");
    v4l2buf->ClearData();
    QueueBuffer(v4l2buf);

    return 0;
}

void V4l2StatefulDecoder::ResetFrameData()
{
    slice_param_count = 0;
    slice_data_count = 0;
    sps_count = 0;
    data_offset = 0;
    first_slice = true;
    data_blocks.clear();
    buf = nullptr;
    if (parser)
        parser->ResetPictureState();
}

int32_t V4l2StatefulDecoder::ProcessBuffers(VABufferID *buffers, int num_buffers)
{
    if (packer == nullptr || parser == nullptr || buf == nullptr)
        return -1;

    uint32_t offset = buf->GetBytesUsed(0);
    void *base = buf->Map(0);
    uint32_t size = buf->GetPlaneSize(0);
    packer->PackVpsNalu(base, size, offset);
    packer->PackSpsNalu(base, size, offset);
    auto bpool = GetBufferPool();

    for (int i = 0; i < num_buffers; i++) {
        auto bufinfo = bpool->GetBufferInfo(buffers[i]);
        if (bufinfo == nullptr)
            continue;
        if (bufinfo->type == VAPictureParameterBufferType) {
            auto pps_block = GetDataBlockByBufferId(buffers[i]);
            if (pps_block) {
                if (pps_block->size > size - offset) {
                    CIX_VAAPI_WARNING("Picture PPS (%u B) exceeds output space (%u B left)\n",
                        pps_block->size, size - offset);
                    return -1;
                }
                offset += packer->CopyPPS(
                    (uint8_t *)base + offset, size - offset,
                    (uint8_t *)pps_block->buffer + pps_block->offset, pps_block->size);
                RemoveDataBlockByBufferId(buffers[i]);
            }
        } else if (bufinfo->type == VAIQMatrixBufferType) {
            /* No IQ matrix payload is merged into the V4L2 bitstream for this path. */
#if (VA_MAJOR_VERSION == 1 && VA_MINOR_VERSION == 22 && VA_MICRO_VERSION == 1)
        } else if (bufinfo->type == VASequenceParameterBufferType) {
            auto sps = GetDataBlockByBufferId(buffers[i]);
            CIX_VAAPI_CHECK_RETURN_CODE(sps != nullptr, -1,
                "Failed to find SPS %d\n", buffers[i]);

            offset += packer->CopySPS(
                (uint8_t *)base + offset, size - offset,
                parser->ParseSPS((uint8_t *)sps->buffer + sps->offset, sps->size));
            sps_id = parser->GetSPSID(
                (uint8_t *)sps->buffer + sps->offset, sps->size);
#endif
        } else if (bufinfo->type == VASliceDataBufferType) {
            auto slice = GetDataBlockByBufferId(buffers[i]);
            CIX_VAAPI_CHECK_RETURN_CODE(slice != nullptr, -1,
                "Failed to find slice %d\n", buffers[i]);
            CIX_VAAPI_INFO("Copy slice data buffer %d: offset = %d, size = %d to dst offset %d\n",
                buffers[i], slice->offset, slice->size, offset);

            if (first_slice) {
                parser->ParseSliceHeader(
                    (uint8_t *)slice->buffer + slice->offset, slice->size);
                int32_t pps_id = parser->GetPPSID();
                packer->PackPpsNalu(base, size, offset,
                    sps_id < 0 ? 0 : sps_id, pps_id < 0 ? 0 : pps_id);
                GetSurfacePool()->SetPicOutput(buf->GetUserData(), parser->GetOutputFlag());
            }

            offset += packer->CopySlice(
                (uint8_t *)base + offset, size - offset,
                (uint8_t *)slice->buffer + slice->offset, slice->size, first_slice);
            first_slice = false;
            RemoveDataBlockByBufferId(buffers[i]);
        }
    }

    buf->SetBytesUsed(0, offset);
    // buf->Unmap(0);

    return 0;
}

int32_t V4l2StatefulDecoder::Submit()
{
    if (buf == nullptr)
        return -1;

    if (packer != nullptr && packer->CheckIntegrity() != 0)
        return -1;

    DUMP_Write(bits_dump, buf->Map(0), buf->GetBytesUsed(0));

    int32_t ret = QueueBuffer(buf);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, ret, "Failed to queue buffer\n");

    // If it's the first buffer, poll for SOURCE_CHANGE event
    if (frame_count++ == 0) {
        do {
            ret = Poll(POLLPRI, 1000);
        } while (ret != V4L2_EVENT_SOURCE_CHANGE);
    }

    return ret;
}

uint32_t V4l2StatefulDecoder::FindV4l2BufferIdBySurfaceId(VASurfaceID surface_id) {
    auto port = GetCapPort();
    auto vbuf = port.FindBufferByRenderTarget(surface_id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr, INVALID_BUFFER_ID,
        "Failed to find V4L2 buffer for surface %d\n", surface_id);
    return vbuf->GetId();
}

Surface *V4l2StatefulDecoder::GetSurfaceFromV4l2BufferId(uint32_t id) {
    auto port = GetCapPort();
    auto vbuf = port.GetBuffer(id);
    CIX_VAAPI_CHECK_RETURN_CODE(vbuf != nullptr, nullptr,
        "Failed to find buffer %d\n", id);
    return vbuf->GetSurface();
}

DataBlock *V4l2StatefulDecoder::GetDataBlockByBufferId(uint32_t id)
{
    for (auto &block : data_blocks) {
        if (block.id == id)
            return &block;
    }

    return nullptr;
}

void V4l2StatefulDecoder::RemoveDataBlockByBufferId(uint32_t id)
{
    for (auto it = data_blocks.begin(); it != data_blocks.end(); it++) {
        if (it->id == id) {
            data_blocks.erase(it);
            break;
        }
    }

    return;
}
