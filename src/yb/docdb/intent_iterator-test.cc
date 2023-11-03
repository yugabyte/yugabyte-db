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

#include <memory>
#include <string>

#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction-test-util.h"

#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/intent_iterator.h"

DECLARE_bool(TEST_docdb_sort_weak_intents);

namespace yb {
namespace docdb {

using dockv::DocPath;
using dockv::KeyEntryValue;
using dockv::PrimitiveValue;
using dockv::SubDocKey;

class IntentIteratorTest : public DocDBTestBase {
 protected:
  IntentIteratorTest() { SeedRandom(); }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_sort_weak_intents) = true;
    DocDBTestBase::SetUp();
  }

  Schema CreateSchema() override {
    return Schema(
      {ColumnSchema("a", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
       ColumnSchema("b", DataType::INT64, ColumnKind::RANGE_ASC_NULL_FIRST),
       // Non-key columns
       ColumnSchema("c", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue),
       ColumnSchema("d", DataType::INT64, ColumnKind::VALUE, Nullable::kTrue),
       ColumnSchema("e", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue)},
      {10_ColId, 20_ColId, 30_ColId, 40_ColId, 50_ColId});
  }
};

const std::string kStrKey1 = "row1";
constexpr int64_t kIntKey1 = 11111;
const std::string kStrKey2 = "row2";
constexpr int64_t kIntKey2 = 22222;

const auto kEncodedDocKey1 = dockv::MakeDocKey(kStrKey1, kIntKey1).Encode();
const auto kEncodedDocKey2 = dockv::MakeDocKey(kStrKey2, kIntKey2).Encode();

void ValidateKeyAndValue(
    const IntentIterator& iter, const dockv::KeyBytes& expected_key_bytes,
    const KeyEntryValue& expected_key_entry_value, HybridTime expected_ht,
    const PrimitiveValue& expected_value, bool expect_int = false) {
  ASSERT_TRUE(iter.valid());
  auto key_result = ASSERT_RESULT(iter.FetchKey());
  ASSERT_EQ(key_result.key.compare_prefix(expected_key_bytes), 0);
  ASSERT_EQ(expected_ht, key_result.write_time.hybrid_time());

  dockv::SubDocument doc(expected_value.value_type());
  ASSERT_OK(doc.DecodeFromValue(iter.value()));

  if (!expected_value.IsTombstone()) {
    SubDocKey key;
    ASSERT_OK(key.DecodeFrom(&key_result.key, dockv::HybridTimeRequired::kFalse));
    ASSERT_EQ(expected_key_entry_value, key.subkeys()[0]);

    ASSERT_EQ(expected_value, doc)
        << "Expected: " << expected_value.ToString() << ", Actual: " << doc.ToString();
  } else {
    ASSERT_TRUE(doc.IsTombstone());
  }
}

TEST_F(IntentIteratorTest, SameTransactionMultipleKeys) {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn);

  SetCurrentTransactionId(*txn);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t1"), HybridTime::FromMicros(500)));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::Primitive("row1_d_t1"), HybridTime::FromMicros(600)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 1 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 2 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 3 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 500 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) "row1_c_t1"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40)]) [kStrongRead, kStrongWrite] \
HT{ physical: 600 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) WriteId(1) "row1_d_t1"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 } -> \
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 500 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 1 } -> \
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 2 } -> \
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 3 } -> \
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 } -> \
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40)]) [kStrongRead, kStrongWrite] \
HT{ physical: 600 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 1 } -> \
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 2 } -> \
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 3 } -> \
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 3 }
      )#");

  const auto txn_context = TransactionOperationContext(*txn, &txn_status_manager);

  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(1000), txn_context, nullptr /*iterate_upper_bound*/);

    iter.Seek(doc_db().key_bounds->lower);

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(500),
        PrimitiveValue::Create("row1_c_t1"));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(600),
        PrimitiveValue::Create("row1_d_t1"));

    iter.Next();
    ASSERT_FALSE(iter.valid());
  }

  // Empirically we require 3 seeks to perform this test.
  // If this number increased, then something got broken and should be fixed.
  // IF this number decreased because of optimization, then we should adjust this check.
  ASSERT_EQ(intents_db_options_.statistics->getTickerCount(rocksdb::Tickers::NUMBER_DB_SEEK), 3);
}

TEST_F(IntentIteratorTest, SameTransactionMultipleUpdatesForSameSubDocKey) {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn);

  SetCurrentTransactionId(*txn);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t1"), HybridTime::FromMicros(500)));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t2"), HybridTime::FromMicros(600)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 1 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 2 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 3 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 600 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) WriteId(1) "row1_c_t2"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 500 } -> \
TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) "row1_c_t1"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 } -> \
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 500 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 1 } -> \
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 2 } -> \
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 3 } -> \
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 } -> \
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
HT{ physical: 600 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 1 } -> \
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 2 } -> \
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 600 w: 3 } -> \
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 600 w: 3 }
      )#");

  const auto txn_context = TransactionOperationContext(*txn, &txn_status_manager);

  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(1000), txn_context, nullptr /*iterate_upper_bound*/);

    iter.Seek(doc_db().key_bounds->lower);

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(600),
        PrimitiveValue::Create("row1_c_t2"));

    iter.Next();
    ASSERT_FALSE(iter.valid());
  }
}

