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
#include "yb/client/transaction.h"
#include "yb/common/clock.h"
#include "yb/common/transaction.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/enums.h"

#include "yb/yql/pggate/pg_gate_fwd.h"
#include "yb/yql/pggate/pg_callbacks.h"

namespace yb {
namespace pggate {

// These should match XACT_READ_UNCOMMITED, XACT_READ_COMMITED, XACT_REPEATABLE_READ,
// XACT_SERIALIZABLE from xact.h. Please do not change this enum.
YB_DEFINE_ENUM(
  PgIsolationLevel,
  ((READ_UNCOMMITED, 0))
  ((READ_COMMITTED, 1))
  ((REPEATABLE_READ, 2))
  ((SERIALIZABLE, 3))
);

std::shared_ptr<yb::client::YBSession> BuildSession(
    yb::client::YBClient* client,
    const scoped_refptr<ClockBase>& clock = nullptr);

class PgTxnManager : public RefCountedThreadSafe<PgTxnManager> {
 public:
  PgTxnManager(PgClient* pg_client,
               scoped_refptr<ClockBase> clock,
               const tserver::TServerSharedObject* tserver_shared_object,
               PgCallbacks pg_callbacks);

  virtual ~PgTxnManager();

  CHECKED_STATUS BeginTransaction();
  CHECKED_STATUS CalculateIsolation(
      bool read_only_op, TxnPriorityRequirement txn_priority_requirement);
  CHECKED_STATUS RecreateTransaction();
  CHECKED_STATUS RestartTransaction();
  CHECKED_STATUS ResetTransactionReadPoint();
  CHECKED_STATUS RestartReadPoint();
  CHECKED_STATUS CommitTransaction();
  CHECKED_STATUS AbortTransaction();
  CHECKED_STATUS SetPgIsolationLevel(int isolation);
  PgIsolationLevel GetPgIsolationLevel();
  CHECKED_STATUS SetReadOnly(bool read_only);
  CHECKED_STATUS EnableFollowerReads(bool enable_follower_reads, int32_t staleness);
  CHECKED_STATUS SetDeferrable(bool deferrable);
  CHECKED_STATUS EnterSeparateDdlTxnMode();
  CHECKED_STATUS ExitSeparateDdlTxnMode(Commit commit);

  bool IsDdlMode() const { return ddl_mode_; }
  bool IsTxnInProgress() const { return txn_in_progress_; }
  IsolationLevel GetIsolationLevel() const { return isolation_level_; }
  bool ShouldUseFollowerReads() const { return read_time_for_follower_reads_.is_valid(); }

  void SetupPerformOptions(tserver::PgPerformOptionsPB* options);

 private:
  YB_STRONGLY_TYPED_BOOL(NeedsHigherPriorityTxn);
  YB_STRONGLY_TYPED_BOOL(SavePriority);

  void ResetTxnAndSession();
  void StartNewSession();
  Status UpdateReadTimeForFollowerReadsIfRequired();
  Status RecreateTransaction(SavePriority save_priority);

  static uint64_t NewPriority(TxnPriorityRequirement txn_priority_requirement);

  std::string TxnStateDebugStr() const;

  CHECKED_STATUS FinishTransaction(Commit commit);

  // ----------------------------------------------------------------------------------------------

  PgClient* client_;
  scoped_refptr<ClockBase> clock_;
  const tserver::TServerSharedObject* const tserver_shared_object_;

  bool txn_in_progress_ = false;
  IsolationLevel isolation_level_ = IsolationLevel::NON_TRANSACTIONAL;
  uint64_t txn_serial_no_ = 0;
  bool need_restart_ = false;
  bool need_defer_read_point_ = false;
  tserver::ReadTimeManipulation read_time_manipulation_ = tserver::ReadTimeManipulation::NONE;

  // Postgres transaction characteristics.
  PgIsolationLevel pg_isolation_level_ = PgIsolationLevel::REPEATABLE_READ;
  bool read_only_ = false;
  bool enable_follower_reads_ = false;
  uint64_t follower_read_staleness_ms_ = 0;
  HybridTime read_time_for_follower_reads_;
  bool deferrable_ = false;

  bool ddl_mode_ = false;

  // On a transaction conflict error we want to recreate the transaction with the same priority as
  // the last transaction. This avoids the case where the current transaction gets a higher priority
  // and cancels the other transaction.
  uint64_t priority_ = 0;
  SavePriority use_saved_priority_ = SavePriority::kFalse;

  std::unique_ptr<tserver::TabletServerServiceProxy> tablet_server_proxy_;

  PgCallbacks pg_callbacks_;

  DISALLOW_COPY_AND_ASSIGN(PgTxnManager);
};

}  // namespace pggate
}  // namespace yb
#endif // YB_YQL_PGGATE_PG_TXN_MANAGER_H_
