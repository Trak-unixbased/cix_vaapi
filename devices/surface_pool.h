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
#include <atomic>
#include <map>
#include <mutex>
#include <functional>
#include <va/va_backend.h>
#include "log.h"

#define CIX_VAAPI_MAX_PLANES 3
#define CIX_VAAPI_DEFAULT_BUFFER_LIST_SIZE 64
#define CIX_VAAPI_DEFAULT_IMAGE_LIST_SIZE 64
#define CIX_VA_BUFFER_ID_START 0
#define CIX_VA_MAX_BUFFER_NUM (1 << 16)
#define CIX_VA_IS_BUFFER_ID(id) \
    (id >= CIX_VA_BUFFER_ID_START && \
     id < CIX_VA_BUFFER_ID_START + CIX_VA_MAX_BUFFER_NUM)
#define CIX_VA_IMAGE_ID_START (CIX_VA_BUFFER_ID_START + CIX_VA_MAX_BUFFER_NUM)
#define CIX_VA_MAX_IMAGE_NUM (1 << 16)
#define CIX_VA_IS_IMAGE_ID(id) \
    (id >= CIX_VA_IMAGE_ID_START && \
     id < CIX_VA_IMAGE_ID_START + CIX_VA_MAX_IMAGE_NUM)

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
    Buffer(uint32_t size, const char *dma_heap);
    Buffer(uint32_t size, VABufferType type = VABufferTypeMax);
    // Buffer(int32_t fd, uint32_t offset, uint32_t size);
    ~Buffer();
    void SetId(uint32_t id) { this->id = id; }
    uint32_t GetId() const { return id; }
    void *Map();
    void Unmap();
    void *GetPtr() const { return ptr; }
    int GetFd() const { return fd_; }
    uint32_t GetSize() const { return size; }
    VABufferType GetType() const { return type; }
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
    Buffer *buf;
    BufferInfo(VABufferID id, VABufferType type, Buffer *buf = nullptr) :
        id(id), type(type), buf(buf) {}
};

class BufferPool {
public:
    BufferPool();
    ~BufferPool();
    BufferInfo *GetFreeSlot(VABufferType type, VAContextID ctx);
    BufferInfo *GetBufferInfo(VABufferID id);
    int32_t ReleaseSlot(VABufferID id);

private:
    std::vector<BufferInfo> buffers;
    std::mutex mutex;
};

struct ImageInfo {
    VAImageID id;
    uint32_t width;
    uint32_t height;
    VASurfaceID surface_id;
    ImageInfo(VABufferID id, VASurfaceID surface_id = VA_INVALID_SURFACE) :
        id(id), surface_id(surface_id) {}
};

class ImagePool {
public:
    ImagePool();
    ~ImagePool() {};
    ImageInfo *GetFreeSlot();
    ImageInfo *GetImageInfo(VAImageID id);

private:
    std::vector<ImageInfo> images;
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
        if (buf) delete buf;
    };

    void SetId(VASurfaceID id) { this->id.store(id); }
    VASurfaceID GetId() const { return id.load(); }
    void SetInUse(bool use) { in_use = use; }
    bool IsInUse() const { return in_use; }
    // bool IsMapped() const { return is_mapped; }
    // void SetMapped(bool mapped) { is_mapped = mapped; }
    // int32_t GetFds(std::vector<int32_t> &fds);
    int32_t GetFd(int32_t plane) { return planes[plane].fd; };
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetFormat() { return format; }
    uint32_t GetNumPlanes() { return planes.size(); }
    uint32_t GetOffset(int32_t plane) { return planes[plane].offset; }
    uint32_t GetSize(int32_t plane) { return planes[plane].size; }
    uint32_t GetPitch(int32_t plane) { return planes[plane].pitch; }
    Buffer *GetBuffer() { return buf; }
    void AddUser(SurfaceUser user) { users.push_back(user); }
    void RemoveUser(SurfaceUser user);
    bool HasUser() { return !users.empty(); }
    void SyncForReadStart();
    void SyncForReadEnd();
    void SetCleanupCallback(std::function<void(Surface*)> cb) { cleanup_callback_ = cb; }

private:
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
    // std::atomic<bool> is_mapped;
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
    // void Extend(uint32_t size) { capacity += size; };
    uint32_t GetWidth() { return width; }
    uint32_t GetHeight() { return height; }
    uint32_t GetFormat() { return format_fourcc; }
    uint32_t GetNumPlanes() { return lengths.size(); }
    uint32_t GetSize() { return buffer_size; }
    uint32_t GetSize(int32_t plane) { return lengths[plane]; }
    uint32_t GetPitch(int32_t plane) { return pitches[plane]; }
    uint32_t GetOffset(int32_t plane) { return offsets[plane]; }
    void AddSurfaceID(VASurfaceID id) { info_list.emplace_back(id, VA_INVALID_ID); }
    void RemoveSurfaceID(VASurfaceID id);
    bool HasSurfaceID(VASurfaceID id);
    Surface *FetchSurface();
    // void ReturnSurface(Surface *surface) { surface->SetInUse(false); }
    Surface *GetSurfaceByID(VASurfaceID id);
    void SetSurfaceContext(VASurfaceID id, VAContextID ctx);
    VAContextID GetContextBySurfaceID(VASurfaceID id);
    void RemoveContext(VAContextID id);
    void SetPicOutput(VASurfaceID id, bool pic_output);
    bool GetPicOutput(VASurfaceID id);
    void DestroyUnusedSurface(VASurfaceID id);
    int32_t CreateSurfaceById(VASurfaceID id);

private:
    int32_t CreateSurface();
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