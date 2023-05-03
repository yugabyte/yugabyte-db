// Copyright (c) YugaByte, Inc.
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
YBCPAllocFn g_palloc_fn = nullptr;
YBCCStringToTextWithLenFn g_cstring_to_text_with_len_fn = nullptr;
}  // anonymous namespace

void YBCSetPAllocFn(YBCPAllocFn palloc_fn) {
  CHECK_NOTNULL(palloc_fn);
  g_palloc_fn = palloc_fn;
}

void* YBCPAlloc(size_t size) {
  CHECK_NOTNULL(g_palloc_fn);
  return g_palloc_fn(size);
}

void YBCSetCStringToTextWithLenFn(YBCCStringToTextWithLenFn fn) {
  CHECK_NOTNULL(fn);
  g_cstring_to_text_with_len_fn = fn;
}

void* YBCCStringToTextWithLen(const char* c, int size) {
  CHECK_NOTNULL(g_cstring_to_text_with_len_fn);
  return g_cstring_to_text_with_len_fn(c, size);
}

YBCStatus ToYBCStatus(const Status& status) {
  return status.RetainStruct();
}

YBCStatus ToYBCStatus(Status&& status) {
  return status.DetachStruct();
}

void FreeYBCStatus(YBCStatus status) {
  // Create Status object that receives control over provided status, so it will be destroyed with
  // yb_status.
  Status yb_status(status, AddRef::kFalse);
}

const char* YBCPAllocStdString(const std::string& s) {
  const size_t len = s.size();
  char* result = static_cast<char*>(YBCPAlloc(len + 1));
  memcpy(result, s.c_str(), len);
  result[len] = 0;
  return result;
}

} // namespace yb::pggate
