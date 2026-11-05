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

#include "yb/docdb/properties_collector/encoded_hybrid_time_range.h"

#include "yb/util/status_format.h"
#include "yb/util/tostring.h"

namespace yb::docdb {

std::string WriteHybridTimeRange::ToString() const {
  return YB_STRUCT_TO_STRING(min, max);
}

Result<WriteHybridTimeRange> EncodedHybridTimeRange::Decode() const {
  SCHECK(!empty(), IllegalState, "No hybrid times were added");
  auto oldest = VERIFY_RESULT(oldest_.Decode());
  auto newest = VERIFY_RESULT(newest_.Decode());
  return WriteHybridTimeRange{.min = oldest.hybrid_time(), .max = newest.hybrid_time()};
}

}  // namespace yb::docdb
