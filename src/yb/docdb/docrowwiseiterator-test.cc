// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction-test-util.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/packed_row.h"
#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"
#include "yb/dockv/schema_packing.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;

DECLARE_bool(TEST_docdb_sort_weak_intents);
DECLARE_bool(use_fast_backward_scan);
DECLARE_bool(use_fast_next_for_iteration);
DECLARE_int32(max_nexts_to_avoid_seek);

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::DocPath;
using dockv::KeyBytes;
using dockv::KeyEntryValue;
using dockv::KeyEntryValues;
using dockv::SubDocKey;

YB_DEFINE_ENUM(IteratorMode, (kGeneric)(kPg));

// Represents operation on a key.
// Key can be inserted and/or deleted.
// Represents when the key is inserted or deleted.
struct KeyOpHtRollback {
  std::optional<MicrosTime> insert_time;
  std::optional<MicrosTime> delete_time;
};

class DocRowwiseIteratorTest : public DocDBTestBase {
 protected:
  DocRowwiseIteratorTest() {
    SeedRandom();
  }
  ~DocRowwiseIteratorTest() override {}

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_sort_weak_intents) = true;
    DocDBTestBase::SetUp();
  }

  const Schema& projection() {
    if (!projection_) {
      projection_ = doc_read_context().schema();
    }
    return *projection_;
  }

  Schema CreateSchema() override;

  void InsertPopulationData();

  void InsertTestRangeData();

  void InsertPackedRow(
      SchemaVersion version, std::reference_wrapper<const dockv::SchemaPacking> schema_packing,
      const KeyBytes &doc_key, HybridTime ht,
      std::initializer_list<std::pair<ColumnId, const QLValuePB>> columns);

  docdb::DocRowwiseIteratorPtr MakeIterator(
      const Schema& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext &txn_op_context,
      const DocDB &doc_db,
      const ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op) {
    reader_projection_.Reset(projection, projection.column_ids());
    return std::make_unique<DocRowwiseIterator>(
        reader_projection_, doc_read_context, txn_op_context, doc_db, read_operation_data,
        pending_op);
  }

  virtual Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
      const Schema &projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext &txn_op_context,
      const DocDB &doc_db,
      const ReadOperationData& read_operation_data,
      const DocQLScanSpec &spec,
      std::reference_wrapper<const ScopedRWOperation> pending_op) {
    auto iter = MakeIterator(
        projection, doc_read_context, txn_op_context, doc_db, read_operation_data, pending_op);
    RETURN_NOT_OK(iter->Init(spec));
    return iter;
  }

  virtual Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
      const Schema &projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext &txn_op_context,
      const DocDB &doc_db,
      const ReadOperationData& read_operation_data,
      const DocPgsqlScanSpec &spec,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      bool liveness_column_expected = false) {
    auto iter = MakeIterator(
        projection, doc_read_context, txn_op_context, doc_db, read_operation_data, pending_op);
    RETURN_NOT_OK(iter->Init(spec));
    return iter;
  }

  virtual Result<YQLRowwiseIteratorIf::UniPtr> CreateIterator(
      const Schema &projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext &txn_op_context,
      const DocDB &doc_db,
      const ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      bool liveness_column_expected = false) {
    auto iter = MakeIterator(
        projection, doc_read_context, txn_op_context, doc_db, read_operation_data, pending_op);
    CHECK_OK(iter->InitForTableType(YQL_TABLE_TYPE));
    return iter;
  }

  // CreateIteratorAndValidate functions create iterator and validate result.
  template <class T>
  void CreateIteratorAndValidate(
      const Schema &schema,
      const ReadHybridTime &read_time,
      const T &spec,
      const std::string &expected,
      const HybridTime &max_seen_ht = HybridTime::kInvalid,
      const Schema *projection = nullptr,
      const TransactionOperationContext &txn_op_context = kNonTransactionalOperationContext);

  void CreateIteratorAndValidate(
      const Schema &schema,
      const ReadHybridTime &read_time,
      const std::string &expected,
      const HybridTime &max_seen_ht = HybridTime::kInvalid,
      const Schema *projection = nullptr,
      const TransactionOperationContext &txn_op_context = kNonTransactionalOperationContext);

  void CreateIteratorAndValidate(
      const ReadHybridTime &read_time,
      const std::string &expected,
      const HybridTime &max_seen_ht = HybridTime::kInvalid,
      const TransactionOperationContext &txn_op_context = kNonTransactionalOperationContext);

  // Test case implementation.
  void TestClusteredFilterRange();
  void TestClusteredFilterRangeWithTableTombstone();
  void TestClusteredFilterRangeWithTableTombstoneReverseScan();
  void TestClusteredFilterHybridScan();
  void TestClusteredFilterSubsetCol();
  void TestClusteredFilterSubsetCol2();
  void TestClusteredFilterMultiIn();
  void TestClusteredFilterEmptyIn();
  void SetupDocRowwiseIteratorData();
  void TestDocRowwiseIterator();
  void TestDocRowwiseIteratorDeletedDocument();
  void TestDocRowwiseIteratorWithRowDeletes();
  void TestBackfillInsert();
  void TestDocRowwiseIteratorHasNextIdempotence();
  void TestDocRowwiseIteratorIncompleteProjection();
  void TestColocatedTableTombstone();
  void TestDocRowwiseIteratorMultipleDeletes();
  void TestDocRowwiseIteratorValidColumnNotInProjection();
  void TestDocRowwiseIteratorKeyProjection();
  void TestDocRowwiseIteratorResolveWriteIntents();
  void TestIntentAwareIteratorSeek();
  void TestSeekTwiceWithinTheSameTxn();
  void TestScanWithinTheSameTxn();
  void TestLargeKeys();
  void TestPackedRow();
  void TestDeleteMarkerWithPackedRow();
  void TestUpdatePackedRow();
  // Restore doesn't use delete tombstones for rows, instead marks all columns
  // as deleted.
  void TestDeletedDocumentUsingLivenessColumnDelete();
  void TestPartialKeyColumnsProjection();
  void TestMaxNextsToAvoidSeek();
  void SetupDataForLastSeenHtRollback(
      TransactionStatusManagerMock& txn_status_manager);
  void TestLastSeenHtRollback(
      int low, int high, MicrosTime read_ht, MicrosTime expected_max_seen_ht);
  void TestHtRollbackWithKeyOps(const std::vector<KeyOpHtRollback>& ops);
  void TestHtRollbackRecursive(std::vector<KeyOpHtRollback>& ops);

  void ValidateIterator(
      YQLRowwiseIteratorIf *iter,
      IteratorMode mode,
      const Schema &schema,
      const Schema *projection,
      const std::string &expected,
      const HybridTime &expected_max_seen_ht);

  std::optional<Schema> projection_;
  dockv::ReaderProjection reader_projection_;
  bool skip_pg_validation_ = false;
};

static const std::string kStrKey1 = "row1";
static constexpr int64_t kIntKey1 = 11111;
static const std::string kStrKey2 = "row2";
static constexpr int64_t kIntKey2 = 22222;

const KeyBytes kEncodedDocKey1 = dockv::MakeDocKey(kStrKey1, kIntKey1).Encode();
const KeyBytes kEncodedDocKey2 = dockv::MakeDocKey(kStrKey2, kIntKey2).Encode();

Schema DocRowwiseIteratorTest::CreateSchema() {
  return Schema({
        ColumnSchema("a", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
        ColumnSchema("b", DataType::INT64, ColumnKind::RANGE_ASC_NULL_FIRST),
        // Non-key columns
        ColumnSchema("c", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue),
        ColumnSchema("d", DataType::INT64, ColumnKind::VALUE, Nullable::kTrue),
        ColumnSchema("e", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue)
    }, {
        10_ColId,
        20_ColId,
        30_ColId,
        40_ColId,
        50_ColId
    });
}
constexpr int32_t kFixedHashCode = 0;

const KeyBytes GetKeyBytes(
    string hash_key, string range_key1, string range_key2, string range_key3) {
  return DocKey(
             kFixedHashCode, dockv::MakeKeyEntryValues(hash_key),
             dockv::MakeKeyEntryValues(range_key1, range_key2, range_key3))
      .Encode();
}

const Schema population_schema(
    {ColumnSchema("global", DataType::STRING, ColumnKind::HASH),
     ColumnSchema("continent", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("country", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("area", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
     // Non-key columns
     ColumnSchema("population", DataType::INT64, ColumnKind::VALUE, Nullable::kTrue)},
    {10_ColId, 20_ColId, 30_ColId, 40_ColId, 50_ColId});

const std::string GLOBAL = "GLOBAL";

const std::string ASIA = "ASIA";
const std::string INDIA = "INDIA";
const std::string JAPAN = "JAPAN";
const std::string SINGAPORE = "SINGAPORE";

const std::string EUROPE = "EUROPE";
const std::string GERMANY = "GERMANY";
const std::string SPAIN = "SPAIN";
const std::string UK = "UK";

const std::string NORTH_AM = "NORTH_AMERICA";
const std::string CANADA = "CANADA";
const std::string MEXICO = "MEXICO";
const std::string USA = "USA";

const std::string AREA1 = "AREA1";
const std::string AREA2 = "AREA2";

void DocRowwiseIteratorTest::InsertPopulationData() {
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, INDIA, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, JAPAN, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, SINGAPORE, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, GERMANY, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, SPAIN, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, UK, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, CANADA, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, MEXICO, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, USA, AREA1), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));

  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, INDIA, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, JAPAN, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, ASIA, SINGAPORE, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, GERMANY, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, SPAIN, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, EUROPE, UK, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, CANADA, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, MEXICO, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(GetKeyBytes(GLOBAL, NORTH_AM, USA, AREA2), KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::PrimitiveInt64(10), HybridTime::FromMicros(1000)));
}

