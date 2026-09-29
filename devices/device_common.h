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

#ifndef DEVICE_COMMON_H_
#define DEVICE_COMMON_H_

#include <stdint.h>
#include <vector>
#include <mutex>
#include <map>
#include <memory>
#include <va/va_backend.h>
#include "surface_pool.h"
#include "log.h"
#include "dump.h"

#define INVALID_IMAGE_ID VA_INVALID_ID
#define IMAGE_START_ID (0)
#define MAX_NUMBER_OF_IMAGES (256)

#define INVALID_BUFFER_ID VA_INVALID_ID
#define CIX_VAAPI_VENDOR_STR "CIX media driver"
#define CIX_VAAPI_MAX_ENTRYPOINTS 3
#define CIX_VAAPI_MAX_SUBPIC_FORMATS 4
#define CIX_VAAPI_MAX_DISPLAY_ATTRIBUTES 0

#if VA_CHECK_VERSION(1, 18, 0)
#define IS_H264_PROFILE(profile) \
    (profile == VAProfileH264Main || profile == VAProfileH264High || \
     profile == VAProfileH264ConstrainedBaseline || profile == VAProfileH264High10)
#else
#define IS_H264_PROFILE(profile) \
    (profile == VAProfileH264Main || profile == VAProfileH264High || \
     profile == VAProfileH264ConstrainedBaseline)
#endif

#define IS_H265_PROFILE(profile) \
    (profile == VAProfileHEVCMain || profile == VAProfileHEVCMain10)

#define IS_VP9_PROFILE(profile) \
    (profile == VAProfileVP9Profile0 || profile == VAProfileVP9Profile1 || \
     profile == VAProfileVP9Profile2 || profile == VAProfileVP9Profile3)

#define IS_AV1_PROFILE(profile) (profile == VAProfileAV1Profile0)

#define IS_VP8_PROFILE(profile) (profile == VAProfileVP8Version0_3)

#if VA_CHECK_VERSION(1, 18, 0)
#define IS_10BIT_PROFILE(profile) \
    (profile == VAProfileHEVCMain10 || profile == VAProfileVP9Profile2 || \
     profile == VAProfileVP9Profile3 || profile == VAProfileH264High10 || \
     profile == VAProfileAV1Profile0)
#else
#define IS_10BIT_PROFILE(profile) \
    (profile == VAProfileHEVCMain10 || profile == VAProfileVP9Profile2 || \
     profile == VAProfileVP9Profile3)
#endif

#define IS_DECODE_ENTRYPOINT(entry_point) (entry_point == VAEntrypointVLD)
#define IS_ENCODE_ENTRYPOINT(entry_point) (entry_point == VAEntrypointEncSliceLP)
#define IS_VPP_ENTRYPOINT(entry_point) (entry_point == VAEntrypointVideoProc)
#define IS_CODEC_ENTRYPOINT(entry_point) (entry_point == VAEntrypointVLD || entry_point == VAEntrypointEncSliceLP)

#define CIX_VAAPI_MAX_SURFACE_ATTRIBUTES 10

inline void SetSurfaceAttribInteger(VASurfaceAttrib *attrib, VASurfaceAttribType type,
                                    int value, uint32_t flags = VA_SURFACE_ATTRIB_GETTABLE)
{
    attrib->type = type;
    attrib->value.type = VAGenericValueTypeInteger;
    attrib->value.value.i = value;
    attrib->flags = flags;
}

class MediaDeviceV4l2Stateful;
class MediaDeviceCme;

struct DeviceConfig {
    uint32_t id;
    VAProfile profile;
    VAEntrypoint entrypoint;
    std::vector<VAConfigAttrib> attribs;
    DeviceConfig(uint32_t id, VAProfile profile, VAEntrypoint entrypoint) :
        id(id), profile(profile), entrypoint(entrypoint) {}
};

class MediaDevice {
public:
    MediaDevice();
    ~MediaDevice();

    static MediaDevice* CreateDevice();

    int32_t GetMaxProfiles() const;
    int32_t GetMaxEntrypoints() const { return CIX_VAAPI_MAX_ENTRYPOINTS; }
    int32_t GetMaxImageFormats() const;
    int32_t GetMaxSubpicFormats() const { return CIX_VAAPI_MAX_SUBPIC_FORMATS; }
    int32_t GetMaxDisplayAttributes() const { return CIX_VAAPI_MAX_DISPLAY_ATTRIBUTES; }
    const char *GetMaxVendorString() const { return CIX_VAAPI_VENDOR_STR; }
    int32_t QuerySurfaceLengthsAndStrides(
        uint32_t width, uint32_t height, uint32_t format,
        uint32_t lengths[CIX_VAAPI_MAX_PLANES],
        uint32_t pitches[CIX_VAAPI_MAX_PLANES]);
    VAStatus SyncBuffer(VABufferID id);
    VAStatus MapCodedBuffer(VABufferID buf_id, void **pbuf);

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

