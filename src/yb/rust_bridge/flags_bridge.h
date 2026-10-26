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


// gflags access for Rust (common/yb-cpp-host/src/ffi/flags.rs, used by common/yb-flags). gflags stays the only flag registry: flags declared in
// Rust are registered with it at startup, before the command line is parsed, so --helpxml,
// flagfiles, /varz and SetFlag treat them like flags defined in C++.

#pragma once

#include <memory>

#include "rust/cxx.h"

#include "yb/util/flags/flags_callback.h"

namespace yb::rust_bridge {

struct FlagWatcher;  // Rust type, defined by the bridge.

// The current value of a flag in gflags' string form. Throws if there is no such flag.
rust::String GetFlagValue(rust::Str name);

// Registers a new flag. `type` is one of bool, int32, int64, uint64, double, string; `tags` are
// yb flag tags such as "runtime" or "advanced". Must run before gflags parses the command line.
void DefineFlag(
    rust::Str type, rust::Str name, rust::Str default_value, rust::Str help, rust::Str file,
    const rust::Vec<rust::String>& tags);

// Calls back into Rust with the new value whenever the flag is set, and once now.
class FlagSubscription {
 public:
  explicit FlagSubscription(FlagCallbackRegistration registration)
      : registration_(std::move(registration)) {}
  ~FlagSubscription() { registration_.Deregister(); }

 private:
  FlagCallbackRegistration registration_;
};

std::unique_ptr<FlagSubscription> WatchFlag(rust::Str name, rust::Box<FlagWatcher> watcher);

}  // namespace yb::rust_bridge
