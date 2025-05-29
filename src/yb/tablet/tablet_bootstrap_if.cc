// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tablet/tablet_bootstrap_if.h"

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_bootstrap.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"

#include "yb/util/debug/trace_event.h"

using std::string;

namespace yb::tablet {

using consensus::ConsensusBootstrapInfo;

TabletStatusListener::TabletStatusListener(const RaftGroupMetadataPtr& meta)
    : meta_(meta) {
}

const string TabletStatusListener::tablet_id() const {
  return meta_->raft_group_id();
}

const string TabletStatusListener::namespace_name() const {
  return meta_->namespace_name();
}

const string TabletStatusListener::table_name() const {
  return meta_->table_name();
}

const string TabletStatusListener::table_id() const {
  return meta_->table_id();
}

std::shared_ptr<dockv::Partition> TabletStatusListener::partition() const {
  return meta_->partition();
}

SchemaPtr TabletStatusListener::schema() const {
  return meta_->schema();
}

TabletStatusListener::~TabletStatusListener() {
}

void TabletStatusListener::StatusMessage(const string& status) {
  LOG(INFO) << "T " << tablet_id() << " P " << meta_->fs_manager()->uuid() << ": "
            << status;
  std::lock_guard l(lock_);
  last_status_ = status;
}

void TabletStatusListener::SetStatusPrefix(const std::string& prefix) {
  std::lock_guard l(lock_);
  status_prefix_ = prefix + "\n";
}

std::optional<RbsProgressInfo> TabletStatusListener::GetRbsProgressInfo() {
  std::lock_guard l(lock_);
  return rbs_progress_info_;
}

void TabletStatusListener::SetInitialRbsProgressInfo(
    const TabletServerId& source_ts_uuid, int64_t sst_bytes_to_download,
    int64_t start_time_micros) {
  std::lock_guard l(lock_);
  rbs_progress_info_ = RbsProgressInfo {
    .source_ts_uuid = source_ts_uuid,
    .sst_bytes_to_download = sst_bytes_to_download,
    .sst_bytes_downloaded = 0,
    .sst_start_time_micros = start_time_micros,
    .sst_end_time_micros = 0,
  };
}

void TabletStatusListener::IncrementSstDownloadProgress(int64_t bytes_downloaded) {
  std::lock_guard l(lock_);
  rbs_progress_info_->sst_bytes_downloaded += bytes_downloaded;
}

void TabletStatusListener::SetSstDownloadDone() {
  std::lock_guard l(lock_);
  rbs_progress_info_->sst_end_time_micros = GetCurrentTimeMicros();
}

void TabletStatusListener::ClearRbsProgressInfo() {
  std::lock_guard l(lock_);
  rbs_progress_info_ = std::nullopt;
}

Status BootstrapTablet(
    const BootstrapTabletData& data,
    TabletPtr* rebuilt_tablet,
    scoped_refptr<log::Log>* rebuilt_log,
    ConsensusBootstrapInfo* consensus_info) {
  const auto& meta = *data.tablet_init_data.metadata;
  TRACE_EVENT1("tablet", "BootstrapTablet", "tablet_id", meta.raft_group_id());
  RETURN_NOT_OK(BootstrapTabletImpl(data, rebuilt_tablet, rebuilt_log, consensus_info));

  // Set WAL retention time from the metadata.
  (*rebuilt_log)->set_wal_retention_secs(meta.wal_retention_secs());
  (*rebuilt_log)->set_cdc_min_replicated_index(meta.cdc_min_replicated_index());

  // This is necessary since OpenNewLog() initially disables sync.
  RETURN_NOT_OK((*rebuilt_log)->ReEnableSyncIfRequired());
  return Status::OK();
}

string DocDbOpIds::ToString() const {
  return YB_STRUCT_TO_STRING(regular, intents, vector_indexes);
}

} // namespace yb::tablet
