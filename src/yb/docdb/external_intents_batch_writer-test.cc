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

#include "yb/common/ql_type.h"

#include "yb/common/transaction.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb-test.h"

#include "yb/docdb/docdb.messages.h"
#include "yb/docdb/rocksdb_writer.h"
#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/partition.h"

namespace yb::docdb {

static const char* kTabletUUID = "4c3e1d91-5ea7-4449-8bb3-8b0a3f9ae903";
static const char* kTxnId = "0000000000000001";

class ExternalIntentsBatchWriterTest : public DocDBTestBase {
 public:
  void SetUp() override {
    DocDBTestBase::SetUp();

    // Needed to ensure that intents are ordered deterministically.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_sort_weak_intents) = true;
  }

  Schema CreateSchema() override { return Schema(); }

  Status SendWriteBatch(
      const docdb::LWKeyValueWriteBatchPB& put_batch, HybridTime write_ht, HybridTime batch_ht) {
    rocksdb::WriteBatch intents_write_batch;
    ExternalIntentsBatchWriter batcher(
        put_batch, write_ht, batch_ht, intents_db(), &intents_write_batch, nullptr);

    rocksdb::WriteBatch regular_write_batch;
    regular_write_batch.SetDirectWriter(&batcher);
    RETURN_NOT_OK(regular_db_->Write(write_options(), &regular_write_batch));

    if (intents_write_batch.Count() != 0) {
      RETURN_NOT_OK(intents_db_->Write(write_options(), &intents_write_batch));
    }

    return Status::OK();
  }

  void AddApplyExternalTxn(
      docdb::LWKeyValueWriteBatchPB* put_batch, const TransactionId& txn_id, HybridTime commit_ht,
      const Slice& filter_start_key = "", const Slice& filter_end_key = "") {
    auto* apply_txn = put_batch->add_apply_external_transactions();
    apply_txn->dup_transaction_id(txn_id.AsSlice());
    apply_txn->set_commit_hybrid_time(commit_ht.ToUint64());
    apply_txn->dup_filter_start_key(filter_start_key);
    apply_txn->dup_filter_end_key(filter_end_key);
  }

  void AddExternalIntentsWritePair(
      docdb::LWKeyValueWriteBatchPB* put_batch, const TransactionId& txn_id,
      SubTransactionId subtransaction_id, const std::vector<ExternalIntent>& intents,
      const Uuid& involved_tablet) {
    auto* write_pair = put_batch->add_write_pairs();
    auto [key, value] = ProcessExternalIntents(txn_id, subtransaction_id, intents, involved_tablet);
    write_pair->dup_key(key.AsSlice());
    write_pair->dup_value(value.AsSlice());
  }

 protected:
  ThreadSafeArena arena_;
};

TEST_F(ExternalIntentsBatchWriterTest, SimpleTransaction) {
  // Simple test where we write two batches of external intents, then apply them.
  // Ensure that we external intents are cleaned up after applying and that regulardb entries have
  // the proper write_ids.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);

  const DocKey hk1(0, MakeKeyEntryValues("h1"));
  const DocKey hk2(1, MakeKeyEntryValues("h2"));
  const auto encoded_hk1 = hk1.Encode();
  const auto encoded_hk2 = hk2.Encode();
  Uuid involved_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  // Construct two batches of external intents.
  std::vector<ExternalIntent> intents = {
      {DocPath(encoded_hk1), EncodeValue(QLValue::Primitive("value1"))},
      {DocPath(encoded_hk2), EncodeValue(QLValue::Primitive("value2"))}};
  AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, involved_tablet);

  // Second batch should end up overwriting the previous batch.
  intents = {
      {DocPath(encoded_hk1), EncodeValue(QLValue::Primitive("value3"))},
      {DocPath(encoded_hk2), EncodeValue(QLValue::Primitive("value4"))}};
  AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, involved_tablet);

  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  // Write to intentsdb.
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Ensure that second batch has the correct write_id, as both batches share the same timestamp.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h1"], []), []) -> "value1", \
    SubDocKey(DocKey(0x0001, ["h2"], []), []) -> "value2"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h1"], []), []) -> "value3", \
    SubDocKey(DocKey(0x0001, ["h2"], []), []) -> "value4"]
    )#");

  // Send the apply transaction.
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT);
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Ensure intents are cleaned up and write_ids are correct.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value3"
SubDocKey(DocKey(0x0000, ["h1"], []), [HT{ physical: 6000 }]) -> "value1"
SubDocKey(DocKey(0x0001, ["h2"], []), [HT{ physical: 6000 w: 3 }]) -> "value4"
SubDocKey(DocKey(0x0001, ["h2"], []), [HT{ physical: 6000 w: 1 }]) -> "value2"
    )#");
}

