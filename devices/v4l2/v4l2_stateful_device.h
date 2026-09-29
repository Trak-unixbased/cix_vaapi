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

#ifndef V4L2_STATEFUL_DEVICE_H_
#define V4L2_STATEFUL_DEVICE_H_

#include <map>
#include <vector>
#include <linux/videodev2.h>
#include <va/va.h>
#include "v4l2_stateful.h"

class MediaDevice;
class SurfacePool;
struct DeviceConfig;

struct CodecProfile {
    VAProfile profile;
    bool support_dec;
    bool support_enc;
    CodecProfile(VAProfile profile, bool support_dec, bool support_enc) :
        profile(profile), support_dec(support_dec), support_enc(support_enc) {}
};

class MediaDeviceV4l2Stateful {
public:
    explicit MediaDeviceV4l2Stateful(MediaDevice &media_device);
    ~MediaDeviceV4l2Stateful();
    bool Initialize();

    int32_t GetMaxProfiles() const { return supported_profiles.size(); }
    int32_t QuerySurfaceLengthsAndStrides(
        uint32_t width, uint32_t height, uint32_t format,
        uint32_t lengths[CIX_VAAPI_MAX_PLANES],
        uint32_t strides[CIX_VAAPI_MAX_PLANES]);
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

    VAStatus QuerySurfaceAttributes(
        const DeviceConfig *config,
        VASurfaceAttrib *attrib_list,
        unsigned int *num_attribs);

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

private:
    void QueryCodecDevices();
    void QueryDecodeCaps() {}
    void QueryEncodeCaps() {}
    int32_t Open(const char* device_name);
    int32_t Close();
    bool hasCompressedFormat(uint32_t type);
    bool hasUncompressedFormat(uint32_t type);
    bool isDecoder(uint32_t out_type, uint32_t cap_type);
    bool isEncoder(uint32_t out_type, uint32_t cap_type);
    void GetSupportedProfiles();
    void EnumProfiles(uint32_t cid, bool is_enc);
    VAProfile GetVAProfileFromV4l2(uint32_t codec, uint32_t profile);
    const char *GetProfileString(VAProfile profile);
    void EnumFormats(uint32_t type, std::vector<uint32_t> &formats);
    uint32_t GetRateControlModeFromConfig(const DeviceConfig *config) const;
    uint32_t GetV4l2PixFmtFromSurface(VASurfaceID id);
    uint32_t GetV4l2PixFmtFromConfig(const DeviceConfig *config);
    uint32_t GetV4l2PixFmtFromVaFourcc(uint32_t fourcc);
    SurfacePool *GetSurfacePoolBySurfaceID(VASurfaceID id);
    SurfacePool *GetSurfacePool(uint32_t width, uint32_t height);
    BufferPool *GetBufferPool();
    void RemoveContextFromSurfaces(VAContextID context);
    uint32_t GetNumPlanes(uint32_t format);
    uint32_t GetRowBpp(uint32_t format, int32_t plane);

    MediaDevice &media_device_;
    std::string dec_device;
    std::string enc_device;
    int32_t fd_;
    uint32_t image_count;
    uint32_t buffer_count;
    typedef std::map<uint32_t, V4l2Stateful *> CodecMap;
    CodecMap codecs;
    uint32_t codec_count;
    std::vector<CodecProfile> supported_profiles;
    VACodedBufferSegment coded_buffer_segment{};
    bool initialized_;
    std::mutex mutex;
};

#endif  // V4L2_STATEFUL_DEVICE_H_
