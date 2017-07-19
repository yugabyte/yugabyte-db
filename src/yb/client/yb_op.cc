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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/yb_op.h"

#include <assert.h>

#include "yb/client/client.h"
#include "yb/common/encoded_key.h"
#include "yb/common/row.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
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
  return RowOperationsPB_Type_UNKNOWN;
}

// WriteOperation --------------------------------------------------------------

YBOperation::YBOperation(const shared_ptr<YBTable>& table)
  : table_(table),
    row_(&internal::GetSchema(table->schema())) {
}

YBOperation::~YBOperation() {}

Status YBOperation::SetKey(const Slice& string_key) {
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
      : YBOperation(table) , ql_response_(new QLResponsePB()) {
}

YBqlOp::~YBqlOp() {
}

// YBqlWriteOp -----------------------------------------------------------------

YBqlWriteOp::YBqlWriteOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table), ql_write_request_(new QLWriteRequestPB()) {
}

YBqlWriteOp::~YBqlWriteOp() {}

static YBqlWriteOp *NewYBqlWriteOp(const shared_ptr<YBTable>& table,
                                   QLWriteRequestPB::QLStmtType stmt_type) {
  YBqlWriteOp *op = new YBqlWriteOp(table);
  QLWriteRequestPB *req = op->mutable_request();
  req->set_type(stmt_type);
  req->set_client(YQL_CLIENT_CQL);
  // TODO: Request ID should be filled with CQL stream ID. Query ID should be replaced too.
  req->set_request_id(reinterpret_cast<uint64_t>(op));
  req->set_query_id(reinterpret_cast<int64_t>(op));

  req->set_schema_version(table->schema().version());

  return op;
}

YBqlWriteOp *YBqlWriteOp::NewInsert(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, QLWriteRequestPB::QL_STMT_INSERT);
}

YBqlWriteOp *YBqlWriteOp::NewUpdate(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, QLWriteRequestPB::QL_STMT_UPDATE);
}

YBqlWriteOp *YBqlWriteOp::NewDelete(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, QLWriteRequestPB::QL_STMT_DELETE);
}

std::string YBqlWriteOp::ToString() const {
  return "QL_WRITE " + ql_write_request_->ShortDebugString();
}

