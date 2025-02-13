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

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/consistent_read_point.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/pg_client.fwd.h"

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
    (GetTableKeyRanges) \
    /**/

YB_STRONGLY_TYPED_BOOL(IsDDL);

struct PgClientSessionContext {
  const TserverXClusterContextIf* xcluster_context;
  YsqlAdvisoryLocksTable& advisory_locks_table;
  PgMutationCounter* pg_node_level_mutation_counter;
  const scoped_refptr<ClockBase>& clock;
  PgTableCache& table_cache;
  PgResponseCache& response_cache;
  PgSequenceCache& sequence_cache;
  PgSharedMemoryPool& shared_mem_pool;
  const EventStatsPtr& stats_exchange_response_size;
};

class PgClientSession final {
 private:
  using SharedThisSource = std::shared_ptr<void>;

 public:
  using TransactionBuilder = std::function<client::YBTransactionPtr(
      IsDDL, client::ForceGlobalTransaction, CoarseTimePoint, client::ForceCreateTransaction)>;

  PgClientSession(
      TransactionBuilder&& transaction_builder, SharedThisSource shared_this_source,
      client::YBClient& client, std::reference_wrapper<const PgClientSessionContext> context,
      uint64_t id, rpc::Scheduler& scheduler);
  ~PgClientSession();

  uint64_t id() const;

  Status Perform(PgPerformRequestPB* req, PgPerformResponsePB* resp, rpc::RpcContext* context);

  void ProcessSharedRequest(size_t size, SharedExchange* exchange);

  size_t SaveData(const RefCntBuffer& buffer, WriteBuffer&& sidecars);

  std::pair<uint64_t, std::byte*> ObtainBigSharedMemorySegment(size_t size);

  void StartShutdown();
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
        PG_CLIENT_SESSION_METHOD_DECLARE, (void, rpc::RpcContext), PG_CLIENT_SESSION_ASYNC_METHODS);

  #undef PG_CLIENT_SESSION_METHOD_DECLARE
  #undef PG_CLIENT_SESSION_METHOD_DECLARE_IMPL

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tserver
} // namespace yb