const KeyBytes GetKeyBytes(int32_t hash_key, int32_t range_key1, int32_t range_key2) {
  return DocKey(kFixedHashCode, dockv::MakeKeyEntryValues(hash_key),
                dockv::MakeKeyEntryValues(range_key1, range_key2)).Encode();
}

const Schema test_range_schema(
    {ColumnSchema("h", DataType::INT32, ColumnKind::HASH),
     ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
     // Non-key columns
     ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
    {10_ColId, 11_ColId, 12_ColId, 13_ColId});

void DocRowwiseIteratorTest::InsertTestRangeData() {
  int h = 5;
  for (int r1 = 5; r1 < 8; r1++) {
    for (int r2 = 4; r2 < 9; r2++) {
      ASSERT_OK(SetPrimitive(
          DocPath(GetKeyBytes(h, r1, r2), KeyEntryValue::MakeColumnId(13_ColId)),
          QLValue::Primitive(r2), HybridTime::FromMicros(1000)));
    }
  }
}

void DocRowwiseIteratorTest::InsertPackedRow(
    SchemaVersion version, std::reference_wrapper<const dockv::SchemaPacking> schema_packing,
    const KeyBytes &doc_key, HybridTime ht,
    std::initializer_list<std::pair<ColumnId, const QLValuePB>> columns) {
  dockv::RowPackerV1 packer(
      version, schema_packing, /* packed_size_limit= */ std::numeric_limits<int64_t>::max(),
      /* value_control_fields= */ Slice());

  for (auto &column : columns) {
    ASSERT_OK(packer.AddValue(column.first, column.second));
  }
  auto packed_row = ASSERT_RESULT(packer.Complete());

  ASSERT_OK(SetPrimitive(
      DocPath(doc_key), dockv::ValueControlFields(), ValueRef(packed_row), ht));
}

Result<std::string> QLTableRowToString(
    const Schema &schema, const qlexpr::QLTableRow &row, const Schema *projection) {
  QLValue value;
  std::stringstream buffer;
  buffer << "{";
  for (size_t idx = 0; idx < schema.num_columns(); idx++) {
    if (idx != 0) {
      buffer << ",";
    }
    if (projection &&
        projection->find_column_by_id(schema.column_id(idx)) == Schema::kColumnNotFound) {
      buffer << "missing";
    } else {
      RETURN_NOT_OK(row.GetValue(schema.column_id(idx), &value));
      buffer << value.ToString();
    }
  }
  buffer << "}";
  return buffer.str();
}

Result<std::string> PgTableRowToString(
    const Schema &schema, const dockv::PgTableRow &row, const Schema *projection) {
  std::stringstream buffer;
  buffer << "{";
  for (size_t idx = 0; idx < schema.num_columns(); idx++) {
    if (idx != 0) {
      buffer << ",";
    }
    if (projection &&
        projection->find_column_by_id(schema.column_id(idx)) == Schema::kColumnNotFound) {
      buffer << "missing";
    } else {
      auto value = row.GetQLValuePB(schema.column_id(idx));
      buffer << QLValue(value).ToString();
    }
  }
  buffer << "}";
  return buffer.str();
}

Result<std::string> ConvertIteratorRowsToString(
    YQLRowwiseIteratorIf *iter,
    IteratorMode mode,
    const Schema &schema,
    const Schema *projection = nullptr) {
  std::stringstream buffer;
  if (mode == IteratorMode::kGeneric) {
    qlexpr::QLTableRow row;
    while (VERIFY_RESULT(iter->FetchNext(&row))) {
      buffer << VERIFY_RESULT(QLTableRowToString(schema, row, projection)) << std::endl;
    }
  } else {
    CHECK(!down_cast<docdb::DocRowwiseIterator*>(iter)->TEST_use_fast_backward_scan());
    down_cast<docdb::DocRowwiseIterator*>(iter)->TEST_force_allow_fetch_pg_table_row();
    dockv::ReaderProjection reader_projection(projection ? *projection : schema);
    dockv::PgTableRow row(reader_projection);
    while (VERIFY_RESULT(iter->PgFetchNext(&row))) {
      buffer << VERIFY_RESULT(PgTableRowToString(schema, row, projection)) << std::endl;
    }
  }

  return buffer.str();
}

void DocRowwiseIteratorTest::ValidateIterator(
    YQLRowwiseIteratorIf *iter,
    IteratorMode mode,
    const Schema &schema,
    const Schema *projection,
    const std::string &expected,
    const HybridTime &expected_max_seen_ht) {
  if (skip_pg_validation_ && mode == IteratorMode::kPg) {
    return;
  }

  SCOPED_TRACE(Format("Iterator mode: $0", mode));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      expected,
      ASSERT_RESULT(
          ConvertIteratorRowsToString(iter, mode, schema, projection)));

  ASSERT_EQ(expected_max_seen_ht, iter->TEST_MaxSeenHt());
}

template <class T>
void DocRowwiseIteratorTest::CreateIteratorAndValidate(
    const Schema &schema,
    const ReadHybridTime &read_time,
    const T &spec,
    const std::string &expected,
    const HybridTime &expected_max_seen_ht,
    const Schema *projection,
    const TransactionOperationContext &txn_op_context) {
  // Fast backward scan should not be used for this test as doc mode of DocRowwiseIterator could
  // be changed after the iterator creation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_fast_backward_scan) = false;

  auto doc_read_context = DocReadContext::TEST_Create(schema);

  for (auto mode : kIteratorModeArray) {
    auto pending_op = ScopedRWOperation::TEST_Create();
    auto iter = ASSERT_RESULT(CreateIterator(
        projection ? *projection : schema, doc_read_context, txn_op_context, doc_db(),
        ReadOperationData::FromReadTime(read_time), spec, pending_op));

    ValidateIterator(
        iter.get(), mode, schema, projection, expected, expected_max_seen_ht);
  }
}

void DocRowwiseIteratorTest::CreateIteratorAndValidate(
    const Schema &schema,
    const ReadHybridTime &read_time,
    const std::string &expected,
    const HybridTime &expected_max_seen_ht,
    const Schema *projection,
    const TransactionOperationContext &txn_op_context) {
  // Fast backward scan should not be used for this test as doc mode of DocRowwiseIterator could
  // be changed after the iterator creation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_fast_backward_scan) = false;

  for (auto mode : kIteratorModeArray) {
    auto pending_op = ScopedRWOperation::TEST_Create();
    auto iter = ASSERT_RESULT(CreateIterator(
        projection ? *projection : schema, doc_read_context(), txn_op_context, doc_db(),
        ReadOperationData::FromReadTime(read_time), pending_op));

    ValidateIterator(iter.get(), mode, schema, projection, expected, expected_max_seen_ht);
  }
}

void DocRowwiseIteratorTest::CreateIteratorAndValidate(
    const ReadHybridTime &read_time,
    const std::string &expected,
    const HybridTime &expected_max_seen_ht,
    const TransactionOperationContext &txn_op_context) {
  // Fast backward scan should not be used for this test as doc mode of DocRowwiseIterator could
  // be changed after the iterator creation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_fast_backward_scan) = false;

  auto &projection = this->projection();

  for (auto mode : kIteratorModeArray) {
    auto pending_op = ScopedRWOperation::TEST_Create();
    auto iter = ASSERT_RESULT(CreateIterator(
        projection, doc_read_context(), txn_op_context, doc_db(),
        ReadOperationData::FromReadTime(read_time), pending_op));

    ValidateIterator(
        iter.get(), mode, doc_read_context().schema(), &doc_read_context().schema(), expected,
        expected_max_seen_ht);
  }
}

