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

#include <memory>

#include "yb/rocksdb/util/statistics.h"

namespace yb {

class PgsqlResponsePB;

namespace docdb {

class DocDBStatistics {
 public:
  rocksdb::Statistics* RegularDBStatistics();
  rocksdb::Statistics* IntentsDBStatistics();

  void MergeAndClear(
      rocksdb::Statistics* regulardb_statistics,
      rocksdb::Statistics* intentsdb_statistics);

  // Returns number of metric changes dumped.
  size_t Dump(std::stringstream* out) const;

  void CopyToPgsqlResponse(PgsqlResponsePB* response) const;

 private:
  rocksdb::ScopedStatistics regulardb_statistics_;
  rocksdb::ScopedStatistics intentsdb_statistics_;
};

} // namespace docdb

} // namespace yb
