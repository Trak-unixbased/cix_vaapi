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

#ifndef DUMP_H_
#define DUMP_H_

typedef void *DUMP_HANDLE;

DUMP_HANDLE DUMP_OpenBitsFile();
DUMP_HANDLE DUMP_OpenFrameFile();
int DUMP_Write(DUMP_HANDLE h, void *data, size_t size);
int DUMP_Close(DUMP_HANDLE h);

#endif  // DUMP_H_

