// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// Unless required by applicable law or agreed to in writing, software distributed under the License
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

// Utilities for implementations of C wrappers around YugaByte C++ code. This file is not intended
// to be included

#ifndef YB_COMMON_YBC_INTERNAL_H
#define YB_COMMON_YBC_INTERNAL_H

#ifndef __cplusplus
#error "This header can only be included in C++ code"
#endif

#include <cstddef>
#include <string>

#include "yb/common/ybc_util.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Convert our C++ status to YBCStatus, which can be returned to PostgreSQL C code.
YBCStatus ToYBCStatus(const Status& status);
YBCStatus ToYBCStatus(Status&& status);
void FreeYBCStatus(YBCStatus status);

void YBCSetPAllocFn(YBCPAllocFn pg_palloc_fn);
void* YBCPAlloc(size_t size);

void YBCSetCStringToTextWithLenFn(YBCCStringToTextWithLenFn fn);
void* YBCCStringToTextWithLen(const char* c, int size);

// YBCStatus definition for Some common Status.
YBCStatus YBCStatusOK();
YBCStatus YBCStatusNotSupport(const std::string& feature_name);

// Duplicate the given string in memory allocated using PostgreSQL's palloc.
const char* YBCPAllocStdString(const std::string& s);

} // namespace yb

#endif // YB_COMMON_YBC_INTERNAL_H
