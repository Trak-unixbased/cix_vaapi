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
#include <time.h>
#include <thread>
#include <mutex>
#include <string>
#include <sstream>

enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
};

extern std::mutex log_mutex;
extern int32_t log_level;

inline std::string tid_to_string(std::thread::id tid) {
    std::ostringstream oss;
    oss << tid;
    return oss.str();
}

// localtime_r() is used instead of localtime(): the latter returns a pointer
// to a static buffer shared with the host application.
#define CIX_VAAPI_LOG_PREFIX \
    do { \
        struct timespec ts_; \
        struct tm tm_info_; \
        char time_buffer_[16]; \
        timespec_get(&ts_, TIME_UTC); \
        localtime_r(&ts_.tv_sec, &tm_info_); \
        strftime(time_buffer_, sizeof(time_buffer_), "%H:%M:%S", &tm_info_); \
        fprintf(stderr, "%s.%06ld %16s ", \
            time_buffer_, ts_.tv_nsec / 1000, \
            tid_to_string(std::this_thread::get_id()).c_str()); \
    } while (0)

#define CIX_VAAPI_LOG(level, ...) \
    do { \
        if (log_level >= LOG_LEVEL_##level) { \
            const std::lock_guard<std::mutex> lock_(log_mutex); \
            CIX_VAAPI_LOG_PREFIX; \
            fprintf(stderr, "[cix-vaapi][" #level "] " __VA_ARGS__); \
            fflush(stderr); \
        } \
    } while (0)

// The whole check is wrapped in do { } while (0) so the macros behave like a
// single statement (the previous "if (...) do { } while (0)" form captured a
// following "else" branch of the caller).
#define CIX_VAAPI_LOG_RETURN(level, exp, ...) \
    do { \
        if (!(exp)) { \
            CIX_VAAPI_LOG(level, __VA_ARGS__); \
            return; \
        } \
    } while (0)

#define CIX_VAAPI_LOG_RETURN_CODE(level, exp, rcode, ...) \
    do { \
        if (!(exp)) { \
            CIX_VAAPI_LOG(level, __VA_ARGS__); \
            return rcode; \
        } \
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

void SetLogLevel();

// Returns a printable FOURCC. The string lives in a small per-thread ring
// buffer, so several calls can be used in the same printf() and concurrent
// threads do not overwrite each other's result.
const char *GetFourccString(uint32_t fourcc);

#endif  // LOG_H_
