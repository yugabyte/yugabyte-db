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
#include <string>
#include <unordered_map>

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
    (GetNewObjectId) \
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
    (GetActiveTransactionList) \
    /**/

class PgClientServiceImpl : public PgClientServiceIf {
 public:
  explicit PgClientServiceImpl(
      std::reference_wrapper<const TabletServerIf> tablet_server,
      const std::shared_future<client::YBClient*>& client_future,
      const scoped_refptr<ClockBase>& clock, TransactionPoolProvider transaction_pool_provider,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      const scoped_refptr<MetricEntity>& entity,
      rpc::Scheduler* scheduler,
      const TserverXClusterContextIf* xcluster_context = nullptr,
      PgMutationCounter* pg_node_level_mutation_counter = nullptr);

  ~PgClientServiceImpl();

  void Perform(
      const PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext context) override;

  void InvalidateTableCache();
  void CheckObjectIdAllocators(const std::unordered_set<uint32_t>& db_oids);

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

#define YB_PG_CLIENT_MOCKABLE_METHODS \
    (Perform) \
    YB_PG_CLIENT_METHODS \
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
          const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)*, \
          BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)*, rpc::RpcContext*)>& mock);

  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_METHOD_DECLARE, ~, YB_PG_CLIENT_MOCKABLE_METHODS);
  BOOST_PP_SEQ_FOR_EACH(YB_PG_CLIENT_MOCK_METHOD_SETTER_DECLARE, ~, YB_PG_CLIENT_MOCKABLE_METHODS);

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
