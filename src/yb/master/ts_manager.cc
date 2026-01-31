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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/ts_manager.h"

#include <mutex>
#include <vector>

#include "yb/common/version_info.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/map-util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_error.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"

#include "yb/server/clock.h"

#include "yb/util/atomic.h"

DEFINE_NON_RUNTIME_bool(master_register_ts_check_desired_host_port, true,
    "When set to true, master will only do duplicate address checks on the used host/port instead "
    "of on all. The used host/port combination depends on the value of --use_private_ip.");
TAG_FLAG(master_register_ts_check_desired_host_port, advanced);

DEFINE_RUNTIME_int32(tserver_unresponsive_timeout_ms, 60 * 1000,
    "The period of time that a Master can go without receiving a heartbeat from a tablet server "
    "before considering it unresponsive. Unresponsive servers are not selected when assigning "
    "replicas during table creation or re-replication.");
TAG_FLAG(tserver_unresponsive_timeout_ms, advanced);

DEFINE_RUNTIME_AUTO_bool(persist_tserver_registry, kLocalPersisted, false, true,
    "Whether to persist the map of registered tservers in the universe to the sys catalog. Also "
    "controls whether to reload the map of registered tservers from the sys catalog when reloading "
    "the sys catalog.");

DEFINE_RUNTIME_bool(skip_tserver_version_checks, false, "Skip all tserver version checks");

namespace yb::master {
namespace {

bool HasSameHostPort(const HostPortPB& lhs, const HostPortPB& rhs);

bool HasSameHostPort(const google::protobuf::RepeatedPtrField<HostPortPB>& lhs,
                     const google::protobuf::RepeatedPtrField<HostPortPB>& rhs);

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const CloudInfoPB& cloud_info);

bool HasSameHostPort(const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs);

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const std::vector<CloudInfoPB>& perspectives);

// Returns a function to determine whether a registered ts matches the host port of the registering
// ts.
std::function<bool(const ServerRegistrationPB&)> GetHostPortCheckerFunction(
    const TSRegistrationPB& registration, const CloudInfoPB& local_master_cloud_info);

class TSDescriptorLoader : public Visitor<PersistentTServerInfo> {
 public:
  explicit TSDescriptorLoader(
      const CloudInfoPB& local_master_cloud_info, rpc::ProxyCache* proxy_cache) noexcept
      : local_master_cloud_info_(local_master_cloud_info),
        proxy_cache_(proxy_cache),
        load_time_(MonoTime::Now()) {}

  TSDescriptorMap&& TakeMap();

 protected:
  Status Visit(const std::string& id, const SysTabletServerEntryPB& metadata) override;

 private:
  TSDescriptorMap map_;
  const CloudInfoPB& local_master_cloud_info_;
  rpc::ProxyCache* proxy_cache_;
  MonoTime load_time_;
};

template <typename... Items>
Status UpsertIfRequired(const LeaderEpoch& epoch, SysCatalogTable& sys_catalog, Items&&... items);

}  // namespace

TSManager::TSManager(SysCatalogTable& sys_catalog) noexcept : sys_catalog_(sys_catalog) {}

Result<TSDescriptorPtr> TSManager::LookupTS(const NodeInstancePB& instance) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);

  auto maybe_desc = LookupTSInternalUnlocked(instance.permanent_uuid());
  if (!maybe_desc) {
    return STATUS_FORMAT(
        NotFound, "Unknown tablet server ID not in map, instance data: $0",
        instance.ShortDebugString());
  }
  auto desc = std::move(maybe_desc).value();
  auto desc_lock = desc->LockForRead();
  if (desc_lock->pb.state() == SysTabletServerEntryPB::REPLACED) {
    return STATUS_FORMAT(
        NotFound,
        "Trying to lookup replaced tablet server, instance data: $0, entry: $1",
        instance.ShortDebugString(), desc->ToString());
  }

  if (instance.instance_seqno() != desc_lock->pb.instance_seqno()) {
    return STATUS_FORMAT(
        NotFound, "mismatched instance sequence number $0, instance $1", desc->latest_seqno(),
        instance.ShortDebugString());
  }

  return desc;
}

Result<TSDescriptorPtr> TSManager::LookupTSByUUID(const std::string& uuid) {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  auto maybe_desc = LookupTSInternalUnlocked(uuid);
  if (!maybe_desc || (*maybe_desc)->IsReplaced()) {
    return STATUS_FORMAT(NotFound, "Tablet server $0 $1",
                         uuid,
                         maybe_desc ? "is replaced" : "not in ts descriptor map");
  }
  return std::move(maybe_desc).value();
}