void DocRowwiseIteratorTest::TestClusteredFilterRange() {
  InsertTestRangeData();

  auto arena = SharedSmallArena();
  const KeyEntryValues hashed_components{KeyEntryValue::Int32(5)};

  QLConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(11_ColId);
  ids->add_elems()->set_column_id(12_ColId);
  cond.set_op(QL_OP_IN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_int32_value(5);
  option1->add_elems()->set_int32_value(6);

  DocQLScanSpec spec(
      test_range_schema, kFixedHashCode, kFixedHashCode, arena,
      dockv::TEST_KeyEntryValuesToSlices(*arena, hashed_components), QLConditionPBPtr(&cond),
      nullptr, rocksdb::kDefaultQueryId);

  CreateIteratorAndValidate(
      test_range_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {int32:5,int32:5,int32:6,int32:6}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterRangeWithTableTombstone() {
  constexpr ColocationId colocation_id(0x4001);

  Schema test_schema(
      {ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       // Non-key columns
       ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
      {10_ColId, 11_ColId, 12_ColId});
  test_schema.set_colocation_id(colocation_id);

  const KeyEntryValues range_components{
      KeyEntryValue::Int32(5), KeyEntryValue::Int32(6)};

  ASSERT_OK(SetPrimitive(
      DocPath(
          DocKey(test_schema, range_components).Encode(), KeyEntryValue::MakeColumnId(12_ColId)),
      QLValue::Primitive(10), HybridTime::FromMicros(1000)));

  // Add colocation table tombstone.
  DocKey colocation_key(colocation_id);
  ASSERT_OK(DeleteSubDoc(DocPath(colocation_key.Encode()), HybridTime::FromMicros(500)));

  PgsqlConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(12_ColId);
  cond.set_op(QL_OP_LESS_THAN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_int32_value(5);

  DocDBDebugDumpToConsole();

  DocPgsqlScanSpec spec(test_schema, &cond);

  CreateIteratorAndValidate(
      test_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {int32:5,int32:6,int32:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterRangeWithTableTombstoneReverseScan() {
  constexpr ColocationId colocation_id(0x4001);

  Schema test_schema(
      {ColumnSchema("r1", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       ColumnSchema("r2", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       // Non-key columns
       ColumnSchema("payload", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)},
      {10_ColId, 11_ColId, 12_ColId});

  const KeyEntryValues range_components{
      KeyEntryValue::Int32(5), KeyEntryValue::Int32(6)};

  ASSERT_OK(SetPrimitive(
      DocPath(
          DocKey(test_schema, range_components).Encode(), KeyEntryValue::MakeColumnId(12_ColId)),
      QLValue::Primitive(10), HybridTime::FromMicros(1000)));

  // Add colocation table tombstone.
  DocKey colocation_key(colocation_id);
  ASSERT_OK(DeleteSubDoc(DocPath(colocation_key.Encode()), HybridTime::FromMicros(500)));

  PgsqlConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(12_ColId);
  cond.set_op(QL_OP_LESS_THAN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_int32_value(5);

  const KeyEntryValues empty_key_components;
  std::optional<int32_t> empty_hash_code;
  static const DocKey default_doc_key;
  DocPgsqlScanSpec spec(
      test_schema, rocksdb::kDefaultQueryId, nullptr, {}, empty_key_components,
      PgsqlConditionPBPtr(&cond), empty_hash_code, empty_hash_code, default_doc_key,
      /* is_forward_scan */ false);

  CreateIteratorAndValidate(
      test_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {int32:5,int32:6,int32:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterHybridScan() {
  InsertPopulationData();

  const KeyEntryValues hashed_components{KeyEntryValue(GLOBAL)};

  QLConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(20_ColId);
  ids->add_elems()->set_column_id(30_ColId);
  ids->add_elems()->set_column_id(40_ColId);
  cond.set_op(QL_OP_IN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_string_value(ASIA);
  option1->add_elems()->set_string_value(JAPAN);
  option1->add_elems()->set_string_value(AREA1);

  auto option2 = options->add_elems()->mutable_tuple_value();
  option2->add_elems()->set_string_value(EUROPE);
  option2->add_elems()->set_string_value(UK);
  option2->add_elems()->set_string_value(AREA1);

  auto arena = SharedSmallArena();
  DocQLScanSpec spec(
      population_schema, kFixedHashCode, kFixedHashCode, arena,
      TEST_KeyEntryValuesToSlices(*arena, hashed_components), QLConditionPBPtr(&cond),
      nullptr, rocksdb::kDefaultQueryId);

  CreateIteratorAndValidate(
      population_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {string:"GLOBAL",string:"ASIA",string:"JAPAN",string:"AREA1",int64:10}
        {string:"GLOBAL",string:"EUROPE",string:"UK",string:"AREA1",int64:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterSubsetCol() {
  InsertPopulationData();

  const KeyEntryValues hashed_components{KeyEntryValue(GLOBAL)};

  QLConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(20_ColId);
  ids->add_elems()->set_column_id(30_ColId);
  cond.set_op(QL_OP_IN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_string_value(ASIA);
  option1->add_elems()->set_string_value(JAPAN);

  auto option2 = options->add_elems()->mutable_tuple_value();
  option2->add_elems()->set_string_value(EUROPE);
  option2->add_elems()->set_string_value(UK);

  auto arena = SharedSmallArena();
  DocQLScanSpec spec(
      population_schema, kFixedHashCode, kFixedHashCode, arena,
      dockv::TEST_KeyEntryValuesToSlices(*arena, hashed_components), QLConditionPBPtr(&cond),
      nullptr, rocksdb::kDefaultQueryId);

  CreateIteratorAndValidate(
      population_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {string:"GLOBAL",string:"ASIA",string:"JAPAN",string:"AREA1",int64:10}
        {string:"GLOBAL",string:"ASIA",string:"JAPAN",string:"AREA2",int64:10}
        {string:"GLOBAL",string:"EUROPE",string:"UK",string:"AREA1",int64:10}
        {string:"GLOBAL",string:"EUROPE",string:"UK",string:"AREA2",int64:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterSubsetCol2() {
  InsertPopulationData();

  const KeyEntryValues hashed_components{KeyEntryValue(GLOBAL)};

  QLConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(30_ColId);
  ids->add_elems()->set_column_id(40_ColId);
  cond.set_op(QL_OP_IN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();

  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_string_value(JAPAN);
  option1->add_elems()->set_string_value(AREA1);

  auto option2 = options->add_elems()->mutable_tuple_value();
  option2->add_elems()->set_string_value(UK);
  option2->add_elems()->set_string_value(AREA1);

  auto arena = SharedSmallArena();
  DocQLScanSpec spec(
      population_schema, kFixedHashCode, kFixedHashCode, arena,
      dockv::TEST_KeyEntryValuesToSlices(*arena, hashed_components), QLConditionPBPtr(&cond),
      nullptr, rocksdb::kDefaultQueryId);

  CreateIteratorAndValidate(
      population_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {string:"GLOBAL",string:"ASIA",string:"JAPAN",string:"AREA1",int64:10}
        {string:"GLOBAL",string:"EUROPE",string:"UK",string:"AREA1",int64:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterMultiIn() {
  InsertPopulationData();

  const KeyEntryValues hashed_components{KeyEntryValue(GLOBAL)};

  QLConditionPB cond;
  cond.set_op(QL_OP_AND);
  auto cond1 = cond.add_operands()->mutable_condition();
  auto cond2 = cond.add_operands()->mutable_condition();

  auto ids = cond1->add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(20_ColId);
  ids->add_elems()->set_column_id(30_ColId);
  cond1->set_op(QL_OP_IN);

  auto options = cond1->add_operands()->mutable_value()->mutable_list_value();
  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_string_value(ASIA);
  option1->add_elems()->set_string_value(JAPAN);
  auto option2 = options->add_elems()->mutable_tuple_value();
  option2->add_elems()->set_string_value(EUROPE);
  option2->add_elems()->set_string_value(UK);

  cond2->add_operands()->set_column_id(40_ColId);
  cond2->set_op(QL_OP_IN);
  auto cond2_options = cond2->add_operands()->mutable_value()->mutable_list_value();
  cond2_options->add_elems()->set_string_value(AREA1);

  auto arena = SharedSmallArena();
  DocQLScanSpec spec(
      population_schema, kFixedHashCode, kFixedHashCode, arena,
      dockv::TEST_KeyEntryValuesToSlices(*arena, hashed_components), QLConditionPBPtr(&cond),
      nullptr, rocksdb::kDefaultQueryId);

  CreateIteratorAndValidate(
      population_schema, ReadHybridTime::FromMicros(2000), spec,
      R"#(
        {string:"GLOBAL",string:"ASIA",string:"JAPAN",string:"AREA1",int64:10}
        {string:"GLOBAL",string:"EUROPE",string:"UK",string:"AREA1",int64:10}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestClusteredFilterEmptyIn() {
  InsertPopulationData();

  const KeyEntryValues hashed_components{KeyEntryValue(GLOBAL)};

  QLConditionPB cond;
  cond.set_op(QL_OP_AND);
  auto cond1 = cond.add_operands()->mutable_condition();
  auto cond2 = cond.add_operands()->mutable_condition();

  auto ids = cond1->add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(20_ColId);
  ids->add_elems()->set_column_id(30_ColId);
  cond1->set_op(QL_OP_IN);

  cond1->add_operands()->mutable_value()->mutable_list_value();

  cond2->add_operands()->set_column_id(40_ColId);
  cond2->set_op(QL_OP_IN);
  auto cond2_options = cond2->add_operands()->mutable_value()->mutable_list_value();
  cond2_options->add_elems()->set_string_value(AREA1);

  auto arena = SharedSmallArena();
  DocQLScanSpec spec(
      population_schema, kFixedHashCode, kFixedHashCode, arena,
      dockv::TEST_KeyEntryValuesToSlices(*arena, hashed_components),
      QLConditionPBPtr(&cond), nullptr, rocksdb::kDefaultQueryId);

  // No rows match the index scan => no rows should be seen by max_seen_ht.
  CreateIteratorAndValidate(
      population_schema, ReadHybridTime::FromMicros(2000), spec,
      "",
      HybridTime::kMin);
}

void DocRowwiseIteratorTest::SetupDocRowwiseIteratorData() {
  // Row 1
  // We don't need any seeks for writes, where column values are primitives.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(1000)));

  // Row 2: one null column, one column that gets deleted and overwritten, another that just gets
  // overwritten. No seeks needed for writes.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(20000), HybridTime::FromMicros(2000)));

  // Deletions normally perform a lookup of the key to see whether it's already there. We will use
  // that to provide the expected result (the number of rows deleted in SQL or whether a key was
  // deleted in Redis). However, because we've just set a value at this path, we don't expect to
  // perform any reads for this deletion.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      HybridTime::FromMicros(2500)));

  // The entire subdocument under DocPath(encoded_doc_key2, 40) just got deleted, and that fact
  // should still be in the write batch's cache, so we should not perform a seek to overwrite it.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(30000), HybridTime::FromMicros(3000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e"), HybridTime::FromMicros(2000)));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_prime"), HybridTime::FromMicros(4000)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 3000 }]) -> 30000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2500 }]) -> DEL
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2000 }]) -> 20000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 4000 }]) -> "row2_e_prime"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 2000 }]) -> "row2_e"
      )#");
}

void DocRowwiseIteratorTest::TestDocRowwiseIterator() {
  SetupDocRowwiseIteratorData();

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
        {string:"row2",int64:22222,null,int64:20000,string:"row2_e"}
      )#",
      HybridTime::FromMicros(2000));

  // Scan at a later hybrid_time.
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(5000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
        {string:"row2",int64:22222,null,int64:30000,string:"row2_e_prime"}
      )#",
      HybridTime::FromMicros(4000));
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorDeletedDocument() {
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(20000), HybridTime::FromMicros(2000)));

  // Delete entire row1 document to test that iterator can successfully jump to next document
  // when it finds deleted document.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1), HybridTime::FromMicros(2500)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 2500 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2000 }]) -> 20000
      )#");

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(5000),
      R"#(
        {string:"row2",int64:22222,null,int64:20000,null}
      )#",
      HybridTime::FromMicros(2500));
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorWithRowDeletes() {
  auto dwb = MakeDocWriteBatch();

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
                                  QLValue::Primitive("row1_c")));

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(10000)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
                                  QLValue::Primitive("row1_e")));

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(20000)));
  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(2800)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 2500 }]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 w: 1 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 2800 }]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2800 w: 1 }]) -> 20000
      )#");

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(5000),
      R"#(
        {string:"row1",int64:11111,null,null,string:"row1_e"}
        {string:"row2",int64:22222,null,int64:20000,null}
      )#",
      HybridTime::FromMicros(2800));
}

