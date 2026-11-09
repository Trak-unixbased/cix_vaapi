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

#ifndef SURFACE_POOL_H_
#define SURFACE_POOL_H_

#include <stdint.h>
#include <vector>
#include <deque>
#include <atomic>
#include <map>
#include <mutex>
#include <functional>
#include <va/va_backend.h>
#include "log.h"

#define CIX_VAAPI_MAX_PLANES 3
#define CIX_VAAPI_DEFAULT_BUFFER_LIST_SIZE 64
#define CIX_VAAPI_DEFAULT_IMAGE_LIST_SIZE 64
#define CIX_VA_BUFFER_ID_START 0u
#define CIX_VA_MAX_BUFFER_NUM (1u << 16)
#define CIX_VA_IS_BUFFER_ID(id) \
    ((uint32_t)(id) - CIX_VA_BUFFER_ID_START < CIX_VA_MAX_BUFFER_NUM)
#define CIX_VA_IMAGE_ID_START (CIX_VA_BUFFER_ID_START + CIX_VA_MAX_BUFFER_NUM)
#define CIX_VA_MAX_IMAGE_NUM (1u << 16)
#define CIX_VA_IS_IMAGE_ID(id) \
    ((uint32_t)(id) >= CIX_VA_IMAGE_ID_START && \
     (uint32_t)(id) - CIX_VA_IMAGE_ID_START < CIX_VA_MAX_IMAGE_NUM)

struct Plane {
    int32_t fd;
    uint32_t size;
    uint32_t offset;
    uint32_t pitch;
    Plane(int32_t fd, uint32_t size, uint32_t offset, uint32_t pitch) :
        fd(fd), size(size), offset(offset), pitch(pitch) {}
};

class Buffer {
public:
    // DMA-BUF backed buffer allocated from a dma-heap. If the requested heap
    // cannot be used, the other usual heaps are tried (see surface_pool.cpp).
    Buffer(uint32_t size, const char *dma_heap);
    // Plain system memory buffer.
    explicit Buffer(uint32_t size, VABufferType type = VABufferTypeMax);
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    void SetId(uint32_t id) { this->id = id; }
    uint32_t GetId() const { return id; }
    void *Map();
    void Unmap();
    void *GetPtr() const { return ptr; }
    int GetFd() const { return fd_; }
    uint32_t GetSize() const { return size; }
    VABufferType GetType() const { return type; }
    bool IsValid() const { return fd_ >= 0 || ptr != nullptr; }
    bool IsDmaBuf() const { return fd_ >= 0; }
    // Grows a system memory buffer, keeping its content. Returns false on
    // failure or when called on a DMA-BUF.
    bool Resize(uint32_t new_size);
    void Sync(uint64_t flags);

private:
    void *ptr;
    int fd_;
    uint32_t offset;
    uint32_t size;
    uint32_t id;
    VABufferType type;
};

struct BufferInfo {
    VABufferID id;
    VABufferType type;
    VAContextID ctx;
    uint32_t size;
    uint32_t num_elements;
    Buffer *buf;
    BufferInfo(VABufferID id, VABufferType type, Buffer *buf = nullptr) :
        id(id), type(type), ctx(VA_INVALID_ID), size(0), num_elements(0), buf(buf) {}
};

// std::deque is used instead of std::vector so that growing the pool never
// invalidates BufferInfo pointers handed out earlier.
class BufferPool {
public:
    BufferPool();
    ~BufferPool();
    BufferInfo *GetFreeSlot(VABufferType type, VAContextID ctx);
    BufferInfo *GetBufferInfo(VABufferID id);
    int32_t ReleaseSlot(VABufferID id);
    void ReleaseContext(VAContextID ctx);

private:
    std::deque<BufferInfo> buffers;
    std::mutex mutex;
};

struct ImageInfo {
    VAImageID id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    VASurfaceID surface_id;
    ImageInfo(VAImageID id, VASurfaceID surface_id = VA_INVALID_SURFACE) :
        id(id), width(0), height(0), format(0), surface_id(surface_id) {}
};

class ImagePool {
public:
    ImagePool();
    ~ImagePool() {}
    ImageInfo *GetFreeSlot();
    ImageInfo *GetImageInfo(VAImageID id);

private:
    std::deque<ImageInfo> images;
    std::mutex mutex;
};

typedef void * SurfaceUser;
class Surface {
public:
    Surface(uint32_t width, uint32_t height, uint32_t format, uint32_t id);
    Surface(uint32_t width, uint32_t height, uint32_t format, uint32_t id,
        std::vector<uint32_t> &lengths,
        std::vector<uint32_t> &pitches,
        std::vector<uint32_t> &offsets);
    ~Surface() {
        if (cleanup_callback_)
            cleanup_callback_(this);
        delete buf;
    }
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;

