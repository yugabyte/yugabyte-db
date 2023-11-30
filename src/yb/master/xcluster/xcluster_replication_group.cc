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

#include "yb/master/xcluster/xcluster_replication_group.h"

#include "yb/client/client.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/sys_catalog.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/result.h"

namespace yb::master {

namespace {
// Returns nullopt when source universe does not support AutoFlags compatibility check.
// Returns a pair of bool which indicates if the configs are compatible and the source universe
// AutoFlags config version.
Result<std::optional<std::pair<bool, uint32>>> ValidateAutoFlagsConfig(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  auto master_addresses = replication_info.LockForRead()->pb.producer_master_addresses();
  auto xcluster_rpc = VERIFY_RESULT(replication_info.GetOrCreateXClusterRpcTasks(master_addresses));
  auto result = VERIFY_RESULT(
      xcluster_rpc->client()->ValidateAutoFlagsConfig(local_config, AutoFlagClass::kExternal));

  if (!result) {
    return std::nullopt;
  }

  auto& [is_valid, source_version] = *result;
  VLOG(2) << "ValidateAutoFlagsConfig for replication group: "
          << replication_info.ReplicationGroupId() << ", is_valid: " << is_valid
          << ", source universe version: " << source_version
          << ", target universe version: " << local_config.config_version();

  return result;
}

Result<cdc::ProducerEntryPB*> GetProducerEntry(
    SysClusterConfigEntryPB& cluster_config_pb,
    const cdc::ReplicationGroupId& replication_group_id) {
  auto producer_entry = FindOrNull(
      *cluster_config_pb.mutable_consumer_registry()->mutable_producer_map(),
      replication_group_id.ToString());
  SCHECK_FORMAT(
      producer_entry, NotFound, "Missing producer entry for replication group $0",
      replication_group_id);

  return producer_entry;
}

// Run AutoFlags compatibility validation.
// If the AutoFlags are compatible, compatible_auto_flag_config_version is set to the source
// universe AutoFlags config version.
// If the AutoFlags are not compatible compatible_auto_flag_config_version is set to
// kInvalidAutoFlagsConfigVersion.
// If validated_local_auto_flags_config_version is less than the new local AutoFLags config version,
// it is updated.
// If cluster_config or replication_info are updated, they are stored in the sys_catalog.
Status ValidateAutoFlagsInternal(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    SysUniverseReplicationEntryPB& replication_info_pb, ClusterConfigInfo& cluster_config,
    SysClusterConfigEntryPB& cluster_config_pb, const AutoFlagsConfigPB& local_auto_flags_config,
    const LeaderEpoch& epoch) {
  const auto local_version = local_auto_flags_config.config_version();
  const auto& replication_group_id = replication_info.ReplicationGroupId();

  bool cluster_config_changed = false;
  bool replication_info_changed = false;

  auto producer_entry = VERIFY_RESULT(GetProducerEntry(cluster_config_pb, replication_group_id));

  auto validate_result =
      VERIFY_RESULT(ValidateAutoFlagsConfig(replication_info, local_auto_flags_config));

  if (validate_result) {
    auto& [is_valid, source_version] = *validate_result;
    VLOG(2) << "ValidateAutoFlagsConfig for replication group: " << replication_group_id
            << ", is_valid: " << is_valid << ", source universe version: " << source_version
            << ", target universe version: " << local_version;
    if (producer_entry->validated_auto_flags_config_version() < source_version) {
      producer_entry->set_validated_auto_flags_config_version(source_version);
      cluster_config_changed = true;
    }
    auto old_compatible_auto_flag_config_version =
        producer_entry->compatible_auto_flag_config_version();
    if (is_valid && old_compatible_auto_flag_config_version < source_version) {
      producer_entry->set_compatible_auto_flag_config_version(source_version);
      cluster_config_changed = true;
    } else if (
        !is_valid && old_compatible_auto_flag_config_version != kInvalidAutoFlagsConfigVersion) {
      // We are not compatible with the source universe anymore.
      producer_entry->set_compatible_auto_flag_config_version(kInvalidAutoFlagsConfigVersion);
      cluster_config_changed = true;
    }
  } else {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id
        << " is running a version that does not support the AutoFlags compatibility check yet";
  }

  if (!replication_info_pb.has_validated_local_auto_flags_config_version() ||
      replication_info_pb.validated_local_auto_flags_config_version() < local_version) {
    replication_info_pb.set_validated_local_auto_flags_config_version(local_version);
    replication_info_changed = true;
  }

  if (cluster_config_changed) {
    // Bump the ClusterConfig version so we'll broadcast new config version to tservers.
    cluster_config_pb.set_version(cluster_config_pb.version() + 1);
  }

  if (cluster_config_changed && replication_info_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &cluster_config, &replication_info),
        "Updating cluster config and replication info in sys-catalog");
  } else if (cluster_config_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &cluster_config),
        "Updating cluster config in sys-catalog");
  } else if (replication_info_changed) {
    RETURN_NOT_OK_PREPEND(
        sys_catalog.Upsert(epoch.leader_term, &replication_info),
        "Updating replication info in sys-catalog");
  }

  return Status::OK();
}
}  // namespace

