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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "dump.h"

#define BITSTREAM_DUMP_BITS_ENV "CIX_VAAPI_DUMP_BITS"
#define BITSTREAM_DUMP_FRAME_ENV "CIX_VAAPI_DUMP_FRAME"

static const char *DUMP_GetFileName(const char *env_var) {
    if (getenv(env_var)) {
        char filename[256];
        const char *ext = (env_var == BITSTREAM_DUMP_BITS_ENV) ? ".bin" : ".yuv";

        do {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(filename, sizeof(filename), "cix-vaapi-dump-%Y%m%d-%H%M%S", tm_info);
            strcat(filename, ext);
        } while (access(filename, F_OK) == 0); // if file already exists, generate a new filename with updated timestamp

        return strdup(filename);
    }

    return nullptr;
}

DUMP_HANDLE DUMP_OpenBitsFile() {
    const char *filename = DUMP_GetFileName(BITSTREAM_DUMP_BITS_ENV);
    if (filename) {
        FILE *f = fopen(filename, "wb");
        free((void *)filename);
        return f;
    }
    else
        return nullptr;
}

DUMP_HANDLE DUMP_OpenFrameFile() {
    const char *filename = DUMP_GetFileName(BITSTREAM_DUMP_FRAME_ENV);
    if (filename)
        return fopen(filename, "wb");
    else
        return nullptr;
}

int DUMP_Write(DUMP_HANDLE h, void *data, size_t size) {
    if (h) {
        int ret = fwrite(data, 1, size, (FILE *)h);
        fflush((FILE *)h);
        return ret;
    } else
        return 0;
}

int DUMP_Close(DUMP_HANDLE h) {
    if (h)
        return fclose((FILE *)h);
    else
        return 0;
}
