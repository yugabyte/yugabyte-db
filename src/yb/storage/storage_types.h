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
//

#pragma once

#include <utility>

#include "yb/storage/storage_fwd.h"

#include "yb/util/enums.h"

namespace yb::storage {

YB_DEFINE_ENUM(FilterDecision, (kKeep)(kDiscard));
YB_DEFINE_ENUM(FlushAbility, (kNoNewData)(kHasNewData)(kAlreadyFlushing));

// A {smallest, largest} pair of frontiers.
struct UserFrontierRange {
  UserFrontierPtr smallest;
  UserFrontierPtr largest;
};

// Which parts of FrontierInfo should be computed by GetFrontiers.
YB_DEFINE_ENUM(FrontierKind, (kFlushed)(kInMemorySmallest)(kInMemoryLargest));
using FrontierKinds = EnumBitSet<FrontierKind>;

// Aggregated frontier information that GetFrontiers computes atomically (under a single lock), so
// that the flushed and in-memory views are mutually consistent.
struct FrontierInfo {
  // Frontier of the data that has already been flushed to disk.
  UserFrontierPtr flushed;
  // {smallest, largest} frontiers of the in-memory (not yet flushed) state.
  UserFrontierRange in_memory;
};

} // namespace yb::storage
