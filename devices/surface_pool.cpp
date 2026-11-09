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
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include "surface_pool.h"

#define DMA_HEAP_SYSTEM "/dev/dma_heap/system"
#define DMA_HEAP_ENV "CIX_VAAPI_DMA_HEAP"

static int AllocFromHeap(const char *heap, uint32_t size)
{
    int heap_fd = open(heap, O_RDWR | O_CLOEXEC);
    if (heap_fd < 0)
        return -errno;

    struct dma_heap_allocation_data data;
    memset(&data, 0, sizeof(data));
    data.len = size;
    data.fd_flags = O_RDWR | O_CLOEXEC;

    int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data);
    int err = errno;
    close(heap_fd);
    if (ret != 0)
        return -err;

    return (int)data.fd;
}

Buffer::Buffer(uint32_t size, const char *dma_heap) :
    ptr(nullptr),
    fd_(-1),
    offset(0),
    size(size),
    id(0),
    type(VABufferTypeMax)
{
    // Heap selection order: $CIX_VAAPI_DMA_HEAP, the requested heap, then the
    // CMA heaps exposed by most arm64 kernels. On some distributions
    // /dev/dma_heap/system is root-only; the Debian package installs a udev
    // rule granting access to the "video" and "render" groups.
    const char *candidates[] = {
        getenv(DMA_HEAP_ENV),
        dma_heap,
        "/dev/dma_heap/linux,cma",
        "/dev/dma_heap/reserved",
    };

    int last_error = 0;
    for (const char *heap : candidates) {
        if (heap == nullptr || *heap == '\0')
            continue;
        int fd = AllocFromHeap(heap, size);
        if (fd >= 0) {
            fd_ = fd;
            return;
        }
        last_error = -fd;
    }

    static std::atomic<bool> reported(false);
    if (!reported.exchange(true)) {
        CIX_VAAPI_ERROR("Failed to allocate a %u bytes DMA-BUF from %s (%s). "
            "Check the permissions of /dev/dma_heap/*.\n",
            size, dma_heap ? dma_heap : "(null)", strerror(last_error));
    }
}

Buffer::Buffer(uint32_t size, VABufferType type) :
    ptr(nullptr),
    fd_(-1),
    offset(0),
    size(size),
    id(0),
    type(type)
{
    ptr = calloc(1, size ? size : 1);
}

Buffer::~Buffer() {
    if (fd_ >= 0) {
        if (ptr)
            munmap(ptr, size);
        close(fd_);
    } else {
        free(ptr);
    }
}

void *Buffer::Map() {
    if (fd_ < 0)
        return ptr;

    if (ptr)
        return ptr;

    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (p == MAP_FAILED) {
        CIX_VAAPI_ERROR("Failed to mmap DMA-BUF fd %d (%u bytes): %s\n",
            fd_, size, strerror(errno));
        return nullptr;
    }

    ptr = p;
    return ptr;
}

void Buffer::Unmap() {
    if (fd_ >= 0 && ptr) {
        munmap(ptr, size);
        ptr = nullptr;
    }
}

bool Buffer::Resize(uint32_t new_size) {
    if (fd_ >= 0)
        return false;
    if (new_size <= size)
        return true;

    void *p = realloc(ptr, new_size);
    if (p == nullptr)
        return false;

    memset((uint8_t *)p + size, 0, new_size - size);
    ptr = p;
    size = new_size;
    return true;
}

void Buffer::Sync(uint64_t flags) {
    if (fd_ < 0)
        return;

    struct dma_buf_sync sync;
    memset(&sync, 0, sizeof(sync));
    sync.flags = flags;

    int ret;
    do {
        ret = ioctl(fd_, DMA_BUF_IOCTL_SYNC, &sync);
    } while (ret != 0 && (errno == EINTR || errno == EAGAIN));
    CIX_VAAPI_CHECK_RETURN(ret == 0, "Failed to sync buffer: %s\n", strerror(errno));
}

BufferPool::BufferPool() {
    for (uint32_t i = 0; i < CIX_VAAPI_DEFAULT_BUFFER_LIST_SIZE; i++)
        buffers.emplace_back(CIX_VA_BUFFER_ID_START + i, VABufferTypeMax, nullptr);
}