void VerifyOldestRecordTime(IntentAwareIterator *iter, const DocKey &doc_key,
                            const SubDocKey &subkey, HybridTime min_hybrid_time,
                            HybridTime expected_oldest_record_time) {
  iter->Seek(doc_key);
  const KeyBytes subkey_bytes = subkey.EncodeWithoutHt();
  const Slice subkey_slice = subkey_bytes.AsSlice();
  Slice read_value;
  HybridTime oldest_past_min_ht =
      ASSERT_RESULT(iter->FindOldestRecord(subkey_slice, min_hybrid_time));
  LOG(INFO) << "iter->FindOldestRecord returned " << oldest_past_min_ht
            << " for " << SubDocKey::DebugSliceToString(subkey_slice);
  ASSERT_EQ(oldest_past_min_ht, expected_oldest_record_time);
}

void VerifyOldestRecordTime(IntentAwareIterator *iter, const DocKey &doc_key,
                            const SubDocKey &subkey, uint64_t min_hybrid_time,
                            uint64_t expected_oldest_record_time) {
  VerifyOldestRecordTime(iter, doc_key, subkey,
                         HybridTime::FromMicros(min_hybrid_time),
                         HybridTime::FromMicros(expected_oldest_record_time));
}

void VerifyOldestRecordTimeIsInvalid(IntentAwareIterator *iter,
                                     const DocKey &doc_key,
                                     const SubDocKey &subkey,
                                     uint64_t min_hybrid_time) {
  VerifyOldestRecordTime(iter, doc_key, subkey,
                         HybridTime::FromMicros(min_hybrid_time),
                         HybridTime::kInvalid);
}

void DocRowwiseIteratorTest::TestBackfillInsert() {
  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey1), 5000_usec_ht));
  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                         QLValue::PrimitiveInt64(10000), 1000_usec_ht));

  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
                         QLValue::Primitive("row1_e"), 1000_usec_ht));

  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                         QLValue::PrimitiveInt64(10000), 900_usec_ht));

  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
                         QLValue::Primitive("row1_e"), 900_usec_ht));

  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey1), 500_usec_ht));
  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                         QLValue::PrimitiveInt64(10000), 300_usec_ht));

  ASSERT_OK(SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
                         QLValue::Primitive("row1_e"), 300_usec_ht));

  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey2), 900_usec_ht));
  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey2), 700_usec_ht));

  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
  Result<TransactionId> txn1 = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn1);
  SetCurrentTransactionId(*txn1);
  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey2), 800_usec_ht));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 500 }]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 900 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 300 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> "row1_e"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 900 }]) -> "row1_e"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 300 }]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [HT{ physical: 900 }]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [HT{ physical: 700 }]) -> DEL
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 800 w: 1 } -> \
  TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 800 w: 2 } -> \
  TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row2", 22222]), []) [kStrongRead, kStrongWrite] HT{ physical: 800 } -> \
  TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) DEL
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 800 } -> \
  SubDocKey(DocKey([], ["row2", 22222]), []) [kStrongRead, kStrongWrite] HT{ physical: 800 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 800 w: 1 } -> \
  SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 800 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 800 w: 2 } -> \
  SubDocKey(DocKey([], ["row2"]), []) [kWeakRead, kWeakWrite] HT{ physical: 800 w: 2 }
      )#");

  TransactionStatusManagerMock myTransactionalOperationContext;
  const TransactionOperationContext kMockTransactionalOperationContext = {
      TransactionId::GenerateRandom(), &myTransactionalOperationContext};
  myTransactionalOperationContext.Commit(*txn1, 800_usec_ht);

  const HybridTime kSafeTime = 50000_usec_ht;
  {
    auto doc_key = dockv::MakeDocKey(kStrKey1, kIntKey1);
    const KeyBytes doc_key_bytes = doc_key.Encode();
    auto iter = CreateIntentAwareIterator(
        doc_db(), BloomFilterOptions::Fixed(doc_key_bytes.AsSlice()),
        rocksdb::kDefaultQueryId, kMockTransactionalOperationContext,
        ReadOperationData::FromSingleReadTime(kSafeTime));

    {
      SubDocKey subkey(doc_key);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 499, 500);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 500, 5000);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 501, 5000);

      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 4999, 5000);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 5000);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 5001);
    }

    {
      SubDocKey subkey(doc_key, KeyEntryValue::MakeColumnId(40_ColId));
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 299, 300);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 300, 900);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 301, 900);

      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 500, 900);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 600, 900);

      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 899, 900);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 900, 1000);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 901, 1000);

      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 999, 1000);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 1000);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 1001);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 40000);
    }
  }

  {
    auto doc_key = dockv::MakeDocKey(kStrKey2, kIntKey2);
    const KeyBytes doc_key_bytes = doc_key.Encode();
    auto iter = CreateIntentAwareIterator(
        doc_db(), BloomFilterOptions::Fixed(doc_key_bytes.AsSlice()),
        rocksdb::kDefaultQueryId, kMockTransactionalOperationContext,
        ReadOperationData::FromSingleReadTime(kSafeTime));

    {
      SubDocKey subkey(doc_key);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 400, 700);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 699, 700);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 700, 800);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 701, 800);

      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 750, 800);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 800, 900);
      VerifyOldestRecordTime(iter.get(), doc_key, subkey, 801, 900);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 900);
      VerifyOldestRecordTimeIsInvalid(iter.get(), doc_key, subkey, 1000);
    }
  }
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorHasNextIdempotence() {
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(2800)));

  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey1), HybridTime::FromMicros(2500)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 2500 }]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 2800 }]) -> "row1_e"
      )#");

  const auto& projection = this->projection();
  auto pending_op = ScopedRWOperation::TEST_Create();
  {
    auto iter = ASSERT_RESULT(CreateIterator(
        projection, doc_read_context(), kNonTransactionalOperationContext, doc_db(),
        ReadOperationData::TEST_FromReadTimeMicros(2800), pending_op));

    qlexpr::QLTableRow row;
    QLValue value;

    ASSERT_TRUE(ASSERT_RESULT(iter->FetchNext(&row)));

    // ColumnId 40 should be deleted whereas ColumnId 50 should be visible.
    ASSERT_OK(row.GetValue(projection.column_id(2), &value));
    ASSERT_TRUE(value.IsNull());

    ASSERT_OK(row.GetValue(projection.column_id(3), &value));
    ASSERT_TRUE(value.IsNull());

    ASSERT_OK(row.GetValue(projection.column_id(4), &value));
    ASSERT_FALSE(value.IsNull());
    ASSERT_EQ("row1_e", value.string_value());
  }
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorIncompleteProjection() {
  auto dwb = MakeDocWriteBatch();

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(10000)));
  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
                                  QLValue::Primitive("row1_e")));
  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(20000)));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 w: 1 }]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 1000 w: 2 }]) -> 20000
      )#");

  Schema projection;
  ASSERT_OK(doc_read_context().schema().TEST_CreateProjectionByNames({"c", "d"}, &projection));

  CreateIteratorAndValidate(
      doc_read_context().schema(), ReadHybridTime::FromMicros(5000),
      R"#(
        {missing,missing,null,int64:10000,missing}
        {missing,missing,null,int64:20000,missing}
      )#",
      HybridTime::FromMicros(1000), &projection);
}

