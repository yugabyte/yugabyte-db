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

#include <algorithm>
#include <array>
#include <list>
#include <set>
#include <vector>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/common/common_flags.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/gutil/walltime.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/scoped_leader_shared_lock.h"
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

  for (const auto& report : req.cdc_table_split_reports()) {
    auto stream_id = xrepl::StreamId::FromString(report.stream_id());
    if (!stream_id.ok()) {
      LOG(DFATAL) << "Invalid stream id in CDC table split report: " << report.ShortDebugString();
      continue;
    }
    // The first report of a tablet is used. A later one, from the poller on a new leader of the
    // tablet, is as good: both positions have been applied.
    cdc_table_split_reports_.emplace(
        std::make_pair(*stream_id, report.tablet_id()), report.position());
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

  cdc::CdcTableStreamEntryPB entry;
  entry.set_stream_id(stream.id());
  entry.set_namespace_id(namespace_id);
  entry.set_cdc_table_id(cdc_table->id());
  for (const auto& table_id : stream.table_id()) {
    auto table = catalog_manager_.GetTableInfo(table_id);
    // The stream holds catalog tables only when the sys catalog tablet is to be polled. Without
    // them, changes to the publications go unnoticed, and the polled tables stay those that were
    // published when the stream was created.
    if (table && !table->IsUserTable()) {
      entry.set_poll_sys_catalog(true);
      break;
    }
  }
  return entry;
}

Result<std::set<TableId>> CdcsdkManager::ReadPublishedTables(
    const CDCStreamInfo& stream, HybridTime time) {
  const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(stream.namespace_id()));
  const auto read_time = ReadHybridTime::SingleTime(time);
  const auto publication_tables =
      VERIFY_RESULT(sys_catalog_.ReadPgPublicationTables(database_oid, read_time));
  const auto relfilenodes = VERIFY_RESULT(
      sys_catalog_.ReadPgClassColumnWithOidValueMap(database_oid, "relfilenode", read_time));

  std::set<TableId> result;
  const bool allow_tables_without_primary_key = stream.IsTablesWithoutPrimaryKeyAllowed();
  auto add_if_eligible = [&](PgOid relfilenode) {
    if (relfilenode == kPgInvalidOid) {
      return;
    }
    // A relation without a table of its own, such as a view, is not found. Nor is a table that has
    // been dropped since and is gone: there is nothing left to poll.
    auto table = catalog_manager_.GetTableInfo(GetPgsqlTableId(database_oid, relfilenode));
    // What decides whether a table is part of a stream. Not the table list of the stream: the CDC
    // bg task adds a new table to it only some time after the table's row in pg_class commits.
    if (table && catalog_manager_.IsTableEligibleForCDCSDKStream(
                     table, table->LockForRead(), /* check_schema= */ true,
                     allow_tables_without_primary_key,
                     /* allow_cdc_used_syscatalog_tables= */ false)) {
      result.insert(table->id());
    }
  };
  if (publication_tables.all_tables) {
    for (const auto& [_, relfilenode] : relfilenodes) {
      add_if_eligible(relfilenode);
    }
  } else {
    for (const auto rel_oid : publication_tables.rel_oids) {
      auto it = relfilenodes.find(rel_oid);
      if (it != relfilenodes.end()) {
        add_if_eligible(it->second);
      }
    }
  }
  return result;
}

std::optional<PgOid> CdcsdkManager::RelationOid(const TableId& table_id) const {
  auto table = catalog_manager_.GetTableInfo(table_id);
  if (!table) {
    return std::nullopt;
  }
  auto relation_oid = table->GetPgTableOid();
  return relation_oid.ok() ? std::optional<PgOid>(*relation_oid) : std::nullopt;
}

