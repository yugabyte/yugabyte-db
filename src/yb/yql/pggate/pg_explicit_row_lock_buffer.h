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
#include <string>

#include "yb/gutil/macros.h"

#include "yb/util/status.h"
#include "yb/util/tostring.h"

#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_ybctid_reader.h"

namespace yb::pggate {

class ExplicitRowLockBuffer {
 public:
  struct LockInfo {
    int rowmark;
    int pg_wait_policy;
    int docdb_wait_policy;
    PgOid database_id;

    friend bool operator==(const LockInfo&, const LockInfo&) = default;
  };

  struct ErrorStatusAdditionalInfo {
    ErrorStatusAdditionalInfo(int pg_wait_policy_, PgOid conflicting_table_id_)
        : pg_wait_policy(pg_wait_policy_), conflicting_table_id(conflicting_table_id_) {}

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(pg_wait_policy, conflicting_table_id);
    }

    int pg_wait_policy;
    PgOid conflicting_table_id;
  };

  struct AddLockData {
    LockInfo lock_info;
    LightweightTableYbctid lock_key;
    const YbcPgTableLocalityInfo& table_locality;
    std::optional<ErrorStatusAdditionalInfo>& error_info;
  };

  explicit ExplicitRowLockBuffer(PgSession& session);
  ~ExplicitRowLockBuffer();

  Status Add(const AddLockData& data);
  Result<YbcIsExplicitlyLockedRowSkippedCheckHandle> AddSkippable(
      const AddLockData& data, std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> handle);

  Status Flush(std::optional<ErrorStatusAdditionalInfo>& error_info);
  void Clear();
  [[nodiscard]] bool HasPendingLocks() const;
  // Check that handle has at least one skipped lock.
  // Note: Each handle can be checked only once.
  //       I.e. in case IsSkipped returned true for some handle next call with same handle will
  //       return false.
  Result<bool> IsSkipped(
      YbcIsExplicitlyLockedRowSkippedCheckHandle handle,
      std::optional<ErrorStatusAdditionalInfo>& error_info);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace yb::pggate
