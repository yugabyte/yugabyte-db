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
//
#pragma once

#include <string>
#include <vector>

#include "yb/util/enums.h"

namespace yb {

YB_DEFINE_ENUM(StackTraceTrackingGroup,
               (kDebugging) // For debugging use via inserted yb::TrackStackTrace() calls.
               (kReadIO) // Read I/O syscalls.
               (kWriteIO)); // Write I/O syscalls.

struct StackTraceEntry {
  StackTraceTrackingGroup group;
  std::string symbolized_trace;
  size_t count;
  size_t weight;
};

void TrackStackTrace(
    StackTraceTrackingGroup group = StackTraceTrackingGroup::kDebugging, size_t weight = 1);

void ResetTrackedStackTraces();

MonoTime GetLastStackTraceTrackerResetTime();

std::vector<StackTraceEntry> GetTrackedStackTraces();

void DumpTrackedStackTracesToLog(StackTraceTrackingGroup group);

} // namespace yb
