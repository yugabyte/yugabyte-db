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

#pragma once

#ifndef __cplusplus
#error "This header can only be included in C++ code"
#endif

#include <cstddef>
#include <string>

#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/util/ybc_util.h"

namespace yb::pggate {

// Convert our C++ status to YBCStatus, which can be returned to PostgreSQL C code.
YBCStatus ToYBCStatus(const Status& status);
YBCStatus ToYBCStatus(Status&& status);
void FreeYBCStatus(YBCStatus status);

void YBCSetPAllocFn(YBCPAllocFn pg_palloc_fn);
void* YBCPAlloc(size_t size);

void YBCSetCStringToTextWithLenFn(YBCCStringToTextWithLenFn fn);
void* YBCCStringToTextWithLen(const char* c, int size);

// Duplicate the given string in memory allocated using PostgreSQL's palloc.
const char* YBCPAllocStdString(const std::string& s);

const char** YBCPAllocStringArray(const std::vector<std::string>& s);

const uint64_t* YBCPAllocStdVectorUint64(const std::vector<uint64_t>& v);

const uint32_t* YBCPAllocStdVectorUint32(const std::vector<uint32_t>& v);

} // namespace yb::pggate
