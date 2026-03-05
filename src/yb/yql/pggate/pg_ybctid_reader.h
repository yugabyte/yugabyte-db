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
#include <span>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/gutil/macros.h"

#include "yb/yql/pggate/pg_session_fwd.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_doc_op.h"

#include "yb/util/result.h"

namespace yb::pggate {

struct YbctidReaderOptions {
  std::optional<int> rowmark{};
  std::optional<int> pg_wait_policy{};
  std::optional<int> docdb_wait_policy{};
  std::optional<PgSessionRunOperationMarker> run_marker{};
};

class YbctidReader {
 public:
  using ReadResult = Result<std::span<LightweightTableYbctid>>;
  using Options = YbctidReaderOptions;

  class BatchAccessor {
   public:
    ~BatchAccessor() {
      if (PREDICT_TRUE(IsActive())) {
        reader_.Clear();
      }
    }

    void Add(const LightweightTableYbctid& ybctid) {
      if (PREDICT_TRUE(IsActive())) {
        reader_.Add(ybctid);
      } else {
        DCHECK(false) << "The batch is inactive";
      }
    }

    ReadResult Read(
        PgOid database_id, const TableLocalityMap& tables_locality, const Options& options = {}) {
      RSTATUS_DCHECK(IsActive(), IllegalState, "Read from inactive batch is not allowed");
      return reader_.Read(database_id, tables_locality, options);
    }

   private:
    friend class YbctidReader;

    BatchAccessor(YbctidReader& reader, size_t signature)
        : reader_(reader), signature_(signature) {}

    [[nodiscard]] bool IsActive() const {
      return reader_.active_batch_accessor_signature_ == signature_;
    }

    YbctidReader& reader_;
    const size_t signature_;

    DISALLOW_COPY_AND_ASSIGN(BatchAccessor);
  };

  explicit YbctidReader(const PgSessionPtr& session);
  ~YbctidReader();
  [[nodiscard]] BatchAccessor StartNewBatch(std::optional<size_t> capacity = {}) {
    const auto signature = ++active_batch_accessor_signature_;
    Clear();
    if (capacity) {
      ybctids_.reserve(*capacity);
    }
    return BatchAccessor(*this, signature);
  }

 private:
  void Add(const LightweightTableYbctid& ybctid) { ybctids_.push_back(ybctid); }
  void Clear() {
    ybctids_.clear();
    holders_->clear();
  }

  ReadResult Read(
      PgOid database_id, const TableLocalityMap& tables_locality, const Options& options);

  const PgSessionPtr& session_;
  BuffersPtr holders_ = std::make_shared<Buffers>();
  boost::container::small_vector<LightweightTableYbctid, 8> ybctids_;
  size_t active_batch_accessor_signature_{0};
};

} // namespace yb::pggate
