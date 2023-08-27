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

#include <mutex>

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

class RWOperationCounter;

namespace tablet {

struct TabletScopedRWOperationPauses;

// Base class for Tablet components, has access to private Tablet fields.
// For methods descriptions see comments for appropriate field or method in Tablet class.
class TabletComponent {
 public:
  explicit TabletComponent(Tablet* tablet) : tablet_(*tablet) {}

  Tablet& tablet() const {
    return tablet_;
  }

 protected:
  TabletScopedRWOperationPauses StartShutdownRocksDBs(
      DisableFlushOnShutdown disable_flush_on_shutdown, AbortOps abort_ops);

  std::vector<std::string> CompleteShutdownRocksDBs(
      const TabletScopedRWOperationPauses& ops_pauses);

  Status DeleteRocksDBs(const std::vector<std::string>& db_paths);

  Status OpenRocksDBs();

  std::string LogPrefix() const;

  RaftGroupMetadata& metadata() const;

  RWOperationCounter& pending_op_counter_blocking_rocksdb_shutdown_start() const;

  rocksdb::DB& regular_db() const;

  bool has_regular_db() const;

  rocksdb::DB& intents_db() const;

  bool has_intents_db() const;

  std::mutex& create_checkpoint_lock() const;

  rocksdb::Env& rocksdb_env() const;

  void RefreshYBMetaDataCache();

 private:
  Tablet& tablet_;
};

} // namespace tablet
} // namespace yb
