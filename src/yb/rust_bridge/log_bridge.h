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

// glog access for Rust (common/yb-cpp-host/src/ffi/log.rs).

#pragma once

#include <cstdint>

#include "rust/cxx.h"

namespace yb::rust_bridge {

void log_message(int32_t severity, rust::Str file, uint32_t line, rust::Str message);

bool vlog_is_on(int32_t level);

}  // namespace yb::rust_bridge
