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

#pragma once

// Utilities for using tcmalloc. Here we abstract away some differences between gperftools
// and Google tcmalloc implementations, as well as the case when tcmalloc is not used.

#include <cstdint>
#include <optional>

#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ReleasePartialAllocPages);

// Returns the tcmalloc property with the given name, or 0 if tcmalloc is not used. In case of an
// error, logs it as DFATAL and returns 0.
uint64_t GetTCMallocProperty(const char* prop);

// Returns the number of bytes of physical memory used by tcmalloc. This excludes any unmapped
// free list pages. Returns 0 if tcmalloc is not being used.
int64_t GetTCMallocPhysicalBytesUsed();

int64_t GetTCMallocCurrentAllocatedBytes();

// This correspondins to the generic.heap_size property of Google TCMalloc, which does not include
// unmapped pages. For gperftools TCMalloc, this function subtracts the unmapped pages.
int64_t GetTCMallocCurrentHeapSizeBytes();

// This is used for the root memtracker.
int64_t GetTCMallocActualHeapSizeBytes();

int64_t GetTCMallocPageHeapFreeBytes();

int64_t GetTCMallocPageHeapUnmappedBytes();

// Attempts to return the given number of bytes to the operating system.
void TCMallocReleaseMemoryToSystem(int64_t bytes);

// Like TCMallocReleaseMemoryToSystem, but Google TCMalloc also releases the free pages that it
// would otherwise keep mapped to cover the peak demand of the last few minutes.
void TCMallocReleaseMemoryToSystemIgnoringRecentDemand(int64_t bytes);

// Like TCMallocReleaseMemoryToSystem, but for this call only, Google TCMalloc keeps the free pages
// in partially used hugepages that cover the peak demand of the given lookbacks instead of those
// of its process-wide lookbacks. Zero disables the corresponding lookback.
void TCMallocReleaseMemoryToSystemWithLookbacks(
    int64_t bytes, int64_t short_interval_ms, int64_t long_interval_ms);

// Returns the objects cached in every per-CPU cache to the central free lists. Does nothing unless
// Google TCMalloc runs with per-CPU caches.
void TCMallocDrainCpuCaches();

void SetTCMallocTotalThreadCacheSize(int64_t max_size);

// Sets tcmalloc properties based on the flags, as well as default values of some tcmalloc-related
// flags.
//
// mem_limit is the maximum amount of memory that can be used by tcmalloc. Usually it is the same as
// the hard limit of the root memory tracker.
void ConfigureTCMalloc(int64_t mem_limit);

int64_t GetTCMallocSamplingPeriod();

void SetTCMallocSamplingPeriod(int64_t sample_period_bytes);

// Starts a thread that runs Google TCMalloc's ProcessBackgroundActions loop for the rest of the
// process lifetime: it reclaims idle per-CPU and transfer caches and releases page heap memory at
// the background release rate. Returns whether the thread runs, which is false if Google TCMalloc
// is not used or does not support background actions on this platform. Does nothing if the thread
// was already started.
Result<bool> StartTCMallocBackgroundThread();

void SetTCMallocBackgroundReleaseRate(int64_t bytes_per_sec);

// Sets how far back Google TCMalloc looks at recent demand when it decides which free pages in
// partially allocated hugepages to keep mapped instead of releasing them. Zero disables the check.
void SetTCMallocSkipSubreleaseIntervals(int32_t short_interval_secs, int32_t long_interval_secs);

// Google TCMalloc raises every release request to at least 10% of the free pages on partially
// released hugepages unless this is disabled, so a small periodic release can turn into a long
// one.
void SetTCMallocReleasePartialAllocPages(ReleasePartialAllocPages enabled);

}  // namespace yb
