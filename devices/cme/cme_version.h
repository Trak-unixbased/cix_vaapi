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

#ifndef __CME_VERSION_H
#define __CME_VERSION_H

#define CME_VERSION_STRING(s) #s
#define CME_VERSION_STRING_EXPAND(s) CME_VERSION_STRING(s)

/* CME api verison */
#define CME_API_MAJOR_VERSION       1
#define CME_API_MINOR_VERSION       1
#define CME_API_REVISION_VERSION    0

#define CME_API_VERSION \
    CME_VERSION_STRING_EXPAND(CME_API_MAJOR_VERSION) "." \
    CME_VERSION_STRING_EXPAND(CME_API_MINOR_VERSION) "." \
    CME_VERSION_STRING_EXPAND(CME_API_REVISION_VERSION)
#define CME_API_FULL_VERSION "CME_api version " CME_API_VERSION

#endif