std::optional<TSDescriptorPtr> TSManager::LookupTSInternalUnlocked(
    const std::string& permanent_uuid) const {
  const TSDescriptorPtr* found_ptr = FindOrNull(servers_by_id_, permanent_uuid);
  if (!found_ptr) {
    return std::nullopt;
  }
  return *found_ptr;
}

Result<TSManager::RegistrationMutationData> TSManager::ComputeRegistrationMutationData(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    CloudInfoPB&& local_master_cloud_info, rpc::ProxyCache* proxy_cache,
    RegisteredThroughHeartbeat registered_through_heartbeat) {
  TSManager::RegistrationMutationData reg_data;
  {
    auto hostport_checker = GetHostPortCheckerFunction(registration, local_master_cloud_info);
    SharedLock<decltype(map_lock_)> map_l(map_lock_);
    const std::string& uuid = instance.permanent_uuid();

    for (auto it = servers_by_id_.begin();; ++it) {
      auto ts_desc = it != servers_by_id_.end() ? it->second : nullptr;
      if (!reg_data.desc && (!ts_desc || ts_desc->permanent_uuid() >= uuid)) {
        if (ts_desc && uuid == ts_desc->permanent_uuid()) {
          // This tserver has registered before. We just need to update its registration metadata.
          reg_data.registered_desc_lock = VERIFY_RESULT(ts_desc->UpdateRegistration(
              instance, registration, registered_through_heartbeat));
          reg_data.desc = ts_desc;
          continue;
        } else {
          // We have no entry for this uuid. Create a new TSDescriptor and make a note to add it
          // to the registry.
          std::tie(reg_data.desc, reg_data.registered_desc_lock) =
              VERIFY_RESULT(TSDescriptor::CreateNew(
                  instance, registration, std::move(local_master_cloud_info), proxy_cache,
                  registered_through_heartbeat));
          reg_data.insert_into_map = true;
        }
      }

      if (!ts_desc) {
        break;
      }

      // Acquire write locks because we may have to mutate later.
      auto l = ts_desc->LockForWrite();
      if (!hostport_checker(l->pb.registration())) {
        continue;
      }
      if (l->pb.instance_seqno() >= instance.instance_seqno()) {
        return STATUS_FORMAT(
            AlreadyPresent, "Cannot register TS $0 $1, host port collision with existing TS $2",
            instance.ShortDebugString(), registration.common().ShortDebugString(),
            l->pb.ShortDebugString());
      }
      l.mutable_data()->pb.set_state(SysTabletServerEntryPB::REPLACED);
      reg_data.replaced_descs.push_back(ts_desc);
      reg_data.replaced_desc_locks.push_back(std::move(l));
    }
  }

  return reg_data;
}

Status TSManager::RegisterFromRaftConfig(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    CloudInfoPB&& local_master_cloud_info, const LeaderEpoch& epoch,
    rpc::ProxyCache* proxy_cache) {
  // todo(zdrudi): what happens if the registration through raft config is blocked because of a host
  // port collision? add a test.
  VERIFY_RESULT(RegisterInternal(
      instance, registration, {}, std::move(local_master_cloud_info), epoch, proxy_cache));
  return Status::OK();
}

Result<TSDescriptorPtr> TSManager::LookupAndUpdateTSFromHeartbeat(
    const TSHeartbeatRequestPB& heartbeat_request, const LeaderEpoch& epoch) const {
  auto desc = VERIFY_RESULT(LookupTS(heartbeat_request.common().ts_instance()));
  auto lock = desc->LockForWrite();
  RETURN_NOT_OK(desc->UpdateFromHeartbeat(heartbeat_request, lock));
  RETURN_NOT_OK(UpsertIfRequired(epoch, sys_catalog_, desc));
  lock.Commit();
  return desc;
}

Result<TSDescriptorPtr> TSManager::RegisterFromHeartbeat(
    const TSHeartbeatRequestPB& heartbeat_request, const LeaderEpoch& epoch,
    CloudInfoPB&& local_master_cloud_info, rpc::ProxyCache* proxy_cache) {
  return RegisterInternal(
      heartbeat_request.common().ts_instance(), heartbeat_request.registration(),
      std::cref(heartbeat_request), std::move(local_master_cloud_info), epoch, proxy_cache);
}

