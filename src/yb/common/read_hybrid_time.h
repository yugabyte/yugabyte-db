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

#ifndef YB_COMMON_READ_HYBRID_TIME_H
#define YB_COMMON_READ_HYBRID_TIME_H

#include "yb/common/hybrid_time.h"
#include "yb/util/format.h"

namespace yb {

// Hybrid time range used for read.
// `read` - hybrid time of read operation.
// `limit` - hybrid time used to filter entries.
// `read` should be <= `limit`.
//
// `limit` is the maximum time that could have existed on any server at the time the read operation
// was initiated, and is used to decide whether the read operation need to be restarted at a higher
// hybrid time than `read`.
struct ReadHybridTime {
  HybridTime read;
  HybridTime limit;

  static ReadHybridTime Max() {
    return {HybridTime::kMax, HybridTime::kMax};
  }

  static ReadHybridTime SingleTime(HybridTime value) {
    return {value, value};
  }

  static ReadHybridTime FromMicros(MicrosTime micros) {
    return SingleTime(HybridTime::FromMicros(micros));
  }

  static ReadHybridTime FromUint64(uint64_t value) {
    return SingleTime(HybridTime(value));
  }

  template <class PB>
  static ReadHybridTime FromPB(const PB& pb) {
    ReadHybridTime result;
    if (pb.has_read_ht()) {
      result.read = HybridTime(pb.read_ht());
    }
    if (pb.has_read_limit_ht()) {
      result.limit = HybridTime(pb.read_limit_ht());
    } else {
      result.limit = result.read;
    }
    return result;
  }

  std::string ToString() const {
    return Format("{ read: $0 limit: $1", read, limit);
  }
};

inline std::ostream& operator<<(std::ostream& out, const ReadHybridTime& read_time) {
  return out << read_time.ToString();
}

} // namespace yb

#endif // YB_COMMON_READ_HYBRID_TIME_H
