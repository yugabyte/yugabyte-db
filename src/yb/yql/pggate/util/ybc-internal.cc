// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
#include "yb/yql/pggate/util/ybc-internal.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"

using std::string;

namespace yb::pggate {

namespace {
YbcPallocFn g_palloc_fn = nullptr;
YbcCstringToTextWithLenFn g_cstring_to_text_with_len_fn = nullptr;
YbcSwitchMemoryContextFn g_switch_mem_context_fn = nullptr;
YbcCreateMemoryContextFn g_create_mem_context_fn = nullptr;
YbcDeleteMemoryContextFn g_delete_mem_context_fn = nullptr;
}  // anonymous namespace

void YBCSetPAllocFn(YbcPallocFn palloc_fn) {
  CHECK_NOTNULL(palloc_fn);
  g_palloc_fn = palloc_fn;
}

void* YBCPAlloc(size_t size) {
  CHECK_NOTNULL(g_palloc_fn);
  return g_palloc_fn(size);
}

void YBCSetCStringToTextWithLenFn(YbcCstringToTextWithLenFn fn) {
  CHECK_NOTNULL(fn);
  g_cstring_to_text_with_len_fn = fn;
}

void* YBCCStringToTextWithLen(const char* c, int size) {
  CHECK_NOTNULL(g_cstring_to_text_with_len_fn);
  return g_cstring_to_text_with_len_fn(c, size);
}

void YBCSetSwitchMemoryContextFn(YbcSwitchMemoryContextFn switch_mem_context_fn) {
  CHECK_NOTNULL(switch_mem_context_fn);
  g_switch_mem_context_fn = switch_mem_context_fn;
}

void* YBCSwitchMemoryContext(void* context) {
  CHECK_NOTNULL(g_switch_mem_context_fn);
  return g_switch_mem_context_fn(context);
}

void YBCSetCreateMemoryContextFn(YbcCreateMemoryContextFn create_mem_context_fn) {
  CHECK_NOTNULL(create_mem_context_fn);
  g_create_mem_context_fn = create_mem_context_fn;
}

void* YBCCreateMemoryContext(void* parent, const char* name) {
  CHECK_NOTNULL(g_create_mem_context_fn);
  return g_create_mem_context_fn(parent, name);
}

void YBCSetDeleteMemoryContextFn(YbcDeleteMemoryContextFn delete_mem_context_fn) {
  CHECK_NOTNULL(delete_mem_context_fn);
  g_delete_mem_context_fn = delete_mem_context_fn;
}

void YBCDeleteMemoryContext(void* context) {
  CHECK_NOTNULL(g_delete_mem_context_fn);
  g_delete_mem_context_fn(context);
}

YbcStatus ToYBCStatus(const Status& status) {
  return status.RetainStruct();
}

YbcStatus ToYBCStatus(Status&& status) {
  return status.DetachStruct();
}

void FreeYBCStatus(YbcStatus status) {
  // Create Status object that receives control over provided status, so it will be destroyed with
  // yb_status.
  Status yb_status(status, AddRef::kFalse);
}

char* YBCPAllocStdString(const std::string& s) {
  const auto len = s.size();
  auto* result = static_cast<char*>(YBCPAlloc(len + 1));
  memcpy(result, s.c_str(), len);
  result[len] = 0;
  return result;
}

} // namespace yb::pggate