Result<TSDescriptorPtr> TSManager::RegisterInternal(
    const NodeInstancePB& instance, const TSRegistrationPB& registration,
    std::optional<std::reference_wrapper<const TSHeartbeatRequestPB>> request,
    CloudInfoPB&& local_master_cloud_info, const LeaderEpoch& epoch,
    rpc::ProxyCache* proxy_cache) {
  TSCountCallback callback;
  TSDescriptorPtr ts_desc = nullptr;
  {
    MutexLock l(registration_lock_);
    auto reg_data = VERIFY_RESULT(ComputeRegistrationMutationData(
        instance, registration, std::move(local_master_cloud_info), proxy_cache,
        RegisteredThroughHeartbeat(request.has_value())));
    if (request.has_value()) {
      RETURN_NOT_OK(reg_data.desc->UpdateFromHeartbeat(*request, reg_data.registered_desc_lock));
    }
    std::tie(ts_desc, callback) =
        VERIFY_RESULT(DoRegistrationMutation(instance, registration, std::move(reg_data), epoch));
  }
  if (callback) {
    callback();
  }
  return ts_desc;
}

Result<std::pair<TSDescriptorPtr, TSCountCallback>> TSManager::DoRegistrationMutation(
    const NodeInstancePB& new_ts_instance, const TSRegistrationPB& registration,
    TSManager::RegistrationMutationData&& registration_data,
    const LeaderEpoch& epoch) {
  TSCountCallback callback;
  RETURN_NOT_OK(UpsertIfRequired(
      epoch, sys_catalog_, registration_data.desc, registration_data.replaced_descs));
  registration_data.registered_desc_lock.Commit();
  for (auto& l : registration_data.replaced_desc_locks) {
    LOG(WARNING) << "Removing entry: " << l->pb.ShortDebugString()
                 << " since we received registration for a tserver with a higher sequence number: "
                 << new_ts_instance.ShortDebugString();
    l.Commit();
  }
  if (registration_data.insert_into_map) {
    std::lock_guard map_l(map_lock_);
    InsertOrDie(&servers_by_id_, new_ts_instance.permanent_uuid(), registration_data.desc);
    LOG(INFO) << "Registered new tablet server { " << new_ts_instance.ShortDebugString()
              << " } with Master, full list: " << yb::ToString(servers_by_id_);
    if (ts_count_callback_) {
      auto new_count = NumDescriptorsUnlocked();
      if (new_count >= ts_count_callback_min_count_) {
        callback = std::move(ts_count_callback_);
        ts_count_callback_min_count_ = 0;
      }
    }
  } else {
    LOG(INFO) << "Re-registered known tablet server { " << new_ts_instance.ShortDebugString()
              << " }: " << registration.ShortDebugString();
  }

  return std::make_pair(std::move(registration_data.desc), std::move(callback));
}

void TSManager::GetDescriptors(std::function<bool(const TSDescriptorPtr&)> condition,
                               TSDescriptorVector* descs) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  GetDescriptorsUnlocked(condition, descs);
}

void TSManager::GetDescriptorsUnlocked(
    std::function<bool(const TSDescriptorPtr&)> predicate, TSDescriptorVector* descs) const {
  descs->clear();

  descs->reserve(servers_by_id_.size());
  for (const auto& [id, desc] : servers_by_id_) {
    if (predicate(desc)) {
      VLOG(1) << " Adding " << yb::ToString(desc);
      descs->push_back(desc);
    } else {
      VLOG(1) << " NOT Adding " << yb::ToString(desc);
    }
  }
}

void TSManager::GetAllDescriptors(TSDescriptorVector* descs) const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  GetAllDescriptorsUnlocked(descs);
}

TSDescriptorVector TSManager::GetAllDescriptors() const {
  TSDescriptorVector descs;
  GetAllDescriptors(&descs);
  return descs;
}

void TSManager::GetAllDescriptorsUnlocked(TSDescriptorVector* descs) const {
  GetDescriptorsUnlocked(
      [](const TSDescriptorPtr& ts) -> bool { return !ts->IsReplaced(); }, descs);
}

void TSManager::GetAllLiveDescriptors(TSDescriptorVector* descs,
                                      const std::optional<BlacklistSet>& blacklist) const {
  GetDescriptors([blacklist](const TSDescriptorPtr& ts) -> bool {
    return ts->IsLive() && !IsTsBlacklisted(ts, blacklist); }, descs);
}

