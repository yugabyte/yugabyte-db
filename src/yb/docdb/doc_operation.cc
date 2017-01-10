// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/ysql_scanspec.h"
#include "yb/server/hybrid_clock.h"

namespace yb {
namespace docdb {

DocPath KuduWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status KuduWriteOperation::Apply(DocWriteBatch* doc_write_batch) {
  return doc_write_batch->SetPrimitive(doc_path_, value_, Timestamp::kMax);
}

DocPath RedisWriteOperation::DocPathToLock() const {
  CHECK_EQ(request_.redis_op_type(), RedisWriteRequestPB_Type_SET)
      << "Currently only SET is supported";
  return DocPath::DocPathFromRedisKey(request_.set_request().key_value().key());
}

Status RedisWriteOperation::Apply(DocWriteBatch* doc_write_batch) {
  CHECK_EQ(request_.redis_op_type(), RedisWriteRequestPB_Type_SET)
      << "Currently only SET is supported";
  const auto kv = request_.set_request().key_value();
  CHECK_EQ(kv.value().size(), 1)
      << "Set operations are expected have exactly one value, found " << kv.value().size();
  const MonoDelta ttl = request_.set_request().has_ttl() ?
      MonoDelta::FromMicroseconds(request_.set_request().ttl()) : Value::kMaxTtl;
  RETURN_NOT_OK(
      doc_write_batch->SetPrimitive(
          DocPath::DocPathFromRedisKey(kv.key()),
          Value(PrimitiveValue(kv.value(0)), ttl), Timestamp::kMax));
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

const RedisResponsePB& RedisWriteOperation::response() { return response_; }

Status RedisReadOperation::Execute(rocksdb::DB *rocksdb, const Timestamp& timestamp) {
  CHECK_EQ(request_.redis_op_type(), RedisReadRequestPB_Type_GET)
      << "Currently only GET is supported";
  const KeyBytes doc_key = DocKey::FromRedisStringKey(
      request_.get_request().key_value().key()).Encode();
  KeyBytes timestamped_key = doc_key;
  timestamped_key.AppendValueType(ValueType::kTimestamp);
  timestamped_key.AppendTimestamp(timestamp);
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(rocksdb->NewIterator(read_opts));
  ROCKSDB_SEEK(iter.get(), timestamped_key.AsSlice());
  if (!iter->Valid()) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return Status::OK();
  }
  const rocksdb::Slice key = iter->key();
  rocksdb::Slice value = iter->value();
  if (!timestamped_key.OnlyDiffersByLastTimestampFrom(key)) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return Status::OK();
  }
  MonoDelta ttl;
  RETURN_NOT_OK(Value::DecodeTtl(&value, &ttl));
  if (!ttl.Equals(Value::kMaxTtl)) {
    SubDocKey sub_doc_key;
    RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(key));
    const Timestamp expiry =
        server::HybridClock::AddPhysicalTimeToTimestamp(sub_doc_key.timestamp(), ttl);
    if (timestamp.CompareTo(expiry) > 0) {
      response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
      return Status::OK();
    }
  }
  Value val;
  RETURN_NOT_OK(val.Decode(value));
  if (val.primitive_value().value_type() == ValueType::kTombstone) {
    response_.set_code(RedisResponsePB_RedisStatusCode_NOT_FOUND);
    return Status::OK();
  }
  if (val.primitive_value().value_type() != ValueType::kString) {
    response_.set_code(RedisResponsePB_RedisStatusCode_WRONG_TYPE);
    return Status::OK();
  }
  val.mutable_primitive_value()->SwapStringValue(response_.mutable_string_response());
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

const RedisResponsePB& RedisReadOperation::response() {
  return response_;
}

namespace {

// Add primary key column values to the component group. Verify that they are in the same order
// as in the table schema.
void YSQLColumnValuesToPrimitiveValues(
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& column_values,
    const Schema& schema, size_t column_idx, const size_t column_count,
    vector<PrimitiveValue>* components) {
  CHECK_EQ(column_values.size(), column_count) << "Primary key column count mismatch";
  for (const auto& column_value : column_values) {
    CHECK_EQ(schema.column_id(column_idx), column_value.column_id())
        << "Primary key column id mismatch";
    components->push_back(PrimitiveValue::FromYSQLValuePB(column_value.value()));
    column_idx++;
  }
}

// Populate dockey from YSQL key columns.
DocKey DocKeyFromYSQLKey(
    const Schema& schema,
    uint32_t hash_code,
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& hashed_column_values,
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& range_column_values) {
  vector<PrimitiveValue> hashed_components;
  vector<PrimitiveValue> range_components;

  // Populate the hashed and range components in the same order as they are in the table schema.
  YSQLColumnValuesToPrimitiveValues(
      hashed_column_values, schema, 0,
      schema.num_hash_key_columns(), &hashed_components);
  YSQLColumnValuesToPrimitiveValues(
      range_column_values, schema, schema.num_hash_key_columns(),
      schema.num_key_columns() - schema.num_hash_key_columns(), &range_components);

  return DocKey(hash_code, hashed_components, range_components);
}

} // namespace

YSQLWriteOperation::YSQLWriteOperation(const YSQLWriteRequestPB& request, const Schema& schema)
    : doc_key_(
          DocKeyFromYSQLKey(
              schema,
              request.hash_code(),
              request.hashed_column_values(),
              request.range_column_values())),
      doc_path_(DocPath(doc_key_.Encode())),
      request_(request) {
}

DocPath YSQLWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status YSQLWriteOperation::Apply(DocWriteBatch* doc_write_batch) {

  if (request_.has_if_()) {
    return STATUS(NotSupported, "IF condition not supported yet");
  }

  const MonoDelta ttl = request_.has_ttl() ?
      MonoDelta::FromMicroseconds(request_.ttl()) : Value::kMaxTtl;

  switch (request_.type()) {
    // YSQL insert == update (upsert) to be consistent with Cassandra's semantics. In either INSERT
    // or UPDATE, if non-key columns are specified, they will be inserted which will cause the
    // primary key to be inserted also when necessary. Otherwise, we should insert the primary key
    // at least.
    case YSQLWriteRequestPB::YSQL_STMT_INSERT:
    case YSQLWriteRequestPB::YSQL_STMT_UPDATE: {
      if (request_.column_values_size() > 0) {
        for (const auto& column_value : request_.column_values()) {
          const DocPath sub_path(doc_key_.Encode(), PrimitiveValue(column_value.column_id()));
          const auto value = Value(PrimitiveValue::FromYSQLValuePB(column_value.value()), ttl);
          RETURN_NOT_OK(doc_write_batch->SetPrimitive(sub_path, value));
        }
      } else {
        const auto value = Value(PrimitiveValue(ValueType::kObject), ttl);
        RETURN_NOT_OK(doc_write_batch->SetPrimitive(doc_path_, value));
      }
      break;
    }
    case YSQLWriteRequestPB::YSQL_STMT_DELETE: {
      // If non-key columns are specified, just the individual columns will be deleted. Otherwise,
      // the whole row is deleted.
      if (request_.column_values_size() > 0) {
        for (const auto& column_value : request_.column_values()) {
          const DocPath sub_path(doc_key_.Encode(), PrimitiveValue(column_value.column_id()));
          RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(sub_path));
        }
      } else {
        RETURN_NOT_OK(doc_write_batch->DeleteSubDoc(doc_path_));
      }
      break;
    }
  }

