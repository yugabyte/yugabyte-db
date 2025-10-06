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

#include "yb/tablet/tablet_component.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_vector_indexes.h"

namespace yb::tablet {

TabletScopedRWOperationPauses TabletComponent::StartShutdownStorages(
    const DisableFlushOnShutdown disable_flush_on_shutdown, const AbortOps abort_ops) {
  return tablet_.StartShutdownStorages(disable_flush_on_shutdown, abort_ops);
}

std::vector<std::string> TabletComponent::CompleteShutdownStorages(
    const TabletScopedRWOperationPauses& ops_pauses) {
  return tablet_.CompleteShutdownStorages(ops_pauses);
}

Status TabletComponent::DeleteStorages(const std::vector<std::string>& db_paths) {
  return tablet_.DeleteStorages(db_paths);
}

Status TabletComponent::OpenStorages() {
  return tablet_.OpenKeyValueTablet();
}

std::string TabletComponent::LogPrefix() const {
  return tablet_.LogPrefix();
}

RaftGroupMetadata& TabletComponent::metadata() const {
  return *tablet_.metadata();
}

RWOperationCounter& TabletComponent::pending_op_counter_blocking_rocksdb_shutdown_start() const {
  return tablet_.pending_op_counter_blocking_rocksdb_shutdown_start_;
}

const TabletId& TabletComponent::tablet_id() const {
  return tablet_.tablet_id();
}

rocksdb::DB& TabletComponent::regular_db() const {
  return *tablet_.regular_db_;
}

bool TabletComponent::has_regular_db() const {
  return tablet_.regular_db_ != nullptr;
}

rocksdb::DB& TabletComponent::intents_db() const {
  return *tablet_.intents_db_;
}

bool TabletComponent::has_intents_db() const {
  return tablet_.intents_db_ != nullptr;
}

docdb::DocDB TabletComponent::doc_db(TabletMetrics* metrics) const {
  return tablet_.doc_db(metrics);
}

std::mutex& TabletComponent::create_checkpoint_lock() const {
  return tablet_.create_checkpoint_lock_;
}

rocksdb::Env& TabletComponent::rocksdb_env() const {
  return tablet_.rocksdb_env();
}

void TabletComponent::RefreshYBMetaDataCache() {
  // Note: every tablet will cleanup the cache, since during restore, there are no
  // operations allowed, this should be fine.
  tablet_.ResetYBMetaDataCache();
}

docdb::DocVectorIndexesPtr TabletComponent::VectorIndexesList() const {
  return tablet_.vector_indexes().List();
}

Status TabletComponent::Flush(FlushMode mode, FlushFlags flags) {
  return tablet_.Flush(mode, flags);
}

} // namespace yb::tablet