void TSManager::GetAllReportedDescriptors(TSDescriptorVector* descs) const {
  GetDescriptors([](const TSDescriptorPtr& ts)
                   -> bool { return ts->IsLive() && ts->has_tablet_report(); }, descs);
}

bool TSManager::IsTsInCluster(const TSDescriptorPtr& ts, const std::string& cluster_uuid) {
  return ts->placement_uuid() == cluster_uuid;
}

bool TSManager::IsTsBlacklisted(const TSDescriptorPtr& ts,
                                const std::optional<BlacklistSet>& blacklist) {
  return blacklist.has_value() && ts->IsBlacklisted(*blacklist);
}

void TSManager::GetAllLiveDescriptorsInCluster(TSDescriptorVector* descs,
    const std::string& placement_uuid,
    const std::optional<BlacklistSet>& blacklist,
    bool primary_cluster) const {
  descs->clear();
  SharedLock<decltype(map_lock_)> l(map_lock_);

  descs->reserve(servers_by_id_.size());
  for (const TSDescriptorMap::value_type& entry : servers_by_id_) {
    const TSDescriptorPtr& ts = entry.second;
    // ts_in_cluster true if there's a matching config and tserver placement uuid or
    // if we're getting primary nodes and the tserver placement uuid is empty.
    bool ts_in_cluster = (IsTsInCluster(ts, placement_uuid) ||
                         (primary_cluster && ts->placement_uuid().empty()));
    if (ts->IsLive() && !IsTsBlacklisted(ts, blacklist) && ts_in_cluster) {
      descs->push_back(ts);
    }
  }
}

size_t TSManager::NumDescriptorsUnlocked() const {
  return std::count_if(
      servers_by_id_.cbegin(), servers_by_id_.cend(),
      [](const auto& entry) -> bool { return !entry.second->IsReplaced(); });
}

// Register a callback to be called when the number of tablet servers reaches a certain number.
void TSManager::SetTSCountCallback(int min_count, TSCountCallback callback) {
  std::lock_guard l(registration_lock_);
  ts_count_callback_ = std::move(callback);
  ts_count_callback_min_count_ = min_count;
}

size_t TSManager::NumDescriptors() const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  return NumDescriptorsUnlocked();
}

size_t TSManager::NumLiveDescriptors() const {
  SharedLock<decltype(map_lock_)> l(map_lock_);
  return std::count_if(
      servers_by_id_.cbegin(), servers_by_id_.cend(),
      [](const auto& entry) -> bool { return entry.second->IsLive(); });
}

Status TSManager::MarkUnresponsiveTServers(const LeaderEpoch& epoch) {
  auto current_time = MonoTime::Now();
  {
    SharedLock<decltype(map_lock_)> l(map_lock_);
    std::vector<TSDescriptor*> updated_descs;
    std::vector<TSDescriptor::WriteLock> cow_locks;
    for (const auto& [id, desc] : servers_by_id_) {
      auto lock_opt = desc->MaybeUpdateLiveness(current_time);
      if (lock_opt) {
        updated_descs.push_back(desc.get());
        cow_locks.push_back(std::move(lock_opt).value());
      }
    }
    RETURN_NOT_OK(UpsertIfRequired(epoch, sys_catalog_, updated_descs));
    for (auto& l : cow_locks) {
      l.Commit();
    }
  }
  return Status::OK();
}

Status TSManager::RunLoader(
    const CloudInfoPB& cloud_info, rpc::ProxyCache* proxy_cache, SysCatalogLoadingState& state) {
  if (!GetAtomicFlag(&FLAGS_persist_tserver_registry)) {
    return Status::OK();
  }
  auto loader = std::make_unique<TSDescriptorLoader>(cloud_info, proxy_cache);
  RETURN_NOT_OK(sys_catalog_.Visit(loader.get()));
  MutexLock l_reg(registration_lock_);
  std::lock_guard l_map(map_lock_);
  servers_by_id_ = loader->TakeMap();
  if (servers_by_id_.size() >= ts_count_callback_min_count_ && ts_count_callback_) {
    state.AddPostLoadTask(
        std::move(ts_count_callback_), "Callback run at minimum number of tservers");
  }
  return Status::OK();
}

