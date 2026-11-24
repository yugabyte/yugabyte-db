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

#include <optional>
#include <vector>

#include "yb/common/pgsql_protocol.messages.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/ql_storage_interface.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb::docdb {

class BatchArgIterator;

class PgsqlReadRequestYbctidProvider {
 public:
  PgsqlReadRequestYbctidProvider(
      const DocReadContext& doc_read_context, const PgsqlReadRequestMsg& request,
      PgsqlResponseMsg& response);
  ~PgsqlReadRequestYbctidProvider();

  YbctidBounds Bounds() const { return bounds_; }

  Result<Slice> FetchKey();

  void AddedKeyToResultSet();

  void KeyNotFound();

  Status UpdatePagingState();

 private:
  Status UpdateResponseForTheEndOfBatch();

  bool valid_ = true;
  const PgsqlReadRequestMsg& request_;
  PgsqlResponseMsg& response_;
  std::unique_ptr<BatchArgIterator> batch_arg_it_;
  YbctidBounds bounds_;
  std::optional<size_t> current_position_;
  std::optional<int64> current_order_;
  std::vector<size_t> retry_keys_;
};

}  // namespace yb::docdb