    void SetId(VASurfaceID id) { this->id.store(id); }
    VASurfaceID GetId() const { return id.load(); }
    void SetInUse(bool use) { in_use = use; }
    bool IsInUse() const { return in_use; }
    bool IsValid() const { return buf != nullptr && buf->IsDmaBuf(); }
    int32_t GetFd(int32_t plane) { return PlaneValid(plane) ? planes[plane].fd : -1; }
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetFormat() { return format; }
    uint32_t GetNumPlanes() { return planes.size(); }
    uint32_t GetOffset(int32_t plane) { return PlaneValid(plane) ? planes[plane].offset : 0; }
    uint32_t GetSize(int32_t plane) { return PlaneValid(plane) ? planes[plane].size : 0; }
    uint32_t GetPitch(int32_t plane) { return PlaneValid(plane) ? planes[plane].pitch : 0; }
    Buffer *GetBuffer() { return buf; }
    void AddUser(SurfaceUser user) { users.push_back(user); }
    void RemoveUser(SurfaceUser user);
    bool HasUser() { return !users.empty(); }
    // CPU access bracketing for the DMA-BUF (read and write: the client may
    // fill an encoder input surface through vaDeriveImage + vaMapBuffer).
    void SyncCpuAccessStart();
    void SyncCpuAccessEnd();
    void SetCleanupCallback(std::function<void(Surface*)> cb) { cleanup_callback_ = cb; }

private:
    bool PlaneValid(int32_t plane) const { return plane >= 0 && (size_t)plane < planes.size(); }
    void CreateBuffer(
        std::vector<uint32_t> &lengths,
        std::vector<uint32_t> &pitches,
        std::vector<uint32_t> &offsets);
    std::vector<Plane> planes;
    Buffer *buf;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    std::atomic<VASurfaceID> id;
    std::atomic<bool> in_use;
    std::vector<SurfaceUser> users;
    std::function<void(Surface*)> cleanup_callback_;
};

struct SurfaceInfo {
    VASurfaceID id;
    VAContextID ctx; // The context which owns this surface
    bool pic_output; // The picture in this surface should be output by decoder or not
    SurfaceInfo(VASurfaceID id, VAContextID ctx) : id(id), ctx(ctx), pic_output(true) {}
};

class SurfacePool {
public:
    SurfacePool(uint32_t width, uint32_t height,
        uint32_t format_fourcc, uint32_t num_planes,
        uint32_t lengths[CIX_VAAPI_MAX_PLANES],
        uint32_t pitches[CIX_VAAPI_MAX_PLANES]);
    ~SurfacePool();
    bool Match(uint32_t width, uint32_t height, uint32_t format) {
        return (this->width == width && this->height == height && this->format_fourcc == format);
    }
    bool Match(uint32_t width, uint32_t height) {
        return (this->width == width && this->height == height);
    }
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetFormat() { return format_fourcc; }
    uint32_t GetNumPlanes() { return lengths.size(); }
    uint32_t GetSize() { return buffer_size; }
    uint32_t GetSize(int32_t plane) { return PlaneValid(plane) ? lengths[plane] : 0; }
    uint32_t GetPitch(int32_t plane) { return PlaneValid(plane) ? pitches[plane] : 0; }
    uint32_t GetOffset(int32_t plane) { return PlaneValid(plane) ? offsets[plane] : 0; }
    void AddSurfaceID(VASurfaceID id) { info_list.emplace_back(id, VA_INVALID_ID); }
    void RemoveSurfaceID(VASurfaceID id);
    bool HasSurfaceID(VASurfaceID id);
    // Returns nullptr when the DMA-BUF of a new surface cannot be allocated.
    Surface *FetchSurface();
    Surface *GetSurfaceByID(VASurfaceID id);
    void SetSurfaceContext(VASurfaceID id, VAContextID ctx);
    VAContextID GetContextBySurfaceID(VASurfaceID id);
    void RemoveContext(VAContextID id);
    void SetPicOutput(VASurfaceID id, bool pic_output);
    bool GetPicOutput(VASurfaceID id);
    void DestroyUnusedSurface(VASurfaceID id);
    int32_t CreateSurfaceById(VASurfaceID id);
    bool IsEmpty() const { return info_list.empty() && surfaces.empty(); }

private:
    bool PlaneValid(int32_t plane) const { return plane >= 0 && (size_t)plane < lengths.size(); }
    Surface *CreateSurface(VASurfaceID id);
    void DestroySurface(VASurfaceID id);
    std::vector<Surface *> surfaces;
    uint32_t width;
    uint32_t height;
    uint32_t format_fourcc;
    uint32_t capacity;
    uint32_t buffer_size;
    std::vector<uint32_t> lengths;
    std::vector<uint32_t> pitches;
    std::vector<uint32_t> offsets;
    std::vector<VASurfaceAttrib> attrib_list;
    std::vector<SurfaceInfo> info_list; // just record the surface IDs but not bind to surfaces
};

#endif  // SURFACE_POOL_H_