Status TSManager::RemoveTabletServer(
    const std::string& permanent_uuid, const BlacklistSet& blacklist,
    const std::vector<TableInfoPtr>& tables, const LeaderEpoch& epoch) {
  // Acquire registration lock so we don't race with this TS re-registering.
  MutexLock l_reg(registration_lock_);
  TSDescriptorPtr desc;
  TSDescriptor::WriteLock write_lock;
  {
    // Best effort pre-flight checks. These conditions can change after we check them but we assume
    // there are no concurrent blacklist modifications.
    SharedLock<decltype(map_lock_)> l(map_lock_);
    auto maybe_desc = LookupTSInternalUnlocked(permanent_uuid);
    if (!maybe_desc) {
      return STATUS(
          NotFound, Format("Couldn't find tserver with uuid $0", permanent_uuid),
          MasterError(MasterErrorPB::TABLET_SERVER_NOT_FOUND));
    }
    desc = std::move(maybe_desc).value();
    auto desc_lock = desc->LockForWrite();
    if (desc_lock->pb.state() == SysTabletServerEntryPB::LIVE) {
      return STATUS_FORMAT(
          InvalidArgument, "Cannot remove tablet server $0 because it is live",
          desc->permanent_uuid());
    }
    if (!IsTsBlacklisted(desc, std::optional(blacklist))) {
      return STATUS_FORMAT(
          InvalidArgument, "Cannot remove tablet server $0 because it is not blacklisted",
          desc->permanent_uuid());
    }
    // Verify tserver is not hosting any tablets.
    for (const auto& table : tables) {
      for (const auto& tablet : VERIFY_RESULT(
               table->GetTabletsIncludeInactive())) {
        auto replicas_map = tablet->GetReplicaLocations();
        if (replicas_map->contains(desc->id())) {
          return STATUS_FORMAT(
              InvalidArgument, "Cannot remove tablet server $0 because it is hosting tablet $1",
              desc->permanent_uuid(), tablet->id());
        }
      }
    }
    write_lock = std::move(desc_lock);
  }
  // Update the TS to REMOVED to signal to code that still has a copy of the shared pointer that
  // this TS has been removed from the universe.
  write_lock.mutable_data()->pb.set_state(SysTabletServerEntryPB::REMOVED);
  if (GetAtomicFlag(&FLAGS_persist_tserver_registry)) {
    auto status = sys_catalog_.Delete(epoch, desc);
    WARN_NOT_OK(status, Format("Failed to remove tablet server $0 from sys catalog", desc->id()));
    RETURN_NOT_OK(status);
  }
  write_lock.Commit();
  {
    std::lock_guard map_l(map_lock_);
    servers_by_id_.erase(desc->id());
  }
  return Status::OK();
}

Status TSManager::ValidateAllTserverVersions(ValidateVersionInfoOp op) const {
  if (FLAGS_skip_tserver_version_checks) {
    return Status::OK();
  }

  std::vector<std::string> invalid_tservers;
  SharedLock l(map_lock_);
  for (const auto& [ts_id, ts_dsc] : servers_by_id_) {
    auto l = ts_dsc->LockForRead();
    if (!l->IsLive()) {
      // This is probably some old tserver that has been dead for hours. When it comes back up we
      // will validate it, so it can be ignored here.
      continue;
    }
    auto version_info_opt =
        l->pb.has_version_info() ? std::optional(std::cref(l->pb.version_info())) : std::nullopt;
    if (!VersionInfo::ValidateVersion(version_info_opt, op)) {
      invalid_tservers.push_back(Format(
          "[TS $0; Version $1]", ts_dsc->ToString(),
          version_info_opt ? version_info_opt->get().ShortDebugString() : "<NA>"));
    }
  }

  SCHECK_FORMAT(
      invalid_tservers.empty(), IllegalState, "yb-tserver(s) not on the correct version: $0",
      yb::ToString(invalid_tservers));

  return Status::OK();
}

namespace {

bool HasSameHostPort(const HostPortPB& lhs, const HostPortPB& rhs) {
  return lhs.host() == rhs.host() && lhs.port() == rhs.port();
}

bool HasSameHostPort(const google::protobuf::RepeatedPtrField<HostPortPB>& lhs,
                     const google::protobuf::RepeatedPtrField<HostPortPB>& rhs) {
  for (const auto& lhs_hp : lhs) {
    for (const auto& rhs_hp : rhs) {
      if (HasSameHostPort(lhs_hp, rhs_hp)) {
        return true;
      }
    }
  }
  return false;
}

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const CloudInfoPB& cloud_info) {
  return HasSameHostPort(DesiredHostPort(lhs, cloud_info), DesiredHostPort(rhs, cloud_info));
}

