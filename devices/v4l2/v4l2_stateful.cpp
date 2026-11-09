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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string>
#include <cstring>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include "cix_va_compat.h"
#include "v4l2_stateful.h"

#define SYNC_SURFACE_POLL_TIMEOUT_MS 1000
#define SYNC_SURFACE_MAX_RETRIES 15
#define DRAIN_MAX_POLLS 64

const std::map<uint32_t, VAImageFormat> image_formats = {
    {V4L2_PIX_FMT_GREY,     {VA_FOURCC_Y800, VA_LSB_FIRST,  8,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_YUV420,   {VA_FOURCC_I420, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_NV12,     {VA_FOURCC_NV12, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_NV21,     {VA_FOURCC_NV21, VA_LSB_FIRST, 12,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_YUYV,     {VA_FOURCC_YUY2, VA_LSB_FIRST, 16,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_UYVY,     {VA_FOURCC_UYVY, VA_LSB_FIRST, 16,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_P010,     {VA_FOURCC_P010, VA_LSB_FIRST, 24,  0, 0, 0, 0, 0}},
    {V4L2_PIX_FMT_RGB_3P,   {VA_FOURCC_RGBP, VA_LSB_FIRST, 24, 24, 0, 0, 0, 0}},
};

// True for the uncompressed (raw picture) formats handled by the ports.
static bool IsRawV4l2Format(uint32_t format)
{
    switch (format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_GREY:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_P010:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_RGB_3P:
            return true;
        default:
            return false;
    }
}

// ioctl() wrapper retrying on EINTR.
static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

V4l2Buffer::V4l2Buffer(int32_t fd, enum v4l2_buf_type type, enum v4l2_memory memory, int32_t index) :
    fd_(fd),
    valid(false),
    in_use(false),
    in_queue(false),
    surface(nullptr)
{
    memset(ptr, 0, sizeof(ptr));
    memset(map_base, 0, sizeof(map_base));
    memset(map_len, 0, sizeof(map_len));
    memset(planes, 0, sizeof(planes));
    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = memory;
    buf.index = index;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;
    int ret = xioctl(fd_, VIDIOC_QUERYBUF, &buf);
    if (ret != 0) {
        CIX_VAAPI_ERROR("Failed to query buffer %d: %s\n", index, strerror(errno));
        buf.length = 0;
        SetUserData(-1);
        return;
    }
    valid = true;

    CIX_VAAPI_INFO("Query buffer: type = %d, memory = %d, index = %d, length = %d, fd = %d\n",
        buf.type, buf.memory, buf.index, buf.length, fd_);

    for (uint32_t i = 0; i < buf.length; i++) {
        struct v4l2_plane &p = buf.m.planes[i];
        CIX_VAAPI_INFO("    Plane %u: length = %u, bytesused = %u, data_offset = %u, mem_offset = %u\n",
            i, p.length, p.bytesused, p.data_offset, p.m.mem_offset);
    }

    // Set user data to an invalid ID now, in case client calls SyncSurface
    // early and matches a buffer in Port::FindBufferByRenderTarget() incorrectly.
    SetUserData(-1);
}

V4l2Buffer::~V4l2Buffer() {
    Unmap();
    if (surface != nullptr)
        surface->RemoveUser((SurfaceUser)this);
}

bool V4l2Buffer::BindSurface(Surface *s) {
    if (surface != nullptr)
        surface->RemoveUser((SurfaceUser)this);
    surface = nullptr;

    CIX_VAAPI_CHECK_RETURN_CODE(s != nullptr && s->IsValid(), false,
        "Cannot bind an invalid surface to buffer %d\n", GetId());
    CIX_VAAPI_CHECK_RETURN_CODE(buf.length <= s->GetNumPlanes(), false,
        "Buffer %d has %u planes but the surface only %u\n",
        GetId(), buf.length, s->GetNumPlanes());

    for (uint32_t i = 0; i < buf.length; i++) {
        struct v4l2_plane &p = buf.m.planes[i];
        CIX_VAAPI_CHECK_RETURN_CODE(s->GetSize(i) == p.length, false,
            "Surface plane %u size %u does not match buffer plane size %u\n",
            i, s->GetSize(i), p.length);
    }

    surface = s;
    surface->AddUser((SurfaceUser)this);
    for (uint32_t i = 0; i < buf.length; i++)
        buf.m.planes[i].m.fd = s->GetFd(i);

    CIX_VAAPI_DEBUG("Bound surface %u to buffer %d\n", surface->GetId(), GetId());
    return true;
}

void V4l2Buffer::ClearData() {
    SetUserData(-1);
    buf.flags = 0;
    for (uint32_t i = 0; i < buf.length; i++)
        buf.m.planes[i].bytesused = 0;
}

void *V4l2Buffer::Map(int32_t plane) {
    if (plane < 0 || (uint32_t)plane >= buf.length)
        return nullptr;

    if (ptr[plane] != nullptr)
        return ptr[plane];

    if (!V4L2_TYPE_IS_MULTIPLANAR(buf.type))
        return nullptr;

    struct v4l2_plane &p = buf.m.planes[plane];
    void *base = MAP_FAILED;
    size_t len = 0;

    if (buf.memory == V4L2_MEMORY_DMABUF) {
        // mmap() offsets must be page aligned: map from the start of the
        // DMA-BUF and point into it. The previous code passed data_offset as
        // the mmap offset (EINVAL for most values) and stored MAP_FAILED.
        len = (size_t)p.data_offset + p.length;
        base = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, p.m.fd, 0);
        if (base != MAP_FAILED)
            ptr[plane] = (uint8_t *)base + p.data_offset;
    } else if (buf.memory == V4L2_MEMORY_MMAP) {
        len = p.length;
        base = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, p.m.mem_offset);
        if (base != MAP_FAILED)
            ptr[plane] = base;
    }

    if (base == MAP_FAILED) {
        CIX_VAAPI_ERROR("Failed to mmap buffer %d plane %d: %s\n",
            buf.index, plane, strerror(errno));
        ptr[plane] = nullptr;
        return nullptr;
    }

    map_base[plane] = base;
    map_len[plane] = len;
    CIX_VAAPI_DEBUG("Mapped buffer %d plane %d (memory %d): ptr = %p, length = %zu\n",
        buf.index, plane, buf.memory, ptr[plane], len);
    return ptr[plane];
}

void V4l2Buffer::Unmap(int32_t plane) {
    if (plane < 0 || plane >= VIDEO_MAX_PLANES)
        return;

    if (map_base[plane] != nullptr)
        munmap(map_base[plane], map_len[plane]);

    map_base[plane] = nullptr;
    map_len[plane] = 0;
    ptr[plane] = nullptr;
}

void V4l2Buffer::Unmap() {
    for (int i = 0; i < VIDEO_MAX_PLANES; i++)
        Unmap(i);
}

Port::Port(int32_t fd, enum v4l2_buf_type type, uint32_t pic_width, uint32_t pic_height, uint32_t format) :
    fd_(fd),
    type(type),
    memory(V4L2_MEMORY_DMABUF),
    width(pic_width),
    height(pic_height),
    format(format),
    streamon(false),
    surface_pool(nullptr)
{
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = type;
}

int32_t Port::GetFormat() {
    fmt.type = type;
    int ret = xioctl(fd_, VIDIOC_G_FMT, &fmt);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to get format: %s\n", strerror(errno));

    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    CIX_VAAPI_INFO("Get format: width = %u, height = %u, format = %s, planes = %u, fd = %d\n",
        f.width, f.height, GetFourccString(f.pixelformat), f.num_planes, fd_);
    return 0;
}

uint32_t Port::GetNumPlanes(uint32_t format) {
    switch (format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_GREY:
            return 1;
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_P010:
            return 2;
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_RGB_3P:
            return 3;
    }

    return 0;
}

uint32_t Port::GetRowBpp(uint32_t format, int32_t plane) {
    switch (format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            return plane == 0 ? 16 : 0;
        case V4L2_PIX_FMT_GREY:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_RGB_3P:
            return 8;
        case V4L2_PIX_FMT_P010:
            return 16;
        case V4L2_PIX_FMT_YUV420:
            return plane == 0 ? 8 : 4;
    }

    return 0;
}

int32_t Port::SetFormat() {
    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    fmt.type = type;
    f.width = width;
    f.height = height;
    f.pixelformat = format;
    f.num_planes = GetNumPlanes(format);
    for (uint32_t i = 0; i < f.num_planes; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        f.plane_fmt[i].bytesperline = (((width * bpp) >> 3) + 63) & ~63; // Align to 64 bytes
        f.plane_fmt[i].sizeimage = 0;
    }

    int ret = xioctl(fd_, VIDIOC_S_FMT, &fmt);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to set format %s %ux%u: %s\n",
        GetFourccString(format), width, height, strerror(errno));

    if (f.pixelformat != format) {
        CIX_VAAPI_ERROR("Format %s is not supported by the device (got %s)\n",
            GetFourccString(format), GetFourccString(f.pixelformat));
        return -1;
    }

    CIX_VAAPI_INFO("Set format: width = %u, height = %u, format = %s, planes = %u\n",
        f.width, f.height, GetFourccString(f.pixelformat), f.num_planes);
    for (uint32_t i = 0; i < f.num_planes; i++) {
        struct v4l2_plane_pix_format *p = &f.plane_fmt[i];
        CIX_VAAPI_INFO("    Plane %u: sizeimage = %u, bytesperline = %u\n",
            i, p->sizeimage, p->bytesperline);
    }
    return 0;
}

int32_t Port::CreateBuffers(uint32_t num_buffers, enum v4l2_memory memory) {
    // Zero the whole structure: "flags" (V4L2_MEMORY_FLAG_NON_COHERENT) and the
    // reserved fields used to contain stack garbage.
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = type;
    reqbuf.memory = memory;
    reqbuf.count = num_buffers;
    int ret = xioctl(fd_, VIDIOC_REQBUFS, &reqbuf);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to request buffers: %s, fd = %d\n",
        strerror(errno), fd_);
    CIX_VAAPI_INFO("Request buffers: type = %d, memory = %d, request = %u, count = %u\n",
        reqbuf.type, reqbuf.memory, num_buffers, reqbuf.count);

    this->memory = memory;

    for (uint32_t i = 0; i < reqbuf.count; i++) {
        auto buf = new V4l2Buffer(fd_, type, memory, i);
        if (!buf->IsValid()) {
            delete buf;
            return -1;
        }
        buffers[buf->GetId()] = buf;

        if (surface_pool != nullptr) {
            auto surface = surface_pool->FetchSurface();
            CIX_VAAPI_CHECK_RETURN_CODE(surface != nullptr, -1,
                "Failed to allocate the surface of buffer %u\n", i);
            if (surface->GetPitch(0) != fmt.fmt.pix_mp.plane_fmt[0].bytesperline ||
                !buf->BindSurface(surface)) {
                CIX_VAAPI_ERROR("Surface layout (pitch %u) does not match buffer %u (bytesperline %u)\n",
                    surface->GetPitch(0), i, fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
                surface->SetInUse(false);
                return -1;
            }
            surface->SetId(VA_INVALID_SURFACE);
        }
    }

    return (int32_t)reqbuf.count;
}

void Port::DestroyBuffers() {
    for (auto it = buffers.begin(); it != buffers.end();) {
        delete it->second;
        it = buffers.erase(it);
    }
}

int32_t Port::StreamOn() {
    if (streamon)
        return 0;
    int ret = xioctl(fd_, VIDIOC_STREAMON, &type);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to stream on: %s\n", strerror(errno));
    CIX_VAAPI_INFO("Stream on: type = %d\n", type);
    streamon = true;
    return 0;
}

int32_t Port::StreamOff() {
    if (!streamon)
        return 0;
    int ret = xioctl(fd_, VIDIOC_STREAMOFF, &type);
    // Whatever the result, the queue must be considered stopped: buffers are
    // returned to userspace by the kernel on STREAMOFF.
    streamon = false;
    for (auto &buf : buffers)
        buf.second->SetInQueue(false);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to stream off: %s\n", strerror(errno));
    CIX_VAAPI_INFO("Stream off: type = %d\n", type);
    return 0;
}

int32_t Port::UpdateResolution(uint32_t w, uint32_t h, uint32_t f) {
    if (w == width && h == height && f == format)
        return 0;

    width = w;
    height = h;
    format = f;

    const auto num_buffers = buffers.size();

    StreamOff();

    /* Return pooled surfaces before destroying V4l2Buffer (see ~V4l2Buffer). */
    for (auto &entry : buffers) {
        Surface *s = entry.second->GetSurface();
        if (s != nullptr)
            s->SetInUse(false);
    }

    DestroyBuffers();

    /* Unmap first via DestroyBuffers; then drop kernel buffer slots. */
    if (CreateBuffers(0, memory) < 0 || SetFormat() != 0 ||
        CreateBuffers(num_buffers, memory) < 0)
        return -1;

    CIX_VAAPI_INFO("Recreated %zu buffers for new resolution %ux%u (%s)\n",
        num_buffers, width, height, GetFourccString(format));
    return StreamOn();
}

V4l2Buffer *Port::GetBuffer(uint32_t id) {
    auto it = buffers.find(id);
    if (it == buffers.end())
        return nullptr;

    return it->second;
}

V4l2Buffer *Port::GetFreeBuffer() {
    for (auto &buf : buffers) {
        if (buf.second->isFreeBuffer()) {
            auto s = buf.second->GetSurface();
            if (s != nullptr && s->GetId() != VA_INVALID_SURFACE)
                continue;
            buf.second->SetInUse(true);
            return buf.second;
        }
    }

    return nullptr;
}

V4l2Buffer *Port::GetDequeuedBuffer() {
    for (auto &buf : buffers) {
        if (buf.second->IsReady()) {
            buf.second->SetInUse(true);
            return buf.second;
        }
    }

    return nullptr;
}

int32_t Port::HandleBuffer(V4l2Buffer *buf) {
    if (buf == nullptr)
        return -1;

    return 0;
}

V4l2Buffer *Port::FindBufferByRenderTarget(VASurfaceID id) {
    if (id == VA_INVALID_SURFACE)
        return nullptr;

    for (auto &buf : buffers) {
        auto surface = buf.second->GetSurface();
        if (surface != nullptr && surface->GetId() == id) {
            CIX_VAAPI_DEBUG("Found buffer %u for surface %u\n", buf.first, id);
            return buf.second;
        }
    }

    return nullptr;
}

V4l2Buffer *Port::GetBufferByUserData(uint32_t userdata) {
    for (auto &buf : buffers) {
        if (buf.second->GetUserData() == userdata) {
            CIX_VAAPI_DEBUG("Found buffer %u with userdata %u\n", buf.first, userdata);
            return buf.second;
        }
    }

    return nullptr;
}

uint32_t Port::GetQueuedBuffers() {
    uint32_t count = 0;
    for (auto &buf : buffers) {
        if (buf.second->IsInQueue())
            count++;
    }

    return count;
}

V4l2Stateful::V4l2Stateful(
    const char *device_name,
    VAContextID context,
    uint32_t pic_width,
    uint32_t pic_height,
    enum v4l2_buf_type out_type,
    enum v4l2_buf_type cap_type,
    uint32_t out_format,
    uint32_t cap_format
) :
    fd_(-1),
    context(context),
    out_port(-1, out_type, pic_width, pic_height, out_format),
    cap_port(-1, cap_type, pic_width, pic_height, cap_format),
    pic_width(pic_width),
    pic_height(pic_height),
    surface_pool(nullptr),
    buffer_pool(nullptr),
    eos(false),
    stopped(false),
    valid_(false)
{
    if (device_name == nullptr || *device_name == '\0' || Open(device_name) < 0)
        return;

    out_port.SetFd(fd_);
    out_port.GetFormat();
    bool out_ok = out_port.SetFormat() == 0;

    cap_port.SetFd(fd_);
    cap_port.GetFormat();
    bool cap_ok = cap_port.SetFormat() == 0;

    // The compressed side must be accepted (otherwise the codec is not
    // supported). A stateful decoder may refuse the raw capture format until
    // the stream headers are parsed (SOURCE_CHANGE), so only warn for it.
    bool ok = true;
    if (!out_ok)
        ok = false;
    if (!cap_ok) {
        if (IsRawV4l2Format(cap_format))
            CIX_VAAPI_WARNING("Capture format %s not accepted yet, continuing\n",
                GetFourccString(cap_format));
        else
            ok = false;
    }

    SubscribeEvent(V4L2_EVENT_SOURCE_CHANGE);
    SubscribeEvent(V4L2_EVENT_EOS);
    valid_ = ok;
}

int32_t V4l2Stateful::Open(const char* device_name) {
    fd_ = open(device_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    CIX_VAAPI_CHECK_RETURN_CODE(fd_ >= 0, -1, "Failed to open device %s: %s\n",
        device_name, strerror(errno));
    CIX_VAAPI_INFO("Open device: %s, fd = %d\n", device_name, fd_);

    return 0;
}

int32_t V4l2Stateful::Close() {
    if (fd_ < 0)
        return 0;

    /* Same order as Drain(): M2M capture off before output. */
    cap_port.StreamOff();
    out_port.StreamOff();
    cap_port.DestroyBuffers();
    out_port.DestroyBuffers();

    CIX_VAAPI_INFO("Close device: fd = %d\n", fd_);
    close(fd_);
    fd_ = -1;
    out_port.SetFd(-1);
    cap_port.SetFd(-1);

    return 0;
}

void V4l2Stateful::SubscribeEvent(uint32_t event) {
    struct v4l2_event_subscription sub;
    memset(&sub, 0, sizeof(sub));
    sub.type = event;
    if (xioctl(fd_, VIDIOC_SUBSCRIBE_EVENT, &sub) != 0)
        CIX_VAAPI_WARNING("Failed to subscribe to event %u: %s\n", event, strerror(errno));
}

uint32_t V4l2Stateful::GetNumBitstreamBuffers() {
    return IS_HUGE_RESOLUTION(pic_width, pic_height) ?
        V4L2_STATEFUL_CODEC_NUM_HUGE_BITSTREAM_BUFFERS :
        IS_BIG_RESOLUTION(pic_width, pic_height) ?
            V4L2_STATEFUL_CODEC_NUM_BIG_BITSTREAM_BUFFERS :
            V4L2_STATEFUL_CODEC_NUM_BITSTREAM_BUFFERS;
}

int32_t V4l2Stateful::QueueBuffer(V4l2Buffer *buf) {
    if (buf == nullptr)
        return -1;

    auto v4l2buf = buf->GetV4l2Buffer();
    int32_t ret = xioctl(fd_, VIDIOC_QBUF, v4l2buf);
    if (ret == 0) {
        buf->SetInQueue(true);
        CIX_VAAPI_INFO("Queued type %d buffer %d: ts = %ld\n",
            v4l2buf->type, buf->GetId(), (long)v4l2buf->timestamp.tv_sec);
        for (uint32_t i = 0; i < v4l2buf->length; i++) {
            CIX_VAAPI_DEBUG("    Plane %u: bytesused = %u, data_offset = %u\n",
                i, v4l2buf->m.planes[i].bytesused, v4l2buf->m.planes[i].data_offset);
        }
    } else {
        CIX_VAAPI_ERROR("Failed to queue type %d buffer %d: %s\n",
            v4l2buf->type, buf->GetId(), strerror(errno));
        ret = -1;
    }
    v4l2buf->timestamp.tv_sec = -1;
    return ret;
}

V4l2Buffer *V4l2Stateful::DequeueBuffer(Port &port) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    memset(planes, 0, sizeof(planes));
    memset(&buf, 0, sizeof(buf));
    buf.type = port.GetBufType();
    buf.memory = port.GetMemoryType();
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    int32_t ret = xioctl(fd_, VIDIOC_DQBUF, &buf);
    if (ret != 0) {
        // EAGAIN: nothing to dequeue yet. EPIPE: last buffer already dequeued.
        if (errno != EAGAIN)
            CIX_VAAPI_DEBUG("Failed to dequeue type %d buffer: %s\n", buf.type, strerror(errno));
        if (errno == EPIPE)
            eos.store(true);
        return nullptr;
    }

    auto buffer = port.GetBuffer(buf.index);
    CIX_VAAPI_CHECK_RETURN_NULL(buffer, "Failed to get buffer: %u\n", buf.index);

    buffer->SetInQueue(false);
    if (buf.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        buffer->SetInUse(false);

        auto surface = buffer->GetSurface();
        if (surface != nullptr) {
            // Once the surface is dequeued, unbind it from the context as
            // it should be independent from the context now.
            FreeSurface(surface->GetId());
        }
    } else if (buf.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        auto vbuf = buffer->GetV4l2Buffer();
        vbuf->index = buf.index;
        vbuf->type = buf.type;
        vbuf->bytesused = buf.bytesused;
        vbuf->flags = buf.flags;
        vbuf->field = buf.field;
        vbuf->timestamp = buf.timestamp;
        vbuf->timecode = buf.timecode;
        vbuf->sequence = buf.sequence;
        vbuf->memory = buf.memory;
        vbuf->length = buf.length;
        for (uint32_t i = 0; i < buf.length && i < VIDEO_MAX_PLANES; i++) {
            // Keep our own fd binding for DMABUF buffers.
            int32_t fd = vbuf->m.planes[i].m.fd;
            vbuf->m.planes[i] = buf.m.planes[i];
            if (buf.memory == V4L2_MEMORY_DMABUF)
                vbuf->m.planes[i].m.fd = fd;
        }

        auto surface = buffer->GetSurface();
        if (surface != nullptr) {
            auto surface_id = buf.m.planes[0].bytesused ?
                            buffer->GetUserData() : VA_INVALID_SURFACE;
            surface->SetId(surface_id); // Bind surface ID
            // Once the surface is dequeued, unbind it from the context as
            // it should be independent from the context now.
            FreeSurface(surface_id);
        }
        if (buf.flags & V4L2_BUF_FLAG_LAST)
            eos.store(true);
    }
    CIX_VAAPI_INFO("Dequeued type %d buffer %u: ts = %ld\n",
        buf.type, buf.index, (long)buf.timestamp.tv_sec);
    return buffer;
}

int32_t V4l2Stateful::Poll(int16_t events, int32_t timeout) {
    int32_t ret = 0;
    struct pollfd p;

    if (fd_ < 0)
        return -1;

    const std::lock_guard<std::mutex> lock(mutex);

    // Retry on EINTR (the previous "continue" inside "do { } while (0)" left
    // the loop instead of retrying).
    do {
        memset(&p, 0, sizeof(p));
        p.fd = fd_;
        p.events = events;
        ret = poll(&p, 1, timeout);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        CIX_VAAPI_ERROR("Poll failed: %s\n", strerror(errno));
        return -1;
    }
    if (ret == 0)
        return -1;  // timeout

    CIX_VAAPI_DEBUG("Poll returned with revents 0x%x\n", p.revents);
    // POLLNVAL / POLLHUP used to be ignored, turning Drain() into a busy loop
    // on a closed or failed device.
    CIX_VAAPI_CHECK_RETURN_CODE(!(p.revents & (POLLERR | POLLNVAL | POLLHUP)), -1,
        "Poll returned error event 0x%x\n", p.revents);

    if (p.revents & POLLOUT) {
        auto buf = DequeueBuffer(out_port);
        if (out_port.HandleBuffer(buf) != 0)
            CIX_VAAPI_DEBUG("No output port buffer to dequeue\n");
    }

    if (p.revents & POLLIN) {
        auto buf = DequeueBuffer(cap_port);
        if (buf)
            eos.store(eos.load() || buf->GetV4l2Buffer()->m.planes[0].bytesused == 0);
        if (cap_port.HandleBuffer(buf) != 0)
            CIX_VAAPI_DEBUG("No capture port buffer to dequeue\n");
    }

    if (p.revents & POLLPRI) {
        ret = HandleEvent();
        eos.store(eos.load() || (ret == V4L2_EVENT_EOS));
    }

    return ret;
}

int32_t V4l2Stateful::SetV4l2Control(uint32_t id, int32_t value) {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;
    int ret = xioctl(fd_, VIDIOC_S_CTRL, &ctrl);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to set control 0x%x to %d: %s\n", id, value, strerror(errno));
    return 0;
}

int32_t V4l2Stateful::SetV4l2Parm(struct v4l2_streamparm *parm) {
    int ret = xioctl(fd_, VIDIOC_S_PARM, parm);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to set stream parm: %s\n", strerror(errno));
    return 0;
}

int32_t V4l2Stateful::SetRateControlMode(uint32_t mode) {
    (void)mode;
    return 0;
}

void V4l2Stateful::Drain() {
    CIX_VAAPI_INFO("Start to drain, eos %d\n", eos.load());

    // Nothing can be drained from a device that failed to open or never
    // started streaming. The previous code polled a closed fd forever.
    if (fd_ < 0 || !out_port.IsStreamOn() || !cap_port.IsStreamOn()) {
        cap_port.StreamOff();
        out_port.StreamOff();
        return;
    }

    if (!stopped.load()) {
        if (SendDecCmd(V4L2_DEC_CMD_STOP) == 0)
            stopped.store(true);
        else
            eos.store(true);
    }

    if (!eos.load()) {
        // Queue an extra buffer to capture port for the EOS flag
        auto v4l2buf = GetCapPort().GetFreeBuffer();
        if (v4l2buf == nullptr) {
            uint32_t retry = 0;
            CIX_VAAPI_INFO("No free buffer available, reuse a dequeued buffer for draining\n");
            v4l2buf = cap_port.GetDequeuedBuffer();
            while (v4l2buf == nullptr && retry++ < 10) {
                CIX_VAAPI_INFO("Failed to get a dequeued buffer for draining. "
                    "Poll and try again (%u).\n", retry);
                Poll(POLLPRI | POLLOUT | POLLIN, 100);
                v4l2buf = cap_port.GetDequeuedBuffer();
            }

            if (v4l2buf != nullptr && v4l2buf->GetSurface() == nullptr && surface_pool != nullptr) {
                auto s = surface_pool->FetchSurface();
                if (s == nullptr || !v4l2buf->BindSurface(s))
                    v4l2buf = nullptr;
            }
        }

        if (v4l2buf != nullptr) {
            v4l2buf->ClearData();
            QueueBuffer(v4l2buf);
        } else {
            CIX_VAAPI_ERROR("No capture buffer available for draining\n");
        }

        // Wait for draining to complete: stop on the LAST buffer / EOS event,
        // on the first timeout or error, and in any case after a bounded
        // number of events.
        uint32_t polls = 0;
        while (!eos.load() && polls++ < DRAIN_MAX_POLLS) {
            if (Poll(POLLPRI | POLLOUT | POLLIN, 1000) == -1)
                break;
        }
    }

    cap_port.StreamOff();
    out_port.StreamOff();
}

void V4l2Stateful::RestartIfStopped() {
    if (!stopped.load())
        return;

    CIX_VAAPI_INFO("Restart the decoder after a drain\n");
    SendDecCmd(V4L2_DEC_CMD_START);
    stopped.store(false);
    eos.store(false);
}

int32_t V4l2Stateful::HandleEvent() {
    struct v4l2_event ev;
    memset(&ev, 0, sizeof(ev));
    int32_t ret = xioctl(fd_, VIDIOC_DQEVENT, &ev);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to dequeue event: %s\n", strerror(errno));

    if (ev.type == V4L2_EVENT_SOURCE_CHANGE) {
        CIX_VAAPI_INFO("Received source change event: %u\n", ev.u.src_change.changes);
        // TODO: Handle source change event. For POC, just assume no resolution change.
        if (ev.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) {
            if (SendDecCmd(V4L2_DEC_CMD_START) != 0)
                return -1;
        }
    } else if (ev.type == V4L2_EVENT_EOS) {
        CIX_VAAPI_INFO("Received end of stream event\n");
    }

    return ev.type;
}

int32_t V4l2Stateful::SendDecCmd(uint32_t cmd) {
    CIX_VAAPI_INFO("Send decoder command: %u\n", cmd);
    struct v4l2_decoder_cmd dec_cmd;
    memset(&dec_cmd, 0, sizeof(dec_cmd));
    dec_cmd.cmd = cmd;
    int ret = xioctl(fd_, VIDIOC_DECODER_CMD, &dec_cmd);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to send decoder command %u: %s\n", cmd, strerror(errno));
    return 0;
}

int32_t V4l2Stateful::SyncSurface(VASurfaceID render_target) {
    V4l2Buffer *buf = nullptr;
    uint32_t retries = 0;
    CIX_VAAPI_INFO("Sync surface: %u\n", render_target);
    while (true) {
        buf = GetSurfacePort().FindBufferByRenderTarget(render_target);
        if (buf && buf->IsReady())
            break;

        // The surface may have been released by a previous dequeue already.
        if (!CheckSurfaceOwnership(render_target) && surface_pool &&
            surface_pool->GetSurfaceByID(render_target) != nullptr)
            break;

        if (Poll(POLLPRI | POLLOUT | POLLIN, SYNC_SURFACE_POLL_TIMEOUT_MS) == -1) {
            if (fd_ < 0)
                return -1;
            // Poll timeout may just mean hardware is busy under heavy load (e.g.
            // 4K@60fps transcode). Retry several times before assuming EOS.
            if (++retries < SYNC_SURFACE_MAX_RETRIES) {
                CIX_VAAPI_WARNING("Poll timeout, retry %u/%u\n",
                    retries, SYNC_SURFACE_MAX_RETRIES);
                continue;
            }
            if (!eos.load() && !IsEncoder()) {
                CIX_VAAPI_WARNING("Poll timeout .. Stop the decoder and poll again\n");
                if (SendDecCmd(V4L2_DEC_CMD_STOP) == 0)
                    stopped.store(true);
                eos.store(true);
                retries = SYNC_SURFACE_MAX_RETRIES - 2;  // two more polls to flush
            } else {
                CIX_VAAPI_ERROR("Failed to sync surface %u\n", render_target);
                // Release the surface so the client does not wait forever on it.
                FreeSurface(render_target);
                return -1;
            }
        }
    }
    return 0;
}

bool V4l2Stateful::CheckSurfaceOwnership(VASurfaceID surface) {
    return surface_pool != nullptr && surface_pool->GetContextBySurfaceID(surface) == context;
}
