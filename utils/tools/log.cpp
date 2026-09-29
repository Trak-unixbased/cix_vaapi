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

#include <stddef.h>
#include <stdlib.h>
#include "log.h"

int32_t log_level = LOG_LEVEL_WARNING;
std::mutex log_mutex;

void SetLogLevel() {
    const char *env_log_level = getenv("CIX_VAAPI_LOG");
    if (env_log_level != NULL) {
        log_level = atoi(env_log_level);
    }
}

char *GetFourccString(uint32_t fourcc) {
    static char fourcc_str[5] = {0};
    fourcc_str[0] = (fourcc >> 0) & 0xff;
    fourcc_str[1] = (fourcc >> 8) & 0xff;
    fourcc_str[2] = (fourcc >> 16) & 0xff;
    fourcc_str[3] = (fourcc >> 24) & 0xff;
    return fourcc_str;
}
