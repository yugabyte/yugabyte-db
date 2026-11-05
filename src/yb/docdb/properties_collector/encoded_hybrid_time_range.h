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

#include <string>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace yb::docdb {

// The range of the write hybrid times of a set of SST entries.
struct WriteHybridTimeRange {
  HybridTime min;
  HybridTime max;

  std::string ToString() const;
};

// The oldest and newest of a stream of encoded DocHybridTimes, for collectors that see every entry
// of an SST build: they are compared encoded, and only the two extremes are decoded.
class EncodedHybridTimeRange {
 public:
  // encoded is a whole encoded DocHybridTime, e.g. the suffix of a DocDB key.
  void Add(Slice encoded) {
    // Encoded DocHybridTimes sort in reverse time order.
    if (newest_.empty() || encoded.compare(newest_.AsSlice()) < 0) {
      newest_.Assign(encoded);
    }
    if (oldest_.empty() || encoded.compare(oldest_.AsSlice()) > 0) {
      oldest_.Assign(encoded);
    }
  }

  void Add(const EncodedDocHybridTime& encoded) {
    Add(encoded.AsSlice());
  }

  bool empty() const {
    return oldest_.empty();
  }

  // Fails when empty, or when the oldest or the newest does not decode.
  Result<WriteHybridTimeRange> Decode() const;

 private:
  EncodedDocHybridTime oldest_;
  EncodedDocHybridTime newest_;
};

}  // namespace yb::docdb
