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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include "surface_pool.h"

#define DMA_HEAP_SYSTEM "/dev/dma_heap/system"

Buffer::Buffer(uint32_t size, const char *dma_heap) :
    ptr(nullptr),
    fd_(-1),
    size(size),
    offset(0),
    type(VABufferTypeMax)
{
    int dma_fd = open(dma_heap, O_RDWR);
    if (dma_fd < 0)
        return;

    struct dma_heap_allocation_data data = {
        .len = size,
        .fd_flags = O_RDWR | O_CLOEXEC,
    };

    if (ioctl(dma_fd, DMA_HEAP_IOCTL_ALLOC, &data) == 0)
        fd_ = data.fd;

    close(dma_fd);
}

Buffer::Buffer(uint32_t size, VABufferType type) :
    ptr(nullptr),
    fd_(-1),
    size(size),
    offset(0),
    type(type)
{
    ptr = malloc(size);
}

// Buffer::Buffer(int32_t fd, uint32_t offset, uint32_t size) :
//     ptr(nullptr),
//     fd_(fd),
//     size(size),
//     offset(offset);
// {
// }

Buffer::~Buffer() {
    if (fd_ >= 0) {
        if (ptr)
            munmap(ptr, size);
        close(fd_);
    } else if (ptr)
        free(ptr);
}

void *Buffer::Map() {
    if (fd_ >= 0) {
        if (ptr)
            return ptr;

        ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    }

    return ptr;
}

void Buffer::Unmap() {
    if (fd_ >= 0 && ptr) {
        munmap(ptr, size);
        ptr = nullptr;
    }
}

void Buffer::Sync(uint64_t flags) {
    if (fd_ >= 0) {
        struct dma_buf_sync sync = { 0 };
        sync.flags = flags;
        int ret = ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync);
        CIX_VAAPI_CHECK_RETURN(ret == 0, "Failed to sync buffer: %d\n", ret);
    }
}

BufferPool::BufferPool() {
    buffers.resize(CIX_VAAPI_DEFAULT_BUFFER_LIST_SIZE,
        BufferInfo(VA_INVALID_ID, VABufferTypeMax, nullptr));

    for (int i = 0; i < buffers.size(); i++)
        buffers[i].id = i;
}

BufferPool::~BufferPool() {
    for (auto &buf : buffers) {
        if (buf.buf)
            delete buf.buf;
    }
}

BufferInfo *BufferPool::GetFreeSlot(VABufferType type, VAContextID ctx) {
    const std::lock_guard<std::mutex> lock(mutex);

    for (int i = 0; i < (int)buffers.size(); i++) {
        if (buffers[i].type == VABufferTypeMax) {
            buffers[i].type = type;
            buffers[i].ctx = ctx;
            return &buffers[i];
        }
    }

    int size = buffers.size();
    int new_size = size << 1;
    CIX_VAAPI_CHECK_RETURN_NULL(new_size <= CIX_VA_MAX_BUFFER_NUM,
        "Reached maximum number of buffers, cannot create more buffer\n");
    buffers.resize(new_size,
        BufferInfo(VA_INVALID_ID, VABufferTypeMax, nullptr));

    for (int i = size; i < buffers.size(); i++)
        buffers[i].id = i;

    buffers[size].type = type;
    buffers[size].ctx = ctx;
    return &buffers.at(size);
}

BufferInfo *BufferPool::GetBufferInfo(VABufferID id) {
    id -= CIX_VA_BUFFER_ID_START;
    if (id >= buffers.size())
        return nullptr;

    return &buffers.at(id);
}

int32_t BufferPool::ReleaseSlot(VABufferID id) {
    const std::lock_guard<std::mutex> lock(mutex);

    uint32_t idx = id - CIX_VA_BUFFER_ID_START;
    if (idx >= buffers.size()) {
        CIX_VAAPI_DEBUG("BufferPool::ReleaseSlot: id=%u -> idx=%u OUT OF RANGE (size=%zu)\n",
            id, idx, buffers.size());
        return -1;
    }

    auto &buf = buffers.at(idx);
    CIX_VAAPI_DEBUG("BufferPool::ReleaseSlot: id=%u, ptr=%p, type=%d, ctx=0x%x, has_buf=%d\n",
        id, &buf, buf.type, buf.ctx, buf.buf != nullptr);
    buf.ctx = VA_INVALID_ID;
    buf.type = VABufferTypeMax;
    if (buf.buf) {
        delete buf.buf;
        buf.buf = nullptr;
    }
    return 0;
}

ImagePool::ImagePool() {
    images.resize(CIX_VAAPI_DEFAULT_IMAGE_LIST_SIZE, ImageInfo(VA_INVALID_ID));

    for (int i = 0; i < images.size(); i++) {
        images[i].id = CIX_VA_IMAGE_ID_START + i;
        images[i].width = 0;
        images[i].height = 0;
    }
}