void DocRowwiseIteratorTest::TestColocatedTableTombstone() {
  constexpr ColocationId colocation_id(0x4001);
  auto dwb = MakeDocWriteBatch();

  DocKey encoded_1_with_colocation_id;

  ASSERT_OK(encoded_1_with_colocation_id.FullyDecodeFrom(kEncodedDocKey1));
  encoded_1_with_colocation_id.set_colocation_id(colocation_id);

  ASSERT_OK(dwb.SetPrimitive(
      DocPath(encoded_1_with_colocation_id.Encode(), KeyEntryValue::kLivenessColumn),
      ValueRef(dockv::ValueEntryType::kNullLow)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  DocKey colocation_key(colocation_id);
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(colocation_key.Encode())));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2000)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(ColocationId=16385, [], []), [HT{ physical: 2000 }]) -> DEL
SubDocKey(DocKey(ColocationId=16385, [], ["row1", 11111]), [SystemColumnId(0); \
    HT{ physical: 1000 }]) -> null
      )#");
  Schema schema_copy = doc_read_context().schema();
  schema_copy.set_colocation_id(colocation_id);
  Schema projection;
  auto pending_op = ScopedRWOperation::TEST_Create();

  // Read should have results before delete...
  {
    auto doc_read_context = DocReadContext::TEST_Create(schema_copy);
    auto iter = ASSERT_RESULT(CreateIterator(
        projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
        ReadOperationData::TEST_FromReadTimeMicros(1500),
        pending_op));
    ASSERT_TRUE(ASSERT_RESULT(iter->FetchNext(nullptr)));
  }
  // ...but there should be no results after delete.
  {
    auto doc_read_context = DocReadContext::TEST_Create(schema_copy);
    auto iter = ASSERT_RESULT(CreateIterator(
        projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
        ReadOperationData(), pending_op));
    ASSERT_FALSE(ASSERT_RESULT(iter->FetchNext(nullptr)));
  }
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorMultipleDeletes() {
  auto dwb = MakeDocWriteBatch();

  MonoDelta ttl = MonoDelta::FromMilliseconds(1);
  MonoDelta ttl_expiry = MonoDelta::FromMilliseconds(2);
  auto read_time = ReadHybridTime::SingleTime(HybridTime::FromMicros(2800).AddDelta(ttl_expiry));

  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
                                  QLValue::Primitive("row1_c")));
  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(10000)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  // Deletes.
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1)));
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey2)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));
  dwb.Clear();

  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      dockv::ValueControlFields {.ttl = ttl}, QLValue::Primitive("row1_e")));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(30_ColId)),
                             ValueRef(dockv::ValueEntryType::kTombstone)));
  ASSERT_OK(dwb.TEST_SetPrimitive(DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
                                  QLValue::PrimitiveInt64(20000)));
  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      dockv::ValueControlFields {.ttl = MonoDelta::FromMilliseconds(3)},
      QLValue::Primitive("row2_e")));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2800)));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 2500 }]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 w: 1 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 2800 }]) -> \
    "row1_e"; ttl: 0.001s
SubDocKey(DocKey([], ["row2", 22222]), [HT{ physical: 2500 w: 1 }]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(30); HT{ physical: 2800 w: 1 }]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 2800 w: 2 }]) -> 20000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 2800 w: 3 }]) -> \
    "row2_e"; ttl: 0.003s
      )#");

  Schema projection;
  ASSERT_OK(doc_read_context().schema().TEST_CreateProjectionByNames({"c", "e"}, &projection));

  // PgFetchNext does not support control fields.
  skip_pg_validation_ = true;

  CreateIteratorAndValidate(
      doc_read_context().schema(), read_time,
      R"#(
        {missing,missing,null,missing,string:"row2_e"}
      )#",
      HybridTime::FromMicros(2800), &projection);
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorValidColumnNotInProjection() {
  auto dwb = MakeDocWriteBatch();

  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000)));
  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(20000)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e")));
  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row2_c")));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e")));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2800)));


  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [HT{ physical: 2500 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 2800 }]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(30); HT{ physical: 2000 w: 1 }]) -> "row2_c"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 1000 w: 1 }]) -> 20000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT{ physical: 2000 }]) -> "row2_e"
      )#");

  Schema projection;
  ASSERT_OK(doc_read_context().schema().TEST_CreateProjectionByNames({"c", "d"}, &projection));

  // PgFetchNext expects liveness column, so does not work in this test.
  skip_pg_validation_ = true;

  CreateIteratorAndValidate(
      doc_read_context().schema(), ReadHybridTime::FromMicros(2800),
      R"#(
        {missing,missing,null,null,missing}
        {missing,missing,string:"row2_c",int64:20000,missing}
      )#",
      HybridTime::FromMicros(2800), &projection);
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorKeyProjection() {
  auto dwb = MakeDocWriteBatch();

  // Row 1
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::kLivenessColumn),
      ValueRef(dockv::ValueEntryType::kNullLow)));
  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000)));
  ASSERT_OK(dwb.TEST_SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e")));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 w: 1 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 w: 2 }]) -> "row1_e"
      )#");

  Schema projection;
  ASSERT_OK(doc_read_context().schema().TEST_CreateProjectionByNames({"a", "b"}, &projection));
  CreateIteratorAndValidate(
      doc_read_context().schema(), ReadHybridTime::FromMicros(2800),
      R"#(
        {string:"row1",int64:11111,missing,missing,missing}
      )#",
      HybridTime::FromMicros(1000), &projection);
}

void DocRowwiseIteratorTest::TestDocRowwiseIteratorResolveWriteIntents() {
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
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(1000)));

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
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e"), HybridTime::FromMicros(2000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_prime"), HybridTime::FromMicros(4000)));

  txn_status_manager.Commit(txn1, HybridTime::FromMicros(3500));

  SetCurrentTransactionId(txn2);
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1),
      HybridTime::FromMicros(4000)));
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

  const auto txn_context = TransactionOperationContext(
      TransactionId::GenerateRandom(), &txn_status_manager);

  LOG(INFO) << "=============================================== ReadTime-2000";
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
        {string:"row2",int64:22222,null,int64:20000,string:"row2_e"}
      )#",
      HybridTime::FromMicros(2000), txn_context);

  // Scan at a later hybrid_time.

  LOG(INFO) << "=============================================== ReadTime-5000";
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(5000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c_t1",int64:40000,string:"row1_e_t1"}
        {string:"row2",int64:22222,null,int64:42000,string:"row2_e_prime"}
      )#",
      HybridTime::FromMicros(4000), txn_context);

  // Scan at a later hybrid_time.
  LOG(INFO) << "=============================================== ReadTime-6000";
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(6000),
      R"#(
        {string:"row2",int64:22222,null,int64:42000,string:"row2_e_t2"}
      )#",
      HybridTime::FromMicros(6000), txn_context);
}

