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

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>

#include "yb/client/client_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/server/server_base_options.h"

#include "yb/tserver/pg_client.service.h"
#include "yb/tserver/pg_txn_snapshot_manager.h"

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
    (DropDatabase) \
    (DropReplicationSlot) \
    (DropTable) \
    (DropTablegroup) \
    (FetchData) \
    (FinishTransaction) \
    (GetActiveTransactionList) \
    (GetCatalogMasterVersion) \
    (GetDatabaseInfo) \
    (GetIndexBackfillProgress) \
    (GetLockStatus) \
    (GetReplicationSlot) \
    (GetTableDiskSize) \
    (GetTablePartitionList) \
    (GetTserverCatalogMessageLists) \
    (SetTserverCatalogMessageList) \
    (GetTserverCatalogVersionInfo) \
    (GetXClusterRole) \
    (Heartbeat) \
    (IsInitDbDone) \
    (IsObjectPartOfXRepl) \
    (ListClones) \
    (QueryAutoAnalyze) \
    (ListLiveTabletServers) \
    (ListSlotEntries) \
    (ListReplicationSlots) \
    (RemoteExec) \
    (ReserveOids) \
    (GetNewObjectId) \
    (RollbackToSubTransaction) \
    (ServersMetrics) \
    (TabletsMetadata) \
    (GetTabletForKey) \
    (TabletServerCount) \
    (TruncateTable) \
    (ValidatePlacement) \
    (WaitForBackendsCatalogVersion) \
    (YCQLStatementStats) \
    (CronSetLastMinute) \
    (CronGetLastMinute) \
    (AcquireAdvisoryLock) \
    (ReleaseAdvisoryLock) \
    (ExportTxnSnapshot) \
    (ImportTxnSnapshot) \
    (ClearExportedTxnSnapshots) \
    (GetYbSystemTableInfo) \
    (ReleaseSessionObjectLock) \
    /**/

#define YB_PG_CLIENT_LW_METHODS \
    (DeleteDBSequences) \
    (DeleteSequenceTuple) \
    (FetchSequenceTuple) \
    (InsertSequenceTuple) \
    (ReadSequenceTuple) \
    (UpdateSequenceTuple) \
    /**/

#define YB_PG_CLIENT_TRIVIAL_METHODS \
    (PollVectorIndexReady) \
    /**/

#define YB_PG_CLIENT_METHOD_ARG(prefix, method, type) \
    BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(\
        BOOST_PP_APPLY(prefix), Pg), method), type), PB)

// Forwards call to corresponding PgClientSession async method (see
// PG_CLIENT_SESSION_ASYNC_METHODS).
#define YB_PG_CLIENT_ASYNC_METHODS \
    (OpenTable) \
    (TriggerRelcacheInitConnection) \
    /**/

#define YB_PG_CLIENT_ASYNC_LW_METHODS \
    (AcquireObjectLock) \
    (GetTableKeyRanges) \
    /**/

class PgClientServiceImpl : public PgClientServiceIf {
 public:
  explicit PgClientServiceImpl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock,
      TransactionManagerProvider transaction_manager_provider,
      TransactionPoolProvider transaction_pool_provider,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      const scoped_refptr<MetricEntity>& entity, rpc::Messenger* messenger,
      const std::string& permanent_uuid, const server::ServerBaseOptions& tablet_server_opts,
      const TserverXClusterContextIf* xcluster_context = nullptr,
      PgMutationCounter* pg_node_level_mutation_counter = nullptr);

  ~PgClientServiceImpl();

  void Perform(
      const PgPerformRequestMsg* req, PgPerformResponseMsg* resp,
      rpc::RpcContext context) override;

  void InvalidateTableCache();
  void InvalidateTableCache(const std::unordered_map<uint32_t, uint64_t>& db_oids_updated,
                            const std::unordered_set<uint32_t>& db_oids_deleted);
  Result<PgTxnSnapshot> GetLocalPgTxnSnapshot(const PgTxnSnapshotLocalId& snapshot_id);

  size_t TEST_SessionsCount();

  void Shutdown() override;

