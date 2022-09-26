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

#ifndef YB_TSERVER_PG_CLIENT_SESSION_H
#define YB_TSERVER_PG_CLIENT_SESSION_H

#include <stdint.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/optional.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/range/iterator_range.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/xcluster_safe_time_map.h"

#include "yb/util/locks.h"

namespace yb {
class ConsistentReadPoint;

namespace tserver {

#define PG_CLIENT_SESSION_METHODS \
    (AlterDatabase) \
    (AlterTable) \
    (BackfillIndex) \
    (CreateDatabase) \
    (CreateTable) \
    (CreateTablegroup) \
    (DeleteDBSequences) \
    (DeleteSequenceTuple) \
    (DropDatabase) \
    (DropTable) \
    (DropTablegroup) \
    (FinishTransaction) \
    (InsertSequenceTuple) \
    (ReadSequenceTuple) \
    (RollbackToSubTransaction) \
    (SetActiveSubTransaction) \
    (TruncateTable) \
    (UpdateSequenceTuple) \
    /**/

using PgClientSessionOperations = std::vector<std::shared_ptr<client::YBPgsqlOp>>;

YB_DEFINE_ENUM(PgClientSessionKind, (kPlain)(kDdl)(kCatalog)(kSequence));

class PgClientSession : public std::enable_shared_from_this<PgClientSession> {
 public:
  struct UsedReadTime {
    simple_spinlock lock;
    ReadHybridTime value;
  };

  using UsedReadTimePtr = std::weak_ptr<UsedReadTime>;

  PgClientSession(
      client::YBClient* client, const scoped_refptr<ClockBase>& clock,
      std::reference_wrapper<const TransactionPoolProvider> transaction_pool_provider,
      PgTableCache* table_cache, uint64_t id,
      const std::shared_ptr<XClusterSafeTimeMap>& xcluster_safe_time_map);

  uint64_t id() const;

  Status Perform(
      const PgPerformRequestPB& req, PgPerformResponsePB* resp, rpc::RpcContext* context);

  #define PG_CLIENT_SESSION_METHOD_DECLARE(r, data, method) \
  Status method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext* context);

  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_METHOD_DECLARE, ~, PG_CLIENT_SESSION_METHODS);

 private:
  std::string LogPrefix();

  Result<const TransactionMetadata*> GetDdlTransactionMetadata(
      bool use_transaction, CoarseTimePoint deadline);
  Status BeginTransactionIfNecessary(
      const PgPerformOptionsPB& options, CoarseTimePoint deadline);
  Result<client::YBTransactionPtr> RestartTransaction(
      client::YBSession* session, client::YBTransaction* transaction);

  Result<std::pair<client::YBSession*, UsedReadTimePtr>> SetupSession(
      const PgPerformRequestPB& req, CoarseTimePoint deadline);
  Status ProcessResponse(
      const PgClientSessionOperations& operations, const PgPerformRequestPB& req,
      PgPerformResponsePB* resp, rpc::RpcContext* context);
  void ProcessReadTimeManipulation(ReadTimeManipulation manipulation);

  client::YBClient& client();
  client::YBSessionPtr& EnsureSession(PgClientSessionKind kind);
  client::YBSessionPtr& Session(PgClientSessionKind kind);
  client::YBTransactionPtr& Transaction(PgClientSessionKind kind);
  Status CheckPlainSessionReadTime();

  // Set the read point to the databases xCluster safe time if consistent reads are enabled
  Status UpdateReadPointForXClusterConsistentReads(
      const PgPerformOptionsPB& options, ConsistentReadPoint* read_point);

  struct SessionData {
    client::YBSessionPtr session;
    client::YBTransactionPtr transaction;
  };

  client::YBClient& client_;
  scoped_refptr<ClockBase> clock_;
  const TransactionPoolProvider& transaction_pool_provider_;
  PgTableCache& table_cache_;
  const uint64_t id_;
  const std::shared_ptr<XClusterSafeTimeMap> xcluster_safe_time_map_;

  std::array<SessionData, kPgClientSessionKindMapSize> sessions_;
  uint64_t txn_serial_no_ = 0;
  boost::optional<uint64_t> saved_priority_;
  TransactionMetadata ddl_txn_metadata_;
  UsedReadTime plain_session_used_read_time_;
};

}  // namespace tserver
}  // namespace yb

#endif  // YB_TSERVER_PG_CLIENT_SESSION_H
