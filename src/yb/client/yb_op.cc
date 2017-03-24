// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "yb/client/yb_op.h"

#include <assert.h>

#include "yb/client/client.h"
#include "yb/common/encoded_key.h"
#include "yb/common/row.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"
#include "yb/redisserver/redis_constants.h"

namespace yb {
namespace client {

using std::shared_ptr;
using std::unique_ptr;

RowOperationsPB_Type ToInternalWriteType(YBOperation::Type type) {
  switch (type) {
    case YBOperation::INSERT: return RowOperationsPB_Type_INSERT;
    case YBOperation::UPDATE: return RowOperationsPB_Type_UPDATE;
    case YBOperation::DELETE: return RowOperationsPB_Type_DELETE;
    default: LOG(FATAL) << "Unexpected write operation type: " << type;
  }
}

// WriteOperation --------------------------------------------------------------

YBOperation::YBOperation(const shared_ptr<YBTable>& table)
  : table_(table),
    row_(table->schema().schema_) {
}

YBOperation::~YBOperation() {}

Status YBOperation::SetKey(const string& string_key) {
  return mutable_row()->SetBinaryCopy(kRedisKeyColumnName, string_key);
}

Status YBOperation::GetPartitionKey(std::string *partition_key) const {
  return table_->partition_schema().EncodeKey(row_, partition_key);
}

int64_t YBOperation::SizeInBuffer() const {
  const Schema* schema = row_.schema();
  int size = 1; // for the operation type

  // Add size of isset bitmap (always present).
  size += BitmapSize(schema->num_columns());
  // Add size of null bitmap (present if the schema has nullables)
  size += ContiguousRowHelper::null_bitmap_size(*schema);
  // The column data itself:
  for (int i = 0; i < schema->num_columns(); i++) {
    if (row_.IsColumnSet(i) && !row_.IsNull(i)) {
      size += schema->column(i).type_info()->size();
      if (schema->column(i).type_info()->physical_type() == BINARY) {
        ContiguousRow row(schema, row_.row_data_);
        Slice bin;
        memcpy(&bin, row.cell_ptr(i), sizeof(bin));
        size += bin.size();
      }
    }
  }
  return size;
}

// Insert -----------------------------------------------------------------------

YBInsert::YBInsert(const shared_ptr<YBTable>& table) : YBOperation(table) {
}

YBInsert::~YBInsert() {
}

// Update -----------------------------------------------------------------------

YBUpdate::YBUpdate(const shared_ptr<YBTable>& table) : YBOperation(table) {
}

YBUpdate::~YBUpdate() {
}

// Delete -----------------------------------------------------------------------

YBDelete::YBDelete(const shared_ptr<YBTable>& table) : YBOperation(table) {
}

YBDelete::~YBDelete() {
}

// YBRedisWriteOp -----------------------------------------------------------------

YBRedisWriteOp::YBRedisWriteOp(const shared_ptr<YBTable>& table)
    : YBOperation(table), redis_write_request_(new RedisWriteRequestPB()) {
}

YBRedisWriteOp::~YBRedisWriteOp() {}

std::string YBRedisWriteOp::ToString() const {
  return "REDIS_WRITE " + redis_write_request_->key_value().key();
}

RedisResponsePB* YBRedisWriteOp::mutable_response() {
  if (!redis_response_) {
    redis_response_.reset(new RedisResponsePB());
  }
  return redis_response_.get();
}

void YBRedisWriteOp::SetHashCode(uint16_t hash_code) {
  redis_write_request_->mutable_key_value()->set_hash_code(hash_code);
}

// YBRedisReadOp -----------------------------------------------------------------

YBRedisReadOp::YBRedisReadOp(const shared_ptr<YBTable>& table)
    : YBOperation(table), redis_read_request_(new RedisReadRequestPB()) {
}

YBRedisReadOp::~YBRedisReadOp() {}

std::string YBRedisReadOp::ToString() const {
  return "REDIS_READ " + redis_read_request_->key_value().key();
}

const RedisResponsePB& YBRedisReadOp::response() const {
  assert(redis_response_ != nullptr);
  return *redis_response_;
}

RedisResponsePB* YBRedisReadOp::mutable_response() {
  if (!redis_response_) {
    redis_response_.reset(new RedisResponsePB());
  }
  return redis_response_.get();
}

void YBRedisReadOp::SetHashCode(uint16_t hash_code) {
  redis_read_request_->mutable_key_value()->set_hash_code(hash_code);
}

// YBqlOp -----------------------------------------------------------------
  YBqlOp::YBqlOp(const shared_ptr<YBTable>& table)
      : YBOperation(table) , yql_response_(new YQLResponsePB()) {
}

YBqlOp::~YBqlOp() {
}

// YBqlWriteOp -----------------------------------------------------------------

YBqlWriteOp::YBqlWriteOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table), yql_write_request_(new YQLWriteRequestPB()) {
}

YBqlWriteOp::~YBqlWriteOp() {}

static YBqlWriteOp *NewYBqlWriteOp(const shared_ptr<YBTable>& table,
                                   YQLWriteRequestPB::YQLStmtType stmt_type) {
  YBqlWriteOp *op = new YBqlWriteOp(table);
  YQLWriteRequestPB *req = op->mutable_request();
  req->set_type(stmt_type);
  req->set_client(YQL_CLIENT_CQL);
  req->set_request_id(reinterpret_cast<uint64_t>(op));

  // TODO(neil): When ALTER TABLE is supported, we'll need to set schema version of 'table'.
  VLOG(4) << "TODO: Schema version is not being used";
  req->set_schema_version(0);

  return op;
}

YBqlWriteOp *YBqlWriteOp::NewInsert(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, YQLWriteRequestPB::YQL_STMT_INSERT);
}

