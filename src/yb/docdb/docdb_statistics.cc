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

#include "yb/docdb/docdb_statistics.h"

#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/util/statistics.h"

namespace yb::docdb {

DocDBStatistics::DocDBStatistics():
    regulardb_statistics_(std::make_unique<rocksdb::ScopedStatistics>()),
    intentsdb_statistics_(std::make_unique<rocksdb::ScopedStatistics>()) {}

DocDBStatistics::~DocDBStatistics() {}

rocksdb::Statistics* DocDBStatistics::RegularDBStatistics() const {
  return regulardb_statistics_.get();
}

rocksdb::Statistics* DocDBStatistics::IntentsDBStatistics() const {
  return intentsdb_statistics_.get();
}

void DocDBStatistics::SetHistogramContext(
    std::shared_ptr<rocksdb::Statistics> regulardb_statistics,
    std::shared_ptr<rocksdb::Statistics> intentsdb_statistics) {
  regulardb_statistics_->SetHistogramContext(std::move(regulardb_statistics));
  intentsdb_statistics_->SetHistogramContext(std::move(intentsdb_statistics));
}

void DocDBStatistics::MergeAndClear(
    rocksdb::Statistics* regulardb_statistics,
    rocksdb::Statistics* intentsdb_statistics) {
  regulardb_statistics_->MergeAndClear(regulardb_statistics);
  intentsdb_statistics_->MergeAndClear(intentsdb_statistics);
}

} // namespace yb::docdb
