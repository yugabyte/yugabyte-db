// Copyright (c) YugaByte, Inc.

#include "yb/common/partial_row.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/server/hybrid_clock.h"

namespace yb {
namespace docdb {

class DocOperationTest : public DocDBTestBase {
 public:
  DocOperationTest() {
    SeedRandom();
  }

  void WriteToRocksDB(const DocWriteBatch &doc_write_batch, rocksdb::DB *db) {
    rocksdb::WriteBatch rocksdb_write_batch;
    doc_write_batch.PopulateRocksDBWriteBatchInTest(&rocksdb_write_batch, HybridTime::kMax);
    rocksdb::WriteOptions write_options;
    CHECK_OK(db->Write(write_options, &rocksdb_write_batch));
  }
};

TEST_F(DocOperationTest, TestRedisSetKVWithTTL) {
  // Write key with ttl to docdb.
  auto db = rocksdb();
  yb::RedisWriteRequestPB redis_write_operation_pb;
  auto set_request_pb = redis_write_operation_pb.mutable_set_request();
  set_request_pb->set_ttl(2000);
  redis_write_operation_pb.mutable_key_value()->set_key("abc");
  redis_write_operation_pb.mutable_key_value()->add_value("xyz");
  RedisWriteOperation redis_write_operation(redis_write_operation_pb, HybridTime::kMax);
  DocWriteBatch doc_write_batch(db);
  CHECK_OK(redis_write_operation.Apply(&doc_write_batch, db, HybridTime()));

  WriteToRocksDB(doc_write_batch, db);

  // Read key from rocksdb.
  const KeyBytes doc_key = DocKey::FromRedisStringKey("abc").Encode();
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(read_opts));
  ROCKSDB_SEEK(iter.get(), doc_key.AsSlice());
  ASSERT_TRUE(iter->Valid());

  // Verify correct ttl.
  MonoDelta ttl;
  auto value = iter->value();
  CHECK_OK(Value::DecodeTTL(&value, &ttl));
  EXPECT_EQ(2000, ttl.ToMilliseconds());
}

TEST_F(DocOperationTest, TestYQLWriteWithTTL) {

  // Write key with ttl to docdb.
  auto db = rocksdb();
  yb::YQLWriteRequestPB  yql_writereq_pb;
  yb::YQLResponsePB  yql_writeresp_pb;

  // Define the schema.
  ColumnSchema hash_column_schema("k", INT32, false, true);
  ColumnSchema range_column_schema("c1", INT32, false, false);
  const vector<ColumnSchema> columns({hash_column_schema, range_column_schema});
  const vector<ColumnId> col_ids({ColumnId(0), ColumnId(1)});
  Schema schema (columns, col_ids, columns.size());

  // Add two columns.
  auto hashed_column = yql_writereq_pb.add_hashed_column_values();
  hashed_column->set_column_id(0);
  hashed_column->mutable_value()->set_datatype(INT32);
  hashed_column->mutable_value()->set_int32_value(1);

  auto range_column = yql_writereq_pb.add_range_column_values();
  range_column->set_column_id(1);
  range_column->mutable_value()->set_datatype(INT32);
  range_column->mutable_value()->set_int32_value(2);

  // Add ttl to request.
  yql_writereq_pb.set_ttl(2000);

  // Write to docdb.
  YQLWriteOperation yql_write_op(yql_writereq_pb, schema, &yql_writeresp_pb);
  DocWriteBatch doc_write_batch(db);
  CHECK_OK(yql_write_op.Apply(&doc_write_batch, db, HybridTime()));

  WriteToRocksDB(doc_write_batch, db);

  // Read from rocksdb and verify ttl.
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(read_opts));
  iter->SeekToFirst();
  while(iter->Valid()) {
    MonoDelta ttl;
    auto value = iter->value();
    CHECK_OK(Value::DecodeTTL(&value, &ttl));
    EXPECT_EQ(2000, ttl.ToMilliseconds());
    iter->Next();
  }
}

}  // namespace docdb
}  // namespace yb
