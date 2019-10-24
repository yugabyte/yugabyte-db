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

#ifndef ENT_SRC_YB_CDC_CDC_PRODUCER_H
#define ENT_SRC_YB_CDC_CDC_PRODUCER_H

#include <memory>
#include <string>

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include "yb/cdc/cdc_service.service.h"
#include "yb/client/client.h"
#include "yb/common/transaction.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/tablet/transaction_participant.h"

namespace yb {
namespace cdc {

// Use boost::unordered_map instead of std::unordered_map because gcc release build
// fails to compile correctly when TxnStatusMap is used with Result<> (due to what seems like
// a bug in gcc where it tries to incorrectly destroy Status part of Result).
typedef boost::unordered_map<
    TransactionId, TransactionStatusResult, TransactionIdHash> TxnStatusMap;
typedef std::pair<uint64_t, size_t> RecordTimeIndex;

struct StreamMetadata {
  TableId table_id;
  CDCRecordType record_type;
  CDCRecordFormat record_format;

  StreamMetadata() = default;

  StreamMetadata(TableId table_id, CDCRecordType record_type, CDCRecordFormat record_format)
      : table_id(table_id), record_type(record_type), record_format(record_format) {
  }
};

class CDCProducer {
 public:
  CDCProducer() = default;

  CDCProducer(const CDCProducer&) = delete;
  void operator=(const CDCProducer&) = delete;

  // Get Changes for tablet since given OpId.
  static CHECKED_STATUS GetChanges(const std::string& stream_id,
                                   const std::string& tablet_id,
                                   const OpIdPB& op_id,
                                   const StreamMetadata& record,
                                   const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                                   GetChangesResponsePB* resp);

 private:
  // Populate CDC record corresponding to WAL batch in ReplicateMsg.
  static CHECKED_STATUS PopulateWriteRecord(const consensus::ReplicateMsgPtr& write_msg,
                                            const TxnStatusMap& txn_map,
                                            const StreamMetadata& metadata,
                                            const Schema& schema,
                                            GetChangesResponsePB* resp);

  // Populate CDC record corresponding to WAL UPDATE_TRANSACTION_OP entry.
  static CHECKED_STATUS PopulateTransactionRecord(const consensus::ReplicateMsgPtr& replicate_msg,
                                                  CDCRecordPB* records);

  static CHECKED_STATUS SetRecordTxnAndTime(const TransactionId& txn_id,
                                            const TxnStatusMap& txn_map,
                                            CDCRecordPB* record);

  // Order WAL records based on transaction commit time.
  // Records in WAL don't represent the exact order in which records are written in DB due to delay
  // in writing txn APPLYING record.
  // Consider the following WAL entries:
  // TO: WRITE K0
  // T1: WRITE K1 (TXN1)
  // T2: WRITE K2 (TXN2)
  // T3: WRITE K3
  // T4: APPLYING TXN2
  // T5: APPLYING TXN1
  // T6: WRITE K4
  // The order in which keys are written to DB in this example is K0, K3, K2, K1, K4.
  // This method will also set checkpoint to the op id of last processed record.
  static Result<consensus::ReplicateMsgs> SortWrites(const consensus::ReplicateMsgs& msgs,
                                                     const TxnStatusMap& txn_map,
                                                     OpIdPB* checkpoint);

  static Result<std::vector<RecordTimeIndex>> GetCommittedRecordIndexes(
      const consensus::ReplicateMsgs& msgs,
      const TxnStatusMap& txn_map,
      OpIdPB* checkpoint);

  // Set committed record information including commit time for record.
  // This will look at transaction status to determine commit time to be used for CDC record.
  // Returns true if we need to stop processing WAL records beyond this, false otherwise.
  static Result<bool> SetCommittedRecordIndexForReplicateMsg(
      const consensus::ReplicateMsgPtr& msg, size_t index, const TxnStatusMap& txn_map,
      std::vector<RecordTimeIndex>* records);

  // Build transaction status as of hybrid_time.
  static Result<TxnStatusMap> BuildTxnStatusMap(const consensus::ReplicateMsgs& messages,
                                                bool more_replicate_msgs,
                                                const HybridTime& hybrid_time,
                                                tablet::TransactionParticipant* txn_participant);

  static Result<TransactionStatusResult> GetTransactionStatus(
      const TransactionId& transaction_id,
      const HybridTime& hybrid_time,
      tablet::TransactionParticipant* txn_participant);
};

}  // namespace cdc
}  // namespace yb

#endif /* ENT_SRC_YB_CDC_CDC_PRODUCER_H */
