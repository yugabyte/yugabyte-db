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
#include <unordered_map>
#include <utility>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/xrepl_types.h"

#include "yb/common/entity_ids_types.h"

#include "yb/master/cdcsdk_manager_if.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.fwd.h"

#include "yb/util/status_fwd.h"

namespace yb::master {

class CDCStreamInfo;

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

 private:
  // The position that the poller of a split tablet reported, by stream and tablet.
  using CdcTableSplitReports =
      std::map<std::pair<xrepl::StreamId, TabletId>, cdc::CdcTablePollerPositionPB>;

  // The tablets polled for a stream. A tablet that replaced its split parent maps to the position
  // it starts from.
  struct CdcTablePolledTablets {
    bool initialized = false;
    std::map<TabletId, std::optional<cdc::CdcTablePollerPositionPB>> tablets;
  };

  // Recomputes the registry of tablets that the CDC table pollers on the tservers poll.
  Status RefreshCdcTableRegistry(const LeaderEpoch& epoch) EXCLUDES(cdc_table_registry_mutex_);

  // Brings the polled tablets of a stream up to date: replaces each tablet whose poller reported a
  // split with both its children.
  Status UpdateCdcTablePolledTablets(
      const xrepl::StreamId& stream_id, const cdc::CdcTableStreamEntryPB& entry,
      const CdcTableSplitReports& split_reports, CdcTablePolledTablets* polled);

  // The parts of a stream's registry entry that are computed once: everything but the tablets.
  // Returns nullopt for a stream whose changes are not written to a CDC table.
  Result<std::optional<cdc::CdcTableStreamEntryPB>> ComputeCdcTableStreamEntry(
      const CDCStreamInfo& stream);

  Master& master_;
  CatalogManager& catalog_manager_;
  SysCatalogTable& sys_catalog_;

  // Only accessed by the CDC bg task.
  // TODO: refresh the published tables of a stream when its publications change.
  std::unordered_map<xrepl::StreamId, cdc::CdcTableStreamEntryPB> cdc_table_stream_entries_;

  // Only accessed by the CDC bg task. Not persisted, and not derived from the tables' current
  // tablets on every pass: a split tablet stays polled until the CDC service tells its poller
  // about the split, which is when its children can take over. A new master leader starts from
  // the tables' current tablets, see UpdateCdcTablePolledTablets.
  std::unordered_map<xrepl::StreamId, CdcTablePolledTablets> cdc_table_polled_tablets_;
  int64_t cdc_table_polled_tablets_leader_term_ = -1;

  mutable std::mutex cdc_table_registry_mutex_;
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
