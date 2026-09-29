/*
 * Copyright 2025-2026 Cix Technology Group Co., Ltd.
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

#ifndef SEQUENCE_H_
#define SEQUENCE_H_

#include <stdint.h>

struct SequenceHeader {
    void *data;  // Pointer to the raw SPS data
    uint32_t size;  // Size of the SPS data
    int32_t sps_seq_parameter_set_id;
    bool has_cropping;
    struct {
        uint32_t conformance_window_flag;
        uint32_t bit_depth_luma_minus8;
    } bit_offset;
};
#endif  // SEQUENCE_H_