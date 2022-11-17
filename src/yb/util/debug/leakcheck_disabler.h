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
#pragma once

#include "yb/gutil/macros.h"
#include "yb/util/debug/leak_annotations.h"

namespace yb {
namespace debug {

// Scoped object that generically disables LSAN leak checking in a given scope.
// While this object is alive, calls to "new" will not be checked for leaks.
class ScopedLeakCheckDisabler {
 public:
  ScopedLeakCheckDisabler() {}

 private:
  ScopedLSANDisabler lsan_disabler;

  DISALLOW_COPY_AND_ASSIGN(ScopedLeakCheckDisabler);
};

#if defined(__has_feature)
  #if __has_feature(address_sanitizer)
    #define DISABLE_ASAN __attribute__((no_sanitize("address")))
  #endif
#endif
#ifndef DISABLE_ASAN
#define DISABLE_ASAN
#endif

#if defined(__has_feature)
  #if __has_feature(address_sanitizer)
    #define DISABLE_UBSAN __attribute__((no_sanitize("undefined")))
  #endif
#endif
#ifndef DISABLE_UBSAN
#define DISABLE_UBSAN
#endif

} // namespace debug
} // namespace yb
