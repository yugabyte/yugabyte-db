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

// Utilities for encoding and decoding key/value pairs that are used in the DocDB code.

#pragma once

#include "yb/docdb/docdb_fwd.h"
#include "yb/rocksdb/db/compaction.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

class QLRangeBasedFileFilter : public rocksdb::ReadFileFilter {
 public:
  QLRangeBasedFileFilter(const dockv::KeyEntryValues& lower_bounds,
                         const std::vector<bool>& lower_bounds_inclusive_,
                         const dockv::KeyEntryValues& upper_bounds,
                         const std::vector<bool>& upper_bounds_inclusive_);

  bool Filter(const rocksdb::FdWithBoundaries& file) const override;

 private:
  std::vector<dockv::KeyBytes> lower_bounds_;
  std::vector<bool> lower_bounds_inclusive_;
  std::vector<dockv::KeyBytes> upper_bounds_;
  std::vector<bool> upper_bounds_inclusive_;
};

}  // namespace docdb
}  // namespace yb
