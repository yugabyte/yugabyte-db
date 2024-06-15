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

#include "yb/server/server_base_options.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/pg_client.service.h"

namespace yb {

class MemTracker;

namespace tserver {

class PgMutationCounter;
class TserverXClusterContextIf;

// Forwards call to corresponding PgClientSession sync method (see PG_CLIENT_SESSION_METHODS).
#define YB_PG_CLIENT_METHODS \
    (ActiveSessionHistory) \
    (AlterDatabase) \
    (AlterTable) \
    (BackfillIndex) \
    (CancelTransaction) \
    (CheckIfPitrActive) \
    (CreateDatabase) \
    (CreateReplicationSlot) \
    (CreateSequencesDataTable) \
    (CreateTable) \
    (CreateTablegroup) \
    (DeleteDBSequences) \
    (DeleteSequenceTuple) \
    (DropDatabase) \
    (DropReplicationSlot) \
    (DropTable) \
    (DropTablegroup) \
    (FetchData) \
    (FetchSequenceTuple) \
    (FinishTransaction) \
    (GetActiveTransactionList) \
    (GetCatalogMasterVersion) \
    (GetDatabaseInfo) \
    (GetIndexBackfillProgress) \
    (GetLockStatus) \
    (GetReplicationSlot) \
    (GetReplicationSlotStatus) \
    (GetTableDiskSize) \
    (GetTablePartitionList) \
    (GetTserverCatalogVersionInfo) \
    (Heartbeat) \
    (InsertSequenceTuple) \
    (IsInitDbDone) \
    (IsObjectPartOfXRepl) \
    (ListLiveTabletServers) \
    (ListReplicationSlots) \
    (OpenTable) \
    (ReadSequenceTuple) \
    (ReserveOids) \
    (GetNewObjectId) \
    (RollbackToSubTransaction) \
    (SetActiveSubTransaction) \
    (TabletsMetadata) \
    (TabletServerCount) \
    (TruncateTable) \
    (UpdateSequenceTuple) \
    (ValidatePlacement) \
    (WaitForBackendsCatalogVersion) \
    (YCQLStatementStats) \
    /**/

// Forwards call to corresponding PgClientSession async method (see
// PG_CLIENT_SESSION_ASYNC_METHODS).
#define YB_PG_CLIENT_ASYNC_METHODS \
    (GetTableKeyRanges) \
    /**/

class PgClientServiceImpl : public PgClientServiceIf {
 public:
  explicit PgClientServiceImpl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock, TransactionPoolProvider transaction_pool_provider,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
      const std::string& permanent_uuid, const server::ServerBaseOptions* tablet_server_opts,
      const TserverXClusterContextIf* xcluster_context = nullptr,
      PgMutationCounter* pg_node_level_mutation_counter = nullptr);

  ~PgClientServiceImpl();

  void Perform(
      const PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext context) override;

  void InvalidateTableCache();
  void InvalidateTableCache(const std::unordered_set<uint32_t>& db_oids_updated,
                            const std::unordered_set<uint32_t>& db_oids_deleted);

  size_t TEST_SessionsCount();

#define YB_PG_CLIENT_METHOD_DECLARE(r, data, method) \
  void method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)* req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext context) override;

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, ~, YB_PG_CLIENT_METHODS);
  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, ~, YB_PG_CLIENT_ASYNC_METHODS);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace tserver
}  // namespace yb
