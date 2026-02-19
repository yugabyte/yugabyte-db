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

#include <sys/types.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/consistent_read_point.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/pg_client.fwd.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/lw_function.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class WriteBuffer;

namespace tserver {

#define PG_CLIENT_SESSION_METHODS \
    (AlterDatabase) \
    (AlterTable) \
    (BackfillIndex) \
    (CreateDatabase) \
    (CreateReplicationSlot) \
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
    (InsertSequenceTuple) \
    (ReadSequenceTuple) \
    (RollbackToSubTransaction) \
    (TruncateTable) \
    (UpdateSequenceTuple) \
    (WaitForBackendsCatalogVersion) \
    (AcquireAdvisoryLock) \
    (ReleaseAdvisoryLock) \
    /**/

// These methods may respond with Status::OK() and continue async processing (including network
// operations). In this case it is their responsibility to fill response and call
// context.RespondSuccess asynchronously.
// If such method responds with error Status, it will be handled by the upper layer that will fill
// response with error status and call context.RespondSuccess.
#define PG_CLIENT_SESSION_ASYNC_METHODS \
    (AcquireObjectLock) \
    (GetTableKeyRanges) \
    /**/

YB_STRONGLY_TYPED_BOOL(IsDDL);

struct PgClientSessionMetrics {
  explicit PgClientSessionMetrics(MetricEntity* metric_entity);

  EventStatsPtr exchange_response_size;
  EventStatsPtr vector_index_fetch_us;
  EventStatsPtr vector_index_collect_us;
  EventStatsPtr vector_index_reduce_us;
};

struct PgClientSessionContext {
  // xcluster_context is nullptr on master.
  const TserverXClusterContextIf* xcluster_context;
  YsqlAdvisoryLocksTable& advisory_locks_table;
  PgMutationCounter* pg_node_level_mutation_counter;
  const scoped_refptr<ClockBase>& clock;
  PgTableCache& table_cache;
  PgResponseCache& response_cache;
  PgSequenceCache& sequence_cache;
  PgSharedMemoryPool& shared_mem_pool;
  PgClientSessionMetrics metrics;
  const std::string& instance_uuid;
  docdb::ObjectLockOwnerRegistry* lock_owner_registry;
  const TransactionManagerProvider transaction_manager_provider;
};

using RequestProcessingPreconditionWaiter = LWFunction<Status(size_t, CoarseTimePoint)>;

class PgClientSession final {
 private:
  using SharedThisSource = std::shared_ptr<void>;

 public:
  using TransactionBuilder = std::function<client::YBTransactionPtr(
      IsDDL, TransactionFullLocality, CoarseTimePoint, client::ForceCreateTransaction)>;

  PgClientSession(
      TransactionBuilder&& transaction_builder, SharedThisSource shared_this_source,
      client::YBClient& client, std::reference_wrapper<const PgClientSessionContext> context,
      uint64_t id, pid_t pid, uint64_t lease_epoch,
      tserver::TSLocalLockManagerPtr ts_local_lock_manager, rpc::Scheduler& scheduler);
  ~PgClientSession();

  uint64_t id() const;

  void SetupSharedObjectLocking(PgSessionLockOwnerTagShared& object_lock_shared);

  void Perform(
      PgPerformRequestPB& req, PgPerformResponsePB& resp, rpc::RpcContext&& context,
      const PgTablesQueryResult& tables);

  void ProcessSharedRequest(
      size_t size, SharedExchange* exchange,
      const RequestProcessingPreconditionWaiter& precondition_waiter);

  size_t SaveData(const RefCntBuffer& buffer, WriteBuffer&& sidecars);

  std::pair<uint64_t, std::byte*> ObtainBigSharedMemorySegment(size_t size);

  void StartShutdown(bool pg_service_shutting_down);
  bool ReadyToShutdown() const;
  void CompleteShutdown();

  Result<ReadHybridTime> GetTxnSnapshotReadTime(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline);

  Status SetTxnSnapshotReadTime(const PgPerformOptionsPB& options, CoarseTimePoint deadline);

  #define PG_CLIENT_SESSION_METHOD_DECLARE_IMPL(ret, ctx_type, method) \
    ret method( \
        const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
        BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
        ctx_type context);

  #define PG_CLIENT_SESSION_METHOD_DECLARE(r, data_tuple, method) \
    PG_CLIENT_SESSION_METHOD_DECLARE_IMPL( \
        BOOST_PP_TUPLE_ELEM(2, 0, data_tuple), BOOST_PP_TUPLE_ELEM(2, 1, data_tuple), method)

  BOOST_PP_SEQ_FOR_EACH(
        PG_CLIENT_SESSION_METHOD_DECLARE, (Status, rpc::RpcContext*), PG_CLIENT_SESSION_METHODS);
  BOOST_PP_SEQ_FOR_EACH(
        PG_CLIENT_SESSION_METHOD_DECLARE,
        (void, rpc::RpcContext&&), PG_CLIENT_SESSION_ASYNC_METHODS);

  #undef PG_CLIENT_SESSION_METHOD_DECLARE
  #undef PG_CLIENT_SESSION_METHOD_DECLARE_IMPL

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

void PreparePgTablesQuery(
    const PgPerformRequestMsg& req, boost::container::small_vector_base<TableId>& table_ids);

} // namespace tserver
} // namespace yb