ImageInfo *ImagePool::GetFreeSlot() {
    for (auto &image : images) {
        if (image.width == 0 && image.height == 0 &&
            image.surface_id == VA_INVALID_SURFACE) {
            return &image;
        }
    }

    int size = images.size();
    int new_size = size << 1;
    CIX_VAAPI_CHECK_RETURN_NULL(new_size <= CIX_VA_MAX_IMAGE_NUM,
        "Reached maximum number of images, cannot create more image\n");
    images.resize(new_size, ImageInfo(VA_INVALID_ID));

    for (int i = size; i < images.size(); i++) {
        images[i].id = CIX_VA_IMAGE_ID_START + i;
        images[i].width = 0;
        images[i].height = 0;
    }

    return &images.at(size);
}

ImageInfo *ImagePool::GetImageInfo(VAImageID id) {
    id -= CIX_VA_IMAGE_ID_START;
    if (id >= images.size())
        return nullptr;

    return &images.at(id);
}

Surface::Surface(uint32_t width, uint32_t height, uint32_t format, uint32_t id) :
    buf(nullptr),
    width(width),
    height(height),
    format(format),
    id(id),
    in_use(false)
{
    std::vector<uint32_t> lengths = {};
    std::vector<uint32_t> pitches = {};
    std::vector<uint32_t> offsets = {};
    uint32_t pitch = width;

    switch (format) {
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * height / 2);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_P010:
            pitch = (width * 2 + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * height / 2);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_I420:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(width * height);
            pitches.push_back(width);
            pitch = (width / 2 + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height / 2);
            pitches.push_back(pitch);
            lengths.push_back(pitch * height / 2);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_Y800:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_YUY2:
        case VA_FOURCC_UYVY:
            pitch = (width * 2 + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_RGBP:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            break;
        default:
            CIX_VAAPI_ERROR("Unsupported format %s\n", GetFourccString(format));
            return;
    }

    uint32_t offset = 0;
    for (int i = 0; i < lengths.size(); i++) {
        offsets.push_back(offset);
        offset += lengths[i];
    }
    CreateBuffer(lengths, pitches, offsets);
}

Surface::Surface(uint32_t width, uint32_t height, uint32_t format, uint32_t id,
    std::vector<uint32_t> &lengths,
    std::vector<uint32_t> &pitches,
    std::vector<uint32_t> &offsets) :
    buf(nullptr),
    width(width),
    height(height),
    format(format),
    id(id),
    in_use(false)
{
    CreateBuffer(lengths, pitches, offsets);
}

void Surface::CreateBuffer(
    std::vector<uint32_t> &lengths,
    std::vector<uint32_t> &pitches,
    std::vector<uint32_t> &offsets)
{
    // Since VAImage supports single buffer surface only, just create one buffer to hold multi-plane here.
    uint32_t size = 0;
    for (int i = 0; i < lengths.size(); i++)
        size += lengths[i];
    buf = new Buffer(size, DMA_HEAP_SYSTEM);

    int fd = buf->GetFd();
    for (int i = 0; i < lengths.size(); i++)
        planes.emplace_back(fd, lengths[i], offsets[i], pitches[i]);
}

void Surface::RemoveUser(SurfaceUser user) {
    for (std::vector<SurfaceUser>::iterator it = users.begin(); it != users.end(); it++) {
        if (*it == user) {
            users.erase(it);
            break;
        }
    }
}

