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

#include "yb/docdb/expiration.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/result.h"

namespace yb {
namespace docdb {

Result<MonoDelta> Expiration::ComputeRelativeTtl(const HybridTime& input_time) {
  if (input_time < write_ht)
    return STATUS(Corruption, "Read time earlier than record write time.");
  if (ttl == Value::kMaxTtl || ttl.IsNegative())
    return ttl;
  MonoDelta elapsed_time = MonoDelta::FromNanoseconds(
      server::HybridClock::GetPhysicalValueNanos(input_time) -
      server::HybridClock::GetPhysicalValueNanos(write_ht));
  // This way, we keep the default TTL, and all negative TTLs are expired.
  MonoDelta new_ttl(ttl);
  return new_ttl -= elapsed_time;
}

std::string Expiration::ToString() const {
  return YB_STRUCT_TO_STRING(ttl, write_ht, always_override);
}

}  // namespace docdb
}  // namespace yb