YBqlWriteOp *YBqlWriteOp::NewUpdate(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, YQLWriteRequestPB::YQL_STMT_UPDATE);
}

YBqlWriteOp *YBqlWriteOp::NewDelete(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, YQLWriteRequestPB::YQL_STMT_DELETE);
}

std::string YBqlWriteOp::ToString() const {
  return "YQL_WRITE " + yql_write_request_->DebugString();
}

namespace {

Status SetColumn(YBPartialRow* row, const int32 column_id, const YQLValuePB& value) {
  const auto column_idx = row->schema()->find_column_by_id(ColumnId(column_id));
  CHECK_NE(column_idx, Schema::kColumnNotFound);
  if (YQLValue::IsNull(value)) {
    return row->SetNull(column_idx);
  }
  const DataType data_type = row->schema()->column(column_idx).type_info()->type();
  switch (data_type) {
    case INT8:     return row->SetInt8(column_idx, YQLValue::int8_value(value));
    case INT16:    return row->SetInt16(column_idx, YQLValue::int16_value(value));
    case INT32:    return row->SetInt32(column_idx, YQLValue::int32_value(value));
    case INT64:    return row->SetInt64(column_idx, YQLValue::int64_value(value));
    case FLOAT:    return row->SetFloat(column_idx, YQLValue::float_value(value));
    case DOUBLE:   return row->SetDouble(column_idx, YQLValue::double_value(value));
    case STRING:   return row->SetString(column_idx, Slice(YQLValue::string_value(value)));
    case BOOL:     return row->SetBool(column_idx, YQLValue::bool_value(value));
    case TIMESTAMP:
      return row->SetTimestamp(column_idx, YQLValue::timestamp_value(value).ToInt64());

    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case DECIMAL: FALLTHROUGH_INTENDED;
    case VARINT: FALLTHROUGH_INTENDED;
    case INET: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case UUID: FALLTHROUGH_INTENDED;
    case TIMEUUID: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;

    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: unsupported datatype " << data_type;
  return STATUS(RuntimeError, "unsupported datatype");
}

Status SetPartialRowKey(
    YBPartialRow* row,
    const google::protobuf::RepeatedPtrField<YQLColumnValuePB>& hashed_column_values) {
  // Set the partition key in partial row from the hashed columns.
  for (const auto& column_value : hashed_column_values) {
    RETURN_NOT_OK(SetColumn(row, column_value.column_id(), column_value.value()));
  }
  // Verify that all hashed columns have been specified.
  const auto* schema = row->schema();
  for (size_t column_idx = 0; column_idx < schema->num_key_columns(); ++column_idx) {
    if (schema->column(column_idx).is_hash_key() && !row->IsColumnSet(column_idx)) {
      return STATUS(IllegalState, "Not all hash column values specified");
    }
  }
  return Status::OK();
}

} // namespace

Status YBqlWriteOp::SetKey() {
  return SetPartialRowKey(mutable_row(), yql_write_request_->hashed_column_values());
}

void YBqlWriteOp::SetHashCode(const uint16_t hash_code) {
  yql_write_request_->set_hash_code(hash_code);
}

// YBqlReadOp -----------------------------------------------------------------

YBqlReadOp::YBqlReadOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table), yql_read_request_(new YQLReadRequestPB()) {
}

YBqlReadOp::~YBqlReadOp() {}

YBqlReadOp *YBqlReadOp::NewSelect(const shared_ptr<YBTable>& table) {
  YBqlReadOp *op = new YBqlReadOp(table);
  YQLReadRequestPB *req = op->mutable_request();
  req->set_client(YQL_CLIENT_CQL);
  req->set_request_id(reinterpret_cast<uint64_t>(op));

  // TODO(neil): When ALTER TABLE is supported, we'll need to set schema version of 'table'.
  VLOG(4) << "TODO: Schema version is not being used";
  req->set_schema_version(0);

  return op;
}

std::string YBqlReadOp::ToString() const {
  return "YQL_READ " + yql_read_request_->DebugString();
}

Status YBqlReadOp::SetKey() {
  return SetPartialRowKey(mutable_row(), yql_read_request_->hashed_column_values());
}

void YBqlReadOp::SetHashCode(const uint16_t hash_code) {
  yql_read_request_->set_hash_code(hash_code);
}

Status YBqlReadOp::GetPartitionKey(string* partition_key) const {
  if (yql_read_request_->has_paging_state() &&
      yql_read_request_->paging_state().has_next_partition_key() &&
      !yql_read_request_->paging_state().next_partition_key().empty()) {
    *partition_key = yql_read_request_->paging_state().next_partition_key();
    return Status::OK();
  } else if (yql_read_request_->hashed_column_values().empty()) {
    partition_key->clear();
    return Status::OK();
  }
  return YBOperation::GetPartitionKey(partition_key);
}

}  // namespace client
}  // namespace yb
