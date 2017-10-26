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

#include "yb/tserver/tserver.pb.h"

#include "yb/util/result.h"

namespace rocksdb {

class DB;
class WriteBatch;

}

namespace yb {

class HybridTime;
class TransactionMetadataPB;

namespace docdb {

class KeyBytes;

}

namespace tablet {

struct TransactionMetadata {
  TransactionId transaction_id; // 16 byte uuid
  IsolationLevel isolation = IsolationLevel::NON_TRANSACTIONAL;
  TabletId status_tablet;
  uint64_t priority;
  HybridTime start_time;

  static Result<TransactionMetadata> FromPB(const TransactionMetadataPB& source);
};

bool operator==(const TransactionMetadata& lhs, const TransactionMetadata& rhs);

inline bool operator!=(const TransactionMetadata& lhs, const TransactionMetadata& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out, const TransactionMetadata& metadata);

struct TransactionStatusResult {
  tserver::TransactionStatus status;

  // Meaning of status_time is related to status value.
  // PENDING - status_time reflects maximal guaranteed PENDING time, i.e. transaction cannot be
  // committed before this time.
  // COMMITTED - status_time is a commit time.
  // ABORTED - not used.
  HybridTime status_time;
};

typedef std::function<void(Result<TransactionStatusResult>)> TransactionStatusCallback;

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

  boost::optional<TransactionMetadata> Metadata(rocksdb::DB* db, const TransactionId& id);

  HybridTime LocalCommitTime(const TransactionId& id);

  void RequestStatusAt(const TransactionId& id,
                       HybridTime time,
                       TransactionStatusCallback callback);

  void Abort(const TransactionId& id, TransactionStatusCallback callback);

  CHECKED_STATUS ProcessApply(const TransactionApplyData& data);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

void AppendTransactionKeyPrefix(const TransactionId& transaction_id, docdb::KeyBytes* out);

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_TRANSACTION_PARTICIPANT_H
