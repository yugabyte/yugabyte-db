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

#include <mutex>
#include <optional>
#include <unordered_map>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/xrepl_types.h"

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

  // Adds the CDC table poller registry if the tserver has not applied the current version.
  void FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp)
      EXCLUDES(cdc_table_registry_mutex_);

 private:
  // Recomputes the registry of tablets that the CDC table pollers on the tservers poll.
  Status RefreshCdcTableRegistry() EXCLUDES(cdc_table_registry_mutex_);

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

  std::mutex cdc_table_registry_mutex_;
  // Not persisted: a new master leader recomputes it. Its version is derived from the wall clock
  // so that it differs from the versions an earlier leader handed out.
  // TODO: send each tserver only the entries relevant to it instead of the full registry.
  cdc::CdcTablePollerRegistryPB cdc_table_registry_ GUARDED_BY(cdc_table_registry_mutex_);
};

}  // namespace yb::master
