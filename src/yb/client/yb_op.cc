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
#include "yb/common/ysql_protocol.pb.h"
#include "yb/common/ysql_rowblock.h"
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

// YBRedisReadOp -----------------------------------------------------------------

YBRedisReadOp::YBRedisReadOp(const shared_ptr<YBTable>& table)
    : YBOperation(table), redis_read_request_(new RedisReadRequestPB()) {
}

YBRedisReadOp::~YBRedisReadOp() {}

std::string YBRedisReadOp::ToString() const {
  return "REDIS_READ " + redis_read_request_->key_value().key();
}

const RedisResponsePB& YBRedisReadOp::response() const {
  // Cannot use CHECK or DCHECK here, or client_samples-test will fail.
  assert(redis_response_ != nullptr);
  return *redis_response_;
}

RedisResponsePB* YBRedisReadOp::mutable_response() {
  if (!redis_response_) {
    redis_response_.reset(new RedisResponsePB());
  }
  return redis_response_.get();
}

// YBSqlOp -----------------------------------------------------------------
YBSqlOp::YBSqlOp(const shared_ptr<YBTable>& table) : YBOperation(table) {
}

YBSqlOp::~YBSqlOp() {
}

// YBSqlWriteOp -----------------------------------------------------------------

YBSqlWriteOp::YBSqlWriteOp(const shared_ptr<YBTable>& table)
    : YBSqlOp(table),
      ysql_write_request_(new YSQLWriteRequestPB()),
      ysql_response_(new YSQLResponsePB()) {
}

YBSqlWriteOp::~YBSqlWriteOp() {}

static YBSqlWriteOp *NewYBSqlWriteOp(const shared_ptr<YBTable>& table,
                                     YSQLWriteRequestPB::YSQLStmtType stmt_type) {
  YBSqlWriteOp *op = new YBSqlWriteOp(table);
  YSQLWriteRequestPB *req = op->mutable_request();
  req->set_type(stmt_type);
  req->set_client(YSQL_CLIENT_CQL);
  req->set_request_id(reinterpret_cast<uint64_t>(op));

  // TODO(neil): When ALTER TABLE is supported, we'll need to set schema version of 'table'.
  VLOG(4) << "TODO: Schema version is not being used";
  req->set_schema_version(0);

  return op;
}

YBSqlWriteOp *YBSqlWriteOp::NewInsert(const std::shared_ptr<YBTable>& table) {
  return NewYBSqlWriteOp(table, YSQLWriteRequestPB::YSQL_STMT_INSERT);
}

YBSqlWriteOp *YBSqlWriteOp::NewUpdate(const std::shared_ptr<YBTable>& table) {
  return NewYBSqlWriteOp(table, YSQLWriteRequestPB::YSQL_STMT_UPDATE);
}

YBSqlWriteOp *YBSqlWriteOp::NewDelete(const std::shared_ptr<YBTable>& table) {
  return NewYBSqlWriteOp(table, YSQLWriteRequestPB::YSQL_STMT_DELETE);
}

std::string YBSqlWriteOp::ToString() const {
  return "YSQL_WRITE " + ysql_write_request_->DebugString();
}

