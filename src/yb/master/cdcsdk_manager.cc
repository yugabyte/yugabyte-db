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

#include "yb/master/cdcsdk_manager.h"

#include <list>
#include <set>

#include "yb/common/common_flags.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"

#include "yb/gutil/walltime.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_test_flag(bool, cdcsdk_skip_processing_dynamic_table_addition, false,
    "Skip finding unprocessed tables for cdcsdk streams");

DEFINE_test_flag(bool, cdcsdk_skip_processing_unqualified_tables, false,
    "Skip the bg task that finds and removes unprocessed unqualified tables from "
    "cdcsdk streams.");

DECLARE_bool(cdcsdk_enable_dynamic_table_addition_with_table_cleanup);

namespace yb::master {

using TableStreamIdsMap = std::unordered_map<TableId, std::list<CDCStreamInfoPtr>>;

CdcsdkManager::CdcsdkManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : master_(master), catalog_manager_(catalog_manager), sys_catalog_(sys_catalog) {}

CdcsdkManager::~CdcsdkManager() = default;

void CdcsdkManager::FillHeartbeatResponse(
    const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) {
  if (!FLAGS_TEST_cdcsdk_enable_cdc_table) {
    return;
  }
  const auto leader_term = catalog_manager_.GetLeaderEpochInternal().leader_term;
  std::lock_guard l(cdc_table_registry_mutex_);
  // Until this leader has computed the registry, the tservers keep polling what they have.
  if (cdc_table_registry_leader_term_ != leader_term) {
    return;
  }
  // Inequality rather than "older than": the versions of a new master leader are unrelated to
  // those of the previous one.
  if (req.cdc_table_registry_version() != cdc_table_registry_.version()) {
    *resp->mutable_cdc_table_registry() = cdc_table_registry_;
  }
}

Result<cdc::CdcTablePollerRegistryPB> CdcsdkManager::GetCdcTableRegistry() const {
  const auto leader_term = catalog_manager_.GetLeaderEpochInternal().leader_term;
  std::lock_guard l(cdc_table_registry_mutex_);
  SCHECK(
      cdc_table_registry_leader_term_ && *cdc_table_registry_leader_term_ == leader_term, TryAgain,
      "The CDC table poller registry has not been computed in leader term $0 yet", leader_term);
  return cdc_table_registry_;
}

Result<std::optional<cdc::CdcTableStreamEntryPB>> CdcsdkManager::ComputeCdcTableStreamEntry(
    const CDCStreamInfo& stream) {
  const auto namespace_id = stream.namespace_id();
  {
    auto l = stream.LockForRead();
    const auto& plugin_name = l->pb.cdcsdk_ysql_replication_slot_plugin_name();
    if (namespace_id.empty() || l->pb.state() != SysCDCStreamEntryPB::ACTIVE ||
        l->pb.cdcsdk_ysql_replication_slot_name().empty() || plugin_name.empty() ||
        plugin_name == kYbGrpcStreamIndicator) {
      return std::nullopt;
    }
  }
  const auto namespace_name =
      VERIFY_RESULT(catalog_manager_.FindNamespaceById(namespace_id))->name();
  if (namespace_name == kYbSystemDbName) {
    return std::nullopt;
  }

  auto cdc_table = catalog_manager_.GetTableInfoFromNamespaceNameAndTableName(
      YQL_DATABASE_PGSQL, namespace_name, kPgYbCdcChangesTableName, "public");
  SCHECK_FORMAT(
      cdc_table, NotFound, "Table $0 not found in database $1", kPgYbCdcChangesTableName,
      namespace_name);

  const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id));
  const auto publication_tables =
      VERIFY_RESULT(sys_catalog_.ReadPgPublicationTables(database_oid));
  std::unordered_set<TableId> explicitly_published;
  if (!publication_tables.all_tables) {
    const auto relfilenodes =
        VERIFY_RESULT(sys_catalog_.ReadPgClassColumnWithOidValueMap(database_oid, "relfilenode"));
    for (const auto rel_oid : publication_tables.rel_oids) {
      auto it = relfilenodes.find(rel_oid);
      if (it != relfilenodes.end() && it->second != kPgInvalidOid) {
        explicitly_published.insert(GetPgsqlTableId(database_oid, it->second));
      }
    }
  }

  cdc::CdcTableStreamEntryPB entry;
  entry.set_stream_id(stream.id());
  entry.set_namespace_id(namespace_id);
  entry.set_cdc_table_id(cdc_table->id());
  for (const auto& table_id : stream.table_id()) {
    auto table = catalog_manager_.GetTableInfo(table_id);
    if (!table) {
      continue;
    }
    if (!table->IsUserTable()) {
      // The stream holds catalog tables only when the sys catalog tablet is to be polled.
      entry.set_poll_sys_catalog(true);
    } else if (publication_tables.all_tables || explicitly_published.contains(table_id)) {
      entry.add_published_table_ids(table_id);
    }
  }
  return entry;
}

