//
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
//

#ifndef YB_TABLET_TRANSACTION_PARTICIPANT_H
#define YB_TABLET_TRANSACTION_PARTICIPANT_H

#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/result.h"

namespace rocksdb {

class WriteBatch;

}

namespace yb {

class HybridTime;
class TransactionMetadataPB;

namespace tablet {

typedef std::function<void(Result<tserver::TransactionStatus>)> RequestTransactionStatusCallback;

class TransactionIntentApplier;

struct TransactionApplyData {
  ProcessingMode mode;
  // Applier should be alive until ProcessApply returns.
  TransactionIntentApplier* applier;
  TransactionId transaction_id;
  consensus::OpId op_id;
  HybridTime hybrid_time;
  TabletId status_tablet;
};

// Interface to object that should apply intents in RocksDB when transaction is applying.
class TransactionIntentApplier {
 public:
  virtual CHECKED_STATUS ApplyIntents(const TransactionApplyData& data) = 0;

 protected:
  ~TransactionIntentApplier() {}
};

class TransactionParticipantContext {
 public:
  virtual const std::string& tablet_id() const = 0;
  virtual const client::YBClientPtr& client() const = 0;

 protected:
  ~TransactionParticipantContext() {}
};

// TransactionParticipant manages running transactions, i.e. transactions that have intents in
// appropriate tablet. Since this class manages transactions of tablet there is separate class
// instance per tablet.
class TransactionParticipant {
 public:
  explicit TransactionParticipant(TransactionParticipantContext* context);
  ~TransactionParticipant();

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch);

  bool CommittedLocally(const TransactionId& id);

  void RequestStatusAt(const TransactionId& id,
                       HybridTime time,
                       RequestTransactionStatusCallback callback);

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_PARTICIPANT_H
