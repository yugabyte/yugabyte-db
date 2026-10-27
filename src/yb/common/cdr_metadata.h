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

#include <cstdint>
#include <string>

#include "yb/common/hybrid_time.h"

#include "yb/util/tostring.h"

namespace yb {

// Replication origin of a write and its commit time on the source, carried from the YSQL backend to
// DocDB for conflict detection and resolution (CDR).
struct CdrMetadata {
  uint32_t source_origin = 0;
  HybridTime source_write_ht = HybridTime::kInvalid;

  std::string ToString() const { return YB_STRUCT_TO_STRING(source_origin, source_write_ht); }
};

} // namespace yb
