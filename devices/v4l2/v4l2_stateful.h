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

#ifndef V4L2_STATEFUL_H_
#define V4L2_STATEFUL_H_

#include <map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <linux/videodev2.h>
#include "device_common.h"
#include "log.h"

#define V4L2_STATEFUL_CODEC_NUM_HUGE_BITSTREAM_BUFFERS  1
#define V4L2_STATEFUL_CODEC_NUM_BIG_BITSTREAM_BUFFERS   15
#define V4L2_STATEFUL_CODEC_NUM_BITSTREAM_BUFFERS       32
#define V4L2_STATEFUL_CODEC_NUM_FRAME_BUFFERS           32
#define IS_HUGE_RESOLUTION(width, height) ((width * height) > (8192 * 8192))
#define IS_BIG_RESOLUTION(width, height) ((width * height) > (4096 * 4096))

extern const std::map<uint32_t, VAImageFormat> image_formats;

class V4l2Buffer {
public:
    V4l2Buffer(int32_t fd, enum v4l2_buf_type type, enum v4l2_memory memory, int32_t index);
    ~V4l2Buffer();
    bool isFreeBuffer() { return !in_use && !in_queue; }
    bool IsReady() { return in_use && !in_queue; }
    bool IsInQueue() { return in_queue; }
    void SetInQueue(bool in) { in_queue = in; }
    void SetInUse(bool in) { in_use = in; }
    void SetBytesUsed(uint32_t plane, uint32_t bytesused) { buf.m.planes[plane].bytesused = bytesused; }
    uint32_t GetBytesUsed(uint32_t plane) { return buf.m.planes[plane].bytesused; }
    void SetDataOffset(uint32_t plane, uint32_t offset) { buf.m.planes[plane].data_offset = offset; }
    uint32_t GetDataOffset(uint32_t plane) { return buf.m.planes[plane].data_offset; }
    void SetUserData(uint32_t userdata) { buf.timestamp.tv_sec = userdata; buf.timestamp.tv_usec = 0; }
    uint32_t GetUserData() { return (uint32_t)buf.timestamp.tv_sec; }
    void SetField(enum v4l2_field field) { buf.field = field; }
    void ClearData();
    int32_t GetId() { return buf.index; }
    uint32_t GetPlaneSize(int32_t plane) { return planes[plane].length; };
    v4l2_buffer *GetV4l2Buffer() { return &buf; }
    Surface *GetSurface() { return surface; }
    void BindSurface(Surface *s);
    void *Map(int32_t plane);
    void Unmap(int32_t plane);
    void Unmap();

private:
    int32_t fd_;
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    bool in_use;
    bool in_queue;
    bool first_use;
    void *ptr[VIDEO_MAX_PLANES];
    Surface *surface;
};

class Port {
public:
    Port(int32_t fd, enum v4l2_buf_type type, uint32_t pic_width, uint32_t pic_height, uint32_t format);
    ~Port() {};

    void SetFd(int32_t fd) { fd_ = fd; }
    void SetBufType(enum v4l2_buf_type t) { type = t; }
    enum v4l2_buf_type GetBufType() { return type; }
    enum v4l2_memory GetMemoryType() { return memory; }
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetPixelFormat() { return format; }
    void GetFormat();
    void SetFormat();
    void CreateBuffers(uint32_t num_buffers, enum v4l2_memory memory);
    void DestroyBuffers();
    // uint32_t GetBufferDelta() { return request_delta; }
    uint32_t GetQueuedBuffers();
    V4l2Buffer *GetBuffer(uint32_t id);
    V4l2Buffer *GetFreeBuffer();
    V4l2Buffer *GetDequeuedBuffer();
    int32_t HandleBuffer(V4l2Buffer *buf);
    V4l2Buffer *FindBufferByRenderTarget(VASurfaceID id);
    V4l2Buffer *GetBufferByUserData(uint32_t userdata);
    // V4l2Buffer *SurfaceToBuffer(Surface *s);
    int32_t StreamOn();
    int32_t StreamOff();
    bool IsStreamOn() { return streamon; }
    void SetSurfacePool(SurfacePool *pool) { surface_pool = pool; };
    void UpdateResolution(uint32_t w, uint32_t h, uint32_t f);

private:
    uint32_t GetNumPlanes(uint32_t format);
    uint32_t GetRowBpp(uint32_t format, int32_t plane);
    int32_t fd_;
    enum v4l2_buf_type type;
    struct v4l2_format fmt;
    enum v4l2_memory memory;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    typedef std::map<uint32_t, V4l2Buffer *> V4l2BufferMap;
    V4l2BufferMap buffers;
    bool streamon;
    // uint32_t request_delta; // requested buffer number - actual buffer number
    uint32_t enqueued_buffers;
    SurfacePool *surface_pool;
};