BufferPool::~BufferPool() {
    for (auto &buf : buffers)
        delete buf.buf;
}

BufferInfo *BufferPool::GetFreeSlot(VABufferType type, VAContextID ctx) {
    const std::lock_guard<std::mutex> lock(mutex);

    for (auto &buf : buffers) {
        if (buf.type == VABufferTypeMax) {
            buf.type = type;
            buf.ctx = ctx;
            buf.size = 0;
            buf.num_elements = 0;
            return &buf;
        }
    }

    uint32_t size = buffers.size();
    uint32_t new_size = size << 1;
    CIX_VAAPI_CHECK_RETURN_NULL(new_size <= CIX_VA_MAX_BUFFER_NUM,
        "Reached maximum number of buffers, cannot create more buffer\n");

    for (uint32_t i = size; i < new_size; i++)
        buffers.emplace_back(CIX_VA_BUFFER_ID_START + i, VABufferTypeMax, nullptr);

    BufferInfo &buf = buffers[size];
    buf.type = type;
    buf.ctx = ctx;
    return &buf;
}

BufferInfo *BufferPool::GetBufferInfo(VABufferID id) {
    const std::lock_guard<std::mutex> lock(mutex);

    uint32_t idx = id - CIX_VA_BUFFER_ID_START;
    if (!CIX_VA_IS_BUFFER_ID(id) || idx >= buffers.size())
        return nullptr;

    BufferInfo &buf = buffers[idx];
    if (buf.type == VABufferTypeMax)
        return nullptr;  // free slot: the buffer was destroyed or never created

    return &buf;
}

int32_t BufferPool::ReleaseSlot(VABufferID id) {
    const std::lock_guard<std::mutex> lock(mutex);

    uint32_t idx = id - CIX_VA_BUFFER_ID_START;
    if (!CIX_VA_IS_BUFFER_ID(id) || idx >= buffers.size()) {
        CIX_VAAPI_DEBUG("BufferPool::ReleaseSlot: id=%u OUT OF RANGE (size=%zu)\n",
            id, buffers.size());
        return -1;
    }

    auto &buf = buffers[idx];
    CIX_VAAPI_DEBUG("BufferPool::ReleaseSlot: id=%u, type=%d, ctx=0x%x, has_buf=%d\n",
        id, buf.type, buf.ctx, buf.buf != nullptr);
    buf.ctx = VA_INVALID_ID;
    buf.type = VABufferTypeMax;
    buf.size = 0;
    buf.num_elements = 0;
    delete buf.buf;
    buf.buf = nullptr;
    return 0;
}

void BufferPool::ReleaseContext(VAContextID ctx) {
    const std::lock_guard<std::mutex> lock(mutex);

    // Buffers belonging to a destroyed context can no longer be used: a
    // later vaMapBuffer() on them must fail instead of reaching a deleted codec.
    for (auto &buf : buffers) {
        if (buf.type != VABufferTypeMax && buf.ctx == ctx)
            buf.ctx = VA_INVALID_ID;
    }
}

ImagePool::ImagePool() {
    for (uint32_t i = 0; i < CIX_VAAPI_DEFAULT_IMAGE_LIST_SIZE; i++)
        images.emplace_back(CIX_VA_IMAGE_ID_START + i);
}

ImageInfo *ImagePool::GetFreeSlot() {
    const std::lock_guard<std::mutex> lock(mutex);

    for (auto &image : images) {
        if (image.width == 0 && image.height == 0 &&
            image.surface_id == VA_INVALID_SURFACE) {
            return &image;
        }
    }

    uint32_t size = images.size();
    uint32_t new_size = size << 1;
    CIX_VAAPI_CHECK_RETURN_NULL(new_size <= CIX_VA_MAX_IMAGE_NUM,
        "Reached maximum number of images, cannot create more image\n");

    for (uint32_t i = size; i < new_size; i++)
        images.emplace_back(CIX_VA_IMAGE_ID_START + i);

    return &images[size];
}

ImageInfo *ImagePool::GetImageInfo(VAImageID id) {
    const std::lock_guard<std::mutex> lock(mutex);

    if (!CIX_VA_IS_IMAGE_ID(id))
        return nullptr;

    uint32_t idx = id - CIX_VA_IMAGE_ID_START;
    if (idx >= images.size())
        return nullptr;

    return &images[idx];
}