bool HasSameHostPort(const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs) {
  return HasSameHostPort(lhs.private_rpc_addresses(), rhs.private_rpc_addresses()) ||
         HasSameHostPort(lhs.broadcast_addresses(), rhs.broadcast_addresses());
}

bool HasSameHostPort(
    const ServerRegistrationPB& lhs, const ServerRegistrationPB& rhs,
    const std::vector<CloudInfoPB>& perspectives) {
  auto find_it = std::find_if(
      perspectives.begin(), perspectives.end(), [&lhs, &rhs](const CloudInfoPB& cloud_info) {
        return HasSameHostPort(lhs, rhs, cloud_info);
      });
  return find_it != perspectives.end();
}

std::function<bool(const ServerRegistrationPB&)> GetHostPortCheckerFunction(
    const TSRegistrationPB& registration, const CloudInfoPB& local_master_cloud_info) {
  // This pivots on a runtime flag so we cannot return a static function.
  if (PREDICT_TRUE(GetAtomicFlag(&FLAGS_master_register_ts_check_desired_host_port))) {
    // When desired host-port check is enabled, we do the following checks:
    // 1. For master, the host-port for existing and registering tservers are different.
    // 2. The existing and registering tservers have distinct host-port from each others
    // perspective.
    return
        [&registration,
         &local_master_cloud_info](const ServerRegistrationPB& existing_ts_registration) {
          auto cloud_info_perspectives = {
              local_master_cloud_info,                // master's perspective
              existing_ts_registration.cloud_info(),  // existing ts' perspective
              registration.common().cloud_info()};    // registering ts' perspective
          return HasSameHostPort(
              existing_ts_registration, registration.common(), cloud_info_perspectives);
        };
  } else {
    return [&registration](const ServerRegistrationPB& existing_ts_registration) {
      return HasSameHostPort(existing_ts_registration, registration.common());
    };
  }
}

TSDescriptorMap&& TSDescriptorLoader::TakeMap() {
  return std::move(map_);
}

Status TSDescriptorLoader::Visit(const std::string& id, const SysTabletServerEntryPB& metadata) {
  // The RegisteredThroughHeartbeat parameter controls how last_heartbeat_ is initialized.
  // If true, last_heartbeat_ is set to now. If false, last_heartbeat_ is an uninitialized MonoTime.
  // Use true here because:
  //   1. if the tserver is live, the only reasonable time to mark it as unresponsive is
  //      tserver_unresponsive_timeout_ms from now.
  //   2. if the tserver is unresponsive, this field doesn't matter.
  DCHECK(metadata.persisted())
      << "All TS descriptors written to the sys catalog should have their persisted bit set.";
  auto desc = TSDescriptor::LoadFromEntry(
      id, metadata, CloudInfoPB(local_master_cloud_info_), proxy_cache_, load_time_);
  auto [it, inserted] = map_.insert({id, std::move(desc)});
  if (!inserted) {
    return STATUS(
        AlreadyPresent, Format(
                            "Collision loading tserver entries in tserver uuids: $0, tserver "
                            "already present: $1, tserver to insert: $2",
                            id, *it, metadata));
  }
  return Status::OK();
}

template <typename Item>
void SetPersistedHelper(Item&& item);


template <>
void SetPersistedHelper(const TSDescriptorPtr& desc) {
  desc->mutable_metadata()->mutable_dirty()->pb.set_persisted(true);
}

template <>
void SetPersistedHelper(const std::vector<TSDescriptor*>& descs) {
  for (const auto& desc : descs) {
    desc->mutable_metadata()->mutable_dirty()->pb.set_persisted(true);
  }
}


template <>
void SetPersistedHelper(const std::vector<TSDescriptorPtr>& descs) {
  for (const auto& desc : descs) {
    desc->mutable_metadata()->mutable_dirty()->pb.set_persisted(true);
  }
}


void SetPersisted() {}

template <typename Item, typename... Items>
void SetPersisted(const Item& item, Items&&... items) {
  SetPersistedHelper(item);
  SetPersisted(std::forward<Items>(items)...);
}

template <typename... Items>
Status UpsertIfRequired(const LeaderEpoch& epoch, SysCatalogTable& sys_catalog, Items&&... items) {
  if (!GetAtomicFlag(&FLAGS_persist_tserver_registry)) {
    return Status::OK();
  }
  SetPersisted(items...);
  return sys_catalog.Upsert(epoch, items...);
}

}  // namespace
}  // namespace yb::master
