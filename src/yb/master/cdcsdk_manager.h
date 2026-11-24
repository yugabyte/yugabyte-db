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

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/xrepl_types.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"

#include "yb/gutil/ref_counted.h"

#include "yb/master/cdcsdk_manager_if.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"

#include "yb/util/status_fwd.h"

namespace yb::cdc {
class GetChangesResponsePB;
}  // namespace yb::cdc

namespace yb::master {

class CDCStreamInfo;
class CdcTablePolledTablesPB;

class CdcsdkManager : public CdcsdkManagerIf {
 public:
  explicit CdcsdkManager(
      Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog);

  ~CdcsdkManager() override;

  void RunBgTasks(const LeaderEpoch& epoch) override;

  // Adds the CDC table poller registry if the tserver has not applied the current version, and
  // takes the tserver's reports of split tablets.
  void FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp)
      EXCLUDES(cdc_table_registry_mutex_);

  // The registry, for the xCluster safe time service, which computes the CDC safe time of a
  // database over the tablets in it. Fails until the registry has been computed in the current
  // leader term: an older one, or none, must not pass for the tablets that are polled now.
  Result<cdc::CdcTablePollerRegistryPB> GetCdcTableRegistry() const
      EXCLUDES(cdc_table_registry_mutex_);

  // Called by the CDC service of the master with the changes of the sys catalog tablet that it is
  // about to return to a stream. For a stream whose changes are written to a CDC table, applies
  // the changes to the publications of the database to the tables that are polled, each as of its
  // own commit time and in commit order, and brings the registry in line.
  //
  // This has to be done before the response is sent. The poller of the sys catalog tablet moves
  // its safe time past the commit time of such a change only once it has the response, and the
  // CDC safe time of the database must not get past that commit time before the tablets of a table
  // that the change published are in the registry: until their own pollers have a safe time, they
  // are what holds the CDC safe time back.
  Status ProcessSysCatalogChanges(
      const xrepl::StreamId& stream_id, const cdc::GetChangesResponsePB& resp)
      EXCLUDES(cdc_table_mutex_, cdc_table_registry_mutex_);

  // A table that was dropped or rewritten, and a tablet that was split, are kept hidden for a
  // CDCSDK stream until the restart time of its slot is past the time they were hidden at. Nothing
  // moves that restart time for a stream whose changes are written to a CDC table. This is the
  // time that decides in its place: the CDC safe time of the database once the table is no longer
  // polled, and the minimum time until then. Returns nullopt for any other stream.
  //
  // Neither condition is enough by itself:
  // - While the table is polled, its pollers still read its tablets.
  // - A table that is not polled can be one that was published and dropped by changes to the
  //   catalog that the master has not been served yet, see ProcessSysCatalogChanges. The CDC safe
  //   time cannot get past the commit time of a change before the master has applied it. So once
  //   it is past the hide time, the table has been polled for every time that it was published,
  //   given that a table is only hidden after the changes that published it have committed.
  std::optional<HybridTime> GetTimeDoneWithHiddenTable(
      const CDCStreamInfo& stream, const TableId& table_id) const;

 private:
  // The position that the poller of a split tablet reported, by stream and tablet.
  using CdcTableSplitReports =
      std::map<std::pair<xrepl::StreamId, TabletId>, cdc::CdcTablePollerPositionPB>;

  // The tablets polled for a stream.
  struct CdcTablePolledTablets {
    struct Tablet {
      // The polled tables that the tablet holds. More than one for a colocated tablet.
      std::set<TableId> table_ids;
      // Where its poller starts, if not from the tablet's own row in cdc_state alone: see
      // CdcTableTabletStartPB.
      std::optional<cdc::CdcTablePollerPositionPB> start_position;
    };

    bool initialized = false;
    std::map<TabletId, Tablet> tablets;
    // The polled tables whose tablets have been added to 'tablets'.
    std::set<TableId> tables;
  };

  // Recomputes the registry of tablets that the CDC table pollers on the tservers poll.
  Status RefreshCdcTableRegistry(const LeaderEpoch& epoch)
      EXCLUDES(cdc_table_mutex_, cdc_table_registry_mutex_);
  Status DoRefreshCdcTableRegistry(const LeaderEpoch& epoch)
      REQUIRES(cdc_table_mutex_) EXCLUDES(cdc_table_registry_mutex_);

