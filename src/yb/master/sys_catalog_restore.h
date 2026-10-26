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


// The part of a sys-catalog PITR restore that runs inside Raft apply. Every master runs it, leader
// or follower, so it must not depend on CatalogManager: the Rust master's host
// (src/yb/master_host) calls it too.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/master_fwd.h"

#include "yb/rocksdb/db.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb::master {

// Writes the sys catalog state of `restoration` into `tablet` and records the restoration's
// progress through `coordinator`. `leader_term` returns the term when this master leads, else -1;
// it is only logged, and read when the restore has written. A NotSupported status means
// validation rejected the restore before anything was written. The caller reloads leader state
// afterwards when it leads.
Status RestoreSysCatalogOnApply(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const docdb::DocReadContext& doc_read_context, MasterSnapshotCoordinator* coordinator,
    server::Clock* clock, const std::function<int64_t()>& leader_term,
    const std::string& log_prefix);

// Opens the sys catalog as of `restore_at` from the local checkpoint of `snapshot_id`.
Result<std::unique_ptr<rocksdb::DB>> RestoreSysCatalogSnapshotToTmpRocksDb(
    tablet::Tablet* tablet, const TxnSnapshotId& snapshot_id, HybridTime restore_at,
    std::string log_prefix);

}  // namespace yb::master