void Surface::SyncForReadStart() {
    buf->Sync(DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
}

void Surface::SyncForReadEnd() {
    buf->Sync(DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
}

SurfacePool::SurfacePool(uint32_t width, uint32_t height,
    uint32_t format_fourcc, uint32_t num_planes,
    uint32_t lengths[CIX_VAAPI_MAX_PLANES],
    uint32_t pitches[CIX_VAAPI_MAX_PLANES]) :
    surfaces(),
    width(width),
    height(height),
    format_fourcc(format_fourcc),
    capacity(0),
    lengths(),
    pitches(),
    offsets(),
    attrib_list(),
    info_list()
{
    CIX_VAAPI_INFO("Created Surface Pool: %dx%d, format %s\n",
        width, height, GetFourccString(format_fourcc));

    uint32_t offset = 0;
    for (int i = 0; i < num_planes; i++) {
        this->lengths.push_back(lengths[i]);
        this->pitches.push_back(pitches[i]);
        this->offsets.push_back(offset);
        offset += lengths[i];
    }
    buffer_size = offset;
}

SurfacePool::~SurfacePool()
{
    for (auto surface : surfaces) {
        if (surface)
            delete surface;
    }
}

void SurfacePool::RemoveSurfaceID(VASurfaceID id)
{
    for (auto it = info_list.begin(); it != info_list.end(); ++it) {
        if (it->id == id) {
            DestroySurface(id);
            info_list.erase(it);
            return;
        }
    }

    CIX_VAAPI_WARNING("Surface ID %d not found in pool\n", id);
}

bool SurfacePool::HasSurfaceID(VASurfaceID id)
{
    for (auto info : info_list) {
        if (info.id == id) {
            return true;
        }
    }

    return false;
}

void SurfacePool::DestroyUnusedSurface(VASurfaceID id)
{
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        auto surface = *it;
        if (surface && surface->GetId() == id && !surface->HasUser()) {
            delete surface;
            it = surfaces.erase(it);
            CIX_VAAPI_INFO("Destroyed unused surface: %d\n", id);
            return;
        } else {
            ++it;
        }
    }
}

int32_t SurfacePool::CreateSurface()
{
    surfaces.push_back(
        new Surface(width, height, format_fourcc,
            VA_INVALID_SURFACE, lengths, pitches, offsets)
    );

    CIX_VAAPI_DEBUG("Created surface in pool: %dx%d, %s, %d planes\n",
        width, height, GetFourccString(format_fourcc), lengths.size());

    return 0;
}

void SurfacePool::DestroySurface(VASurfaceID id)
{
    for (auto it = surfaces.begin(); it != surfaces.end(); ++it) {
        auto surface = *it;
        if (surface->GetId() == id) {
            auto context = GetContextBySurfaceID(id);
            CIX_VAAPI_CHECK_RETURN(context == VA_INVALID_ID,
                "Surface %d is in use by context %d\n", id, context);
            delete surface;
            surfaces.erase(it);
            CIX_VAAPI_INFO("Destroyed surface: %d\n", id);
            return;
        }
    }
}

Surface *SurfacePool::FetchSurface()
{
    for (auto surface : surfaces) {
        if (!surface->IsInUse()) {
            surface->SetInUse(true);
            return surface;
        }
    }

    // No free surface, create a new one
    CreateSurface();
    surfaces.back()->SetInUse(true);
    return surfaces.back();
}

Surface *SurfacePool::GetSurfaceByID(VASurfaceID id)
{
    for (auto surface : surfaces) {
        if (surface->GetId() == id) {
            return surface;
        }
    }

    return nullptr;
}

int32_t SurfacePool::CreateSurfaceById(VASurfaceID id)
{
    // Check if a surface with this ID already exists
    for (auto surface : surfaces) {
        if (surface->GetId() == id) {
            CIX_VAAPI_DEBUG("Surface with ID %d already exists\n", id);
            return 0;
        }
    }

    // Reuse only truly unowned anonymous surfaces. Decoder capture buffers
    // also use VA_INVALID_SURFACE while bound to V4L2 buffers.
    for (auto surface : surfaces) {
        if (surface->GetId() == VA_INVALID_SURFACE && !surface->HasUser()) {
            surface->SetId(id);
            CIX_VAAPI_DEBUG("Assigned ID %d to existing surface\n", id);
            return 0;
        }
    }

    // No existing surface to reuse, create a new one
    surfaces.push_back(
        new Surface(width, height, format_fourcc, id, lengths, pitches, offsets)
    );

    CIX_VAAPI_DEBUG("Created surface with ID %d in pool\n", id);
    return 0;
}

void SurfacePool::SetSurfaceContext(VASurfaceID id, VAContextID ctx)
{
    for (auto &info : info_list) {
        if (info.id == id) {
            CIX_VAAPI_INFO("Set context %d for surface ID %d\n", ctx, id);
            info.ctx = ctx;
            return;
        }
    }
}

VAContextID SurfacePool::GetContextBySurfaceID(VASurfaceID id)
{
    for (auto info : info_list) {
        if (info.id == id) {
            return info.ctx;
        }
    }

    return VA_INVALID_ID;
}

void SurfacePool::RemoveContext(VAContextID id)
{
    CIX_VAAPI_INFO("Remove context %d from surface pool %dx%d\n", id, width, height);
    for (auto it = info_list.begin(); it != info_list.end(); ++it) {
        if (it->ctx == id) {
            it->ctx = VA_INVALID_ID;
        }
    }

    // Clean up surfaces that are no longer used by any context to avoid memory leak
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        auto surface = *it;
        if (surface && !surface->HasUser() && surface->GetId() == VA_INVALID_SURFACE) {
            CIX_VAAPI_INFO("Destroy surface as it's no longer used by any context\n");
            delete surface;
            it = surfaces.erase(it);
        } else {
            ++it;
        }
    }
}

void SurfacePool::SetPicOutput(VASurfaceID id, bool pic_output)
{
    for (auto &info : info_list) {
        if (info.id == id) {
            info.pic_output = pic_output;
            return;
        }
    }
}

bool SurfacePool::GetPicOutput(VASurfaceID id)
{
    for (auto info : info_list) {
        if (info.id == id) {
            return info.pic_output;
        }
    }

    return true;
}