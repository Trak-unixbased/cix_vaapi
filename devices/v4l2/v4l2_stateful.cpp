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
#include <string>
#include <cstring>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include "v4l2_stateful.h"
#include "mvx-v4l2-controls.h"


#define SYNC_SURFACE_POLL_TIMEOUT_MS 1000
#define SYNC_SURFACE_MAX_RETRIES 15

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

V4l2Buffer::V4l2Buffer(int32_t fd, enum v4l2_buf_type type, enum v4l2_memory memory, int32_t index) :
    fd_(fd),
    in_use(false),
    in_queue(false),
    surface(nullptr)
{
    memset(ptr, 0, sizeof(ptr));
    memset(planes, 0, sizeof(planes));
    memset(&buf, 0, sizeof(buf));
    buf.type = type;
    buf.memory = memory;
    buf.index = index;
    buf.length = 3;
    buf.m.planes = planes;
    int ret = ioctl(fd_, VIDIOC_QUERYBUF, &buf);
    CIX_VAAPI_CHECK_RETURN(ret == 0, "Failed to query buffer: %d\n", ret);

    CIX_VAAPI_INFO("Query buffer: type = %d, memory = %d, index = %d, length = %d, fd = %d\n",
        buf.type, buf.memory, buf.index, buf.length, fd_);

    for (int i = 0; i < buf.length; i++) {
        struct v4l2_plane &p = buf.m.planes[i];
        CIX_VAAPI_INFO("    Plane %d: length = %d, bytesused = %d, data_offset = %d, mem_offset = %d\n",
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

void V4l2Buffer::BindSurface(Surface *s) {
    if (surface != nullptr)
        surface->RemoveUser((SurfaceUser)this);
    surface = s;
    surface->AddUser((SurfaceUser)this);
    for (int i = 0; i < buf.length; i++) {
        struct v4l2_plane &p = buf.m.planes[i];
        CIX_VAAPI_CHECK_RETURN(s->GetSize(i) == p.length,
            "Surface plane %d size %d does not match buffer plane size %d\n",
            i, s->GetSize(i), p.length);
        p.m.fd = s->GetFd(i);
    }
    CIX_VAAPI_DEBUG("Bound surface %d to buffer %d\n", surface ? surface->GetId() : -1, GetId());
}

void V4l2Buffer::ClearData() {
    SetUserData(-1);
    buf.flags = 0;
    for (int i = 0; i < buf.length; i++) {
        buf.m.planes[i].bytesused = 0;
        // buf.m.planes[i].data_offset = 0;
    }
}

void *V4l2Buffer::Map(int32_t plane) {
    if (ptr[plane] != nullptr)
        return ptr[plane];

    if (!V4L2_TYPE_IS_MULTIPLANAR(buf.type))
        return nullptr;

    if (buf.memory == V4L2_MEMORY_DMABUF) {
        for (int i = 0; i < buf.length; i++) {
            struct v4l2_plane &p = buf.m.planes[i];
            ptr[i] = mmap(NULL, p.length, PROT_READ | PROT_WRITE, MAP_SHARED, p.m.fd, p.data_offset);
            CIX_VAAPI_INFO("V4L2_MEMORY_DMABUF %d: mmap plane %d: fd = %d, length = %d, offset = %d, ptr = %p\n",
                buf.index, i, p.m.fd, p.length, p.data_offset, ptr[i]);
        }
    } else if (buf.memory == V4L2_MEMORY_MMAP) {
        for (int i = 0; i < buf.length; i++) {
            struct v4l2_plane &p = buf.m.planes[i];
            ptr[i] = mmap(NULL, p.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, p.m.mem_offset);
            CIX_VAAPI_INFO("V4L2_MEMORY_MMAP %d: mmap plane %d: mem_offset = %d, length = %d, offset = %d, ptr = %p, fd = %d\n",
                buf.index, i, p.m.mem_offset, p.length, p.data_offset, ptr[i], fd_);
            CIX_VAAPI_CHECK_RETURN_CODE(ptr[i] != MAP_FAILED, nullptr,
                "Failed to mmap buffer %d plane %d: %d (%s)\n", buf.index, i, errno, strerror(errno));
        }
    }

    return ptr[plane];
}

void V4l2Buffer::Unmap(int32_t plane) {
    struct v4l2_plane &p = buf.m.planes[plane];

    if (!V4L2_TYPE_IS_MULTIPLANAR(buf.type))
        return;

    if (buf.memory == V4L2_MEMORY_MMAP) {
        if (ptr[plane] != nullptr)
            munmap(ptr[plane], p.length);
    } else if (buf.memory == V4L2_MEMORY_DMABUF) {
        if (ptr[plane] != nullptr)
            munmap(ptr[plane], p.length);
    }

    ptr[plane] = nullptr;
}

void V4l2Buffer::Unmap() {
    if (!V4L2_TYPE_IS_MULTIPLANAR(buf.type))
        return;

    for (int i = 0; i < buf.length; i++)
        Unmap(i);
}

Port::Port(int32_t fd, enum v4l2_buf_type type, uint32_t pic_width, uint32_t pic_height, uint32_t format) :
    fd_(fd),
    type(type),
    width(pic_width),
    height(pic_height),
    format(format),
    memory(V4L2_MEMORY_DMABUF),
    streamon(false),
    surface_pool(nullptr)
{
    fmt = { 0 };
    fmt.type = type;
}

void Port::GetFormat() {
    fmt.type = type;
    ioctl(fd_, VIDIOC_G_FMT, &fmt);

    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    CIX_VAAPI_INFO("Get format: width = %d, height = %d, format = %s, planes = %d, fd = %d\n",
        f.width, f.height, GetFourccString(f.pixelformat), f.num_planes, fd_);
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

void Port::SetFormat() {
    struct v4l2_pix_format_mplane &f = fmt.fmt.pix_mp;
    f.width = width;
    f.height = height;
    f.pixelformat = format;
    f.num_planes = GetNumPlanes(format);
    for (int i = 0; i < f.num_planes; i++) {
        uint32_t bpp = GetRowBpp(format, i);
        f.plane_fmt[i].bytesperline = (((width * bpp) >> 3) + 63) & ~63; // Align to 64 bytes
        f.plane_fmt[i].sizeimage = 0;
    }

    int ret = ioctl(fd_, VIDIOC_S_FMT, &fmt);
    CIX_VAAPI_CHECK_RETURN(ret == 0, "Failed to set format: %d\n", ret);

    CIX_VAAPI_INFO("Set format: width = %d, height = %d, format = %s, planes = %d\n",
        f.width, f.height, GetFourccString(f.pixelformat), f.num_planes);
    for (int i = 0; i < f.num_planes; i++) {
        struct v4l2_plane_pix_format *p = &f.plane_fmt[i];
        CIX_VAAPI_INFO("    Plane %d: sizeimage = %d, bytesperline = %d\n",
            i, p->sizeimage, p->bytesperline);
    }
}

void Port::CreateBuffers(uint32_t num_buffers, enum v4l2_memory memory) {
    struct v4l2_requestbuffers reqbuf;
    reqbuf.type = type;
    reqbuf.memory = memory;
    reqbuf.count = num_buffers;
    int ret = ioctl(fd_, VIDIOC_REQBUFS, &reqbuf);
    CIX_VAAPI_CHECK_RETURN(ret == 0, "Failed to request buffer: %d, fd = %d\n", ret, fd_);
    CIX_VAAPI_INFO("Request buffers: type = %d, memory = %d, request = %d, count = %d\n",
        reqbuf.type, reqbuf.memory, num_buffers, reqbuf.count);

    for (int i = 0; i < reqbuf.count; i++) {
        auto buf = new V4l2Buffer(fd_, type, memory, i);
        buffers[buf->GetId()] = buf;
        if (surface_pool != nullptr) {
            auto surface = surface_pool->FetchSurface();
            CIX_VAAPI_CHECK_RETURN(surface->GetPitch(0) == fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
                "Surface plane %d pitch %d does not match buffer plane bytesperline %d\n",
                0, surface->GetPitch(0), fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
            buf->BindSurface(surface);
            surface->SetId(VA_INVALID_SURFACE);
        }
    }

    this->memory = memory;
    // request_delta = num_buffers - reqbuf.count;
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
    int ret = ioctl(fd_, VIDIOC_STREAMON, &type);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to stream on: %d\n", ret);
    CIX_VAAPI_INFO("Stream on: type = %d\n", type);
    streamon = true;
    return 0;
}

int32_t Port::StreamOff() {
    if (!streamon)
        return 0;
    int ret = ioctl(fd_, VIDIOC_STREAMOFF, &type);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to stream off: %d\n", ret);
    CIX_VAAPI_INFO("Stream off: type = %d\n", type);
    streamon = false;
    return 0;
}

void Port::UpdateResolution(uint32_t w, uint32_t h, uint32_t f) {
    if (w == width && h == height && f == format)
        return;

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
    CreateBuffers(0, memory);
    SetFormat();
    CreateBuffers(num_buffers, memory);
    CIX_VAAPI_INFO("Recreated %d buffers for new resolution %dx%d (%s)\n",
        num_buffers, width, height, GetFourccString(format));
    StreamOn();
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

    auto v4l2buf = buf->GetV4l2Buffer();
    if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        // Handle output buffer
    } else if (v4l2buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        // Handle capture buffer
    }

    return 0;
}

V4l2Buffer *Port::FindBufferByRenderTarget(VASurfaceID id) {
    for (auto &buf : buffers) {
        auto surface = buf.second->GetSurface();
        if (surface != nullptr && surface->GetId() == id) {
            CIX_VAAPI_DEBUG("Found buffer %d for surface %d\n", buf.first, id);
            return buf.second;
        }
    }

    return nullptr;
}

V4l2Buffer *Port::GetBufferByUserData(uint32_t userdata) {
    for (auto &buf : buffers) {
        if (buf.second->GetUserData() == userdata) {
            CIX_VAAPI_DEBUG("Found buffer %d with userdata %d\n", buf.first, userdata);
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

// V4l2Buffer *Port::SurfaceToBuffer(Surface *s) {
//     for (auto &buf : buffers) {
//         if (buf.second->GetSurface() == s)
//             return buf.second;
//     }

//     return nullptr;
// }

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
    out_port(fd_, out_type, pic_width, pic_height, out_format),
    cap_port(fd_, cap_type, pic_width, pic_height, cap_format),
    pic_width(pic_width),
    pic_height(pic_height),
    surface_pool(nullptr),
    buffer_pool(nullptr),
    eos(false)
{
    if (Open(device_name) < 0)
        return;

    out_port.SetFd(fd_);
    out_port.GetFormat();
    out_port.SetFormat();

    cap_port.SetFd(fd_);
    cap_port.GetFormat();
    cap_port.SetFormat();

    SubscribeEvent(V4L2_EVENT_SOURCE_CHANGE);
    SubscribeEvent(V4L2_EVENT_EOS);
}

int32_t V4l2Stateful::Open(const char* device_name) {
    fd_ = open(device_name, O_RDWR | O_NONBLOCK);
    CIX_VAAPI_CHECK_RETURN_CODE(fd_ >= 0, -1, "Failed to open device: %s\n", device_name);
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
    struct v4l2_event_subscription sub = { 0 };
    sub.type = event;
    ioctl(fd_, VIDIOC_SUBSCRIBE_EVENT, &sub);
}

uint32_t V4l2Stateful::GetNumBitstreamBuffers() {
    return IS_HUGE_RESOLUTION(pic_width, pic_height) ?
        V4L2_STATEFUL_CODEC_NUM_HUGE_BITSTREAM_BUFFERS :
        IS_BIG_RESOLUTION(pic_width, pic_height) ?
            V4L2_STATEFUL_CODEC_NUM_BIG_BITSTREAM_BUFFERS :
            V4L2_STATEFUL_CODEC_NUM_BITSTREAM_BUFFERS;
}

int32_t V4l2Stateful::QueueBuffer(V4l2Buffer *buf) {
    auto v4l2buf = buf->GetV4l2Buffer();
    // CIX_VAAPI_INFO("Queuing type %d buffer %d: ts = %d\n",
    //     v4l2buf->type, buf->GetId(), v4l2buf->timestamp.tv_sec);
    int32_t ret = ioctl(fd_, VIDIOC_QBUF, v4l2buf);
    if (ret == 0) {
        buf->SetInQueue(true);
        CIX_VAAPI_INFO("Queued type %d buffer %d: ts = %d\n",
            v4l2buf->type, buf->GetId(), v4l2buf->timestamp.tv_sec);
        for (int i = 0; i < v4l2buf->length; i++) {
            CIX_VAAPI_INFO("    Plane %d: bytesused = %d, data_offset = %d\n",
                i, v4l2buf->m.planes[i].bytesused, v4l2buf->m.planes[i].data_offset);
        }
    } else {
        CIX_VAAPI_ERROR("Failed to queue type %d buffer %d: ret = %d\n",
            v4l2buf->type, buf->GetId(), ret);
    }
    v4l2buf->timestamp.tv_sec = -1;
    return ret;
}

V4l2Buffer *V4l2Stateful::DequeueBuffer(Port &port) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf = { 0 };
    buf.type = port.GetBufType();
    buf.memory = port.GetMemoryType();
    buf.length = 3;
    buf.m.planes = planes;

    int32_t ret = ioctl(fd_, VIDIOC_DQBUF, &buf);
    CIX_VAAPI_CHECK_RETURN_NULL(ret == 0, "Failed to dequeue type %d buffer: %d\n", buf.type, ret);

    auto buffer = port.GetBuffer(buf.index);
    CIX_VAAPI_CHECK_RETURN_NULL(buffer, "Failed to get buffer: %d\n", buf.index);

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
        for (int i = 0; i < buf.length; i++)
            vbuf->m.planes[i] = buf.m.planes[i];

        auto surface = buffer->GetSurface();
        if (surface != nullptr) {
            auto surface_id = buf.m.planes[0].bytesused ?
                            buffer->GetUserData() : VA_INVALID_SURFACE;
            surface->SetId(surface_id); // Bind surface ID
            // Once the surface is dequeued, unbind it from the context as
            // it should be independent from the context now.
            FreeSurface(surface_id);
            eos.store(eos.load() || (buf.flags & V4L2_BUF_FLAG_LAST));
        }
    }
    CIX_VAAPI_INFO("Dequeued type %d buffer %d: ts = %d\n",
        buf.type, buf.index, buf.timestamp.tv_sec);
    return buffer;
}

int32_t V4l2Stateful::Poll(int16_t events, int32_t timeout) {
    int32_t ret = 0;

    const std::lock_guard<std::mutex> lock(mutex);
    do {
        struct pollfd p = {
            .fd = fd_, .events = events
        };

        // CIX_VAAPI_DEBUG("Poll from fd %d\n", fd_);
        ret = poll(&p, 1, timeout);
        if (ret < 0) {
            if (errno == EAGAIN)
                continue;
            else
                break;
        }

        CIX_VAAPI_DEBUG("Poll returned with revents 0x%x\n", p.revents);
        CIX_VAAPI_CHECK_RETURN_CODE(!(p.revents & POLLERR), -1,
            "Poll returned error event\n");
        // CIX_VAAPI_CHECK_RETURN_CODE(ret > 0, -1, "Poll timeout\n");
        if (ret == 0)
            return -1;

        if (p.revents & POLLOUT) {
            auto buf = DequeueBuffer(out_port);
            CIX_VAAPI_CHECK_RETURN_CODE(out_port.HandleBuffer(buf) == 0, -1,
                "Failed to handle output port buffer\n");
        }

        if (p.revents & POLLIN) {
            auto buf = DequeueBuffer(cap_port);
            if (buf)
                eos.store(eos.load() || buf->GetV4l2Buffer()->m.planes[0].bytesused == 0);
            CIX_VAAPI_CHECK_RETURN_CODE(cap_port.HandleBuffer(buf) == 0, -1,
                "Failed to handle capture port buffer\n");
        }

        if (p.revents & POLLPRI) {
            ret = HandleEvent();
            eos.store(eos.load() || (ret == V4L2_EVENT_EOS));
            if (ret < 0)
                break;
        }
    } while (0);

    return ret;
}

int32_t V4l2Stateful::SetV4l2Control(uint32_t id, int32_t value) {
    struct v4l2_control ctrl = { 0 };
    ctrl.id = id;
    ctrl.value = value;
    int ret = ioctl(fd_, VIDIOC_S_CTRL, &ctrl);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to set control %d: %d\n", id, ret);
    return ret;
}

int32_t V4l2Stateful::SetV4l2Parm(struct v4l2_streamparm *parm) {
    int ret = ioctl(fd_, VIDIOC_S_PARM, parm);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to set stream parm: %d\n", ret);
    return ret;
}

int32_t V4l2Stateful::SetRateControlMode(uint32_t mode) {
    (void)mode;
    return 0;
}

void V4l2Stateful::Drain() {
    CIX_VAAPI_INFO("Start to drain, eos %d\n", eos.load());
    SendDecCmd(V4L2_DEC_CMD_STOP);

    // Queue an extra buffer to capture port for the EOS flag
    auto v4l2buf = GetCapPort().GetFreeBuffer();
    if (v4l2buf == nullptr) {
        uint32_t retry = 0;
        // Allocate one if no free buffer
        CIX_VAAPI_INFO("No free buffer available, allocate one more buffer for draining\n");
        v4l2buf = cap_port.GetDequeuedBuffer();
        while (v4l2buf == nullptr && retry++ < 10) {
            CIX_VAAPI_INFO("Failed to get a dequeued buffer for draining. \
                Poll and try again (%d).\n", retry);
            Poll(POLLPRI | POLLOUT | POLLIN, 100);
            v4l2buf = cap_port.GetDequeuedBuffer();
        }

        if (v4l2buf == nullptr) {
            CIX_VAAPI_ERROR("Failed to get a dequeued buffer for draining after %d retries. \
                Stop draining.\n", retry);
            cap_port.StreamOff();
            out_port.StreamOff();
            return;
        }

        auto s = surface_pool->FetchSurface();
        v4l2buf->BindSurface(s);
    }
    v4l2buf->ClearData();
    QueueBuffer(v4l2buf);

    // Wait for draining to complete
    while (!eos.load()) {
        if (Poll(POLLPRI | POLLOUT | POLLIN, 1000) == -1)
            eos.store(true);
    }

    cap_port.StreamOff();
    out_port.StreamOff();
}

int32_t V4l2Stateful::HandleEvent() {
    struct v4l2_event ev = { 0 };
    int32_t ret = ioctl(fd_, VIDIOC_DQEVENT, &ev);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1, "Failed to dequeue event: %d\n", ret);

    if (ev.type == V4L2_EVENT_SOURCE_CHANGE) {
        CIX_VAAPI_INFO("Received source change event: %d\n", ev.u.src_change.changes);
        // TODO: Handle source change event. For POC, just assume no resolution change.
        if (ev.u.src_change.changes == V4L2_EVENT_SRC_CH_RESOLUTION) {
            if (SendDecCmd(V4L2_DEC_CMD_START) != 0)
                return -1;
        }
    } else if (ev.type == V4L2_EVENT_EOS) {
        CIX_VAAPI_INFO("Received end of stream event\n");
        // TODO: Handle end of stream event
    }

    return ev.type;
}

int32_t V4l2Stateful::SendDecCmd(uint32_t cmd) {
    CIX_VAAPI_INFO("Send decoder command: %d\n", cmd);
    struct v4l2_decoder_cmd dec_cmd = { 0 };
    dec_cmd.cmd = cmd;
    int ret = ioctl(fd_, VIDIOC_DECODER_CMD, &dec_cmd);
    CIX_VAAPI_CHECK_RETURN_CODE(ret == 0, -1,
        "Failed to send decoder command %d: %d\n", cmd, ret);
    return ret;
}

int32_t V4l2Stateful::SyncSurface(VASurfaceID render_target) {
    V4l2Buffer *buf = nullptr;
    uint32_t retries = 0;
    CIX_VAAPI_INFO("Sync surface: %d\n", render_target);
    while (!buf) {
        // TODO: It's not expected if a buffer is found but not ready. Handle this error.
        buf = GetSurfacePort().FindBufferByRenderTarget(render_target);
        if (buf) CIX_VAAPI_DEBUG("buffer is ready: %d\n", buf->IsReady());
        if (buf && buf->IsReady())
            break;
        else if (Poll(POLLPRI | POLLOUT | POLLIN, SYNC_SURFACE_POLL_TIMEOUT_MS) == -1) {
            // Poll timeout may just mean hardware is busy under heavy load (e.g.
            // 4K@60fps transcode). Retry several times before assuming EOS.
            if (++retries < SYNC_SURFACE_MAX_RETRIES) {
                CIX_VAAPI_WARNING("Poll timeout, retry %u/%u\n",
                    retries, SYNC_SURFACE_MAX_RETRIES);
                continue;
            }
            CIX_VAAPI_WARNING("Poll timeout .. Stop the decoder and poll again\n");
            if (!eos.load()) {
                SendDecCmd(V4L2_DEC_CMD_STOP);
                eos.store(true);
            } else {
                CIX_VAAPI_ERROR("Failed to sync surface %d\n", render_target);
                return -1;
            }
        }
    }
    return 0;
}

bool V4l2Stateful::CheckSurfaceOwnership(VASurfaceID surface) {
    return surface_pool->GetContextBySurfaceID(surface) == context;
}