TEST_F(IntentIteratorTest, OverlappingTransactions) {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  auto txn1 = ASSERT_RESULT(FullyDecodeTransactionId("0000000000000001"));
  auto txn2 = ASSERT_RESULT(FullyDecodeTransactionId("0000000000000002"));

  SetCurrentTransactionId(txn1);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t1"), HybridTime::FromMicros(500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(40000), HybridTime::FromMicros(500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e_t1"), HybridTime::FromMicros(500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(42000), HybridTime::FromMicros(500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_t1"), HybridTime::FromMicros(500)));
  ResetCurrentTransactionId();

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)), QLValue::Primitive("row1_c"),
      HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)), QLValue::Primitive("row1_e"),
      HybridTime::FromMicros(1000)));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(20000), HybridTime::FromMicros(2000)));

  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      HybridTime::FromMicros(2500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(30000), HybridTime::FromMicros(3000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)), QLValue::Primitive("row2_e"),
      HybridTime::FromMicros(2000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_prime"), HybridTime::FromMicros(4000)));

  txn_status_manager.Commit(txn1, HybridTime::FromMicros(3500));

  SetCurrentTransactionId(txn2);
  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey1), HybridTime::FromMicros(4000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_t2"), HybridTime::FromMicros(4000)));
  ResetCurrentTransactionId();
  txn_status_manager.Commit(txn2, HybridTime::FromMicros(6000));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 3000 }]) -> 30000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2500 }]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2000 }]) -> 20000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 4000 }]) -> "row2_e_prime"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 2000 }]) -> "row2_e"
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kStrongRead, kStrongWrite] HT{ physical: 4000 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) WriteId(5) DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) "row1_c_t1"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(1) 40000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(2) "row1_e_t1"
SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row2", 22222]), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 3 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["row2", 22222]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(3) 42000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 4000 } \
    -> TransactionId(30303030-3030-3030-3030-303030303032) WriteId(6) "row2_e_t2"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(4) "row2_e_t1"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 } -> \
    SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 2 } -> \
    SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 3 } -> \
    SubDocKey(DocKey([], ["row2", 22222]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 4000 } -> \
    SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 4000 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 4000 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 4000 w: 2 } -> \
    SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 4000 w: 3 } -> \
    SubDocKey(DocKey([], ["row2", 22222]), []) [kWeakRead, kWeakWrite] HT{ physical: 4000 w: 3 }
      )#");

  const auto txn_context =
      TransactionOperationContext(TransactionId::GenerateRandom(), &txn_status_manager);

  // No committed intents as of HT 2000.
  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(2000), txn_context, nullptr /*iterate_upper_bound*/);
    iter.Seek(doc_db().key_bounds->lower);

    ASSERT_FALSE(iter.valid());
  }

  LOG(INFO) << "Validating intent iterator@HT 5000";
  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(5000), txn_context, nullptr /*iterate_upper_bound*/);
    iter.Seek(doc_db().key_bounds->lower);

    ASSERT_TRUE(iter.valid());

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Create("row1_c_t1"));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Int64(40000));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Create("row1_e_t1"));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Int64(42000));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Create("row2_e_t1"));

    iter.Next();

    ASSERT_FALSE(iter.valid());
  }

  LOG(INFO) << "Validating intent iterator@HT 6000";
  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(6000), txn_context, nullptr /*iterate_upper_bound*/);
    iter.Seek(doc_db().key_bounds->lower);

    ASSERT_TRUE(iter.valid());

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(6000),
        PrimitiveValue::kTombstone);

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Create("row1_c_t1"));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Int64(40000));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Create("row1_e_t1"));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Int64(42000));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId), HybridTime::FromMicros(6000),
        PrimitiveValue::Create("row2_e_t2"));

    iter.Next();

    ASSERT_FALSE(iter.valid());
  }

  LOG(INFO) << "Validating intent iterator@HT 6000 and SeekOutOfSubKey";
  {
    IntentIterator iter(
        intents_db(), doc_db().key_bounds, CoarseTimePoint::max() /* deadline */,
        ReadHybridTime::FromMicros(6000), txn_context, nullptr /*iterate_upper_bound*/);
    iter.Seek(doc_db().key_bounds->lower);

    ASSERT_TRUE(iter.valid());

    ValidateKeyAndValue(
        iter, kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId), HybridTime::FromMicros(6000),
        PrimitiveValue::kTombstone);

    dockv::KeyBytes doc_key1;
    doc_key1.Append(kEncodedDocKey1);
    iter.SeekOutOfSubKey(&doc_key1);

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId), HybridTime::FromMicros(3500),
        PrimitiveValue::Int64(42000));

    iter.Next();

    ValidateKeyAndValue(
        iter, kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId), HybridTime::FromMicros(6000),
        PrimitiveValue::Create("row2_e_t2"));

    iter.Next();

    ASSERT_FALSE(iter.valid());
  }
}

}  // namespace docdb
}  // namespace yb