Result<bool> CdcsdkManager::UpdatePolledTablesAsOf(
    const CDCStreamInfo& stream, HybridTime time, CdcTablePolledTablesPB* polled_tables) {
  const auto published = VERIFY_RESULT(ReadPublishedTables(stream, time));
  bool changed = false;
  std::set<TableId> polled;
  // The tables that are found to be no longer published in this call, by relation.
  std::map<PgOid, TableId> unpublished_tables;
  for (auto& table : *polled_tables->mutable_tables()) {
    polled.insert(table.table_id());
    const bool is_published = published.contains(table.table_id());
    if (is_published && table.has_stop_time()) {
      // Published again before it left. Its pollers never stopped, so nothing is to be done for
      // the changes from now on. Those from in between are in the CDC table without being wanted,
      // like the ones a poller writes before it gets to know that its table is no longer
      // published: a reader has to go by the publications as of the commit time of a change.
      LOG(INFO) << "Table " << table.table_id() << " of stream " << stream.id()
                << " is published again as of " << time << ", and stays polled";
      table.clear_stop_time();
      changed = true;
    } else if (!is_published && !table.has_stop_time()) {
      // Its pollers may not have read the changes up to now yet. The table stays until they have,
      // see RemoveStoppedTables.
      LOG(INFO) << "Table " << table.table_id() << " of stream " << stream.id()
                << " is no longer published as of " << time
                << ". It is polled until the CDC safe time reaches that";
      table.set_stop_time(time.ToUint64());
      changed = true;
      if (const auto relation_oid = RelationOid(table.table_id())) {
        unpublished_tables.emplace(*relation_oid, table.table_id());
      }
    }
  }
  for (const auto& table_id : published) {
    if (polled.contains(table_id)) {
      continue;
    }
    // One tick before, so that the changes that commit at this very time are polled: a
    // transaction can publish a table and then write to it. A poller asks for the changes that
    // commit after its safe time.
    const auto start_time = time.ToUint64() - 1;
    // A statement that rewrites a table moves its relation to another DocDB table, by a change to
    // the relation's row in pg_class. As of the commit time of that change the relation's old
    // table is not published and its new one is, so nothing but the two cases of this function is
    // needed: the old table is polled until the CDC table has its changes up to this time, and
    // the new one from this time on. That includes the rows that the statement copied from the
    // old table, which commit with the change to pg_class. The virtual WAL sends them too.
    const auto relation_oid = RelationOid(table_id);
    const auto rewritten_table = relation_oid ? unpublished_tables.find(*relation_oid)
                                              : unpublished_tables.end();
    LOG(INFO) << "Table " << table_id << " of stream " << stream.id() << " is published as of "
              << time
              << (rewritten_table != unpublished_tables.end()
                      ? Format(", in place of table $0 that was rewritten", rewritten_table->second)
                      : "")
              << ", and is polled for the changes that commit after " << start_time;
    auto* table = polled_tables->add_tables();
    table->set_table_id(table_id);
    table->set_start_time(start_time);
    changed = true;
  }
  if (changed) {
    polled_tables->set_catalog_time(time.ToUint64());
  }
  return changed;
}

Result<CdcTablePolledTablesPB> CdcsdkManager::GetOrInitPolledTables(
    const CDCStreamInfoPtr& stream, const LeaderEpoch& epoch) {
  {
    auto l = stream->LockForRead();
    if (l->pb.has_cdc_table_polled_tables()) {
      return l->pb.cdc_table_polled_tables();
    }
  }

  // As of the time the stream starts from, not as of now: the poller of the sys catalog tablet
  // starts from that time too, so every later change to the publications gets to
  // ProcessSysCatalogChanges, and is applied on top of this.
  SCHECK_FORMAT(
      stream->IsConsistentSnapshotStream(), IllegalState,
      "Stream $0 has no consistent snapshot time", stream->id());
  const auto start_time = stream->GetConsistentSnapshotHybridTime();
  CdcTablePolledTablesPB polled_tables;
  polled_tables.set_catalog_time(start_time.ToUint64());
  for (const auto& table_id : VERIFY_RESULT(ReadPublishedTables(*stream, start_time))) {
    polled_tables.add_tables()->set_table_id(table_id);
  }
  LOG(INFO) << "Tables polled for stream " << stream->id()
            << " from its start: " << polled_tables.ShortDebugString();
  RETURN_NOT_OK(PersistPolledTables(stream, polled_tables, epoch));
  return polled_tables;
}

