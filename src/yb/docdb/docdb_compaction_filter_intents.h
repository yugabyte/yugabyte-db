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

#include <atomic>
#include <memory>
#include <vector>

#include "yb/common/hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/rocksdb/compaction_filter.h"

namespace yb {
namespace docdb {

class DocDBIntentsCompactionFilterFactory : public rocksdb::CompactionFilterFactory {
 public:
  explicit DocDBIntentsCompactionFilterFactory(tablet::Tablet* tablet, const KeyBounds* key_bounds);

  ~DocDBIntentsCompactionFilterFactory() override;

  std::unique_ptr<rocksdb::CompactionFilter> CreateCompactionFilter(
      const rocksdb::CompactionFilter::Context& context) override;

  const char* Name() const override;

 private:
  tablet::Tablet* const tablet_;
  const KeyBounds* key_bounds_;
};

}  // namespace docdb
}  // namespace yb
