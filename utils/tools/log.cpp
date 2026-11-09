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
#include <ctype.h>
#include "log.h"

int32_t log_level = LOG_LEVEL_WARNING;
std::mutex log_mutex;

void SetLogLevel() {
    const char *env_log_level = getenv("CIX_VAAPI_LOG");
    if (env_log_level == NULL || *env_log_level == '\0')
        return;

    char *end = NULL;
    long value = strtol(env_log_level, &end, 10);
    if (end == env_log_level)
        return;  // not a number, keep the default level

    if (value < LOG_LEVEL_ERROR)
        value = LOG_LEVEL_ERROR;
    if (value > LOG_LEVEL_DEBUG)
        value = LOG_LEVEL_DEBUG;
    log_level = (int32_t)value;
}

const char *GetFourccString(uint32_t fourcc) {
    static thread_local char ring[4][5];
    static thread_local unsigned int index = 0;

    char *str = ring[index++ & 3];
    for (int i = 0; i < 4; i++) {
        char c = (char)((fourcc >> (8 * i)) & 0xff);
        str[i] = isprint((unsigned char)c) ? c : '.';
    }
    str[4] = '\0';
    return str;
}
