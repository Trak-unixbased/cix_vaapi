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

/*
 * Build compatibility layer.
 *
 * 1. CIX libva extensions
 *    The CIX build of libva (cix-libva2, VA-API 1.22.1) adds a
 *    VASequenceParameterBufferType buffer (raw SPS / AV1 sequence header) and
 *    VADecPictureParameterBufferAV1Ext (raw AV1 frame header). Upstream libva
 *    (Ubuntu 26.04 ships libva 2.23 / VA-API 1.23.0) has neither, and the
 *    previous "VA_MICRO_VERSION == 1" test both broke the build with stock
 *    libva and would silently disable the feature on any newer CIX libva.
 *    CMake now probes the installed headers and defines:
 *      CIX_VA_HAS_SEQUENCE_PARAM_BUFFER  (VASequenceParameterBufferType)
 *      CIX_VA_HAS_AV1_EXT                (VADecPictureParameterBufferAV1Ext)
 *
 * 2. Arm/CIX MVX V4L2 driver UAPI
 *    "mvx-v4l2-controls.h" is shipped by the CIX VPU driver package and is not
 *    part of this repository. When it cannot be found, the few identifiers the
 *    driver needs are defined here with the values of the MVX UAPI header
 *    (cixtech/cix_opensource__vpu_driver, driver/linux/mvx-v4l2-controls.h).
 */

#ifndef CIX_VA_COMPAT_H_
#define CIX_VA_COMPAT_H_

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <va/va.h>

#ifndef CIX_VA_HAS_SEQUENCE_PARAM_BUFFER
#define CIX_VA_HAS_SEQUENCE_PARAM_BUFFER 0
#endif

#ifndef CIX_VA_HAS_AV1_EXT
#define CIX_VA_HAS_AV1_EXT 0
#endif

#if defined(__has_include)
#if __has_include("mvx-v4l2-controls.h")
#include "mvx-v4l2-controls.h"
#define CIX_VA_HAVE_MVX_CONTROLS_H 1
#endif
#endif

#ifndef CIX_VA_HAVE_MVX_CONTROLS_H
#ifndef V4L2_CTRL_CLASS_MPEG
#define V4L2_CTRL_CLASS_MPEG V4L2_CTRL_CLASS_CODEC
#endif
/* enum v4l2_cid_mve_video in mvx-v4l2-controls.h */
#define V4L2_CID_MVE_VIDEO_AV1_PROFILE       (V4L2_CTRL_CLASS_MPEG + 0x2000)
#define V4L2_CID_MVE_VIDEO_FRAME_REORDERING  (V4L2_CTRL_CLASS_MPEG + 0x2007)
#endif

#ifndef V4L2_PIX_FMT_AV1
#define V4L2_PIX_FMT_AV1 v4l2_fourcc('A', 'V', '0', '1')
#endif

#ifndef V4L2_PIX_FMT_RGB_3P
#define V4L2_PIX_FMT_RGB_3P v4l2_fourcc('R', 'G', 'B', 'M')
#endif

#ifndef V4L2_PIX_FMT_P010
#define V4L2_PIX_FMT_P010 v4l2_fourcc('P', '0', '1', '0')
#endif

#endif  // CIX_VA_COMPAT_H_