void DocRowwiseIteratorTest::TestIntentAwareIteratorSeek() {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn);

  // Have a mix of transactional / non-transaction writes.
  SetCurrentTransactionId(*txn);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_txn"), HybridTime::FromMicros(500)));

  txn_status_manager.Commit(*txn, HybridTime::FromMicros(600));

  ResetCurrentTransactionId();

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row2_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(20000), HybridTime::FromMicros(1000)));

  // Verify the content of RocksDB.
  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(30); HT{ physical: 1000 }]) -> "row2_c"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT{ physical: 1000 }]) -> 20000
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) "row1_c_txn"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 } -> \
    SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) [kStrongRead, kStrongWrite] \
    HT{ physical: 500 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 2 } -> \
    SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 500 w: 3 } -> \
    SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 }
    )#");

  // Create a new IntentAwareIterator and seek to an empty DocKey. Verify that it returns the
  // first non-intent key.
  IntentAwareIterator iter(
      doc_db(), rocksdb::ReadOptions(),
      ReadOperationData::TEST_FromReadTimeMicros(1000),
      TransactionOperationContext());
  iter.Seek(DocKey());
  auto key_data = ASSERT_RESULT(iter.Fetch()).get();
  ASSERT_TRUE(key_data);
  SubDocKey subdoc_key;
  ASSERT_OK(subdoc_key.FullyDecodeFrom(key_data.key, dockv::HybridTimeRequired::kFalse));
  ASSERT_EQ(subdoc_key.ToString(), R"#(SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]))#");
  ASSERT_EQ(key_data.write_time.ToString(), "HT{ physical: 1000 }");
}

void DocRowwiseIteratorTest::TestSeekTwiceWithinTheSameTxn() {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn);

  SetCurrentTransactionId(*txn);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t1"), HybridTime::FromMicros(500)));

  // Verify the content of RocksDB.
  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1"]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["row1", 11111]), []) [kWeakRead, kWeakWrite] HT{ physical: 500 w: 3 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
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
      )#");

  IntentAwareIterator iter(
      doc_db(), rocksdb::ReadOptions(),
      ReadOperationData::TEST_FromReadTimeMicros(1000),
      TransactionOperationContext(*txn, &txn_status_manager));
  for (int i = 1; i <= 2; ++i) {
    iter.Seek(DocKey());
    ASSERT_TRUE(ASSERT_RESULT(iter.Fetch()).get()) << "Seek #" << i << " failed";
  }
}

void DocRowwiseIteratorTest::TestScanWithinTheSameTxn() {
  // Fast backward scan should not be used for this test as doc mode of DocRowwiseIterator could
  // be changed after the iterator creation.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_fast_backward_scan) = false;

  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn);

  SetCurrentTransactionId(*txn);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row2_c_t1"), HybridTime::FromMicros(500)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c_t1"), HybridTime::FromMicros(600)));

  LOG(INFO) << "Dump:\n" << DocDBDebugDumpToStr();

  const auto txn_context = TransactionOperationContext(*txn, &txn_status_manager);
  Schema projection = this->projection();

  auto pending_op = ScopedRWOperation::TEST_Create();
  auto iter = ASSERT_RESULT(CreateIterator(
      projection, doc_read_context(), txn_context, doc_db(),
      ReadOperationData::TEST_FromReadTimeMicros(1000), pending_op));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ASSERT_RESULT(ConvertIteratorRowsToString(
          iter.get(), IteratorMode::kGeneric, doc_read_context().schema())),
      R"#(
        {string:"row1",int64:11111,string:"row1_c_t1",null,null}
        {string:"row2",int64:22222,string:"row2_c_t1",null,null}
      )#");

  // Empirically we require 3 seeks to perform this test.
  // If this number increased, then something got broken and should be fixed.
  // IF this number decreased because of optimization, then we should adjust this check.
  ASSERT_EQ(intents_db_options_.statistics->getTickerCount(rocksdb::Tickers::NUMBER_DB_SEEK), 3);
}

void DocRowwiseIteratorTest::TestLargeKeys() {
  constexpr size_t str_key_size = 0x100;
  std::string str_key(str_key_size, 't');
  auto kEncodedKey = dockv::MakeDocKey(str_key, kIntKey1).Encode();

  // Row 1
  // We don't need any seeks for writes, where column values are primitives.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedKey, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedKey, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedKey, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(1000)));

  DocDBDebugDumpToConsole();

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      Format(
          R"#(
            {string:"$0",int64:$1,string:"row1_c",int64:10000,string:"row1_e"}
          )#",
          str_key, kIntKey1),
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestPackedRow() {
  constexpr int kVersion = 0;
  auto& schema_packing = ASSERT_RESULT(
      doc_read_context().schema_packing_storage.GetPacking(kVersion)).get();

  InsertPackedRow(
      kVersion, schema_packing, kEncodedDocKey1, HybridTime::FromMicros(1000),
      {
          {30_ColId, QLValue::Primitive("row1_c")},
          {40_ColId, QLValue::PrimitiveInt64(10000)},
          {50_ColId, QLValue::Primitive("row1_e")},
      });

  // Add row2 with missing columns.
  InsertPackedRow(
      kVersion, schema_packing, kEncodedDocKey2, HybridTime::FromMicros(1000),
      {
          {30_ColId, QLValue::Primitive("row2_c")},
      });

  DocDBDebugDumpToConsole();

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
        {string:"row2",int64:22222,string:"row2_c",null,null}
      )#",
      HybridTime::FromMicros(1000));
}

void DocRowwiseIteratorTest::TestDeleteMarkerWithPackedRow() {
  constexpr int kVersion = 0;
  auto& schema_packing = ASSERT_RESULT(
      doc_read_context().schema_packing_storage.GetPacking(kVersion)).get();

  InsertPackedRow(
      kVersion, schema_packing, kEncodedDocKey1, HybridTime::FromMicros(1000),
      {
          {30_ColId, QLValue::Primitive("row1_c")},
          {40_ColId, QLValue::PrimitiveInt64(10000)},
          {50_ColId, QLValue::Primitive("row1_e")},
      });

  DocDBDebugDumpToConsole();

  // Test delete marker with lower timestamp than packed row.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1), HybridTime::FromMicros(800)));

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
      )#",
      HybridTime::FromMicros(1000));

  // Delete document with higher timestamp than packed row.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1), HybridTime::FromMicros(1100)));

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#()#",
      HybridTime::FromMicros(1100));
}

void DocRowwiseIteratorTest::TestDeletedDocumentUsingLivenessColumnDelete() {
  // Row 1
  // We don't need any seeks for writes, where column values are primitives.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::kLivenessColumn),
      ValueRef(dockv::ValueEntryType::kNullLow), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)), QLValue::Primitive("row1_c"),
      HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)), QLValue::Primitive("row1_e"),
      HybridTime::FromMicros(1000)));

  // Delete a single column of Row1.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      HybridTime::FromMicros(1100)));

  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1, KeyEntryValue::kLivenessColumn),
      HybridTime::FromMicros(1500)));

  // Delete other columns as well, as expected by iterator V1.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      HybridTime::FromMicros(1500)));
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      HybridTime::FromMicros(1500)));

  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [SystemColumnId(0); HT{ physical: 1500 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1100 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1500 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1500 }]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> "row1_e"
      )#");

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(1000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
      )#",
      HybridTime::FromMicros(1000));

  LOG(INFO) << "Validate one deleted column is removed";
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(1100),
      R"#(
        {string:"row1",int64:11111,null,int64:10000,string:"row1_e"}
      )#",
      HybridTime::FromMicros(1100));

  LOG(INFO) << "Validate that row is not visible when liveness column is tombstoned";
  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(1500),
      "",
      HybridTime::FromMicros(1500));
}

void DocRowwiseIteratorTest::TestUpdatePackedRow() {
  constexpr int kVersion = 0;
  auto& schema_packing = ASSERT_RESULT(
      doc_read_context().schema_packing_storage.GetPacking(kVersion)).get();

  InsertPackedRow(
      kVersion, schema_packing, kEncodedDocKey1, HybridTime::FromMicros(1000),
      {
          {30_ColId, QLValue::Primitive("row1_c")},
          {40_ColId, QLValue::PrimitiveInt64(10000)},
          {50_ColId, QLValue::Primitive("row1_e")},
      });

  InsertPackedRow(
      kVersion, schema_packing, kEncodedDocKey1, HybridTime::FromMicros(1500),
      {
          {30_ColId, QLValue::Primitive("row1_c_prime")},
          {40_ColId, QLValue::PrimitiveInt64(20000)},
          {50_ColId, QLValue::Primitive("row1_e_prime")},
      });

  DocDBDebugDumpToConsole();

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(1000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c",int64:10000,string:"row1_e"}
      )#",
      HybridTime::FromMicros(1000));

  CreateIteratorAndValidate(
      ReadHybridTime::FromMicros(2000),
      R"#(
        {string:"row1",int64:11111,string:"row1_c_prime",int64:20000,string:"row1_e_prime"}
      )#",
      HybridTime::FromMicros(1500));
}

