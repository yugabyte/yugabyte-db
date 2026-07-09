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
// Forward declaration of yb::MemTracker and its standard alias MemTrackerPtr. Use this header
// instead of yb/util/mem_tracker.h when the consumer only refers to MemTracker by pointer or by
// reference (e.g. `const MemTrackerPtr&` parameters, `std::shared_ptr<MemTracker>` /
// `std::weak_ptr<MemTracker>` members with out-of-line destructors). The full definition lives
// in yb/util/mem_tracker.h and is required when constructing/copying MemTracker by value or
// calling its methods inline.

#pragma once

#include <memory>

namespace yb {

class MemTracker;
using MemTrackerPtr = std::shared_ptr<MemTracker>;

}  // namespace yb
