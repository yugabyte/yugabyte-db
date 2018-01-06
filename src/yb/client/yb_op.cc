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

#include "yb/client/client.h"
#include "yb/common/row.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/yql/redis/redisserver/redis_constants.h"

namespace yb {
namespace client {

using std::shared_ptr;
using std::unique_ptr;

// YBOperation --------------------------------------------------------------

YBOperation::YBOperation(const shared_ptr<YBTable>& table)
  : table_(table) {
}

YBOperation::~YBOperation() {}

// YBRedisOp ----------------------------------------------------------------------

YBRedisOp::YBRedisOp(const shared_ptr<YBTable>& table)
    : YBOperation(table) {
}

YBRedisOp::~YBRedisOp() {}

RedisResponsePB* YBRedisOp::mutable_response() {
  if (!redis_response_) {
    redis_response_.reset(new RedisResponsePB());
  }
  return redis_response_.get();
}


const RedisResponsePB& YBRedisOp::response() const {
  return *DCHECK_NOTNULL(redis_response_.get());
}

// YBRedisWriteOp -----------------------------------------------------------------

YBRedisWriteOp::YBRedisWriteOp(const shared_ptr<YBTable>& table)
    : YBRedisOp(table), redis_write_request_(new RedisWriteRequestPB()) {
}

YBRedisWriteOp::~YBRedisWriteOp() {}

std::string YBRedisWriteOp::ToString() const {
  return "REDIS_WRITE " + redis_write_request_->key_value().key();
}

void YBRedisWriteOp::SetHashCode(uint16_t hash_code) {
  redis_write_request_->mutable_key_value()->set_hash_code(hash_code);
}

const std::string& YBRedisWriteOp::GetKey() const {
  return redis_write_request_->key_value().key();
}

Status YBRedisWriteOp::GetPartitionKey(std::string *partition_key) const {
  const Slice& slice(redis_write_request_->key_value().key());
  return table_->partition_schema().EncodeRedisKey(slice, partition_key);
}

// YBRedisReadOp -----------------------------------------------------------------

YBRedisReadOp::YBRedisReadOp(const shared_ptr<YBTable>& table)
    : YBRedisOp(table), redis_read_request_(new RedisReadRequestPB()) {
}

YBRedisReadOp::~YBRedisReadOp() {}

std::string YBRedisReadOp::ToString() const {
  return "REDIS_READ " + redis_read_request_->key_value().key();
}

void YBRedisReadOp::SetHashCode(uint16_t hash_code) {
  redis_read_request_->mutable_key_value()->set_hash_code(hash_code);
}

const std::string& YBRedisReadOp::GetKey() const {
  return redis_read_request_->key_value().key();
}

Status YBRedisReadOp::GetPartitionKey(std::string *partition_key) const {
  const Slice& slice(redis_read_request_->key_value().key());
  return table_->partition_schema().EncodeRedisKey(slice, partition_key);
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

Status YBqlWriteOp::GetPartitionKey(string* partition_key) const {
  return table_->partition_schema().EncodeKey(ql_write_request_->hashed_column_values(),
                                              partition_key);
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

void YBqlReadOp::SetHashCode(const uint16_t hash_code) {
  ql_read_request_->set_hash_code(hash_code);
}

Status YBqlReadOp::GetPartitionKey(string* partition_key) const {
  if (!ql_read_request_->hashed_column_values().empty()) {
    // If hashed columns are set, use them to compute the exact key and set the bounds
    RETURN_NOT_OK(table_->partition_schema().EncodeKey(ql_read_request_->hashed_column_values(),
        partition_key));

    // TODO: If user specified token range doesn't contain the hash columns specified then the query
    // will have no effect. We need to implement an exit path rather than requesting the tablets.
    // For now, we set point query some value that is not equal to the hash to the hash columns
    // Which will return no result.

    // Make sure given key is not smaller than lower bound (if any)
    if (ql_read_request_->has_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->hash_code());
      auto lower_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key < lower_bound) *partition_key = std::move(lower_bound);
    }

    // Make sure given key is not bigger than upper bound (if any)
    if (ql_read_request_->has_max_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->max_hash_code());
      auto upper_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key > upper_bound) *partition_key = std::move(upper_bound);
    }

    // Set both bounds to equal partition key now, because this is a point get
    ql_read_request_->set_hash_code(
          PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
    ql_read_request_->set_max_hash_code(
          PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
  } else {
    // Otherwise, set the partition key to the hash_code (lower bound of the token range).
    if (ql_read_request_->has_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->hash_code());
      *partition_key = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
    } else {
      // Default to empty key, this will start a scan from the beginning.
      partition_key->clear();
    }
  }

  // If this is a continued query use the partition key from the paging state
  // If paging state is there, set hash_code = paging state. This is only supported for forward
  // scans.
  if (ql_read_request_->has_paging_state() &&
      ql_read_request_->paging_state().has_next_partition_key() &&
      !ql_read_request_->paging_state().next_partition_key().empty()) {
    *partition_key = ql_read_request_->paging_state().next_partition_key();
    ql_read_request_->set_hash_code(
        PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
  }

  return Status::OK();
}

std::vector<ColumnSchema> MakeColumnSchemasFromColDesc(
  const google::protobuf::RepeatedPtrField<QLRSColDescPB>& rscol_descs) {
  std::vector<ColumnSchema> column_schemas;
  column_schemas.reserve(rscol_descs.size());
  for (const auto& rscol_desc : rscol_descs) {
    column_schemas.emplace_back(rscol_desc.name(), QLType::FromQLTypePB(rscol_desc.ql_type()));
  }
  return column_schemas;
}

std::vector<ColumnSchema> YBqlReadOp::MakeColumnSchemasFromRequest() const {
  // Tests don't have access to the QL internal statement object, so they have to use rsrow
  // descriptor from the read request.
  return MakeColumnSchemasFromColDesc(request().rsrow_desc().rscol_descs());
}

Result<QLRowBlock> YBqlReadOp::MakeRowBlock() const {
  Schema schema(MakeColumnSchemasFromRequest(), 0);
  QLRowBlock result(schema);
  Slice data(rows_data_);
  if (!data.empty()) {
    RETURN_NOT_OK(result.Deserialize(request().client(), &data));
  }
  return result;
}

}  // namespace client
}  // namespace yb