void DocRowwiseIteratorTest::TestPartialKeyColumnsProjection() {
  InsertPopulationData();

  auto doc_read_context = DocReadContext::TEST_Create(population_schema);

  Schema projection;
  ASSERT_OK(population_schema.TEST_CreateProjectionByNames({"population"}, &projection));

  auto pending_op = ScopedRWOperation::TEST_Create();
  auto iter = ASSERT_RESULT(CreateIterator(
      projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
      ReadOperationData::TEST_FromReadTimeMicros(1000), pending_op));

  qlexpr::QLTableRow row;
  ASSERT_TRUE(ASSERT_RESULT(iter->FetchNext(&row)));
  // Expected count is non-key column (1) + num of key columns.
  ASSERT_EQ(1, row.ColumnCount());
}

void DocRowwiseIteratorTest::TestMaxNextsToAvoidSeek() {
  constexpr auto kNumNonKeyCols = 5;
  const auto ht = HybridTime::FromMicros(1000);

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::kLivenessColumn),
      ValueRef(dockv::ValueEntryType::kNullLow),
      ht));
  for (int i = 1; i <= kNumNonKeyCols; ++i) {
    ASSERT_OK(SetPrimitive(
        DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(ColumnId(10 * i))),
        QLValue::PrimitiveInt64(10000 * i),
        ht));
  }
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::kLivenessColumn),
      ValueRef(dockv::ValueEntryType::kNullLow),
      ht));
  ASSERT_DOCDB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(20); HT{ physical: 1000 }]) -> 20000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT{ physical: 1000 }]) -> 30000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT{ physical: 1000 }]) -> 40000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT{ physical: 1000 }]) -> 50000
SubDocKey(DocKey([], ["row2", 22222]), [SystemColumnId(0); HT{ physical: 1000 }]) -> null
      )#");

  // Test each combination of
  // - use_fast_next_for_iteration: false, true.
  // - max_nexts_to_avoid_seek: 0, 1, ..., kNumNonKeyCols - 1.
  // - use_seek_forward: false, true.
  for (bool use_fast_next : {false, true}) {
    FLAGS_use_fast_next_for_iteration = use_fast_next;
    for (FLAGS_max_nexts_to_avoid_seek = 0;
         FLAGS_max_nexts_to_avoid_seek <= kNumNonKeyCols + 1;
         ++FLAGS_max_nexts_to_avoid_seek) {
      for (bool use_seek_forward : {false, true}) {
        LOG(INFO) << "Testing fast_next=" << FLAGS_use_fast_next_for_iteration
                  << ", max_nexts=" << FLAGS_max_nexts_to_avoid_seek
                  << ", seek_forward=" << use_seek_forward;

        uint64_t num_nexts;
        uint64_t num_seeks;
        {
          IntentAwareIterator iter(
              doc_db(), rocksdb::ReadOptions(),
              ReadOperationData::TEST_FromReadTimeMicros(2000),
              TransactionOperationContext());

          LOG(INFO) << "Seek to first key";
          iter.Seek(DocKey());
          if (VLOG_IS_ON(1)) {
            iter.DebugDump();
          }
          // Seek must be followed by fetch before next seek: there is a debug build sanity check.
          ASSERT_OK(iter.Fetch());

          // Remember stats.
          num_nexts = regular_db_options_.statistics->getTickerCount(
              rocksdb::Tickers::NUMBER_DB_NEXT);
          num_seeks = regular_db_options_.statistics->getTickerCount(
              rocksdb::Tickers::NUMBER_DB_SEEK);

          LOG(INFO) << "Seek to second key";
          if (use_seek_forward) {
            iter.SeekForward(kEncodedDocKey2);
          } else {
            iter.Seek(kEncodedDocKey2, SeekFilter::kAll);
          }
          if (VLOG_IS_ON(1)) {
            iter.DebugDump();
          }

          // If fast nexts are enabled, next stats aren't updated until internal iter is destroyed,
          // so do that now.  (We have to trust that the first seek did not incur any nexts.)
        }

        LOG(INFO) << "Check stats delta upon executing the second seek";
        EXPECT_EQ(
            FLAGS_max_nexts_to_avoid_seek,
            (regular_db_options_.statistics->getTickerCount(rocksdb::Tickers::NUMBER_DB_NEXT)
             - num_nexts));
        // Expect 1 seek if max nexts are exhausted and target key was not reached; 0 otherwise.
        if (FLAGS_max_nexts_to_avoid_seek == kNumNonKeyCols + 1) {
          EXPECT_EQ(
              0,
              (regular_db_options_.statistics->getTickerCount(rocksdb::Tickers::NUMBER_DB_SEEK)
               - num_seeks));
        } else {
          EXPECT_EQ(
              1,
              (regular_db_options_.statistics->getTickerCount(rocksdb::Tickers::NUMBER_DB_SEEK)
               - num_seeks));
        }
      }
    }
  }
}

void DocRowwiseIteratorTest::SetupDataForLastSeenHtRollback(
    TransactionStatusManagerMock& txn_status_manager) {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

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
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(30_ColId)),
      QLValue::Primitive("row1_c"), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(40_ColId)),
      QLValue::PrimitiveInt64(10000), HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row1_e"), HybridTime::FromMicros(1000)));

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
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e"), HybridTime::FromMicros(2000)));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, KeyEntryValue::MakeColumnId(50_ColId)),
      QLValue::Primitive("row2_e_prime"), HybridTime::FromMicros(4000)));

  txn_status_manager.Commit(txn1, HybridTime::FromMicros(3500));

  SetCurrentTransactionId(txn2);
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1),
      HybridTime::FromMicros(4000)));
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
}

void DocRowwiseIteratorTest::TestLastSeenHtRollback(
    int low, int high, MicrosTime read_ht, MicrosTime expected_max_seen_ht) {
  TransactionStatusManagerMock txn_status_manager;
  SetupDataForLastSeenHtRollback(txn_status_manager);

  const auto txn_context = TransactionOperationContext(
      TransactionId::GenerateRandom(), &txn_status_manager);

  const std::string strKeyLow = Format("row$0", low);
  const int64_t intKeyLow = 11111 * low;
  const std::string strKeyHigh = Format("row$0", high);
  const int64_t intKeyHigh = 11111 * high;

  PgsqlConditionPB cond;
  auto ids = cond.add_operands()->mutable_tuple();
  ids->add_elems()->set_column_id(10_ColId);
  ids->add_elems()->set_column_id(20_ColId);
  cond.set_op(QL_OP_IN);
  auto options = cond.add_operands()->mutable_value()->mutable_list_value();
  auto option1 = options->add_elems()->mutable_tuple_value();
  option1->add_elems()->set_string_value(strKeyLow);
  option1->add_elems()->set_int64_value(intKeyLow);
  auto option2 = options->add_elems()->mutable_tuple_value();
  option2->add_elems()->set_string_value(strKeyHigh);
  option2->add_elems()->set_int64_value(intKeyHigh);

  DocPgsqlScanSpec spec(doc_read_context().schema(), &cond);

  LOG(INFO) << Format(
      "SELECT ... WHERE key IN (key$0, key$1) at time=$2",
      low, high, read_ht);
  CreateIteratorAndValidate(
      doc_read_context().schema(),
      ReadHybridTime::FromMicros(read_ht),
      spec,
      "",
      HybridTime::FromMicros(expected_max_seen_ht),
      nullptr,
      txn_context);
}

constexpr MicrosTime kInsertBeforeReadTime = 1000;
constexpr MicrosTime kDeleteBeforeReadTime = 1000;
constexpr MicrosTime kReadTime = 3000;
constexpr MicrosTime kInsertAfterReadTime = 4000;
constexpr MicrosTime kDeleteAfterReadTime = 5000;
constexpr int kNumKeyOps = 5;