Status CdcsdkManager::RefreshCdcTableRegistry(const LeaderEpoch& epoch) {
  cdc::CdcTablePollerRegistryPB registry;

  // Sorted, so that the same stream is picked on every pass.
  const auto unordered_stream_ids = catalog_manager_.GetAllXReplStreamIds();
  const std::set<xrepl::StreamId> stream_ids(
      unordered_stream_ids.begin(), unordered_stream_ids.end());
  std::erase_if(cdc_table_stream_entries_, [&stream_ids](const auto& entry) {
    return !stream_ids.contains(entry.first);
  });

  for (const auto& stream_id : stream_ids) {
    auto it = cdc_table_stream_entries_.find(stream_id);
    if (it == cdc_table_stream_entries_.end()) {
      auto stream = catalog_manager_.GetXReplStreamInfo(stream_id);
      if (!stream.ok()) {
        continue;
      }
      auto entry = VERIFY_RESULT(ComputeCdcTableStreamEntry(**stream));
      if (!entry) {
        continue;
      }
      LOG(INFO) << "CDC table poller registry entry for stream " << stream_id << ": "
                << entry->ShortDebugString();
      it = cdc_table_stream_entries_.emplace(stream_id, std::move(*entry)).first;
    }
    if (registry.streams_size() > 0) {
      // TODO: support more than one stream.
      YB_LOG_EVERY_N_SECS(WARNING, 60)
          << "Only one stream is polled into a CDC table, ignoring stream " << stream_id;
      continue;
    }

    auto* entry = registry.add_streams();
    *entry = it->second;
    // A colocated tablet hosts several tables.
    std::set<TabletId> tablet_ids;
    for (const auto& table_id : entry->published_table_ids()) {
      auto table = catalog_manager_.GetTableInfo(table_id);
      if (!table) {
        continue;
      }
      for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
        tablet_ids.insert(tablet->id());
      }
    }
    for (const auto& tablet_id : tablet_ids) {
      entry->add_tablet_ids(tablet_id);
    }
  }

  if (registry.streams_size() > 0) {
    TSDescriptorVector live_tservers;
    master_.ts_manager()->GetAllLiveDescriptors(&live_tservers);
    std::string sys_catalog_poller;
    for (const auto& tserver : live_tservers) {
      if (sys_catalog_poller.empty() || tserver->permanent_uuid() < sys_catalog_poller) {
        sys_catalog_poller = tserver->permanent_uuid();
      }
    }
    registry.set_sys_catalog_poller_ts_uuid(sys_catalog_poller);
  }

  bool start_safe_time_service;
  {
    std::lock_guard l(cdc_table_registry_mutex_);
    const bool first_in_leader_term = cdc_table_registry_leader_term_ != epoch.leader_term;
    cdc_table_registry_leader_term_ = epoch.leader_term;

    registry.set_version(cdc_table_registry_.version());
    const bool changed = registry.SerializeAsString() != cdc_table_registry_.SerializeAsString();
    if (changed) {
      // An empty registry keeps version 0, which is what a tserver without pollers reports.
      registry.set_version(registry.streams_size() > 0 ? GetCurrentTimeMicros() : 0);
      LOG(INFO) << "New CDC table poller registry: " << registry.ShortDebugString();
      cdc_table_registry_ = std::move(registry);
    }
    start_safe_time_service =
        (changed || first_in_leader_term) && cdc_table_registry_.streams_size() > 0;
  }

  if (start_safe_time_service) {
    // The service computes the CDC safe time of the database from the safe times that the pollers
    // write to its table. It idles while no tablets are polled, and when the leader changes.
    catalog_manager_.GetXClusterManagerImpl()->CreateXClusterSafeTimeTableAndStartService();
  }
  return Status::OK();
}

