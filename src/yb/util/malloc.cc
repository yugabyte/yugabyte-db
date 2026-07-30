// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
//
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/malloc.h"

#if defined(__linux__)
#include <malloc.h>
#else
#include <malloc/malloc.h>
#endif // defined(__linux__)

DEFINE_RUNTIME_uint64(malloc_with_check_large_alloc_threshold_bytes, 512 * 1024 * 1024,
    "Log a warning with stack trace for any allocation through malloc_with_check "
    "(used by RefCntBuffer) that is >= this size in bytes. Set to 0 to disable.");
TAG_FLAG(malloc_with_check_large_alloc_threshold_bytes, advanced);

namespace yb {

size_t malloc_usable_size(const void* obj) {
#if defined(__linux__)
  return ::malloc_usable_size(const_cast<void*>(obj));
#else
  return malloc_size(obj);
#endif // defined(__linux__)
}

char* malloc_with_check(size_t size) {
  // Log large allocations with stack trace before attempting the allocation.
  const auto threshold = FLAGS_malloc_with_check_large_alloc_threshold_bytes;
  if (threshold > 0 && size >= threshold) {
    LOG(WARNING) << "Large allocation in malloc_with_check: " << size << " bytes\n"
                 << GetStackTrace();
  }

  auto data = static_cast<char*>(malloc(size));
  CHECK(data != nullptr) << "failed to allocate " << size << " bytes\n" << GetStackTrace();
  return data;
}

} // namespace yb
