// Copyright (c) YugabyteDB, Inc.
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

// Convert our C++ status to YbcStatus, which can be returned to PostgreSQL C code.
YbcStatus ToYBCStatus(const Status& status);
YbcStatus ToYBCStatus(Status&& status);
void FreeYBCStatus(YbcStatus status);

void YBCSetPAllocFn(YbcPallocFn pg_palloc_fn);
void* YBCPAlloc(size_t size);

void YBCSetCStringToTextWithLenFn(YbcCstringToTextWithLenFn fn);
void* YBCCStringToTextWithLen(const char* c, int size);

void YBCSetSwitchMemoryContextFn(YbcSwitchMemoryContextFn switch_mem_context_fn);
void* YBCSwitchMemoryContext(void* context);

void YBCSetCreateMemoryContextFn(YbcCreateMemoryContextFn create_mem_context_fn);
void* YBCCreateMemoryContext(void* parent, const char* name);

void YBCSetDeleteMemoryContextFn(YbcDeleteMemoryContextFn delete_mem_context_fn);
void YBCDeleteMemoryContext(void* context);

// Duplicate the given string in memory allocated using PostgreSQL's palloc.
char* YBCPAllocStdString(const std::string& s);

} // namespace yb::pggate