TEST_F(ExternalIntentsBatchWriterTest, ApplyFilterOnHashExternalIntents) {
  // Test using the filter_start_key and filter_end_key of ApplyExternalTransactionPB on hashed
  // keys. Ensure that only the correct intents are applied.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  Uuid hash_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  const int kNumKeys = 10;
  vector<KeyBytes> encoded_hash_keys;
  // Mimic xCluster and group each key in separate batches.
  for (int i = 0; i < kNumKeys; ++i) {
    const DocKey hk(i * 100, MakeKeyEntryValues(Format("h$0", i)));
    std::vector<ExternalIntent> intents = {
        {DocPath(hk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i)))},
        {DocPath(hk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i + 1)))}};
    AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, hash_tablet);
  }

  // Write intents.
  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0000, ["h0"], []), []) -> "value0", \
    SubDocKey(DocKey(0x0000, ["h0"], []), []) -> "value1"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0064, ["h1"], []), []) -> "value2", \
    SubDocKey(DocKey(0x0064, ["h1"], []), []) -> "value3"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x00c8, ["h2"], []), []) -> "value4", \
    SubDocKey(DocKey(0x00c8, ["h2"], []), []) -> "value5"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x012c, ["h3"], []), []) -> "value6", \
    SubDocKey(DocKey(0x012c, ["h3"], []), []) -> "value7"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value14", \
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value16", \
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value18", \
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value19"]
    )#");

  // Apply with a filter, [0,400).
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, "", dockv::PartitionSchema::EncodeMultiColumnHashValue(400));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Should apply the first 4 rows, and keep other the intents.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value14", \
    SubDocKey(DocKey(0x02bc, ["h7"], []), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value16", \
    SubDocKey(DocKey(0x0320, ["h8"], []), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value18", \
    SubDocKey(DocKey(0x0384, ["h9"], []), []) -> "value19"]
    )#");

  // Apply with a filter, [700, "").
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, dockv::PartitionSchema::EncodeMultiColumnHashValue(700), "");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 4 }]) -> "value18"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value8", \
    SubDocKey(DocKey(0x0190, ["h4"], []), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value10", \
    SubDocKey(DocKey(0x01f4, ["h5"], []), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value12", \
    SubDocKey(DocKey(0x0258, ["h6"], []), []) -> "value13"]
    )#");

  // Apply remaining section. There should be no intents remaining in the end.
  put_batch.Clear();
  AddApplyExternalTxn(
      &put_batch, txn1, kWriteHT, dockv::PartitionSchema::EncodeMultiColumnHashValue(400),
      dockv::PartitionSchema::EncodeMultiColumnHashValue(700));
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey(0x0000, ["h0"], []), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey(0x0064, ["h1"], []), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey(0x00c8, ["h2"], []), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey(0x012c, ["h3"], []), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey(0x0190, ["h4"], []), [HT{ physical: 6000 w: 1 }]) -> "value9"
SubDocKey(DocKey(0x0190, ["h4"], []), [HT{ physical: 6000 }]) -> "value8"
SubDocKey(DocKey(0x01f4, ["h5"], []), [HT{ physical: 6000 w: 3 }]) -> "value11"
SubDocKey(DocKey(0x01f4, ["h5"], []), [HT{ physical: 6000 w: 2 }]) -> "value10"
SubDocKey(DocKey(0x0258, ["h6"], []), [HT{ physical: 6000 w: 5 }]) -> "value13"
SubDocKey(DocKey(0x0258, ["h6"], []), [HT{ physical: 6000 w: 4 }]) -> "value12"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey(0x02bc, ["h7"], []), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey(0x0320, ["h8"], []), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey(0x0384, ["h9"], []), [HT{ physical: 6000 w: 4 }]) -> "value18"
    )#");
}

