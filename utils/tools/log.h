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

#ifndef LOG_H_
#define LOG_H_

#include <stdint.h>
#include <stdio.h>
#include <thread>
#include <time.h>
#include <mutex>

enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
};

#include <sstream>

extern std::mutex log_mutex;

inline std::string tid_to_string(std::thread::id tid) {
    std::ostringstream oss;
    oss << tid;
    return oss.str();
}

#define CIX_VAAPI_LOG_PREFIX \
    do { \
        struct timespec ts; \
        timespec_get(&ts, TIME_UTC); \
        struct tm *tm_info = localtime(&ts.tv_sec); \
        char time_buffer[10]; \
        strftime(time_buffer, 10, "%H:%M:%S", tm_info); \
        fprintf(stderr, "%s.%06ld %16s ", \
            time_buffer, ts.tv_nsec / 1000, \
            tid_to_string(std::this_thread::get_id()).c_str()); \
    } while (0)

#define CIX_VAAPI_LOG(level, ...) \
    do { \
        if (log_level >= LOG_LEVEL_##level) { \
            const std::lock_guard<std::mutex> lock(log_mutex); \
            CIX_VAAPI_LOG_PREFIX; \
            fprintf(stderr, "[" #level "] " __VA_ARGS__); \
            fflush(stderr); \
        } \
    } while (0)

#define CIX_VAAPI_LOG_RETURN(level, exp, ...) \
    if (!(exp)) \
        do { \
            CIX_VAAPI_LOG(level, __VA_ARGS__); \
            return; \
        } while (0)

#define CIX_VAAPI_LOG_RETURN_CODE(level, exp, rcode, ...) \
    if (!(exp)) \
        do { \
            CIX_VAAPI_LOG(level, __VA_ARGS__); \
            return rcode; \
        } while (0)

#define CIX_VAAPI_ERROR(...) CIX_VAAPI_LOG(ERROR, __VA_ARGS__)
#define CIX_VAAPI_WARNING(...) CIX_VAAPI_LOG(WARNING, __VA_ARGS__)
#define CIX_VAAPI_INFO(...) CIX_VAAPI_LOG(INFO, __VA_ARGS__)
#define CIX_VAAPI_DEBUG(...) CIX_VAAPI_LOG(DEBUG, __VA_ARGS__)

#define CIX_VAAPI_CHECK_RETURN(exp, ...) CIX_VAAPI_LOG_RETURN(ERROR, exp, __VA_ARGS__)
#define CIX_VAAPI_CHECK_RETURN_CODE(exp, rcode, ...) CIX_VAAPI_LOG_RETURN_CODE(ERROR, exp, rcode, __VA_ARGS__)
#define CIX_VAAPI_CHECK_RETURN_NULL(exp, ...) CIX_VAAPI_LOG_RETURN_CODE(ERROR, exp, nullptr, __VA_ARGS__)

#define CIX_VAAPI_ENTER_FUNCTION CIX_VAAPI_DEBUG("Enter %s\n", __func__)
#define CIX_VAAPI_EXIT_FUNCTION CIX_VAAPI_DEBUG("Exit %s\n", __func__)

extern int32_t log_level;

void SetLogLevel();
char *GetFourccString(uint32_t fourcc);

#endif  // LOG_H_