bool CdcsdkManager::RemoveStoppedTables(
    const CDCStreamInfo& stream, CdcTablePolledTablesPB* polled_tables) {
  if (std::ranges::none_of(
          polled_tables->tables(), [](const auto& table) { return table.has_stop_time(); })) {
    return false;
  }
  // The safe time of the database is the minimum over the tablets in the registry, and the
  // tablets of these tables have been in it all along. Once it is at stop_time, their pollers
  // have written every change up to stop_time to the CDC table.
  const auto safe_times =
      catalog_manager_.GetXClusterManagerImpl()->GetXClusterNamespaceToSafeTimeMap();
  const auto it = safe_times.find(stream.namespace_id());
  if (it == safe_times.end() || !it->second.is_valid() || it->second.is_special()) {
    return false;
  }
  const auto safe_time = it->second.ToUint64();

  bool changed = false;
  auto* tables = polled_tables->mutable_tables();
  for (auto table_it = tables->begin(); table_it != tables->end();) {
    if (table_it->has_stop_time() && table_it->stop_time() <= safe_time) {
      LOG(INFO) << "Table " << table_it->table_id() << " of stream " << stream.id()
                << " is no longer polled: the CDC safe time " << it->second
                << " has reached the time it was last published, " << table_it->stop_time();
      table_it = tables->erase(table_it);
      changed = true;
    } else {
      ++table_it;
    }
  }
  return changed;
}

Status CdcsdkManager::PersistPolledTables(
    const CDCStreamInfoPtr& stream, const CdcTablePolledTablesPB& polled_tables,
    const LeaderEpoch& epoch) {
  auto l = stream->LockForWrite();
  SCHECK_FORMAT(
      l->pb.state() == SysCDCStreamEntryPB::ACTIVE, IllegalState, "Stream $0 is not active",
      stream->id());
  *l.mutable_data()->pb.mutable_cdc_table_polled_tables() = polled_tables;
  RETURN_NOT_OK_PREPEND(
      sys_catalog_.Upsert(epoch, stream),
      Format("Failed to persist the polled tables of stream $0", stream->id()));
  l.Commit();
  return Status::OK();
}

Status CdcsdkManager::ProcessSysCatalogChanges(
    const xrepl::StreamId& stream_id, const cdc::GetChangesResponsePB& resp) {
  if (!FLAGS_TEST_cdcsdk_enable_cdc_table || resp.cdc_sdk_proto_records().empty()) {
    return Status::OK();
  }
  SCOPED_LEADER_SHARED_LOCK(leader_lock, &catalog_manager_);
  RETURN_NOT_OK(leader_lock.first_failed_status());
  const auto& epoch = leader_lock.epoch();

  std::lock_guard l(cdc_table_mutex_);
  auto stream = VERIFY_RESULT(catalog_manager_.GetXReplStreamInfo(stream_id));
  CdcTablePolledTablesPB polled_tables;
  {
    auto stream_lock = stream->LockForRead();
    // The CDC bg task sets them before the tservers get to know about the stream, so a CDC table
    // poller is not what asked.
    if (!stream_lock->pb.has_cdc_table_polled_tables()) {
      return Status::OK();
    }
    polled_tables = stream_lock->pb.cdc_table_polled_tables();
  }

  // Which tables are published follows from these catalog tables. Any change to one of them is
  // taken as a possible change to that, and the published tables are read again as of its commit
  // time. A change is not interpreted: that would need the publications each table is in.
  const auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(stream->namespace_id()));
  const std::array<TableId, 3> catalog_table_ids = {
      GetPgsqlTableId(database_oid, kPgPublicationOid),
      GetPgsqlTableId(database_oid, kPgPublicationRelOid),
      GetPgsqlTableId(database_oid, kPgClassTableOid)};
  std::set<uint64_t> commit_times;
  for (const auto& record : resp.cdc_sdk_proto_records()) {
    const auto& row = record.row_message();
    const bool is_dml = row.op() == cdc::RowMessage::INSERT ||
                        row.op() == cdc::RowMessage::UPDATE ||
                        row.op() == cdc::RowMessage::DELETE;
    if (is_dml && std::ranges::find(catalog_table_ids, row.table_id()) != catalog_table_ids.end()) {
      commit_times.insert(row.commit_time());
    }
  }
  if (commit_times.empty()) {
    return Status::OK();
  }

  // In commit order, and each as of its own commit time. The catalog as of now would do for a
  // single change, but not for a table that was published and unpublished again since: its
  // changes from in between have to be polled.
  bool changed = false;
  for (const auto commit_time : commit_times) {
    // Applied before: the response is a repeat, or came from a second poller of the sys catalog
    // tablet. Applying an older change again would undo the later ones.
    if (commit_time <= polled_tables.catalog_time()) {
      continue;
    }
    changed |= VERIFY_RESULT(
        UpdatePolledTablesAsOf(*stream, HybridTime(commit_time), &polled_tables));
  }
  if (changed) {
    RETURN_NOT_OK(PersistPolledTables(stream, polled_tables, epoch));
  }
  // Also when nothing changed here. An earlier call can have failed after it persisted the
  // tables, and this one must not return before the registry has them either.
  return DoRefreshCdcTableRegistry(epoch);
}

