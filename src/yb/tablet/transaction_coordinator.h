//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_TABLET_TRANSACTION_COORDINATOR_H
#define YB_TABLET_TRANSACTION_COORDINATOR_H

#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"

namespace rocksdb {

class WriteBatch;

}

namespace yb {

class TransactionMetadataPB;

namespace server {

class Clock;

}

namespace tserver {

class TransactionStatePB;

}

namespace tablet {

class UpdateTxnTransactionState;

// Interface to object that should apply intents in RocksDB when transaction is applying.
class TransactionIntentApplier {
 public:
  virtual CHECKED_STATUS ApplyIntents(const TransactionId& id,
                                      const consensus::OpId& op_id,
                                      HybridTime hybrid_time) = 0;
 protected:
  ~TransactionIntentApplier() {}
};

// Context for transaction coordinator. I.e. access to external facilities required by
// transaction coordinator to do its job.
class TransactionCoordinatorContext {
 public:
  virtual const std::string& tablet_id() const = 0;
  virtual const client::YBClientPtr& client() const = 0;
  virtual server::Clock& clock() const = 0;

  virtual std::unique_ptr<UpdateTxnTransactionState> CreateUpdateTransactionState(
      tserver::TransactionStatePB* request) = 0;
  virtual void SubmitUpdateTransaction(std::unique_ptr<UpdateTxnTransactionState> state) = 0;

 protected:
  ~TransactionCoordinatorContext() {}
};

// Processing mode:
//   LEADER - processing in leader.
//   NON_LEADER - processing in non leader.
YB_DEFINE_ENUM(ProcessingMode, (NON_LEADER)(LEADER));

// Coordinates all transactions managed by specific tablet, i.e. all transactions
// that selected this tablet as status tablet for it.
// Also it handles running transactions, i.e. transactions that has intents in appropriate tablet.
// Each tablet has separate transaction coordinator.
class TransactionCoordinator {
 public:
  explicit TransactionCoordinator(TransactionCoordinatorContext* context);
  ~TransactionCoordinator();

  // Used to pass arguments to ProcessReplicated.
  struct ReplicatedData {
    ProcessingMode mode;
    TransactionIntentApplier* applier;
    const tserver::TransactionStatePB& state;
    const consensus::OpId& op_id;
    HybridTime hybrid_time;
  };

  // Process new transaction state.
  CHECKED_STATUS ProcessReplicated(const ReplicatedData& data);

  // Clears locks for transaction updates. Used when leader changes.
  void ClearLocks();

  // Handles new request for transaction udpate.
  void Handle(std::unique_ptr<tablet::UpdateTxnTransactionState> request);

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch);

  // Prepares log garbage collection. Return min index that should be preserved.
  int64_t PrepareGC(ProcessingMode mode);

  // Returns count of managed transactions. Used in tests.
  size_t test_count_transactions() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_COORDINATOR_H