void DocRowwiseIteratorTest::TestHtRollbackWithKeyOps(const std::vector<KeyOpHtRollback>& ops) {
  // Setup rockdb for each test separately.
  ASSERT_OK(DestroyRocksDB());
  ASSERT_OK(InitRocksDBDir());
  ASSERT_OK(OpenRocksDB());
  ResetMonotonicCounter();

  // Setup data for each op.
  for (auto key = 0; key < kNumKeyOps; key++) {
    const KeyBytes encodedKey = dockv::MakeDocKey(Format("row$0", key), key * 11111).Encode();
    auto op = ops[key];
    if (!op.insert_time) {
      continue;
    }
    auto insert_time = HybridTime::FromMicros(*op.insert_time);
    ASSERT_OK(SetPrimitive(
        DocPath(encodedKey, KeyEntryValue::MakeColumnId(30_ColId)),
        QLValue::Primitive("row_c"), insert_time));
    if (!op.delete_time) {
      continue;
    }
    auto delete_time = HybridTime::FromMicros(*op.delete_time);
    ASSERT_OK(DeleteSubDoc(
        DocPath(encodedKey, KeyEntryValue::MakeColumnId(30_ColId)), delete_time));
  }

  // Test queries of form: key >= 1 && key <= 3
  for (auto strict_ineq1 : {false, true}) {
    for (auto strict_ineq2 : {false, true}) {
      PgsqlConditionPB cond;
      cond.set_op(QL_OP_BETWEEN);

      // a
      auto* op1 = cond.add_operands();
      op1->set_column_id(10_ColId);

      // a >= "row1"
      auto* op2 = cond.add_operands();
      op2->mutable_value()->set_string_value("row1");

      // a <= "row3"
      auto* op3 = cond.add_operands();
      op3->mutable_value()->set_string_value("row3");

      // inclusive?
      auto* op4 = cond.add_operands();
      auto* op5 = cond.add_operands();
      op4->mutable_value()->set_bool_value(!strict_ineq1);
      op5->mutable_value()->set_bool_value(!strict_ineq2);

      auto schema = doc_read_context().schema();
      DocPgsqlScanSpec spec(schema, &cond);

      // Compute expected values.
      std::string expected;
      HybridTime max_seen_ht = HybridTime::kMin;
      // Key satsifies the condition. Output the key.
      auto output_op = [&](int key) {
        auto op = ops[key];
        // Ignore if the insert happened after the read.
        if (!op.insert_time || op.insert_time > kReadTime) {
          return;
        }
        // Check if the key is deleted before read.
        if (op.delete_time && *op.delete_time < kReadTime) {
          max_seen_ht.MakeAtLeast(HybridTime::FromMicros(kDeleteBeforeReadTime));
        } else {
          max_seen_ht.MakeAtLeast(HybridTime::FromMicros(kInsertBeforeReadTime));
        }
        // Output key if it is not deleted before read.
        if (!op.delete_time || *op.delete_time > kReadTime) {
          expected.append(Format(
              "{string:\"row$0\",missing,missing,missing,missing}\n", key));
        }
      };

      // Output key = 1 when cond is key >= 1.
      if (!strict_ineq1) {
        output_op(1);
      }

      // 2 is always present in the output.
      output_op(2);

      // Output key = 3 when cond is key <= 3.
      if (!strict_ineq2) {
        output_op(3);
      }

      // SELECT a @ kReadTime
      Schema projection;
      ASSERT_OK(doc_read_context().schema().TEST_CreateProjectionByNames({"a"}, &projection));
      CreateIteratorAndValidate(
          doc_read_context().schema(),
          ReadHybridTime::FromMicros(kReadTime),
          spec,
          expected,
          max_seen_ht,
          &projection);
    }
  }
}

void DocRowwiseIteratorTest::TestHtRollbackRecursive(std::vector<KeyOpHtRollback>& ops) {
  if (ops.size() == kNumKeyOps) {
    return TestHtRollbackWithKeyOps(ops);
  }

  auto add_op = [&](KeyOpHtRollback op) {
    ops.push_back(op);
    TestHtRollbackRecursive(ops);
    ops.pop_back();
  };

  // What happens to key: ops.size() + 1?
  // Case 1: Never present.
  add_op(KeyOpHtRollback{
    .insert_time = std::nullopt,
    .delete_time = std::nullopt,
  });

  // Case 2: Insert before read.
  add_op(KeyOpHtRollback{
    .insert_time = kInsertBeforeReadTime,
    .delete_time = std::nullopt,
  });

  // Case 3: Insert after read.
  add_op(KeyOpHtRollback{
    .insert_time = kInsertAfterReadTime,
    .delete_time = std::nullopt,
  });

  // Case 4: Insert before read and delete before read.
  add_op(KeyOpHtRollback{
    .insert_time = kInsertBeforeReadTime,
    .delete_time = kDeleteBeforeReadTime,
  });

  // Case 5: Insert before read and delete after read.
  add_op(KeyOpHtRollback{
    .insert_time = kInsertBeforeReadTime,
    .delete_time = kDeleteAfterReadTime,
  });

  // Case 6: Insert after read and delete after read.
  add_op(KeyOpHtRollback{
    .insert_time = kInsertAfterReadTime,
    .delete_time = kDeleteAfterReadTime,
  });
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterTestRange) {
  TestClusteredFilterRange();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterRangeWithTableTombstone) {
  TestClusteredFilterRangeWithTableTombstone();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterRangeWithTableTombstoneReverseScan) {
  TestClusteredFilterRangeWithTableTombstoneReverseScan();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterHybridScanTest) {
  TestClusteredFilterHybridScan();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterSubsetColTest) {
  TestClusteredFilterSubsetCol();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterSubsetColTest2) {
  TestClusteredFilterSubsetCol2();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterMultiInTest) {
  TestClusteredFilterMultiIn();
}

TEST_F(DocRowwiseIteratorTest, ClusteredFilterEmptyInTest) {
  TestClusteredFilterEmptyIn();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorTest) {
  TestDocRowwiseIterator();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorDeletedDocumentTest) {
  TestDocRowwiseIteratorDeletedDocument();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorTestRowDeletes) {
  TestDocRowwiseIteratorWithRowDeletes();
}

TEST_F(DocRowwiseIteratorTest, BackfillInsert) {
  TestBackfillInsert();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorHasNextIdempotence) {
  TestDocRowwiseIteratorHasNextIdempotence();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorIncompleteProjection) {
  TestDocRowwiseIteratorIncompleteProjection();
}

TEST_F(DocRowwiseIteratorTest, ColocatedTableTombstoneTest) {
  TestColocatedTableTombstone();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorMultipleDeletes) {
  TestDocRowwiseIteratorMultipleDeletes();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorValidColumnNotInProjection) {
  TestDocRowwiseIteratorValidColumnNotInProjection();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorKeyProjection) {
  TestDocRowwiseIteratorKeyProjection();
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorResolveWriteIntents) {
  TestDocRowwiseIteratorResolveWriteIntents();
}

TEST_F(DocRowwiseIteratorTest, IntentAwareIteratorSeek) {
  TestIntentAwareIteratorSeek();
}

TEST_F(DocRowwiseIteratorTest, SeekTwiceWithinTheSameTxn) {
  TestSeekTwiceWithinTheSameTxn();
}

TEST_F(DocRowwiseIteratorTest, ScanWithinTheSameTxn) {
  TestScanWithinTheSameTxn();
}

TEST_F(DocRowwiseIteratorTest, LargeKeysTest) {
  TestLargeKeys();
}

TEST_F(DocRowwiseIteratorTest, BasicPackedRowTest) {
  TestPackedRow();
}

TEST_F(DocRowwiseIteratorTest, DeleteMarkerWithPackedRow) {
  TestDeleteMarkerWithPackedRow();
}

TEST_F(DocRowwiseIteratorTest, UpdatePackedRow) {
  TestUpdatePackedRow();
}

TEST_F(DocRowwiseIteratorTest, DeletedDocumentUsingLivenessColumnDeleteTest) {
  TestDeletedDocumentUsingLivenessColumnDelete();
}

TEST_F(DocRowwiseIteratorTest, PartialKeyColumnsProjection) {
  TestPartialKeyColumnsProjection();
}

TEST_F(DocRowwiseIteratorTest, MaxNextsToAvoidSeek) {
  TestMaxNextsToAvoidSeek();
}

// Absent Keys should not influence max_seen_ht.
// However, the iterator abstraction makes it difficult to do so.
// This test demonstrates behavior of point reads on absent keys.
TEST_F(DocRowwiseIteratorTest, NoHtSeenOnAbsentKeys) {
  // max_seen_ht = 6000 without GH Issue #25214 since
  // it touches a key from txn2 committed at 6000.
  TestLastSeenHtRollback(0, 9, 6000, 0);
}

// Same goes for keys invisible because of MVCC
TEST_F(DocRowwiseIteratorTest, NoHtSeenOnInvisibleKeys) {
  // max_seen_ht = 1000 without GH Issue #25214 since the iteration
  // touches key11 in regularDB even though it is not part of spec.
  TestLastSeenHtRollback(0, 2, 1000, 0);
}

// We cannot ignore tombstones when calculating max_seen_ht.
// This is a simple test case checking the same.
TEST_F(DocRowwiseIteratorTest, HtSeenOnDeletedKeys) {
  TestLastSeenHtRollback(0, 1, 6000, 6000);
}

TEST_F(DocRowwiseIteratorTest, ExhaustiveHtRollbackTest) {
  // Run the test for 6^5 combinations of ops.
  std::vector<KeyOpHtRollback> ops;
  ops.reserve(kNumKeyOps);
  TestHtRollbackRecursive(ops);
}

}  // namespace docdb
}  // namespace yb