Status CdcsdkManager::UpdateCdcTablePolledTablets(
    const xrepl::StreamId& stream_id, const CdcTablePolledTablesPB& polled_tables,
    const CdcTableSplitReports& split_reports, CdcTablePolledTablets* polled) {
  if (polled->initialized) {
    // A polled tablet that no longer exists was not handed over to its children, for example
    // because its table was dropped.
    for (const auto& [tablet_id, _] : polled->tablets) {
      auto tablet = catalog_manager_.GetTabletInfo(tablet_id);
      if (!tablet.ok() || (*tablet)->LockForRead()->is_deleted()) {
        LOG(WARNING) << "Polled tablet " << tablet_id << " of stream " << stream_id
                     << " is gone, starting over from the current tablets of its tables";
        polled->initialized = false;
        break;
      }
    }
  }

  if (!polled->initialized) {
    // The tablets are added again below, each polled from its own row in cdc_state. For a tablet
    // whose parent was polled, that row is a copy the master made of the parent's row when the
    // split was registered, so nothing is skipped. The parent may have been polled further since,
    // and those changes are read again.
    polled->tablets.clear();
    polled->tables.clear();
    polled->initialized = true;
  }

  std::set<TableId> polled_table_ids;
  for (const auto& polled_table : polled_tables.tables()) {
    const auto& table_id = polled_table.table_id();
    polled_table_ids.insert(table_id);
    if (polled->tables.contains(table_id)) {
      continue;
    }
    // The tablets of a table that was published after the stream was created have rows in
    // cdc_state from when the table was added to the stream, with a checkpoint from before the
    // table was published. Their pollers read from there, and leave out the changes that
    // committed before it was.
    std::optional<cdc::CdcTablePollerPositionPB> start_position;
    if (polled_table.has_start_time()) {
      start_position.emplace();
      start_position->set_term(OpId::Invalid().term);
      start_position->set_index(OpId::Invalid().index);
      start_position->set_safe_hybrid_time(polled_table.start_time());
    }
    auto table = catalog_manager_.GetTableInfo(table_id);
    if (table) {
      for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
        if (tablet->LockForRead()->is_deleted()) {
          continue;
        }
        polled->tablets.emplace(
            tablet->id(),
            CdcTablePolledTablets::Tablet{.table_id = table_id, .start_position = start_position});
      }
    } else {
      // Dropped, and nothing of it is left to poll. It leaves once the CDC safe time, which it
      // does not hold back, reaches the time of the drop.
      LOG(WARNING) << "Polled table " << table_id << " of stream " << stream_id << " is gone";
    }
    // Only now: if the tablets could not be had, the next pass tries again.
    polled->tables.insert(table_id);
  }
  // The tables that left. Their pollers have read what was wanted, see RemoveStoppedTables.
  std::erase_if(polled->tables, [&polled_table_ids](const auto& table_id) {
    return !polled_table_ids.contains(table_id);
  });
  std::erase_if(polled->tablets, [&polled_table_ids](const auto& tablet) {
    return !polled_table_ids.contains(tablet.second.table_id);
  });

  for (const auto& [stream_and_tablet, position] : split_reports) {
    const auto& tablet_id = stream_and_tablet.second;
    if (stream_and_tablet.first != stream_id || !polled->tablets.contains(tablet_id)) {
      continue;
    }

    std::vector<TabletId> children;
    {
      auto parent = VERIFY_RESULT(catalog_manager_.GetTabletInfo(tablet_id));
      auto parent_lock = parent->LockForRead();
      const auto& split_tablet_ids = parent_lock->pb.split_tablet_ids();
      children.assign(split_tablet_ids.begin(), split_tablet_ids.end());
    }
    const bool children_known =
        children.size() == 2 && std::ranges::all_of(children, [this](const auto& child) {
          return catalog_manager_.GetTabletInfo(child).ok();
        });
    if (!children_known) {
      // The tserver repeats the report.
      YB_LOG_EVERY_N_SECS(WARNING, 10)
          << "Tablet " << tablet_id << " of stream " << stream_id
          << " was reported as split, but its children are not known yet: " << AsString(children);
      continue;
    }

    // The parent leaves and both children enter in the same pass, and so in the same registry
    // version. With only one of them polled, or with neither the parent nor the children, the CDC
    // safe time would be computed without part of the key range, and could pass changes that are
    // not in the CDC table.
    LOG(INFO) << "Tablet " << tablet_id << " of stream " << stream_id
              << " has been split. Its children " << AsString(children)
              << " are polled from where its poller stopped: " << position.ShortDebugString();
    const auto table_id = polled->tablets[tablet_id].table_id;
    polled->tablets.erase(tablet_id);
    for (const auto& child : children) {
      polled->tablets[child] =
          CdcTablePolledTablets::Tablet{.table_id = table_id, .start_position = position};
    }
  }
  return Status::OK();
}

