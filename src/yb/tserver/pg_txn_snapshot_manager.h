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
#include <memory>
#include <string>

#include "yb/common/read_hybrid_time.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/result.h"

namespace yb::tserver {

struct PgTxnSnapshot {
  ReadHybridTime read_time;
  uint32_t db_oid;
  uint32_t isolation_level;
  bool read_only;

  template <class PB>
  static PgTxnSnapshot Make(const PB& pb, const ReadHybridTime& read_time) {
    return {.read_time = read_time,
            .db_oid = pb.db_oid(),
            .isolation_level = pb.isolation_level(),
            .read_only = pb.read_only()};
  }

  template <class PB>
  void ToPBNoReadTime(PB& pb) const {
    pb.set_db_oid(db_oid);
    pb.set_isolation_level(isolation_level);
    pb.set_read_only(read_only);
  }
};

class PgTxnSnapshotManager {
 public:
  using TsProxyProvider =
      std::function<Result<std::shared_ptr<TabletServerServiceProxy>>(const std::string&)>;

  PgTxnSnapshotManager(
      std::reference_wrapper<const std::string> instance_id, TsProxyProvider&& proxy_provider);
  ~PgTxnSnapshotManager();

  Result<std::string> Register(uint64_t session_id, const PgTxnSnapshot& snapshot);
  void UnregisterAll(uint64_t session_id);
  Result<PgTxnSnapshot> Get(const std::string& snapshot_id);
  Result<PgTxnSnapshot> Get(const PgTxnSnapshotLocalId& snapshot_id);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace yb::tserver
