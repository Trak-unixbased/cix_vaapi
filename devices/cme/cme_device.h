/*
 * Copyright 2026 Cix Technology Group Co., Ltd.
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

#ifndef CME_DEVICE_H_
#define CME_DEVICE_H_

#include <map>
#include <vector>
#include <mutex>
#include <va/va.h>
#include "surface_pool.h"

// CIX_VAAPI_HAVE_CME is set by CMake when libcme (CIX 2D engine, used for
// VA-API video processing) is available. Without it the VPP entrypoint is
// simply not exposed and the driver still provides decode / encode.
#ifndef CIX_VAAPI_HAVE_CME
#define CIX_VAAPI_HAVE_CME 0
#endif

#if CIX_VAAPI_HAVE_CME
#include "cme_context.h"
#else
struct cme_img;
class CmeContext;
#endif

class MediaDevice;
struct DeviceConfig;

class MediaDeviceCme {
public:
    explicit MediaDeviceCme(MediaDevice &media_device);
    ~MediaDeviceCme();
    MediaDeviceCme(const MediaDeviceCme &) = delete;
    MediaDeviceCme &operator=(const MediaDeviceCme &) = delete;

    // Returns false when libcme is missing or failed to initialize.
    bool Initialize();
    bool IsAvailable() const { return available_; }
    // CME VPP supports exactly one VA profile: VAProfileNone.
    int32_t GetMaxProfiles() const { return available_ ? 1 : 0; }

    // ====== Config management (parallel to V4l2Stateful) ======
    VAStatus QueryConfigProfiles(
        VAProfile *profile_list,
        int *num_profiles);

    VAStatus QueryConfigEntrypoints(
        VAProfile profile,
        VAEntrypoint *entrypoint_list,
        int *num_entrypoints);

    VAStatus GetConfigAttributes(
        VAProfile profile,
        VAEntrypoint entrypoint,
        VAConfigAttrib *attrib_list,
        int num_attribs);

    VAStatus QuerySurfaceAttributes(
        const DeviceConfig *config,
        VASurfaceAttrib *attrib_list,
        unsigned int *num_attribs);

    // ====== Context management (parallel to V4l2Stateful) ======
    VAStatus CreateContext(
        const DeviceConfig *config,
        int picture_width,
        int picture_height,
        int flag,
        VASurfaceID *render_targets,
        int num_render_targets,
        VAContextID *context);

    VAStatus DestroyContext(VAContextID context);

    bool HasContextId(VAContextID context) const;

    // ====== Buffer/Picture (parallel to V4l2Stateful) ======
    VAStatus CreateBuffer(
        VAContextID context,
        VABufferType type,
        unsigned int size,
        unsigned int num_elements,
        void *data,
        VABufferID *buf_id);

    VAStatus BeginPicture(VAContextID context, VASurfaceID render_target);

    VAStatus RenderPicture(
        VAContextID context,
        VABufferID *buffers,
        int num_buffers);

    VAStatus EndPicture(VAContextID context);

    VAStatus SyncSurface(VASurfaceID render_target);

    VAStatus QueryImageFormats(VAImageFormat *format_list, int *num_formats);

    // ====== Helper methods ======
    int32_t GetMaxImageFormats() const;

    // ====== CME image lifecycle ======
    cme_img* LookupCmeImg(Buffer* buf);
    void RegisterCmeImg(Buffer* buf, cme_img* img);
    void CleanupCmeImg(Surface* surface);

    // ====== Resource callbacks (implemented in .cpp, parallel to V4l2Stateful) ======
    SurfacePool *GetSurfacePoolBySurfaceID(VASurfaceID id);
    BufferPool *GetBufferPool();
    uint32_t GetNumPlanes(uint32_t format);
    uint32_t GetRowBpp(uint32_t format, int32_t plane);

private:
    MediaDevice &media_device_;
    std::map<uint32_t, CmeContext*> cme_contexts;
    uint32_t cme_context_count;
    std::map<Buffer*, cme_img*> img_map_;
    std::mutex img_map_mutex_;
    bool initialized_;
    bool available_;
};

#endif  // CME_DEVICE_H_
