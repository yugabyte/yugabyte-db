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
#include "yb/util/crc.h"

#include <crcutil/interface.h>

#include "yb/gutil/once.h"
#include "yb/util/crc32c_accelerated.h"
#include "yb/util/debug/leakcheck_disabler.h"

namespace yb::crc {

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

namespace {

uint32_t CrcutilExtend(uint32_t crc, const void* data, size_t length) {
  uint64_t state = crc;
  GetCrc32cInstance()->Compute(data, length, &state);
  return static_cast<uint32_t>(state);
}

// Set before AbseilExtend is listed as an implementation.
const internal::Crc32cEngine* abseil_engine = nullptr;

uint32_t AbseilExtend(uint32_t crc, const void* data, size_t length) {
  crc = ~crc;
  abseil_engine->Extend(&crc, data, length);
  return ~crc;
}

std::vector<Crc32cImplementation> DetectImplementations() {
  std::vector<Crc32cImplementation> result;
#if defined(__x86_64__)
  __builtin_cpu_init();
  // The engine's PCLMULQDQ paths are compiled with AVX.
  const bool try_abseil = __builtin_cpu_supports("sse4.2") && __builtin_cpu_supports("pclmul") &&
                          __builtin_cpu_supports("avx");
#else
  // Checks the CPU features itself.
  const bool try_abseil = true;
#endif
  if (try_abseil) {
    abseil_engine = internal::TryNewCRC32AcceleratedX86ARMCombined();
  }
  if (abseil_engine) {
    result.push_back({"abseil", &AbseilExtend});
  }
  result.push_back({"crcutil", &CrcutilExtend});
  return result;
}

Crc32cExtendFunction BestExtend() {
  static const Crc32cExtendFunction extend = Crc32cImplementations().front().extend;
  return extend;
}

} // namespace

const std::vector<Crc32cImplementation>& Crc32cImplementations() {
  static const std::vector<Crc32cImplementation> implementations = DetectImplementations();
  return implementations;
}

uint32_t Crc32c(const void* data, size_t length) {
  return BestExtend()(0, data, length);
}

uint32_t Crc32cExtend(uint32_t crc, const void* data, size_t length) {
  return BestExtend()(crc, data, length);
}

void Crc32Accumulator::Feed(const void* data, size_t length) {
  state_ = BestExtend()(static_cast<uint32_t>(state_), data, length);
}

} // namespace yb::crc