TEST_F(ExternalIntentsBatchWriterTest, ApplyFilterOnRangedExternalIntents) {
  // Test using the filter_start_key and filter_end_key of ApplyExternalTransactionPB on ranged
  // keys. Ensure that only the correct intents are applied.
  docdb::LWKeyValueWriteBatchPB put_batch(&arena_);
  Uuid range_tablet = ASSERT_RESULT(Uuid::FromString(kTabletUUID));
  TransactionId txn1 = ASSERT_RESULT(FullyDecodeTransactionId(kTxnId));

  const int kNumKeys = 10;
  // Mimic xCluster and group each key in separate batches.
  for (int i = 0; i < kNumKeys; ++i) {
    const DocKey rk(MakeKeyEntryValues(Format("r$0", i)));
    std::vector<ExternalIntent> intents = {
        {DocPath(rk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i)))},
        {DocPath(rk.Encode()), EncodeValue(QLValue::Primitive(Format("value$0", 2 * i + 1)))}};
    AddExternalIntentsWritePair(&put_batch, txn1, kMinSubTransactionId, intents, range_tablet);
  }

  // Write intents.
  const auto kBatchHT = 5000_usec_ht;
  const auto kWriteHT = 6000_usec_ht;
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r0"]), []) -> "value0", \
    SubDocKey(DocKey([], ["r0"]), []) -> "value1"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r1"]), []) -> "value2", \
    SubDocKey(DocKey([], ["r1"]), []) -> "value3"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r2"]), []) -> "value4", \
    SubDocKey(DocKey([], ["r2"]), []) -> "value5"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r3"]), []) -> "value6", \
    SubDocKey(DocKey([], ["r3"]), []) -> "value7"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r7"]), []) -> "value14", \
    SubDocKey(DocKey([], ["r7"]), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r8"]), []) -> "value16", \
    SubDocKey(DocKey([], ["r8"]), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r9"]), []) -> "value18", \
    SubDocKey(DocKey([], ["r9"]), []) -> "value19"]
    )#");

  // Apply with a filter, [0,r4).
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT, "", "r4");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));

  // Should apply the first 4 rows, and keep the other intents.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 7 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r7"]), []) -> "value14", \
    SubDocKey(DocKey([], ["r7"]), []) -> "value15"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 8 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r8"]), []) -> "value16", \
    SubDocKey(DocKey([], ["r8"]), []) -> "value17"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 9 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r9"]), []) -> "value18", \
    SubDocKey(DocKey([], ["r9"]), []) -> "value19"]
    )#");

  // Apply with a filter, [r7, "").
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT, "r7", "");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 4 }]) -> "value18"
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 4 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r4"]), []) -> "value8", \
    SubDocKey(DocKey([], ["r4"]), []) -> "value9"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 5 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r5"]), []) -> "value10", \
    SubDocKey(DocKey([], ["r5"]), []) -> "value11"]
TXN EXT 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 6 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["r6"]), []) -> "value12", \
    SubDocKey(DocKey([], ["r6"]), []) -> "value13"]
    )#");

  // Apply remaining section. All intents should be cleaned up.
  put_batch.Clear();
  AddApplyExternalTxn(&put_batch, txn1, kWriteHT, "r4", "r7");
  ASSERT_OK(SendWriteBatch(put_batch, kWriteHT, kBatchHT));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 w: 1 }]) -> "value1"
SubDocKey(DocKey([], ["r0"]), [HT{ physical: 6000 }]) -> "value0"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 3 }]) -> "value3"
SubDocKey(DocKey([], ["r1"]), [HT{ physical: 6000 w: 2 }]) -> "value2"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 5 }]) -> "value5"
SubDocKey(DocKey([], ["r2"]), [HT{ physical: 6000 w: 4 }]) -> "value4"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 7 }]) -> "value7"
SubDocKey(DocKey([], ["r3"]), [HT{ physical: 6000 w: 6 }]) -> "value6"
SubDocKey(DocKey([], ["r4"]), [HT{ physical: 6000 w: 1 }]) -> "value9"
SubDocKey(DocKey([], ["r4"]), [HT{ physical: 6000 }]) -> "value8"
SubDocKey(DocKey([], ["r5"]), [HT{ physical: 6000 w: 3 }]) -> "value11"
SubDocKey(DocKey([], ["r5"]), [HT{ physical: 6000 w: 2 }]) -> "value10"
SubDocKey(DocKey([], ["r6"]), [HT{ physical: 6000 w: 5 }]) -> "value13"
SubDocKey(DocKey([], ["r6"]), [HT{ physical: 6000 w: 4 }]) -> "value12"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 w: 1 }]) -> "value15"
SubDocKey(DocKey([], ["r7"]), [HT{ physical: 6000 }]) -> "value14"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 3 }]) -> "value17"
SubDocKey(DocKey([], ["r8"]), [HT{ physical: 6000 w: 2 }]) -> "value16"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 5 }]) -> "value19"
SubDocKey(DocKey([], ["r9"]), [HT{ physical: 6000 w: 4 }]) -> "value18"
    )#");
}

}  // namespace yb::docdb