Surface::Surface(uint32_t width, uint32_t height, uint32_t format, uint32_t id) :
    buf(nullptr),
    width(width),
    height(height),
    format(format),
    id(id),
    in_use(false)
{
    std::vector<uint32_t> lengths;
    std::vector<uint32_t> pitches;
    std::vector<uint32_t> offsets;
    uint32_t pitch = width;
    uint32_t chroma_height = (height + 1) / 2;

    switch (format) {
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * chroma_height);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_P010:
            pitch = (width * 2 + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * chroma_height);
            pitches.push_back(pitch);
            break;
        case VA_FOURCC_I420:
            pitch = (width + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * height);
            pitches.push_back(pitch);
            pitch = ((width + 1) / 2 + 63) & ~63; // Align to 64 bytes
            lengths.push_back(pitch * chroma_height);
            pitches.push_back(pitch);
            lengths.push_back(pitch * chroma_height);
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
            for (int i = 0; i < 3; i++) {
                lengths.push_back(pitch * height);
                pitches.push_back(pitch);
            }
            break;
        default:
            CIX_VAAPI_ERROR("Unsupported format %s\n", GetFourccString(format));
            return;
    }

    uint32_t offset = 0;
    for (size_t i = 0; i < lengths.size(); i++) {
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
    for (size_t i = 0; i < lengths.size(); i++)
        size += lengths[i];

    if (size == 0) {
        CIX_VAAPI_ERROR("Refusing to allocate an empty %ux%u surface (%s)\n",
            width, height, GetFourccString(format));
        return;
    }

    buf = new Buffer(size, DMA_HEAP_SYSTEM);

    int fd = buf->GetFd();
    for (size_t i = 0; i < lengths.size(); i++)
        planes.emplace_back(fd, lengths[i], offsets[i], pitches[i]);
}

void Surface::RemoveUser(SurfaceUser user) {
    for (auto it = users.begin(); it != users.end(); it++) {
        if (*it == user) {
            users.erase(it);
            break;
        }
    }
}

void Surface::SyncCpuAccessStart() {
    if (buf)
        buf->Sync(DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW);
}