  // Brings the polled tablets of a stream up to date:
  // - adds the tablets of the tables that are polled now and were not before,
  // - removes the tablets of the tables that are no longer polled,
  // - replaces each tablet whose poller reported a split with both its children.
  Status UpdateCdcTablePolledTablets(
      const xrepl::StreamId& stream_id, const CdcTablePolledTablesPB& polled_tables,
      const CdcTableSplitReports& split_reports, CdcTablePolledTablets* polled);

  // The parts of a stream's registry entry that are computed once: everything but the tables and
  // their tablets. Returns nullopt for a stream whose changes are not written to a CDC table.
  Result<std::optional<cdc::CdcTableStreamEntryPB>> ComputeCdcTableStreamEntry(
      const CDCStreamInfo& stream);

  // The tables of the stream that some publication of its database had at the given time.
  Result<std::set<TableId>> ReadPublishedTables(const CDCStreamInfo& stream, HybridTime time);

  // The PG relation that the table stores, or nullopt if the table is gone. A relation that is
  // rewritten keeps its oid and gets another table.
  std::optional<PgOid> RelationOid(const TableId& table_id) const;

  // The split tablets that the tablet descends from and that still exist, nearest first. See
  // CdcTableTabletStartPB for what a poller of the tablet does with them.
  std::vector<TabletId> SplitAncestors(const TabletId& tablet_id) const;

  // Makes polled_tables what it has to be after a change to the publications, or to the tables
  // of the published relations, that committed at the given time. Returns whether it changed.
  Result<bool> UpdatePolledTablesAsOf(
      const CDCStreamInfo& stream, HybridTime time, CdcTablePolledTablesPB* polled_tables);

  // The polled tables of the stream. Computed as of the time the stream starts from, and
  // persisted, if the stream has none yet.
  Result<CdcTablePolledTablesPB> GetOrInitPolledTables(
      const scoped_refptr<CDCStreamInfo>& stream, const LeaderEpoch& epoch)
      REQUIRES(cdc_table_mutex_);

  // Removes the tables that no publication has any more, once the CDC table has their changes up
  // to the time they were last published. Returns whether polled_tables changed.
  bool RemoveStoppedTables(const CDCStreamInfo& stream, CdcTablePolledTablesPB* polled_tables);

  Status PersistPolledTables(
      const scoped_refptr<CDCStreamInfo>& stream, const CdcTablePolledTablesPB& polled_tables,
      const LeaderEpoch& epoch) REQUIRES(cdc_table_mutex_);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  // Held for a whole pass over the streams, by the CDC bg task and by the CDC service when it has
  // read a change to the publications. Sys catalog reads and writes are done under it, so it must
  // not be taken with CatalogManager::mutex_ or a catalog entity lock held.
  std::mutex cdc_table_mutex_;

  std::unordered_map<xrepl::StreamId, cdc::CdcTableStreamEntryPB> cdc_table_stream_entries_
      GUARDED_BY(cdc_table_mutex_);

  // Not persisted, and not derived from the tables' current tablets on every pass: a split tablet
  // stays polled until the CDC service tells its poller about the split, which is when its
  // children can take over. A new master leader starts from the current tablets of the polled
  // tables, which are persisted with the stream. See UpdateCdcTablePolledTablets.
  std::unordered_map<xrepl::StreamId, CdcTablePolledTablets> cdc_table_polled_tablets_
      GUARDED_BY(cdc_table_mutex_);
  int64_t cdc_table_polled_tablets_leader_term_ GUARDED_BY(cdc_table_mutex_) = -1;

  mutable std::mutex cdc_table_registry_mutex_ ACQUIRED_AFTER(cdc_table_mutex_);
  // Not persisted: a new master leader recomputes it. Its version is derived from the wall clock
  // so that it differs from the versions an earlier leader handed out.
  // TODO: send each tserver only the entries relevant to it instead of the full registry.
  cdc::CdcTablePollerRegistryPB cdc_table_registry_ GUARDED_BY(cdc_table_registry_mutex_);
  // The leader term in which cdc_table_registry_ was last computed.
  std::optional<int64_t> cdc_table_registry_leader_term_ GUARDED_BY(cdc_table_registry_mutex_);
  // From the heartbeats since the last pass of the CDC bg task. The tservers repeat their reports,
  // so nothing is lost by dropping the ones that a pass could not use.
  CdcTableSplitReports cdc_table_split_reports_ GUARDED_BY(cdc_table_registry_mutex_);
};

}  // namespace yb::master
