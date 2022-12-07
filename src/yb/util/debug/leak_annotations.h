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

// API definitions from LLVM lsan_interface.h

extern "C" {
  // Allocations made between calls to __lsan_disable() and __lsan_enable() will
  // be treated as non-leaks. Disable/enable pairs may be nested.
  void __lsan_disable();
  void __lsan_enable();
  // The heap object into which p points will be treated as a non-leak.
  void __lsan_ignore_object(const void *p);
  // The user may optionally provide this function to disallow leak checking
  // for the program it is linked into (if the return value is non-zero). This
  // function must be defined as returning a constant value; any behavior beyond
  // that is unsupported.
  int __lsan_is_turned_off();
  // Calling this function makes LSan enter the leak checking phase immediately.
  // Use this if normal end-of-process leak checking happens too late (e.g. if
  // you have intentional memory leaks in your shutdown code). Calling this
  // function overrides end-of-process leak checking; it must be called at
  // most once per process. This function will terminate the process if there
  // are memory leaks and the exit_code flag is non-zero.
  void __lsan_do_leak_check();
}  // extern "C"

namespace yb {
namespace debug {

class ScopedLSANDisabler {
 public:
  ScopedLSANDisabler() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    __lsan_disable();
#endif
#endif
  }

  ~ScopedLSANDisabler() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
    __lsan_enable();
#endif
#endif
  }
};

} // namespace debug
} // namespace yb
