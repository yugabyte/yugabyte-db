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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/util/crc.h"

#include <crcutil/interface.h>

#include "yb/gutil/once.h"
#include "yb/util/debug/leakcheck_disabler.h"

namespace yb {
namespace crc {

using debug::ScopedLeakCheckDisabler;

static GoogleOnceType crc32c_once = GOOGLE_ONCE_INIT;
static Crc* crc32c_instance = nullptr;

static void InitCrc32cInstance() {
  ScopedLeakCheckDisabler disabler; // CRC instance is never freed.
  // TODO: Is initial = 0 and roll window = 4 appropriate for all cases?
  crc32c_instance = crcutil_interface::CRC::CreateCrc32c(true, 0, 4, nullptr);
}

Crc* GetCrc32cInstance() {
  GoogleOnceInit(&crc32c_once, &InitCrc32cInstance);
  return crc32c_instance;
}

uint32_t Crc32c(const void* data, size_t length) {
  uint64_t crc32 = 0;
  GetCrc32cInstance()->Compute(data, length, &crc32);
  return static_cast<uint32_t>(crc32); // Only uses lower 32 bits.
}

} // namespace crc
} // namespace yb
