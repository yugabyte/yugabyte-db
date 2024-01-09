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

#include "yb/master/xcluster/xcluster_config.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog.h"

namespace yb::master {

XClusterConfig::XClusterConfig(SysCatalogTable* sys_catalog) : sys_catalog_(sys_catalog) {}

void XClusterConfig::ClearState() {
  std::lock_guard l(mutex_);
  xcluster_config_info_.reset();
}

void XClusterConfig::Load(const SysXClusterConfigEntryPB& metadata) {
  std::lock_guard mutex_lock(mutex_);
  DCHECK(!xcluster_config_info_) << "Already have xCluster config data!";

  auto config = std::make_shared<XClusterConfigInfo>();
  auto l = config->LockForWrite();
  l.mutable_data()->pb.CopyFrom(metadata);
  l.Commit();

  xcluster_config_info_ = std::move(config);
}

Status XClusterConfig::PrepareDefault(int64_t term, bool re_create) {
  std::lock_guard mutex_lock(mutex_);

  if (xcluster_config_info_ && !re_create) {
    LOG(INFO) << "Cluster configuration has already been set up, skipping re-initialization.";
    return Status::OK();
  }
  xcluster_config_info_.reset();

  // Create default.
  SysXClusterConfigEntryPB config;
  config.set_version(0);

  // Create in memory object.
  xcluster_config_info_ = std::make_shared<XClusterConfigInfo>();

  // Prepare write.
  auto l = xcluster_config_info_->LockForWrite();
  l.mutable_data()->pb = std::move(config);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->Upsert(term, xcluster_config_info_.get()));
  l.Commit();

  return Status::OK();
}

Result<uint32_t> XClusterConfig::GetVersion() const {
  SharedLock mutex_lock(mutex_);
  SCHECK(xcluster_config_info_, IllegalState, "XCluster config is not initialized");
  return xcluster_config_info_->LockForRead()->pb.version();
}

Result<SysXClusterConfigEntryPB> XClusterConfig::GetXClusterConfigEntryPB() const {
  SharedLock mutex_lock(mutex_);
  SCHECK(xcluster_config_info_, IllegalState, "XCluster config is not initialized");
  return xcluster_config_info_->LockForRead()->pb;
}

Status XClusterConfig::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const {
  SharedLock mutex_lock(mutex_);
  SCHECK(xcluster_config_info_, IllegalState, "XCluster config is not initialized");
  auto l = xcluster_config_info_->LockForRead();
  const auto& config_pb = l->pb;

  if (req.has_xcluster_config_version() && req.xcluster_config_version() < config_pb.version()) {
    resp->set_xcluster_config_version(config_pb.version());
    *resp->mutable_xcluster_producer_registry() = config_pb.xcluster_producer_registry();
  }

  return Status::OK();
}

Status XClusterConfig::BumpVersionUpsertAndCommit(
    const LeaderEpoch& epoch, CowWriteLock<PersistentXClusterConfigInfo>& l) {
  auto& config_pb = l.mutable_data()->pb;
  config_pb.set_version(config_pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(epoch.leader_term, xcluster_config_info_.get()),
      "updating xcluster config in sys-catalog"));
  l.Commit();

  return Status::OK();
}

Status XClusterConfig::RemoveStreams(
    const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams) {
  SharedLock mutex_lock(mutex_);
  SCHECK(xcluster_config_info_, IllegalState, "XCluster config is not initialized");

  auto l = xcluster_config_info_->LockForWrite();
  auto& config_pb = l.mutable_data()->pb;
  auto paused_producer_stream_ids =
      config_pb.mutable_xcluster_producer_registry()->mutable_paused_producer_stream_ids();
  for (const auto& stream : streams) {
    paused_producer_stream_ids->erase(stream->id());
  }

  return BumpVersionUpsertAndCommit(epoch, l);
}

Status XClusterConfig::PauseResumeXClusterProducerStreams(
    const LeaderEpoch& epoch, std::vector<xrepl::StreamId> stream_ids, bool pause) {
  SharedLock lock(mutex_);
  SCHECK(xcluster_config_info_, IllegalState, "XCluster config is not initialized");

  auto l = xcluster_config_info_->LockForWrite();
  auto* paused_producer_stream_ids = l.mutable_data()
                                         ->pb.mutable_xcluster_producer_registry()
                                         ->mutable_paused_producer_stream_ids();

  for (const auto& stream_id : stream_ids) {
    auto stream_id_str = stream_id.ToString();
    bool stream_paused = paused_producer_stream_ids->count(stream_id_str);
    if (pause && !stream_paused) {
      // Insert stream id to pause replication on that stream.
      paused_producer_stream_ids->insert({stream_id_str, true});
    } else if (!pause && stream_paused) {
      // Erase stream id to resume replication on that stream.
      paused_producer_stream_ids->erase(stream_id_str);
    }
  }

  return BumpVersionUpsertAndCommit(epoch, l);
}

};  // namespace yb::master
