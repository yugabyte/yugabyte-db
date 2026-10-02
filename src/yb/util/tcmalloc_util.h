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
#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

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
void TCMallocReleaseMemoryToSystem();

void SetTCMallocTotalThreadCacheSize(int64_t max_size);

// Sets tcmalloc properties based on the flags, as well as default values of some tcmalloc-related
// flags.
//
// mem_limit is the maximum amount of memory that can be used by tcmalloc. Usually it is the same as
// the hard limit of the root memory tracker.
void ConfigureTCMalloc(int64_t mem_limit);

int64_t GetTCMallocSamplingPeriod();

void SetTCMallocSamplingPeriod(int64_t sample_period_bytes);

// Returns false unless Google TCMalloc is used and its per-CPU caches are active.
bool TCMallocPerCpuCachesActive();

// Whether the given kernel release (as reported by uname) is in the Linux 6.19.0 through 7.0.13
// range. These kernels do not rewrite rseq cpu_id_start on every return to user space, which
// TCMalloc per-CPU caches rely on, so they can crash or corrupt memory there.
bool IsKernelUnsafeForTCMallocPerCpuCaches(const std::string& kernel_release);

// Logs a warning if TCMalloc per-CPU caches are inactive, and returns an error if they are active
// on a kernel where they are unsafe.
Status CheckTCMallocPerCpuCaches();

}  // namespace yb