namespace {

Status SetColumn(YBPartialRow* row, const int32 column_id, const YSQLValuePB& value) {
  const auto column_idx = row->schema()->find_column_by_id(ColumnId(column_id));
  CHECK_NE(column_idx, Schema::kColumnNotFound);
  CHECK(value.has_datatype());
  switch (value.datatype()) {
    case INT8:
      return value.has_int8_value() ?
          row->SetInt8(column_idx, static_cast<int8_t>(value.int8_value())) :
          row->SetNull(column_idx);
    case INT16:
      return value.has_int16_value() ?
          row->SetInt16(column_idx, static_cast<int16_t>(value.int32_value())) :
          row->SetNull(column_idx);
    case INT32:
      return value.has_int32_value() ?
          row->SetInt32(column_idx, value.int32_value()) : row->SetNull(column_idx);
    case INT64:
      return value.has_int64_value() ?
          row->SetInt64(column_idx, value.int64_value()) : row->SetNull(column_idx);
    case FLOAT:
      return value.has_float_value() ?
          row->SetFloat(column_idx, value.float_value()) : row->SetNull(column_idx);
    case DOUBLE:
      return value.has_double_value() ?
          row->SetDouble(column_idx, value.double_value()) : row->SetNull(column_idx);
    case STRING:
      return value.has_string_value() ?
          row->SetString(column_idx, Slice(value.string_value())) : row->SetNull(column_idx);
    case BOOL:
      return value.has_bool_value() ?
          row->SetBool(column_idx, value.bool_value()) : row->SetNull(column_idx);
    case TIMESTAMP:
      return value.has_timestamp_value() ?
          row->SetTimestamp(column_idx, value.timestamp_value()) : row->SetNull(column_idx);

    case UINT8:  FALLTHROUGH_INTENDED;
    case UINT16: FALLTHROUGH_INTENDED;
    case UINT32: FALLTHROUGH_INTENDED;
    case UINT64: FALLTHROUGH_INTENDED;
    case BINARY: FALLTHROUGH_INTENDED;
    case UNKNOWN_DATA:
      break;

    // default: fall through
  }

  LOG(ERROR) << "Internal error: unsupported datatype " << value.datatype();
  return STATUS(RuntimeError, "unsupported datatype");
}

Status SetPartialRowKey(
    YBPartialRow* row,
    const google::protobuf::RepeatedPtrField<YSQLColumnValuePB>& hashed_column_values) {
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

Status YBSqlWriteOp::SetKey() {
  return SetPartialRowKey(mutable_row(), ysql_write_request_->hashed_column_values());
}

void YBSqlWriteOp::SetHashCode(const uint16_t hash_code) {
  ysql_write_request_->set_hash_code(hash_code);
}

YSQLRowBlock* YBSqlWriteOp::GetRowBlock() const {
  Schema schema;
  CHECK_OK(ColumnPBsToSchema(ysql_response_->column_schemas(), &schema));
  unique_ptr<YSQLRowBlock> rowblock(new YSQLRowBlock(schema));
  Slice data(rows_data_);
  if (!data.empty()) {
    // TODO: a better way to handle errors here?
    CHECK_OK(rowblock->Deserialize(ysql_write_request_->client(), &data));
  }
  return rowblock.release();
}

// YBSqlReadOp -----------------------------------------------------------------

YBSqlReadOp::YBSqlReadOp(const shared_ptr<YBTable>& table)
    : YBSqlOp(table),
      ysql_read_request_(new YSQLReadRequestPB()),
      ysql_response_(new YSQLResponsePB()) {
}

YBSqlReadOp::~YBSqlReadOp() {}

YBSqlReadOp *YBSqlReadOp::NewSelect(const shared_ptr<YBTable>& table) {
  YBSqlReadOp *op = new YBSqlReadOp(table);
  YSQLReadRequestPB *req = op->mutable_request();
  req->set_client(YSQL_CLIENT_CQL);
  req->set_request_id(reinterpret_cast<uint64_t>(op));

  // TODO(neil): When ALTER TABLE is supported, we'll need to set schema version of 'table'.
  VLOG(4) << "TODO: Schema version is not being used";
  req->set_schema_version(0);

  return op;
}

std::string YBSqlReadOp::ToString() const {
  return "YSQL_READ " + ysql_read_request_->DebugString();
}

Status YBSqlReadOp::SetKey() {
  return SetPartialRowKey(mutable_row(), ysql_read_request_->hashed_column_values());
}

void YBSqlReadOp::SetHashCode(const uint16_t hash_code) {
  ysql_read_request_->set_hash_code(hash_code);
}

YSQLRowBlock* YBSqlReadOp::GetRowBlock() const {
  vector<ColumnId> column_ids;
  for (const auto column_id : ysql_read_request_->column_ids()) {
    column_ids.emplace_back(column_id);
  }
  unique_ptr<YSQLRowBlock> rowblock(new YSQLRowBlock(*table_->schema().schema_, column_ids));
  Slice data(rows_data_);
  if (!data.empty()) {
    // TODO: a better way to handle errors here?
    CHECK_OK(rowblock->Deserialize(ysql_read_request_->client(), &data));
  }
  return rowblock.release();
}

}  // namespace client
}  // namespace yb