    VAStatus CreateConfig(
        VAProfile profile,
        VAEntrypoint entrypoint,
        VAConfigAttrib *attrib_list,
        int num_attribs,
        VAConfigID *config_id);

    VAStatus DestroyConfig(VAConfigID config_id);

    VAStatus QueryConfigAttributes(
        VAConfigID config_id,
        VAProfile *profile,
        VAEntrypoint *entrypoint,
        VAConfigAttrib *attrib_list,
        int *num_attribs);

    VAStatus QuerySurfaceAttributes(
        VAConfigID config,
        VASurfaceAttrib *attrib_list,
        unsigned int *num_attribs);

    VAStatus CreateSurfaces(
        int32_t width,
        int32_t height,
        int32_t format,
        int32_t num_surfaces,
        VASurfaceID *surfaces);

    VAStatus CreateSurfaces2(
        int32_t width,
        int32_t height,
        int32_t format,
        int32_t num_surfaces,
        VASurfaceID *surfaces,
        VASurfaceAttrib *attrib_list,
        int32_t num_attribs);

    VAStatus DestroySurfaces(
        VASurfaceID *surface_list,
        int num_surfaces);

    VAStatus CreateContext(
        VAConfigID config_id,
        int picture_width,
        int picture_height,
        int flag,
        VASurfaceID *render_targets,
        int num_render_targets,
        VAContextID *context);

    VAStatus DestroyContext(VAContextID context);

    VAStatus CreateBuffer(
        VAContextID context,
        VABufferType type,
        unsigned int size,
        unsigned int num_elements,
        void *data,
        VABufferID *buf_id);

    VAStatus DestroyBuffer(VABufferID buf_id);

    VAStatus MapBuffer(VABufferID buf_id, void **pbuf);

    VAStatus UnmapBuffer(VABufferID buf_id);

    VAStatus BeginPicture(VAContextID context, VASurfaceID render_target);

    VAStatus RenderPicture(
        VAContextID context,
        VABufferID *buffers,
        int num_buffers);

    VAStatus EndPicture(VAContextID context);

    VAStatus SyncSurface(VASurfaceID render_target);

    VAStatus QueryImageFormats(VAImageFormat *format_list, int *num_formats);

    VAStatus DeriveImage(VASurfaceID surface, VAImage *image);

    VAStatus CreateImage(
        VAImageFormat *format,
        int width,
        int height,
        VAImage *image);

    VAStatus GetImage(
        VASurfaceID surface,
        int x,
        int y,
        unsigned int width,
        unsigned int height,
        VAImageID image);

    VAStatus DestroyImage(VAImageID image);

    VAStatus AcquireBufferHandle(VABufferID buf_id, VABufferInfo *buf_info);

    VAStatus ReleaseBufferHandle(VABufferID buf_id);

    VAStatus ExportSurfaceHandle(
        VASurfaceID surface_id,
        uint32_t mem_type,
        uint32_t flags,
        void *descriptor);

    SurfacePool *GetSurfacePool(uint32_t width, uint32_t height);
    SurfacePool *GetSurfacePool(uint32_t width, uint32_t height, uint32_t format);
    SurfacePool *GetSurfacePoolBySurfaceID(VASurfaceID id);
    uint32_t GetNumPlanes(uint32_t format);
    uint32_t GetRowBpp(uint32_t format, int32_t plane);
    BufferPool *GetBufferPool() { return &buffer_pool; }
    MediaDeviceCme *GetCme() { return cme_.get(); }
    DeviceConfig *GetConfig(VAConfigID id);

    void RemoveContextFromSurfaces(VAContextID context);

private:
    bool Initialize();
    VAStatus FillImageInfo(VAImage *image, SurfacePool *pool, VABufferID buf);

    uint32_t surface_count;
    std::vector<SurfacePool *> surface_pools;
    BufferPool buffer_pool;
    ImagePool image_pool;
    std::vector<DeviceConfig> configs;
    uint32_t config_count;
    DUMP_HANDLE frame_dump;
    typedef std::map<VAContextID, VAConfigID> ContextConfigMap;
    ContextConfigMap context_config_map;
    // Keep this declared after surface_pools so it is destroyed first.
    // MediaDeviceV4l2Stateful::~MediaDeviceV4l2Stateful() may call
    // RemoveContextFromSurfaces(), which traverses surface_pools.
    // cme_ is declared before v4l2_ so it is destroyed after v4l2_ (C++ reverse order).
    std::unique_ptr<MediaDeviceCme> cme_;
    std::unique_ptr<MediaDeviceV4l2Stateful> v4l2_;
    bool initialized_;
};

#endif  // DEVICE_COMMON_H_