#define YB_PG_CLIENT_METHOD_DECLARE(r, data, method) \
  void method( \
      const YB_PG_CLIENT_METHOD_ARG(data, method, Request)* req, \
      YB_PG_CLIENT_METHOD_ARG(data, method, Response)* resp, \
      rpc::RpcContext context) override;

#define YB_PG_CLIENT_TRIVIAL_METHOD_DECLARE(r, data, method) \
  Result<YB_PG_CLIENT_METHOD_ARG(data, method, Response)> method( \
      const YB_PG_CLIENT_METHOD_ARG(data, method, Request)& req, \
      CoarseTimePoint deadline) override;

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, BOOST_PP_NIL, YB_PG_CLIENT_METHODS);
  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, BOOST_PP_NIL, YB_PG_CLIENT_ASYNC_METHODS);
  BOOST_PP_SEQ_FOR_EACH(
      YB_PG_CLIENT_METHOD_DECLARE, (LW), YB_PG_CLIENT_LW_METHODS);
  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, (LW), YB_PG_CLIENT_ASYNC_LW_METHODS);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_TRIVIAL_METHOD_DECLARE, ~, YB_PG_CLIENT_TRIVIAL_METHODS);

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

#define YB_PG_CLIENT_MOCKABLE_METHODS \
    YB_PG_CLIENT_METHODS \
    YB_PG_CLIENT_ASYNC_METHODS \
    /**/

#define YB_PG_CLIENT_MOCKABLE_LW_METHODS \
    (Perform) \
    YB_PG_CLIENT_LW_METHODS \
    YB_PG_CLIENT_ASYNC_LW_METHODS \
    /**/

// PgClientServiceMockImpl implements the PgClientService interface to allow for mocking of tserver
// responses in MiniCluster tests. This implementation defaults to forwarding calls to
// PgClientServiceImpl if a suitable mock is not available. Usage of this implementation can be
// toggled via the test tserver gflag 'FLAGS_TEST_enable_pg_client_mock'.
class PgClientServiceMockImpl : public PgClientServiceIf {
 public:
  using Functor = std::function<Status(const void*, void*, rpc::RpcContext*)>;
  using SharedFunctor = std::shared_ptr<Functor>;

  PgClientServiceMockImpl(const scoped_refptr<MetricEntity>& entity, PgClientServiceIf* impl);

  class Handle {
    explicit Handle(SharedFunctor&& mock) : mock_(std::move(mock)) {}
    SharedFunctor mock_;

    friend class PgClientServiceMockImpl;
  };

#define YB_PG_CLIENT_MOCK_METHOD_SETTER_DECLARE(r, data, method) \
  [[nodiscard]] Handle BOOST_PP_CAT(Mock, method)( \
      const std::function<Status( \
          const YB_PG_CLIENT_METHOD_ARG(data, method, Request)*, \
          YB_PG_CLIENT_METHOD_ARG(data, method, Response)*, rpc::RpcContext*)>& mock);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, BOOST_PP_NIL, YB_PG_CLIENT_MOCKABLE_METHODS);
  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, (LW), YB_PG_CLIENT_MOCKABLE_LW_METHODS);
  BOOST_PP_SEQ_FOR_EACH(
      YB_PG_CLIENT_MOCK_METHOD_SETTER_DECLARE, BOOST_PP_NIL, YB_PG_CLIENT_MOCKABLE_METHODS);
  BOOST_PP_SEQ_FOR_EACH(
      YB_PG_CLIENT_MOCK_METHOD_SETTER_DECLARE, (LW), YB_PG_CLIENT_MOCKABLE_LW_METHODS);

  Result<PgPollVectorIndexReadyResponsePB> PollVectorIndexReady(
      const PgPollVectorIndexReadyRequestPB& req, CoarseTimePoint deadline) override {
    return STATUS(NotSupported, "Mocking PollVectorIndexReady is not supported");
  }

  void UnsetMock(const std::string& method);

 private:
  PgClientServiceIf* impl_;
  std::unordered_map<std::string, SharedFunctor::weak_type> mocks_;
  rw_spinlock mutex_;

  Result<bool> DispatchMock(
      const std::string& method, const void* req, void* resp, rpc::RpcContext* context);
  Handle SetMock(const std::string& method, SharedFunctor&& mock);
};

}  // namespace tserver
}  // namespace yb
