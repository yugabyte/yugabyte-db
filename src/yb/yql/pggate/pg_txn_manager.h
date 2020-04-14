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

// No include guards here because this file is expected to be included multiple times.

#ifndef YB_YQL_PGGATE_PG_TXN_MANAGER_H_
#define YB_YQL_PGGATE_PG_TXN_MANAGER_H_

#include <mutex>

#include "yb/client/client_fwd.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/async_initializer.h"
#include "yb/common/clock.h"
#include "yb/gutil/ref_counted.h"
#include "yb/tserver/tserver_util_fwd.h"
#include "yb/util/result.h"

namespace yb {
namespace tserver {

class TabletServerServiceProxy;

} // namespace tserver

namespace pggate {

// These should match XACT_READ_UNCOMMITED, XACT_READ_COMMITED, XACT_REPEATABLE_READ,
// XACT_SERIALIZABLE from xact.h.
enum class PgIsolationLevel {
  READ_UNCOMMITED = 0,
  READ_COMMITED = 1,
  REPEATABLE_READ = 2,
  SERIALIZABLE = 3,
};

class PgTxnManager : public RefCountedThreadSafe<PgTxnManager> {
 public:
  PgTxnManager(client::AsyncClientInitialiser* async_client_init,
               scoped_refptr<ClockBase> clock,
               const tserver::TServerSharedObject* tserver_shared_object);

  virtual ~PgTxnManager();

  CHECKED_STATUS BeginTransaction();
  CHECKED_STATUS RestartTransaction();
  CHECKED_STATUS CommitTransaction();
  CHECKED_STATUS AbortTransaction();
  CHECKED_STATUS SetIsolationLevel(int isolation);
  CHECKED_STATUS SetReadOnly(bool read_only);
  CHECKED_STATUS SetDeferrable(bool deferrable);
  CHECKED_STATUS EnterSeparateDdlTxnMode();
  CHECKED_STATUS ExitSeparateDdlTxnMode(bool success);

  // Returns the transactional session, starting a new transaction if necessary.
  yb::Result<client::YBSession*> GetTransactionalSession();

  CHECKED_STATUS BeginWriteTransactionIfNecessary(bool read_only_op,
                                                  bool needs_pessimistic_locking = false);

  bool CanRestart() { return can_restart_.load(std::memory_order_acquire); }

  bool IsDdlMode() const { return ddl_session_.get() != nullptr; }

 private:

  client::TransactionManager* GetOrCreateTransactionManager();
  void ResetTxnAndSession();
  void StartNewSession();

  client::AsyncClientInitialiser* async_client_init_ = nullptr;
  scoped_refptr<ClockBase> clock_;
  const tserver::TServerSharedObject* const tserver_shared_object_;

  bool txn_in_progress_ = false;
  client::YBTransactionPtr txn_;
  client::YBSessionPtr session_;

  std::atomic<client::TransactionManager*> transaction_manager_{nullptr};
  std::mutex transaction_manager_mutex_;
  std::unique_ptr<client::TransactionManager> transaction_manager_holder_;

  // Postgres transaction characteristics.
  PgIsolationLevel isolation_level_ = PgIsolationLevel::REPEATABLE_READ;
  bool read_only_ = false;
  bool deferrable_ = false;

  client::YBTransactionPtr ddl_txn_;
  client::YBSessionPtr ddl_session_;

  std::atomic<bool> can_restart_{true};

  std::unique_ptr<tserver::TabletServerServiceProxy> tablet_server_proxy_;

  DISALLOW_COPY_AND_ASSIGN(PgTxnManager);
};

}  // namespace pggate
}  // namespace yb
#endif // YB_YQL_PGGATE_PG_TXN_MANAGER_H_
