// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_CDC_CDC_PRODUCER_H
#define ENT_SRC_YB_CDC_CDC_PRODUCER_H

#include <memory>
#include <string>

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include "yb/cdc/cdc_service.service.h"
#include "yb/common/transaction.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/docdb/docdb.pb.h"

namespace yb {
namespace cdc {

// Use boost::unordered_map instead of std::unordered_map because gcc release build
// fails to compile correctly when TxnStatusMap is used with Result<> (due to what seems like
// a bug in gcc where it tries to incorrectly destroy Status part of Result).
typedef boost::unordered_map<
    TransactionId, TransactionStatusResult, TransactionIdHash> TxnStatusMap;
typedef std::pair<uint64_t, size_t> RecordTimeIndex;

struct CDCRecordMetadata {
  CDCRecordType record_type;
  CDCRecordFormat record_format;

  CDCRecordMetadata(CDCRecordType record_type, CDCRecordFormat record_format)
      : record_type(record_type), record_format(record_format) {
  }
};

class CDCProducer {
 public:
  explicit CDCProducer(const std::shared_ptr<tablet::TabletPeer>& tablet_peer)
      : tablet_peer_(tablet_peer) {
  }

  CDCProducer(const CDCProducer&) = delete;
  void operator=(const CDCProducer&) = delete;

  // Get Changes for tablet since given OpId.
  CHECKED_STATUS GetChanges(const GetChangesRequestPB& req,
                            GetChangesResponsePB* resp);

  // Get CDC record type and format for a given subscriber.
  Result<CDCRecordMetadata> GetRecordMetadataForSubscriber(
      const std::string& subscriber_uuid);

 private:
  // Populate CDC record corresponding to WAL batch in ReplicateMsg.
  CHECKED_STATUS PopulateWriteRecord(const consensus::ReplicateMsgPtr& write_msg,
                                     const TxnStatusMap& txn_map,
                                     const CDCRecordMetadata& metadata,
                                     GetChangesResponsePB* resp);

  // Populate CDC record corresponding to WAL UPDATE_TRANSACTION_OP entry.
  CHECKED_STATUS PopulateTransactionRecord(const consensus::ReplicateMsgPtr& replicate_msg,
                                           CDCRecordPB* records);

  CHECKED_STATUS SetRecordTxnAndTime(const TransactionId& txn_id,
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
  Result<consensus::ReplicateMsgs> SortWrites(const consensus::ReplicateMsgs& msgs,
                                              const TxnStatusMap& txn_map);

  Result<std::vector<RecordTimeIndex>> GetCommittedRecordIndexes(
      const consensus::ReplicateMsgs& msgs,
      const TxnStatusMap& txn_map);

  OpIdPB GetLastCheckpoint(const std::string& subscriber_uuid);

  // Build transaction status as of hybrid_time.
  Result<TxnStatusMap> BuildTxnStatusMap(const consensus::ReplicateMsgs& messages,
                                         const HybridTime& hybrid_time);

  Result<TransactionStatusResult> GetTransactionStatus(const TransactionId& transaction_id,
                                                       const HybridTime& hybrid_time);

  std::shared_ptr<tablet::TabletPeer> tablet_peer_;
};

}  // namespace cdc
}  // namespace yb

#endif /* ENT_SRC_YB_CDC_CDC_PRODUCER_H */