Status CdcsdkManager::RefreshCdcTableRegistry(const LeaderEpoch& epoch) {
  std::lock_guard l(cdc_table_mutex_);
  return DoRefreshCdcTableRegistry(epoch);
}

Status CdcsdkManager::DoRefreshCdcTableRegistry(const LeaderEpoch& epoch) {
  cdc::CdcTablePollerRegistryPB registry;

  // Sorted, so that the same stream is picked on every pass.
  const auto unordered_stream_ids = catalog_manager_.GetAllXReplStreamIds();
  const std::set<xrepl::StreamId> stream_ids(
      unordered_stream_ids.begin(), unordered_stream_ids.end());
  std::erase_if(cdc_table_stream_entries_, [&stream_ids](const auto& entry) {
    return !stream_ids.contains(entry.first);
  });

  // What an earlier leader term of this master left is out of date.
  if (cdc_table_polled_tablets_leader_term_ != epoch.leader_term) {
    cdc_table_polled_tablets_.clear();
    cdc_table_polled_tablets_leader_term_ = epoch.leader_term;
  }
  std::erase_if(cdc_table_polled_tablets_, [&stream_ids](const auto& entry) {
    return !stream_ids.contains(entry.first);
  });
  CdcTableSplitReports split_reports;
  {
    std::lock_guard l(cdc_table_registry_mutex_);
    split_reports.swap(cdc_table_split_reports_);
  }

  for (const auto& stream_id : stream_ids) {
    auto stream_result = catalog_manager_.GetXReplStreamInfo(stream_id);
    if (!stream_result.ok()) {
      continue;
    }
    const auto& stream = *stream_result;
    auto it = cdc_table_stream_entries_.find(stream_id);
    if (it == cdc_table_stream_entries_.end()) {
      auto entry = VERIFY_RESULT(ComputeCdcTableStreamEntry(*stream));
      if (!entry) {
        continue;
      }
      LOG(INFO) << "CDC table poller registry entry for stream " << stream_id << ": "
                << entry->ShortDebugString();
      it = cdc_table_stream_entries_.emplace(stream_id, std::move(*entry)).first;
    }
    if (stream->LockForRead()->pb.state() != SysCDCStreamEntryPB::ACTIVE) {
      continue;
    }
    if (registry.streams_size() > 0) {
      // TODO: support more than one stream.
      YB_LOG_EVERY_N_SECS(WARNING, 60)
          << "Only one stream is polled into a CDC table, ignoring stream " << stream_id;
      continue;
    }

    auto polled_tables = VERIFY_RESULT(GetOrInitPolledTables(stream, epoch));
    if (RemoveStoppedTables(*stream, &polled_tables)) {
      RETURN_NOT_OK(PersistPolledTables(stream, polled_tables, epoch));
    }

    auto* entry = registry.add_streams();
    *entry = it->second;
    for (const auto& polled_table : polled_tables.tables()) {
      entry->add_published_table_ids(polled_table.table_id());
    }
    auto& polled = cdc_table_polled_tablets_[stream_id];
    RETURN_NOT_OK(UpdateCdcTablePolledTablets(stream_id, polled_tables, split_reports, &polled));
    for (const auto& [tablet_id, tablet] : polled.tablets) {
      entry->add_tablet_ids(tablet_id);
      if (tablet.start_position) {
        auto* tablet_start = entry->add_tablet_starts();
        tablet_start->set_tablet_id(tablet_id);
        *tablet_start->mutable_position() = *tablet.start_position;
      }
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
