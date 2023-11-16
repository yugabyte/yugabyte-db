// Copyright (c) YugaByte, Inc.
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

#include <functional>
#include <future>
#include <memory>
#include <optional>

#include "yb/client/client_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/pg_client.service.h"
#include "yb/tserver/xcluster_context.h"

namespace yb {

class MemTracker;

namespace tserver {

class PgMutationCounter;

#define YB_PG_CLIENT_METHODS \
    (AlterDatabase) \
    (AlterTable) \
    (BackfillIndex) \
    (CreateDatabase) \
    (CreateSequencesDataTable) \
    (CreateTable) \
    (CreateTablegroup) \
    (DeleteDBSequences) \
    (DeleteSequenceTuple) \
    (DropDatabase) \
    (DropTable) \
    (DropTablegroup) \
    (FetchSequenceTuple) \
    (FinishTransaction) \
    (GetCatalogMasterVersion) \
    (GetDatabaseInfo) \
    (GetIndexBackfillProgress) \
    (GetLockStatus) \
    (GetTableDiskSize) \
    (GetTablePartitionList) \
    (Heartbeat) \
    (InsertSequenceTuple) \
    (IsInitDbDone) \
    (IsObjectPartOfXRepl) \
    (ListLiveTabletServers) \
    (OpenTable) \
    (ReadSequenceTuple) \
    (ReserveOids) \
    (RollbackToSubTransaction) \
    (SetActiveSubTransaction) \
    (TabletServerCount) \
    (TruncateTable) \
    (UpdateSequenceTuple) \
    (ValidatePlacement) \
    (CheckIfPitrActive) \
    (GetTserverCatalogVersionInfo) \
    (WaitForBackendsCatalogVersion) \
    (CancelTransaction) \
    (ActiveUniverseHistory) \
    (TableIDMetadata) \
    (TabletIDMetadata) \
    (GetTServerUUID) \
    (GetActiveTransactionList) \
    (YCQLStatStatements) \
    /**/

class PgClientServiceImpl : public PgClientServiceIf {
 public:
  explicit PgClientServiceImpl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock,
      TransactionPoolProvider transaction_pool_provider,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      const scoped_refptr<MetricEntity>& entity,
      rpc::Scheduler* scheduler,
      const std::optional<XClusterContext>& xcluster_context = std::nullopt,
      PgMutationCounter* pg_node_level_mutation_counter = nullptr);

  ~PgClientServiceImpl();

  void Perform(
      const PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext context) override;

  void InvalidateTableCache();

  size_t TEST_SessionsCount();

#define YB_PG_CLIENT_METHOD_DECLARE(r, data, method) \
  void method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext context) override;

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, ~, YB_PG_CLIENT_METHODS);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace tserver
}  // namespace yb
