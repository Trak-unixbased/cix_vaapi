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

#ifndef __CME_COMMON_H
#define __CME_COMMON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * query api version
 * @returns
 *       version string, for example, "1.0.1"
 */
const char* query_api_version(void);

/**
 * query all capacity of current platform
 * @returns cme_capacity
 *       struct cme_capacity
 */
const cme_capacity* query_all_capability(void);

/**
 * set hwaccel mode
 * @param hwaccel_mode
 *     CME_HWACCEL_MODE_PERFORMANCE
 *     CME_HWACCEL_MODE_ENERGY_EFFICIENCY
 *     CME_HWACCEL_MODE_LOW_POWER
 */
void cme_set_hwaccel_mode(CME_HWACCEL_MODE  hwaccel_mode);

extern volatile CME_HWACCEL_MODE cme_hwaccel_mode; 

#ifdef __cplusplus
}
#endif

#endif