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

#pragma once

#include <cstdint>
#include <string>

#include "yb/util/tostring.h"

namespace yb {
namespace master {

typedef int32_t PitrCount;

struct LeaderEpoch {
  int64_t leader_term;
  // Required to write to tablets and tables in the sys catalog.
  // Begins at 0 when a master process assumes leadership for the first time.
  // Incremented each time a PITR restore is done.
  // Prevents table and tablet info read from memory before a PITR is begun from
  // being committed after a PITR completes, clobbering pitr writes.
  PitrCount pitr_count;

  explicit LeaderEpoch(int64_t term, PitrCount pitr_count)
      : leader_term(term), pitr_count(pitr_count) {}

  explicit LeaderEpoch(int64_t term) : LeaderEpoch(term, 0) {}

  LeaderEpoch() : LeaderEpoch(-1, 0) {}

  bool operator==(const LeaderEpoch& rhs) const {
    return leader_term == rhs.leader_term && pitr_count == rhs.pitr_count;
  }

  std::string ToString() const { return YB_STRUCT_TO_STRING(leader_term, pitr_count); }
};

}  // namespace master
}  // namespace yb
