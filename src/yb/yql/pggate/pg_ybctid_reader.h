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
#include <optional>
#include <span>
#include <utility>

#include "yb/gutil/macros.h"

#include "yb/yql/pggate/pg_session_fwd.h"
#include "yb/yql/pggate/pg_tools.h"

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
  class Impl;

  class BatchAccessor {
   public:
    ~BatchAccessor();

    void Add(const LightweightTableYbctid& ybctid);

    ReadResult Read(
        PgOid database_id, const TableLocalityMap& tables_locality, const Options& options = {});

   private:
    friend class YbctidReader;

    BatchAccessor(Impl& impl, size_t signature) : impl_{impl}, signature_{signature} {}

    [[nodiscard]] bool IsActive() const;

    Impl& impl_;
    const size_t signature_;

    DISALLOW_COPY_AND_ASSIGN(BatchAccessor);
  };

  explicit YbctidReader(PgSession& session);
  ~YbctidReader();
  [[nodiscard]] BatchAccessor StartNewBatch(size_t capacity = 0);

 private:
  std::unique_ptr<Impl> impl_;
};

} // namespace yb::pggate