Result<uint32> GetAutoFlagConfigVersionIfCompatible(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  const auto& replication_group_id = replication_info.ReplicationGroupId();

  VLOG_WITH_FUNC(2) << "Validating AutoFlags config for replication group: " << replication_group_id
                    << " with target config version: " << local_config.config_version();

  auto validate_result = VERIFY_RESULT(ValidateAutoFlagsConfig(replication_info, local_config));

  if (!validate_result) {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id
        << " is running a version that does not support the AutoFlags compatibility check yet";
    return kInvalidAutoFlagsConfigVersion;
  }

  auto& [is_valid, source_version] = *validate_result;

  SCHECK(
      is_valid, IllegalState,
      "AutoFlags between the universes are not compatible. Upgrade the target universe to a "
      "version higher than or equal to the source universe");

  return source_version;
}

Status RefreshAutoFlagConfigVersion(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, uint32 requested_auto_flag_version,
    std::function<AutoFlagsConfigPB()> get_local_auto_flags_config_func, const LeaderEpoch& epoch) {
  VLOG_WITH_FUNC(2) << "Revalidating AutoFlags config for replication group: "
                    << replication_info.ReplicationGroupId()
                    << " with requested source config version: " << requested_auto_flag_version;

  // Lock ClusterConfig before ReplicationInfo.
  auto cluster_config_lock = cluster_config.LockForWrite();
  auto& cluster_config_pb = cluster_config_lock.mutable_data()->pb;

  auto producer_entry =
      VERIFY_RESULT(GetProducerEntry(cluster_config_pb, replication_info.ReplicationGroupId()));

  if (requested_auto_flag_version <= producer_entry->validated_auto_flags_config_version()) {
    VLOG_WITH_FUNC(2)
        << "Requested source universe AutoFlags config version has already been validated";
    return Status::OK();
  }

  auto replication_info_write_lock = replication_info.LockForWrite();
  auto& replication_info_pb = replication_info_write_lock.mutable_data()->pb;

  const auto local_auto_flags_config = get_local_auto_flags_config_func();
  RETURN_NOT_OK(ValidateAutoFlagsInternal(
      sys_catalog, replication_info, replication_info_pb, cluster_config, cluster_config_pb,
      std::move(local_auto_flags_config), epoch));

  replication_info_write_lock.Commit();
  cluster_config_lock.Commit();

  return Status::OK();
}

Status HandleLocalAutoFlagsConfigChange(
    SysCatalogTable& sys_catalog, UniverseReplicationInfo& replication_info,
    ClusterConfigInfo& cluster_config, const AutoFlagsConfigPB& local_auto_flags_config,
    const LeaderEpoch& epoch) {
  const auto local_config_version = local_auto_flags_config.config_version();
  VLOG_WITH_FUNC(2) << "Revalidating AutoFlags config for replication group: "
                    << replication_info.ReplicationGroupId()
                    << " with local config version: " << local_config_version;

  {
    auto replication_info_read_lock = replication_info.LockForRead();
    auto& replication_info_pb = replication_info_read_lock->pb;

    if (replication_info_pb.has_validated_local_auto_flags_config_version() &&
        replication_info_pb.validated_local_auto_flags_config_version() >= local_config_version) {
      return Status::OK();
    }
  }

  // Lock ClusterConfig before ReplicationInfo.
  auto cluster_config_lock = cluster_config.LockForWrite();
  auto& cluster_config_pb = cluster_config_lock.mutable_data()->pb;

  auto replication_info_write_lock = replication_info.LockForWrite();
  auto& replication_info_pb = replication_info_write_lock.mutable_data()->pb;

  RETURN_NOT_OK(ValidateAutoFlagsInternal(
      sys_catalog, replication_info, replication_info_pb, cluster_config, cluster_config_pb,
      local_auto_flags_config, epoch));

  replication_info_write_lock.Commit();
  cluster_config_lock.Commit();

  return Status::OK();
}
}  // namespace yb::master
