// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
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
  doc_write_batch->SetPrimitive(
      DocPath::DocPathFromRedisKey(kv.key()),
      Value(PrimitiveValue(kv.value(0)), ttl), Timestamp::kMax);
  response_.set_code(RedisResponsePB_RedisStatusCode_OK);
  return Status::OK();
}

const RedisResponsePB& RedisWriteOperation::response() { return response_; }

Status RedisReadOperation::Execute(rocksdb::DB *rocksdb, Timestamp timestamp) {
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

// Populate dockey from YSQL key columns
static DocKey DocKeyFromYSQLKey(
      uint32_t hash_code,
      const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& hashed_column_values,
      const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& range_column_values) {
  std::vector<PrimitiveValue> hashed_components;
  std::vector<PrimitiveValue> range_components;
  for (const auto& column_value : hashed_column_values) {
    DCHECK(column_value.has_value()) << "Hashed column value missing for column id "
                                     << column_value.column_id();
    hashed_components.push_back(PrimitiveValue::FromYSQLValuePB(column_value.value()));
  }
  for (const auto& column_value : range_column_values) {
    DCHECK(column_value.has_value()) << "range column value missing for column id "
                                     << column_value.column_id();
    range_components.push_back(PrimitiveValue::FromYSQLValuePB(column_value.value()));
  }
  return DocKey(hash_code, hashed_components, range_components);
}

YSQLWriteOperation::YSQLWriteOperation(const YSQLWriteRequestPB& request)
    : doc_key_(
          DocKeyFromYSQLKey(
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
          doc_write_batch->SetPrimitive(sub_path, value);
        }
      } else {
        const auto value = Value(PrimitiveValue(ValueType::kObject), ttl);
        doc_write_batch->SetPrimitive(doc_path_, value);
      }
      break;
    }
    case YSQLWriteRequestPB::YSQL_STMT_DELETE: {
      // If non-key columns are specified, just the individual columns will be deleted. Otherwise,
      // the whole row is deleted.
      if (request_.column_values_size() > 0) {
        for (const auto& column_value : request_.column_values()) {
          const DocPath sub_path(doc_key_.Encode(), PrimitiveValue(column_value.column_id()));
          doc_write_batch->DeleteSubDoc(sub_path);
        }
      } else {
        doc_write_batch->DeleteSubDoc(doc_path_);
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

// A doc visitor to retrieve selected column values
class YSQLReadOperation::YSQLRowReader : public DocVisitor {

 public:
  explicit YSQLRowReader(YSQLRowBlock* rowblock) : rowblock_(rowblock) { }

  // Called once in the beginning of every new document.
  Status StartDocument(const DocKey& key) override {
    return Status::OK();
  };

  // Called in the end of a document.
  Status EndDocument() override {
    return Status::OK();
  }

  // VisitKey and VisitValue are called as part of enumerating key-value pairs in an object, e.g.
  // VisitKey(key1), VisitValue(value1), VisitKey(key2), VisitValue(value2), etc.
  Status VisitKey(const PrimitiveValue& key) override {
    DCHECK_EQ(key.value_type(), ValueType::kInt64);
    column_id_ = static_cast<int32_t>(key.GetInt64());
    return Status::OK();
  }

  Status VisitValue(const PrimitiveValue& value) override {
    const auto column_idx = row_->schema().find_column_by_id(ColumnId(column_id_));
    if (column_idx != Schema::kColumnNotFound) {
      PrimitiveValue::SetYSQLRowColumn(value, row_, column_idx);
    }
    return Status::OK();
  }

  // Called in the beginning of an object, before any key/value pairs.
  Status StartObject() override {
    level_++;
    // We expect only 1 level of object(row) since we are not supporting set, list, map and
    // abstract data type (ADT) yet.
    DCHECK_EQ(level_, 1);
    row_ = &rowblock_->Extend();
    return Status::OK();
  }

  // Called after all key/value pairs in an object.
  Status EndObject() override {
    level_--;
    return Status::OK();
  }

  // Called before enumerating elements of an array. Not used as of 9/26/2016.
  Status StartArray() override {
    return Status::OK();
  }

  // Called after enumerating elements of an array. Not used as of 9/26/2016.
  Status EndArray() override {
    return Status::OK();
  }

  bool found() const { return (row_ != nullptr); }

 private:
  int level_ = 0;
  int32_t column_id_ = 0;
  YSQLRowBlock* rowblock_;
  YSQLRow* row_ = nullptr;
};

YSQLReadOperation::YSQLReadOperation(const YSQLReadRequestPB& request) : request_(request) {
}

Status YSQLReadOperation::Execute(
    rocksdb::DB *rocksdb, Timestamp timestamp, YSQLRowBlock* rowblock) {

  // Populate dockey from YSQL key columns
  docdb::DocKeyHash hash_code = request_.hash_code();
  vector<PrimitiveValue> hashed_components;
  vector<PrimitiveValue> range_components;
  for (const auto& column_value : request_.hashed_column_values()) {
    PrimitiveValue value = PrimitiveValue::FromYSQLValuePB(column_value.value());
    hashed_components.push_back(value);
  }
  for (const auto& relation : request_.relations()) {
    if (relation.has_op() && relation.op() == YSQL_OP_EQUAL) {
      PrimitiveValue value = PrimitiveValue::FromYSQLValuePB(relation.value());
      range_components.push_back(value);
    } else {
      return STATUS(NotSupported, "Only equal relation operator is supported");
    }
  }
  DocKey doc_key(hash_code, hashed_components, range_components);
  DocPath doc_path(doc_key.Encode());

  // Scan docdb for the row
  YSQLRowReader row_reader(rowblock);
  RETURN_NOT_OK(ScanDocument(rocksdb, doc_path.encoded_doc_key(), &row_reader, timestamp));
  if (row_reader.found()) {
    auto& row = rowblock->rows().back();
    for (const auto& column_value : request_.hashed_column_values()) {
      const auto column_idx = row.schema().find_column_by_id(ColumnId(column_value.column_id()));
      if (column_idx != Schema::kColumnNotFound) {
        PrimitiveValue::SetYSQLRowColumn(
            PrimitiveValue::FromYSQLValuePB(column_value.value()), &row, column_idx);
      }
    }
    for (const auto& relation : request_.relations()) {
      const auto column_idx = row.schema().find_column_by_id(ColumnId(relation.range_column_id()));
      if (column_idx != Schema::kColumnNotFound) {
        PrimitiveValue::SetYSQLRowColumn(
            PrimitiveValue::FromYSQLValuePB(relation.value()), &row, column_idx);
      }
    }
  }

  return Status::OK();
}

const YSQLResponsePB& YSQLReadOperation::response() { return response_; }

}  // namespace docdb
}  // namespace yb