void Surface::SyncCpuAccessEnd() {
    if (buf)
        buf->Sync(DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW);
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
    buffer_size(0),
    lengths(),
    pitches(),
    offsets(),
    attrib_list(),
    info_list()
{
    CIX_VAAPI_INFO("Created Surface Pool: %ux%u, format %s\n",
        width, height, GetFourccString(format_fourcc));

    if (num_planes > CIX_VAAPI_MAX_PLANES)
        num_planes = CIX_VAAPI_MAX_PLANES;

    uint32_t offset = 0;
    for (uint32_t i = 0; i < num_planes; i++) {
        this->lengths.push_back(lengths[i]);
        this->pitches.push_back(pitches[i]);
        this->offsets.push_back(offset);
        offset += lengths[i];
    }
    buffer_size = offset;
}

SurfacePool::~SurfacePool()
{
    for (auto surface : surfaces)
        delete surface;
    surfaces.clear();
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

    CIX_VAAPI_WARNING("Surface ID %u not found in pool\n", id);
}

bool SurfacePool::HasSurfaceID(VASurfaceID id)
{
    for (const auto &info : info_list) {
        if (info.id == id)
            return true;
    }

    return false;
}

void SurfacePool::DestroyUnusedSurface(VASurfaceID id)
{
    for (auto it = surfaces.begin(); it != surfaces.end(); ++it) {
        auto surface = *it;
        if (surface && surface->GetId() == id && !surface->HasUser()) {
            delete surface;
            surfaces.erase(it);
            CIX_VAAPI_INFO("Destroyed unused surface: %u\n", id);
            return;
        }
    }
}

Surface *SurfacePool::CreateSurface(VASurfaceID id)
{
    Surface *surface = new Surface(width, height, format_fourcc, id, lengths, pitches, offsets);
    if (!surface->IsValid()) {
        CIX_VAAPI_ERROR("Failed to allocate surface memory: %ux%u, %s (%u bytes)\n",
            width, height, GetFourccString(format_fourcc), buffer_size);
        delete surface;
        return nullptr;
    }

    surfaces.push_back(surface);
    CIX_VAAPI_DEBUG("Created surface in pool: %ux%u, %s, %zu planes\n",
        width, height, GetFourccString(format_fourcc), lengths.size());
    return surface;
}

void SurfacePool::DestroySurface(VASurfaceID id)
{
    for (auto it = surfaces.begin(); it != surfaces.end(); ++it) {
        auto surface = *it;
        if (surface->GetId() == id) {
            auto context = GetContextBySurfaceID(id);
            CIX_VAAPI_CHECK_RETURN(context == VA_INVALID_ID,
                "Surface %u is in use by context %u\n", id, context);
            if (surface->HasUser()) {
                // Still bound to a V4L2 buffer: detach the ID only, the codec
                // owns the memory and will reuse it.
                surface->SetId(VA_INVALID_SURFACE);
                return;
            }
            delete surface;
            surfaces.erase(it);
            CIX_VAAPI_INFO("Destroyed surface: %u\n", id);
            return;
        }
    }
}

Surface *SurfacePool::FetchSurface()
{
    // A surface is only reusable when nobody references it: not in use, not
    // bound to a V4L2 buffer and not carrying a VA surface ID (a surface
    // assigned by CreateSurfaceById() keeps in_use == false).
    for (auto surface : surfaces) {
        if (!surface->IsInUse() && !surface->HasUser() &&
            surface->GetId() == VA_INVALID_SURFACE) {
            surface->SetInUse(true);
            return surface;
        }
    }

    // No free surface, create a new one
    Surface *surface = CreateSurface(VA_INVALID_SURFACE);
    if (surface)
        surface->SetInUse(true);
    return surface;
}

Surface *SurfacePool::GetSurfaceByID(VASurfaceID id)
{
    if (id == VA_INVALID_SURFACE)
        return nullptr;

    for (auto surface : surfaces) {
        if (surface->GetId() == id)
            return surface;
    }

    return nullptr;
}

int32_t SurfacePool::CreateSurfaceById(VASurfaceID id)
{
    // Check if a surface with this ID already exists
    for (auto surface : surfaces) {
        if (surface->GetId() == id) {
            CIX_VAAPI_DEBUG("Surface with ID %u already exists\n", id);
            return 0;
        }
    }

    // Reuse only truly unowned anonymous surfaces. Decoder capture buffers
    // also use VA_INVALID_SURFACE while bound to V4L2 buffers.
    for (auto surface : surfaces) {
        if (surface->GetId() == VA_INVALID_SURFACE && !surface->HasUser()) {
            surface->SetId(id);
            CIX_VAAPI_DEBUG("Assigned ID %u to existing surface\n", id);
            return 0;
        }
    }

    // No existing surface to reuse, create a new one
    if (CreateSurface(id) == nullptr)
        return -1;

    CIX_VAAPI_DEBUG("Created surface with ID %u in pool\n", id);
    return 0;
}

void SurfacePool::SetSurfaceContext(VASurfaceID id, VAContextID ctx)
{
    for (auto &info : info_list) {
        if (info.id == id) {
            CIX_VAAPI_DEBUG("Set context %u for surface ID %u\n", ctx, id);
            info.ctx = ctx;
            return;
        }
    }
}

VAContextID SurfacePool::GetContextBySurfaceID(VASurfaceID id)
{
    for (const auto &info : info_list) {
        if (info.id == id)
            return info.ctx;
    }

    return VA_INVALID_ID;
}

void SurfacePool::RemoveContext(VAContextID id)
{
    CIX_VAAPI_INFO("Remove context %u from surface pool %ux%u\n", id, width, height);
    for (auto &info : info_list) {
        if (info.ctx == id)
            info.ctx = VA_INVALID_ID;
    }

    // Clean up surfaces that are no longer used by any context to avoid memory leak
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        auto surface = *it;
        if (surface && !surface->HasUser() && surface->GetId() == VA_INVALID_SURFACE) {
            CIX_VAAPI_DEBUG("Destroy surface as it's no longer used by any context\n");
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
    for (const auto &info : info_list) {
        if (info.id == id)
            return info.pic_output;
    }

    return true;
}