class V4l2Stateful {
public:
    V4l2Stateful(
        const char *device_name,
        VAContextID context,
        uint32_t pic_width,
        uint32_t pic_height,
        enum v4l2_buf_type out_type,
        enum v4l2_buf_type cap_type,
        uint32_t out_format,
        uint32_t cap_format
    );
    virtual ~V4l2Stateful() { Close(); };

    int32_t Open(const char* device_name);
    int32_t Close();
    int32_t GetFd() { return fd_; }
    void SubscribeEvent(uint32_t event);
    uint32_t GetNumBitstreamBuffers();
    virtual void CreateBuffers() = 0;
    virtual void AddSurfaces(uint32_t num) = 0;
    virtual void SetSeqParamBuffer(void *data, uint32_t size) {};
    virtual void SetPicParamBuffer(void *data, uint32_t size, VABufferID id) = 0;
    virtual void SetRateControl(void *data, uint32_t size) {};
    virtual void SetFrameRate(void *data, uint32_t size) {};
    virtual int32_t SetIQMatrix(void *data, uint32_t size) { return 0;};
    virtual void SetSliceParamBuffer(void *data, uint32_t size, uint32_t num_elements) = 0;
    virtual void SetSliceDataBuffer(void *data, uint32_t size, VABufferID id) {};
    virtual void SetSpsBuffer(void *data, uint32_t size, VABufferID id) {};
    virtual void ResetFrameData() = 0;
    virtual int32_t InitializeBuffers(VABufferID render_target) = 0;
    virtual int32_t ProcessBuffers(VABufferID *buffers, int num_buffers) = 0;
    virtual int32_t SyncSurface(VASurfaceID render_target);
    virtual int32_t SyncBuffer(VABufferID id) { return 0; };
    virtual uint32_t FindV4l2BufferIdBySurfaceId(VASurfaceID surface) = 0;
    virtual Surface *GetSurfaceFromV4l2BufferId(uint32_t id) = 0;
    virtual int32_t Submit() = 0;
    virtual bool IsEncoder() { return false; }
    virtual VABufferID GetCodedBufferID() { return VA_INVALID_ID; }
    virtual int32_t SetRateControlMode(uint32_t mode);
    virtual int32_t SetProfile(VAProfile profile) { return 0; }
    Port &GetOutPort() { return out_port; }
    Port &GetCapPort() { return cap_port; }
    virtual Port &GetSurfacePort() = 0;
    int32_t QueueBuffer(V4l2Buffer *buf);
    V4l2Buffer *DequeueBuffer(Port &port);
    int32_t Poll(int16_t events, int32_t timeout);
    uint32_t GetPicWidth() { return pic_width; }
    uint32_t GetPicHeight() { return pic_height; }
    int32_t SetV4l2Control(uint32_t id, int32_t value);
    int32_t SetV4l2Parm(struct v4l2_streamparm *parm);
    void SetSurfacePool(SurfacePool *pool) { surface_pool = pool; };
    SurfacePool *GetSurfacePool() { return surface_pool; };
    void SetBufferPool(BufferPool *pool) { buffer_pool = pool; };
    BufferPool *GetBufferPool() { return buffer_pool; };
    bool CheckSurfaceOwnership(VASurfaceID surface);
    void OwnSurface(VASurfaceID surface) { surface_pool->SetSurfaceContext(surface, context); };
    void FreeSurface(VASurfaceID surface) { surface_pool->SetSurfaceContext(surface, VA_INVALID_ID); }
    void Drain();

private:
    int32_t HandleEvent();
    int32_t SendDecCmd(uint32_t cmd);
    int32_t fd_;
    VAContextID context;
    Port out_port;
    Port cap_port;
    uint32_t pic_width;
    uint32_t pic_height;
    std::mutex mutex;
    SurfacePool *surface_pool;
    BufferPool *buffer_pool;
    std::atomic<bool> eos;
};

#endif  // V4L2_STATEFUL_H_