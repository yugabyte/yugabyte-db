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

#ifndef YB_DOCDB_ROCKSDB_WRITER_H
#define YB_DOCDB_ROCKSDB_WRITER_H

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb.fwd.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/intent.h"

#include "yb/rocksdb/write_batch.h"

namespace yb {
namespace docdb {

class NonTransactionalWriter : public rocksdb::DirectWriter {
 public:
  NonTransactionalWriter(
    std::reference_wrapper<const KeyValueWriteBatchPB> put_batch, HybridTime hybrid_time);

  bool Empty() const;

  CHECKED_STATUS Apply(rocksdb::DirectWriteHandler* handler) override;

 private:
  const docdb::KeyValueWriteBatchPB& put_batch_;
  HybridTime hybrid_time_;
};

// Buffer for encoding DocHybridTime
class DocHybridTimeBuffer {
 public:
  DocHybridTimeBuffer();

  Slice EncodeWithValueType(const DocHybridTime& doc_ht) {
    auto end = doc_ht.EncodedInDocDbFormat(buffer_.data() + 1);
    return Slice(buffer_.data(), end);
  }

  Slice EncodeWithValueType(HybridTime ht, IntraTxnWriteId write_id) {
    return EncodeWithValueType(DocHybridTime(ht, write_id));
  }
 private:
  std::array<char, 1 + kMaxBytesPerEncodedHybridTime> buffer_;
};

class TransactionalWriter : public rocksdb::DirectWriter {
 public:
  TransactionalWriter(
      std::reference_wrapper<const docdb::KeyValueWriteBatchPB> put_batch,
      HybridTime hybrid_time,
      const TransactionId& transaction_id,
      IsolationLevel isolation_level,
      PartialRangeKeyIntents partial_range_key_intents,
      const Slice& replicated_batches_state,
      IntraTxnWriteId intra_txn_write_id);

  CHECKED_STATUS Apply(rocksdb::DirectWriteHandler* handler) override;

  IntraTxnWriteId intra_txn_write_id() const {
    return intra_txn_write_id_;
  }

  void SetMetadataToStore(const TransactionMetadataPB* value) {
    metadata_to_store_ = value;
  }

  CHECKED_STATUS operator()(
      IntentStrength intent_strength, FullDocKey, Slice value_slice, KeyBytes* key,
      LastKey last_key);

 private:
  CHECKED_STATUS Finish();
  CHECKED_STATUS AddWeakIntent(
      const std::pair<KeyBuffer, IntentTypeSet>& intent_and_types,
      const std::array<Slice, 2>& value,
      DocHybridTimeBuffer* doc_ht_buffer);

  const docdb::KeyValueWriteBatchPB& put_batch_;
  HybridTime hybrid_time_;
  TransactionId transaction_id_;
  IsolationLevel isolation_level_;
  PartialRangeKeyIntents partial_range_key_intents_;
  Slice replicated_batches_state_;
  IntraTxnWriteId intra_txn_write_id_;
  IntraTxnWriteId write_id_ = 0;
  const TransactionMetadataPB* metadata_to_store_ = nullptr;

  // TODO(dtxn) weak & strong intent in one batch.
  // TODO(dtxn) extract part of code knowing about intents structure to lower level.
  // Handler is initialized in Apply method, and not used after apply returns.
  rocksdb::DirectWriteHandler* handler_;
  RowMarkType row_mark_;
  SubTransactionId subtransaction_id_;
  IntentTypeSet strong_intent_types_;
  std::unordered_map<KeyBuffer, IntentTypeSet, ByteBufferHash> weak_intents_;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_ROCKSDB_WRITER_H