void CdcsdkManager::RunBgTasks(const LeaderEpoch& epoch) {
  if (FLAGS_TEST_cdcsdk_enable_cdc_table) {
    WARN_NOT_OK(
        RefreshCdcTableRegistry(epoch), "Failed to refresh the CDC table poller registry");
  }

  if (!FLAGS_TEST_cdcsdk_skip_processing_dynamic_table_addition) {
    // Find if there have been any new tables added to any namespace with an active cdcsdk
    // stream.
    TableStreamIdsMap table_unprocessed_streams_map;
    // In case of master leader restart of leadership changes, we will scan all streams for
    // unprocessed tables, but from the second iteration onwards we will only consider the
    // 'cdcsdk_unprocessed_tables' field of CDCStreamInfo object stored in the cdc_state_map.
    Status s = catalog_manager_.FindCDCSDKStreamsForAddedTables(&table_unprocessed_streams_map);

    if (s.ok() && !table_unprocessed_streams_map.empty()) {
      s = catalog_manager_.ProcessNewTablesForCDCSDKStreams(table_unprocessed_streams_map, epoch);
    }
    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10)
          << "Encountered failure while trying to add unprocessed tables to cdc_state table: "
          << s.ToString();
    }
  } else {
    LOG(INFO) << "Skipping processing of dynamic table addition due to "
                 "cdcsdk_skip_processing_dynamic_table_addition being true";
  }

  if (FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup) {
    // Find if there are any non eligible tables (indexes, mat views) present in cdcsdk streams.
    TableStreamIdsMap non_eligible_tables_to_streams_map;
    // In case of master leader restart or leadership changes, we would have scanned all
    // streams in ACTIVE/DELETING METADATA state for non eligible tables and marked such tables for
    // removal in namespace_to_cdcsdk_non_eligible_table_map_.
    Status s =
        catalog_manager_.FindCDCSDKStreamsForNonEligibleTables(&non_eligible_tables_to_streams_map);

    if (s.ok() && !non_eligible_tables_to_streams_map.empty()) {
      s = catalog_manager_.ProcessTablesToBeRemovedFromCDCSDKStreams(
          non_eligible_tables_to_streams_map, /* non_eligible_table_cleanup */ true, epoch);
    }
    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10) << "Encountered failure while trying to remove non eligible "
                                     "tables from cdc_state table: "
                                  << s.ToString();
    }
  }

  if (FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup &&
      !FLAGS_TEST_cdcsdk_skip_processing_unqualified_tables) {
    TableStreamIdsMap tables_to_be_removed_streams_map;
    Status s = catalog_manager_.FindCDCSDKStreamsForUnprocessedUnqualifiedTables(
        &tables_to_be_removed_streams_map);

    if (s.ok() && !tables_to_be_removed_streams_map.empty()) {
      s = catalog_manager_.ProcessTablesToBeRemovedFromCDCSDKStreams(
          tables_to_be_removed_streams_map, /* non_eligible_table_cleanup */ false, epoch);
    }

    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10) << "Encountered failure while trying to remove unqualified "
                                     "tables from stream metadata & updating cdc_state table: "
                                  << s.ToString();
    }
  }

  if (FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup) {
    TableStreamIdsMap ineligible_tables_to_streams_map;
    Status s =
        catalog_manager_.FindCDCSDKStreamsForIneligibleTables(&ineligible_tables_to_streams_map);
    if (s.ok() && !ineligible_tables_to_streams_map.empty()) {
      s = catalog_manager_.ProcessTablesToBeDeletedFromCDCStateTable(
          ineligible_tables_to_streams_map, epoch);
    }

    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10) << "Encountered failure while trying to delete entries for "
                                     "ineligible tables from cdc_state table: "
                                  << s.ToString();
    }
  }
}

}  // namespace yb::master