  // In all cases, something should be written so the write batch shouldn't be empty.
  CHECK(!doc_write_batch->IsEmpty()) << "Empty write batch " << request_.type();

  response_.set_status(YSQLResponsePB::YSQL_STATUS_OK);

  return Status::OK();
}

const YSQLResponsePB& YSQLWriteOperation::response() { return response_; }

YSQLReadOperation::YSQLReadOperation(const YSQLReadRequestPB& request) : request_(request) {
}

Status YSQLReadOperation::Execute(
    rocksdb::DB *rocksdb, const Timestamp& timestamp, const Schema& schema,
    YSQLRowBlock* rowblock) {

  if (request_.has_order_by_column_id()) {
    return STATUS(RuntimeError, "order by query not supported yet");
  }

  if (request_.has_limit() && request_.limit() == 0) {
    return Status::OK();
  }

  // Populate dockey from YSQL key columns.
  docdb::DocKeyHash hash_code = request_.hash_code();
  vector<PrimitiveValue> hashed_components;
  YSQLColumnValuesToPrimitiveValues(
      request_.hashed_column_values(), schema, 0,
      schema.num_hash_key_columns(), &hashed_components);

  // Construct the scan spec basing on the WHERE condition.
  YSQLScanSpec spec(
      schema, hash_code, hashed_components,
      request_.has_condition() ? &request_.condition() : nullptr,
      request_.has_limit() ? request_.limit() : std::numeric_limits<std::size_t>::max());

  // Find the non-key columns selected by the row block plus any referenced in the WHERE condition.
  // When DocRowwiseIterator::NextBlock() populates a YSQLRowBlock, it uses this projection only to
  // scan sub-documents. YSQLRowBlock's own projection schema is used to pupulate the row block,
  // including key columns if any.
  std::unordered_set<ColumnId> non_key_columns;
  for (size_t idx = 0; idx < rowblock->schema().num_columns(); idx++) {
    const auto column_id = rowblock->schema().column_id(idx);
    if (!schema.is_key_column(schema.find_column_by_id(column_id))) {
      non_key_columns.insert(column_id);
    }
  }
  if (spec.non_key_columns() != nullptr) {
    non_key_columns.insert(spec.non_key_columns()->begin(), spec.non_key_columns()->end());
  }

  Schema projection;
  RETURN_NOT_OK(
      schema.CreateProjectionByIdsIgnoreMissing(
          vector<ColumnId>(non_key_columns.begin(), non_key_columns.end()), &projection));

  // Scan docdb for the row.
  DocRowwiseIterator iterator(projection, schema, rocksdb, timestamp);
  RETURN_NOT_OK(iterator.Init(spec));
  while (iterator.HasNext()) {
    RETURN_NOT_OK(iterator.NextBlock(spec, rowblock));
  }

  return Status::OK();
}

const YSQLResponsePB& YSQLReadOperation::response() { return response_; }

}  // namespace docdb
}  // namespace yb