namespace {

Status SetColumn(YBPartialRow* row, const int32 column_id, const QLValuePB& value) {
  const auto column_idx = row->schema()->find_column_by_id(ColumnId(column_id));
  CHECK_NE(column_idx, Schema::kColumnNotFound);
  if (QLValue::IsNull(value)) {
    return row->SetNull(column_idx);
  }
  const DataType data_type = row->schema()->column(column_idx).type_info()->type();
  switch (data_type) {
    case INT8:     return row->SetInt8(column_idx, QLValue::int8_value(value));
    case INT16:    return row->SetInt16(column_idx, QLValue::int16_value(value));
    case INT32:    return row->SetInt32(column_idx, QLValue::int32_value(value));
    case INT64:    return row->SetInt64(column_idx, QLValue::int64_value(value));
    case FLOAT:    return row->SetFloat(column_idx, QLValue::float_value(value));
    case DOUBLE:   return row->SetDouble(column_idx, QLValue::double_value(value));
    case DECIMAL:  return row->SetDecimal(column_idx, QLValue::decimal_value(value));
    case STRING:   return row->SetString(column_idx, Slice(QLValue::string_value(value)));
    case BOOL:     return row->SetBool(column_idx, QLValue::bool_value(value));
    case TIMESTAMP:
      return row->SetTimestamp(column_idx, QLValue::timestamp_value(value).ToInt64());
    case INET: {
      string bytes;
      RETURN_NOT_OK(QLValue::inetaddress_value(value).ToBytes(&bytes));
      return row->SetInet(column_idx, Slice(bytes));
    }
    case UUID: {
      string bytes;
      RETURN_NOT_OK(QLValue::uuid_value(value).ToBytes(&bytes));
      return row->SetUuidCopy(column_idx, Slice(bytes));
    }
    case TIMEUUID: {
      string bytes;
      RETURN_NOT_OK(QLValue::timeuuid_value(value).ToBytes(&bytes));
      return row->SetTimeUuidCopy(column_idx, Slice(bytes));
    };

    case NULL_VALUE_TYPE: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case VARINT: FALLTHROUGH_INTENDED;
    case FROZEN: FALLTHROUGH_INTENDED;
    case LIST: FALLTHROUGH_INTENDED;
    case MAP: FALLTHROUGH_INTENDED;
    case SET: FALLTHROUGH_INTENDED;
    case TUPLE: FALLTHROUGH_INTENDED;
    case TYPEARGS: FALLTHROUGH_INTENDED;
    case USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DATE: FALLTHROUGH_INTENDED;
    case TIME: FALLTHROUGH_INTENDED;

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

Status SetColumn(YBPartialRow* row, const int32 column_id, const QLExpressionPB& ql_expr) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      return SetColumn(row, column_id, ql_expr.value());

    case QLExpressionPB::ExprCase::kBfcall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kTscall:
      LOG(FATAL) << "Builtin call is not yet supported";
      return Status::OK();

    case QLExpressionPB::ExprCase::kColumnId:
      return STATUS(RuntimeError, "unexpected column reference");

    case QLExpressionPB::ExprCase::kSubscriptedCol:
      return STATUS(RuntimeError, "unexpected subscripted-column reference");

    case QLExpressionPB::ExprCase::kCondition:
      return STATUS(RuntimeError, "unexpected relational expression");

    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      return STATUS(Corruption, "expression not set");
  }

  return STATUS(RuntimeError, "unexpected case");
}

Status SetPartialRowKey(
    YBPartialRow* row,
    const google::protobuf::RepeatedPtrField<QLColumnValuePB>& hashed_column_values) {
  // Set the partition key in partial row from the hashed columns.
  for (const auto& column_value : hashed_column_values) {
    RETURN_NOT_OK(SetColumn(row, column_value.column_id(), column_value.expr()));
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
  return SetPartialRowKey(mutable_row(), ql_write_request_->hashed_column_values());
}

void YBqlWriteOp::SetHashCode(const uint16_t hash_code) {
  ql_write_request_->set_hash_code(hash_code);
}

// YBqlReadOp -----------------------------------------------------------------

YBqlReadOp::YBqlReadOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table),
      ql_read_request_(new QLReadRequestPB()),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

YBqlReadOp::~YBqlReadOp() {}

YBqlReadOp *YBqlReadOp::NewSelect(const shared_ptr<YBTable>& table) {
  YBqlReadOp *op = new YBqlReadOp(table);
  QLReadRequestPB *req = op->mutable_request();
  req->set_client(YQL_CLIENT_CQL);
  // TODO: Request ID should be filled with CQL stream ID. Query ID should be replaced too.
  req->set_request_id(reinterpret_cast<uint64_t>(op));
  req->set_query_id(reinterpret_cast<int64_t>(op));

  req->set_schema_version(table->schema().version());

  return op;
}

std::string YBqlReadOp::ToString() const {
  return "QL_READ " + ql_read_request_->DebugString();
}

Status YBqlReadOp::SetKey() {
  return SetPartialRowKey(mutable_row(), ql_read_request_->hashed_column_values());
}

void YBqlReadOp::SetHashCode(const uint16_t hash_code) {
  ql_read_request_->set_hash_code(hash_code);
}

Status YBqlReadOp::GetPartitionKey(string* partition_key) const {
  // if this is a continued query use the partition key from the paging state
  if (ql_read_request_->has_paging_state() &&
      ql_read_request_->paging_state().has_next_partition_key() &&
      !ql_read_request_->paging_state().next_partition_key().empty()) {
    *partition_key = ql_read_request_->paging_state().next_partition_key();
    return Status::OK();
  }

  // otherwise, if hashed columns are set, use them to compute the exact key
  if (!ql_read_request_->hashed_column_values().empty()) {
    RETURN_NOT_OK(YBOperation::GetPartitionKey(partition_key));

    // make sure given key is not smaller than lower bound (if any)
    if (ql_read_request_->has_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->hash_code());
      auto lower_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key < lower_bound) *partition_key = std::move(lower_bound);
    }

    // make sure given key is not bigger than upper bound (if any)
    if (ql_read_request_->has_max_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->max_hash_code());
      auto upper_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key > upper_bound) *partition_key = std::move(upper_bound);
    }

    return Status::OK();
  }

  // otherwise, use request hash code if set (i.e. lower-bound from condition using "token")
  if (ql_read_request_->has_hash_code()) {
    uint16 hash_code = static_cast<uint16>(ql_read_request_->hash_code());
    *partition_key = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
    return Status::OK();
  }

  // default to empty key, this will start a scan from the beginning
  partition_key->clear();
  return Status::OK();
}

}  // namespace client
}  // namespace yb
