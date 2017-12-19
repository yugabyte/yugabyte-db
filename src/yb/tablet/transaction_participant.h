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

#include <future>
#include <memory>

#include <boost/optional/optional.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/consensus/opid_util.h"

#include "yb/util/opid.pb.h"
#include "yb/util/result.h"

namespace rocksdb {

class DB;
class WriteBatch;

}

namespace yb {

class HybridTime;
class TransactionMetadataPB;

namespace tablet {

class TransactionIntentApplier;

struct TransactionApplyData {
  ProcessingMode mode;
  // Applier should be alive until ProcessApply returns.
  TransactionIntentApplier* applier;
  TransactionId transaction_id;
  consensus::OpId op_id;
  HybridTime commit_time;
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
  virtual const std::shared_future<client::YBClientPtr>& client_future() const = 0;
  virtual HybridTime Now() = 0;
  virtual void UpdateClock(HybridTime hybrid_time) = 0;

 protected:
  ~TransactionParticipantContext() {}
};

// TransactionParticipant manages running transactions, i.e. transactions that have intents in
// appropriate tablet. Since this class manages transactions of tablet there is separate class
// instance per tablet.
class TransactionParticipant : public TransactionStatusManager {
 public:
  explicit TransactionParticipant(TransactionParticipantContext* context);
  virtual ~TransactionParticipant();

  // Adds new running transaction.
  void Add(const TransactionMetadataPB& data, rocksdb::WriteBatch *write_batch);

  boost::optional<TransactionMetadata> Metadata(const TransactionId& id) override;

  HybridTime LocalCommitTime(const TransactionId& id) override;

  void RequestStatusAt(const StatusRequest& request) override;

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override;

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data);

  void SetDB(rocksdb::DB* db);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_PARTICIPANT_H
