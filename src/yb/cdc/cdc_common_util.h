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

#pragma once

#include <string>
#include <boost/functional/hash.hpp>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replicate_msgs_holder.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/docdb/value_type.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/format.h"

namespace yb {
namespace cdc {

YB_STRONGLY_TYPED_BOOL(ReplicateIntents);

// Use boost::unordered_map instead of std::unordered_map because gcc release build
// fails to compile correctly when TxnStatusMap is used with Result<> (due to what seems like
// a bug in gcc where it tries to incorrectly destroy Status part of Result).
typedef boost::unordered_map<TransactionId,
                             TransactionStatusResult,
                             TransactionIdHash> TxnStatusMap;
typedef std::pair<uint64_t, size_t> RecordTimeIndex;

template <class Value>
void AddColumnToMap(
    const ColumnSchema &col_schema, const Value &col, cdc::KeyValuePairPB *kv_pair) {
  kv_pair->set_key(col_schema.name());
  col.ToQLValuePB(col_schema.type(), kv_pair->mutable_value());
}

void AddProtoRecordColumnToMap(
    const ColumnSchema &col_schema,
    const docdb::PrimitiveValue &col,
    cdc::KeyValuePairPB *kv_pair,
    bool is_proto_record,
    DatumMessagePB *cdc_datum_message = nullptr);

Result<bool> SetCommittedRecordIndexForReplicateMsg(
    const consensus::ReplicateMsgPtr &msg,
    size_t index,
    const TxnStatusMap &txn_map,
    ReplicateIntents replicate_intents,
    std::vector<RecordTimeIndex> *records);

Result<std::vector<RecordTimeIndex>> GetCommittedRecordIndexes(
    const consensus::ReplicateMsgs &msgs,
    const TxnStatusMap &txn_map,
    ReplicateIntents replicate_intents,
    OpId *checkpoint);

Result<consensus::ReplicateMsgs> FilterAndSortWrites(
    const consensus::ReplicateMsgs &msgs,
    const TxnStatusMap &txn_map,
    ReplicateIntents replicate_intents,
    OpId *checkpoint);

Result<TransactionStatusResult> GetTransactionStatus(
    const TransactionId &txn_id,
    const HybridTime &hybrid_time,
    tablet::TransactionParticipant *txn_participant);

Result<TxnStatusMap> BuildTxnStatusMap(
    const consensus::ReplicateMsgs &messages,
    bool more_replicate_msgs,
    const std::shared_ptr<tablet::TabletPeer> &tablet_peer,
    tablet::TransactionParticipant *txn_participant);

}  // namespace cdc
}  // namespace yb
