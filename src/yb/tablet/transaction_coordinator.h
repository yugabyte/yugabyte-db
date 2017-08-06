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

// Context for transaction coordinator.
class TransactionCoordinatorContext {
 public:
  virtual CHECKED_STATUS ApplyIntents(const TransactionId& id,
                                      const consensus::OpId& op_id,
                                      HybridTime hybrid_time) = 0;
  virtual const std::string& tablet_id() const = 0;
  virtual const client::YBClientPtr& client() const = 0;
  virtual const scoped_refptr<server::Clock>& clock() const = 0;
 protected:
  ~TransactionCoordinatorContext() {}
};

enum class ProcessMode {
  NON_LEADER,
  LEADER,
};

// Coordinates all transactions managed by specific tablet, i.e. all transactions
// that selected this tablet as status tablet for it.
class TransactionCoordinator {
 public:
  explicit TransactionCoordinator(TransactionCoordinatorContext* context);
  ~TransactionCoordinator();

  // Process new transaction state.
  CHECKED_STATUS Process(ProcessMode mode,
                         const tserver::TransactionStatePB& state,
                         const consensus::OpId& op_id,
                         HybridTime hybrid_time);

  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch);

  size_t test_count_transactions() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_COORDINATOR_H
