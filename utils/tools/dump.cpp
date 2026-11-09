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
#include <fcntl.h>
#include <atomic>
#include "dump.h"

#define BITSTREAM_DUMP_BITS_ENV "CIX_VAAPI_DUMP_BITS"
#define BITSTREAM_DUMP_FRAME_ENV "CIX_VAAPI_DUMP_FRAME"

static std::atomic<unsigned int> dump_counter(0);

static bool DUMP_IsEnabled(const char *env_var) {
    const char *value = getenv(env_var);
    return value != NULL && *value != '\0' && strcmp(value, "0") != 0;
}

// Opens a new dump file. The previous implementation busy-looped until the
// wall clock changed second whenever two dump files were opened in the same
// second (decoder + device frame dump), and leaked the generated file name.
// The PID and a per-process counter now make the name unique, and O_EXCL
// guarantees an existing file is never truncated.
static FILE *DUMP_Open(const char *env_var, const char *ext) {
    if (!DUMP_IsEnabled(env_var))
        return nullptr;

    char stamp[64];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_info);

    for (int attempt = 0; attempt < 100; attempt++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "cix-vaapi-dump-%s-%d-%u%s",
            stamp, (int)getpid(), dump_counter.fetch_add(1), ext);

        int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0)
            continue;

        FILE *f = fdopen(fd, "wb");
        if (f == nullptr)
            close(fd);
        return f;
    }

    return nullptr;
}

DUMP_HANDLE DUMP_OpenBitsFile() {
    return DUMP_Open(BITSTREAM_DUMP_BITS_ENV, ".bin");
}

DUMP_HANDLE DUMP_OpenFrameFile() {
    return DUMP_Open(BITSTREAM_DUMP_FRAME_ENV, ".yuv");
}

int DUMP_Write(DUMP_HANDLE h, void *data, size_t size) {
    if (h == nullptr || data == nullptr || size == 0)
        return 0;

    size_t ret = fwrite(data, 1, size, (FILE *)h);
    fflush((FILE *)h);
    return (int)ret;
}

int DUMP_Close(DUMP_HANDLE h) {
    if (h)
        return fclose((FILE *)h);
    return 0;
}
