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

#include <functional>
#include <optional>

#include "yb/gutil/macros.h"

#include "yb/util/status.h"
#include "yb/util/lw_function.h"

#include "yb/yql/pggate/pg_tools.h"

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

  ExplicitRowLockBuffer(
      TableYbctidVectorProvider& ybctid_container_provider,
      std::reference_wrapper<const YbctidReader> ybctid_reader);
  Status Add(
      Info&& info, const LightweightTableYbctid& key, bool is_region_local);
  Status Flush();
  void Clear();
  bool IsEmpty() const;

 private:
  Status DoFlush();

  TableYbctidVectorProvider& ybctid_container_provider_;
  const YbctidReader& ybctid_reader_;
  TableYbctidSet intents_;
  OidSet region_local_tables_;
  std::optional<Info> info_;
};

} // namespace yb::pggate
