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

#include "yb/docdb/doc_ttl_util.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"

using std::string;

using yb::HybridTime;

namespace yb {
namespace docdb {

bool HasExpiredTTL(const HybridTime& key_hybrid_time, const MonoDelta &ttl,
                   const HybridTime& read_hybrid_time) {
  if (ttl.Equals(Value::kMaxTtl) || ttl.Equals(Value::kResetTtl)) {
    return false;
  }
  return server::HybridClock::CompareHybridClocksToDelta(
      key_hybrid_time, read_hybrid_time, ttl) > 0;
}

const MonoDelta TableTTL(const Schema& schema) {
  MonoDelta ttl = Value::kMaxTtl;
  if (schema.table_properties().HasDefaultTimeToLive()) {
    uint64_t default_ttl = schema.table_properties().DefaultTimeToLive();
    return default_ttl == kResetTTL ? Value::kMaxTtl : MonoDelta::FromMilliseconds(default_ttl);
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const MonoDelta& default_ttl) {
  MonoDelta ttl;
  if (!value_ttl.Equals(Value::kMaxTtl)) {
    ttl = value_ttl.ToMilliseconds() == kResetTTL ? Value::kMaxTtl : value_ttl;
  } else {
    ttl = default_ttl;
  }
  return ttl;
}

const MonoDelta ComputeTTL(const MonoDelta& value_ttl, const Schema& schema) {
  return ComputeTTL(value_ttl, TableTTL(schema));
}

}  // namespace docdb
}  // namespace yb
