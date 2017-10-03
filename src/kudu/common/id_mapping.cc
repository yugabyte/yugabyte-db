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

#include "kudu/common/id_mapping.h"

#include "kudu/util/malloc.h"

namespace kudu {

const int IdMapping::kNoEntry = -1;

size_t IdMapping::memory_footprint_excluding_this() const {
  if (entries_.capacity() > 0) {
    return kudu_malloc_usable_size(entries_.data());
  } else {
    return 0;
  }
}

size_t IdMapping::memory_footprint_including_this() const {
  return kudu_malloc_usable_size(this) + memory_footprint_excluding_this();
}

} // namespace kudu
