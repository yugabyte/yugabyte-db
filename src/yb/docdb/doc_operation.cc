// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/server/hybrid_clock.h"

namespace yb {
namespace docdb {

DocPath YSQLWriteOperation::DocPathToLock() const {
  return doc_path_;
}

Status YSQLWriteOperation::Apply(DocWriteBatch* doc_write_batch) {
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

}  // namespace docdb
}  // namespace yb
