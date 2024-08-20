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

#pragma once

#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/tostring.h"

namespace yb::docdb {

// Struct to pass read associated data over multiple layers.
struct ReadOperationData {
  CoarseTimePoint deadline = CoarseTimePoint::max();
  ReadHybridTime read_time = ReadHybridTime::Max();
  const DocDBStatistics* statistics = nullptr;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(deadline, read_time);
  }

  ReadOperationData WithAlteredReadTime(const ReadHybridTime& read_time_) const {
    auto result = *this;
    result.read_time = read_time_;
    return result;
  }

  ReadOperationData WithStatistics(const DocDBStatistics* statistics_) const {
    auto result = *this;
    result.statistics = statistics_;
    return result;
  }

  static ReadOperationData FromReadTime(const ReadHybridTime& read_time) {
    return ReadOperationData {
      .deadline = CoarseTimePoint::max(),
      .read_time = read_time,
    };
  }

  static ReadOperationData FromSingleReadTime(HybridTime read_time_ht) {
    return FromReadTime(ReadHybridTime::SingleTime(read_time_ht));
  }

  static ReadOperationData TEST_FromReadTimeMicros(MicrosTime micros) {
    return FromReadTime(ReadHybridTime::FromMicros(micros));
  }
};

}  // namespace yb::docdb
