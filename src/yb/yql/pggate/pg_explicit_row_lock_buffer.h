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

#include "yb/gutil/macros.h"

#include "yb/util/status.h"

#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_ybctid_reader.h"

namespace yb::pggate {

class ExplicitRowLockBuffer {
 public:
  struct Info {
    int rowmark;
    int pg_wait_policy;
    int docdb_wait_policy;
    PgOid database_id;

    friend bool operator==(const Info&, const Info&) = default;
  };

  struct ErrorStatusAdditionalInfo {
    ErrorStatusAdditionalInfo(int pg_wait_policy_, PgOid conflicting_table_id_)
        : pg_wait_policy(pg_wait_policy_), conflicting_table_id(conflicting_table_id_) {}

    int pg_wait_policy;
    PgOid conflicting_table_id;
  };

  explicit ExplicitRowLockBuffer(const PgSessionPtr& pg_session) : ybctid_reader_(pg_session) {}

  Status Add(
      const Info& info, const LightweightTableYbctid& key,
      const YbcPgTableLocalityInfo& locality_info,
      std::optional<ErrorStatusAdditionalInfo>& error_info);
  Status Flush(std::optional<ErrorStatusAdditionalInfo>& error_info);
  void Clear();
  bool IsEmpty() const;

 private:
  Status DoFlush(std::optional<ErrorStatusAdditionalInfo>& error_info);
  Status DoFlushImpl();

  YbctidReader ybctid_reader_;
  MemoryOptimizedTableYbctidSet intents_;
  TableLocalityMap table_locality_map_;
  std::optional<Info> info_;
};

} // namespace yb::pggate
