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

#ifndef CME_CONTEXT_H_
#define CME_CONTEXT_H_

#include <vector>
#include <va/va_backend.h>
#include <va/va_vpp.h>
#include <cme.h>
#include "device_common.h"


struct CmePipelineEntry {
    VABufferID param_buf_id;
    VAProcPipelineParameterBuffer* params;
};

class CmeContext {
public:
    CmeContext(uint32_t id, int width, int height);
    ~CmeContext();

    VAStatus BeginPicture(VASurfaceID render_target, MediaDevice* device);
    VAStatus RenderPicture(VABufferID* buffers, int num_buffers, MediaDevice* device);
    VAStatus EndPicture(MediaDevice* device);
    VAStatus SyncSurface(VASurfaceID render_target, MediaDevice* device);

private:
    int ExecuteSinglePipeline(VAProcPipelineParameterBuffer* params, Surface* output_surface, MediaDevice* device);
    int ExecuteOverlay(std::vector<CmePipelineEntry>& entries, Surface* output_surface, MediaDevice* device);

    int VaFourccToCmeFormat(uint32_t va_fourcc);
    int VaColorStandardToCmeColorSpace(uint32_t va_standard, uint32_t va_range);

    cme_img* CreateCmeImageFromSurface(Surface* surface, SurfacePool* pool, MediaDevice* device);
    Surface* GetSurfaceByIdSynced(VASurfaceID surface_id, MediaDevice* device);

    uint32_t context_id;
    int width;
    int height;
    VASurfaceID output_surface_id;
    std::vector<CmePipelineEntry> pipeline_entries;
};

#endif  // CME_CONTEXT_H_
