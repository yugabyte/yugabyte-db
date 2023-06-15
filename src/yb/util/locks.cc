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

#include "yb/util/locks.h"

#include "yb/util/malloc.h"

namespace yb {

size_t PerCpuRwMutex::memory_footprint_excluding_this() const {
  // Because locks_ is a dynamic array of non-trivially-destructable types,
  // the returned pointer from new[] isn't guaranteed to point at the start of
  // a memory block, rendering it useless for malloc_usable_size().
  //
  // Rather than replace locks_ with a vector or something equivalent, we'll
  // just measure the memory footprint using sizeof(), with the understanding
  // that we might be inaccurate due to malloc "slop".
  //
  // See https://code.google.com/p/address-sanitizer/issues/detail?id=395 for
  // more details.
  return n_cpus_ * sizeof(padded_lock);
}

size_t PerCpuRwMutex::memory_footprint_including_this() const {
  return malloc_usable_size(this) + memory_footprint_excluding_this();
}

} // namespace yb
