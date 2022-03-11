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

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/range/iterator_range.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/transaction.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/pg_client.pb.h"

namespace yb {
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
    (RollbackSubTransaction) \
    (SetActiveSubTransaction) \
    (TruncateTable) \
    (UpdateSequenceTuple) \
    /**/

using PgClientSessionOperations = std::vector<std::shared_ptr<client::YBPgsqlOp>>;
class PgClientSessionLocker;

YB_DEFINE_ENUM(PgClientSessionKind, (kPlain)(kDdl)(kCatalog)(kSequence));

class PgClientSession {
 public:
  PgClientSession(
      client::YBClient* client, const scoped_refptr<ClockBase>& clock,
      std::reference_wrapper<const TransactionPoolProvider> transaction_pool_provider,
      PgTableCache* table_cache, uint64_t id);

  uint64_t id() const;

  CHECKED_STATUS Perform(
      const PgPerformRequestPB& req, PgPerformResponsePB* resp, rpc::RpcContext* context);

  #define PG_CLIENT_SESSION_METHOD_DECLARE(r, data, method) \
  CHECKED_STATUS method( \
      const BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), RequestPB)& req, \
      BOOST_PP_CAT(BOOST_PP_CAT(Pg, method), ResponsePB)* resp, \
      rpc::RpcContext* context);

  BOOST_PP_SEQ_FOR_EACH(PG_CLIENT_SESSION_METHOD_DECLARE, ~, PG_CLIENT_SESSION_METHODS);

 private:
  friend class PgClientSessionLocker;

  std::string LogPrefix();

  Result<const TransactionMetadata*> GetDdlTransactionMetadata(bool use_transaction);
  CHECKED_STATUS BeginTransactionIfNecessary(const PgPerformOptionsPB& options);
  Result<client::YBTransactionPtr> RestartTransaction(
      client::YBSession* session, client::YBTransaction* transaction);

  Result<client::YBSession*> SetupSession(const PgPerformRequestPB& req, CoarseTimePoint deadline);
  CHECKED_STATUS ProcessResponse(
      const PgClientSessionOperations& operations, const PgPerformRequestPB& req,
      PgPerformResponsePB* resp, rpc::RpcContext* context);
  void ProcessReadTimeManipulation(ReadTimeManipulation manipulation);

  client::YBClient& client();
  client::YBSessionPtr& EnsureSession(PgClientSessionKind kind);
  client::YBSessionPtr& Session(PgClientSessionKind kind);
  client::YBTransactionPtr& Transaction(PgClientSessionKind kind);

  client::YBClient& client_;
  scoped_refptr<ClockBase> clock_;
  const TransactionPoolProvider& transaction_pool_provider_;
  PgTableCache& table_cache_;
  const uint64_t id_;

  std::mutex mutex_;
  struct SessionData {
    client::YBSessionPtr session;
    client::YBTransactionPtr transaction;
  };
  std::array<SessionData, kPgClientSessionKindMapSize> sessions_;
  uint64_t txn_serial_no_ = 0;
  boost::optional<uint64_t> saved_priority_;
  TransactionMetadata ddl_txn_metadata_;
};

class PgClientSessionLocker {
 public:
  explicit PgClientSessionLocker(PgClientSession* session)
      : session_(session), lock_(session->mutex_) {}

  PgClientSession* operator->() const {
    return session_;
  }

  void Unlock() {
    lock_.unlock();
  }

 private:
  PgClientSession* session_;
  std::unique_lock<std::mutex> lock_;
};

}  // namespace tserver
}  // namespace yb

#endif  // YB_TSERVER_PG_CLIENT_SESSION_H
