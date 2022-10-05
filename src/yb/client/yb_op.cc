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
#include "yb/client/client-internal.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/primitive_value_util.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DEFINE_bool(redis_allow_reads_from_followers, false,
            "If true, the read will be served from the closest replica in the same AZ, which can "
            "be a follower.");
TAG_FLAG(redis_allow_reads_from_followers, evolving);
TAG_FLAG(redis_allow_reads_from_followers, runtime);

namespace yb {
namespace client {

using std::shared_ptr;
using std::unique_ptr;

namespace {

void SetPartitionKey(const Slice& value, LWPgsqlReadRequestPB* request) {
  request->dup_partition_key(value);
}

void SetPartitionKey(const Slice& value, PgsqlReadRequestPB* request) {
  request->set_partition_key(value.cdata(), value.size());
}

void SetPartitionKey(const Slice& value, LWPgsqlWriteRequestPB* request) {
  request->dup_partition_key(value);
}

void SetPartitionKey(const Slice& value, PgsqlWriteRequestPB* request) {
  request->set_partition_key(value.cdata(), value.size());
}

void SetKey(const Slice& value, LWPgsqlPartitionBound* bound) {
  bound->dup_key(value);
}

void SetKey(const Slice& value, PgsqlPartitionBound* bound) {
  bound->set_key(value.cdata(), value.size());
}

template<class Req>
Status InitHashPartitionKey(
    const Schema& schema, const PartitionSchema& partition_schema, Req* request) {
  // Seek a specific partition_key from read_request.
  // 1. Not specified hash condition - Full scan.
  // 2. paging_state -- Set by server to continue current request.
  // 3. hash column values -- Given to scan ONE SET of specific hash values.
  // 4. lower and upper bound -- Set by PgGate to query a specific set of hash values.
  // 5. range and regular condition - These are filter expression and will be processed by DocDB.
  //    Shouldn't we able to set RANGE boundary here?

  // If primary index lookup using ybctid requests are batched, there is a possibility that tablets
  // might get split after the batch of requests have been prepared. Hence, we need to execute the
  // prepared request in both tablet partitions. For this purpose, we use paging state to continue
  // executing the request in the second sub-partition after completing the first sub-partition.
  //
  // batched ybctids
  // In order to represent a single ybctid or a batch of ybctids, we leverage the lower bound and
  // upper bounds to set hash codes and max hash codes.

  bool has_paging_state =
      request->has_paging_state() && request->paging_state().has_next_partition_key();
  if (has_paging_state) {
    // If this is a subsequent query, use the partition key from the paging state. This is only
    // supported for forward scan.
    SetPartitionKey(request->paging_state().next_partition_key(), request);

    // Check that the paging state hash_code is within [ hash_code, max_hash_code ] bounds.
    if (schema.num_hash_key_columns() > 0 && !request->partition_key().empty()) {
      uint16 paging_state_hash_code = PartitionSchema::DecodeMultiColumnHashValue(
          request->partition_key());
      if ((request->has_hash_code() && paging_state_hash_code < request->hash_code()) ||
          (request->has_max_hash_code() && paging_state_hash_code > request->max_hash_code())) {
        return STATUS_SUBSTITUTE(
            InternalError,
            "Out of bounds partition key found in paging state:"
            "Query's partition bounds: [$0, $1], paging state partition: $2",
            request->has_hash_code() ? request->hash_code() : 0,
            request->has_max_hash_code() ? request->max_hash_code() : 0,
            paging_state_hash_code);
      }
      request->set_hash_code(paging_state_hash_code);
    }
  } else if (!request->partition_column_values().empty()) {
    // If hashed columns are set, use them to compute the exact key and set the bounds
    std::string temp;
    RETURN_NOT_OK(partition_schema.EncodePgsqlKey(request->partition_column_values(), &temp));
    SetPartitionKey(temp, request);

    // Make sure given key is not smaller than lower bound (if any)
    if (request->has_hash_code()) {
      auto hash_code = static_cast<uint16>(request->hash_code());
      auto lower_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (request->partition_key() < lower_bound) {
        SetPartitionKey(std::move(lower_bound), request);
      }
    }

    // Make sure given key is not bigger than upper bound (if any)
    if (request->has_max_hash_code()) {
      auto hash_code = static_cast<uint16>(request->max_hash_code());
      auto upper_bound = PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (request->partition_key() > upper_bound) {
        SetPartitionKey(std::move(upper_bound), request);
      }
    }

    if (!request->partition_key().empty()) {
      // If one specific partition_key is found, set both bounds to equal partition key now because
      // this is a point get.
      auto hash_code = PartitionSchema::DecodeMultiColumnHashValue(request->partition_key());
      request->set_hash_code(hash_code);
      request->set_max_hash_code(hash_code);
    }

  } else if (request->has_lower_bound() || request->has_upper_bound()) {
    // Batched requests contain the combination of lower and upper bounds (except during single
    // tablet scenario). For example, ybctids are always prepared as batched requests grouping them
    // into their respective tablets that they belong to.
    // Here there are two components that are of importance.
    // 1. partition key -- We start scanning from the tablet containing partition key.
    // 2. upper bound -- We end scanning when we reach the upper bound.
    // During automatic tablet splitting, batched request could be prepared before the tablets
    // are split. In that case, if docDB's scanning iterator that starts scanning from the
    // partition key does not reach the end of the tablet (upper bound), it will throw a paging
    // state and continue from there.

    // If the read request does not provide a specific partition key, but it does provide scan
    // boundary, use the given boundary to setup the scan lower and upper bound.
    if (request->has_lower_bound()) {
      auto hash = PartitionSchema::DecodeMultiColumnHashValue(request->lower_bound().key());
      if (!request->lower_bound().is_inclusive()) {
        ++hash;
      }
      request->set_hash_code(hash);

    }
    if (request->has_upper_bound()) {
      auto hash = PartitionSchema::DecodeMultiColumnHashValue(request->upper_bound().key());
      if (!request->upper_bound().is_inclusive()) {
        --hash;
      }
      request->set_max_hash_code(hash);
    }
    // Set partition key to lower bound. If lower bound is empty, then it is set to 0 which is the
    // first potential hash_key from which table entries start. Lower bounds are empty for the
    // first tablet or if it is a single tablet scenario.
    SetPartitionKey(request->lower_bound().key(), request);
  } else {
    // Full scan. Default to empty key.
    request->clear_partition_key();
  }

  return Status::OK();
}

template <class Req>
Status SetRangePartitionBounds(const Schema& schema,
                                       const std::string& last_partition,
                                       Req* request,
                                       std::string* key_upper_bound) {
  vector<docdb::KeyEntryValue> range_components, range_components_end;
  RETURN_NOT_OK(GetRangePartitionBounds(
      schema, *request, &range_components, &range_components_end));
  if (range_components.empty() && range_components_end.empty()) {
    if (request->is_forward_scan()) {
      request->clear_partition_key();
    } else {
      // In case of backward scan process must be start from the last partition.
      SetPartitionKey(last_partition, request);
    }
    key_upper_bound->clear();
    return Status::OK();
  }
  auto upper_bound_key = docdb::DocKey(std::move(range_components_end)).Encode().ToStringBuffer();
  if (request->is_forward_scan()) {
    SetPartitionKey(docdb::DocKey(std::move(range_components)).Encode().AsSlice(), request);
    *key_upper_bound = std::move(upper_bound_key);
  } else {
    // Backward scan should go from upper bound to lower. But because DocDB can check upper bound
    // only it is not set here. Lower bound will be checked on client side in the
    // ReviewResponsePagingState function.
    SetPartitionKey(upper_bound_key, request);
    key_upper_bound->clear();
  }
  return Status::OK();
}

template<class Req>
Status InitRangePartitionKey(
    const Schema& schema, const std::string& last_partition, Req* request) {
  // Seek a specific partition_key from read_request.
  // 1. Not specified range condition - Full scan.
  // 2. paging_state -- Set by server to continue the same request.
  // 3. upper and lower bound -- Set by PgGate to fetch rows within a boundary.
  // 4. range column values -- Given to fetch rows for one set of specific range values.
  // 5. condition expr -- Given to fetch rows that satisfy specific conditions.

  if (request->has_paging_state() &&
      request->paging_state().has_next_partition_key()) {
    // If this is a subsequent query, use the partition key from the paging state.
    SetPartitionKey(request->paging_state().next_partition_key(), request);
  } else if (request->has_lower_bound()) {
      // There are two cases here.
      // Case 1: batching ybctids
      // In this situation, requests belonging to the same tablets are batched. Here there are two
      // components that are of importance.
      // partition key --  We start scanning from the tablet containing partition key.
      // upper bound -- We end scanning when we reach the upper bound.
      // During automatic tablet splitting, batched request could be prepared before the tablets
      // are split. In that case, if docDB's scanning iterator that starts scanning from the
      // partition key does not reach the end of the tablet (upper bound), it will throw a paging
      // state and continue from there.
      //
      // Case 2: Range expression optimization
      //
      // When PgGate optimizes RANGE expressions, it will set lower_bound and upper_bound by itself.
      // In that case, we use them without recompute them here.
      //
      // NOTE: Currently, PgGate uses this optimization ONLY for COUNT operator and backfill
      // requests. It has not done any optimization on RANGE values yet.
      SetPartitionKey(request->lower_bound().key(), request);
  } else {
    // Evaluate condition to return partition_key and set the upper bound.
    string max_key;
    RETURN_NOT_OK(SetRangePartitionBounds(schema, last_partition, request, &max_key));
    if (!max_key.empty()) {
      SetKey(max_key, request->mutable_upper_bound());
      request->mutable_upper_bound()->set_is_inclusive(true);
    }
  }

  return Status::OK();
}

template <class Col>
Result<std::vector<docdb::KeyEntryValue>> GetRangeComponents(
    const Schema& schema, const Col& range_cols, const bool lower_bound) {
  size_t column_idx = 0;
  auto range_cols_it = range_cols.begin();
  const auto num_range_key_columns = schema.num_range_key_columns();
  std::vector<docdb::KeyEntryValue> result;
  for (const auto& col_id : schema.column_ids()) {
    if (!schema.is_range_column(col_id)) {
      continue;
    }

    const ColumnSchema& column_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    if (schema.table_properties().partitioning_version() > 0) {
      if (column_idx < static_cast<size_t>(range_cols.size())) {
        result.push_back(docdb::KeyEntryValue::FromQLValuePBForKey(
            range_cols_it->value(), column_schema.sorting_type()));
      } else {
        result.emplace_back(
            lower_bound ? docdb::KeyEntryType::kLowest : docdb::KeyEntryType::kHighest);
      }
    } else {
      if (column_idx >= static_cast<size_t>(range_cols.size()) ||
          range_cols_it->value().value_case() == QLValuePB::VALUE_NOT_SET) {
        result.emplace_back(
            lower_bound ? docdb::KeyEntryType::kLowest : docdb::KeyEntryType::kHighest);
      } else {
        result.push_back(docdb::KeyEntryValue::FromQLValuePB(
            range_cols_it->value(), column_schema.sorting_type()));
      }
    }

    ++range_cols_it;
    if (++column_idx == num_range_key_columns) {
      break;
    }
  }

  if (!lower_bound) {
    result.emplace_back(docdb::KeyEntryType::kHighest);
  }
  return result;
}

template <class Col>
Result<std::string> GetRangePartitionKey(
    const Schema& schema, const Col& range_cols) {
  RSTATUS_DCHECK(!schema.num_hash_key_columns(), IllegalState,
      "Cannot get range partition key for hash partitioned table");

  auto range_components = VERIFY_RESULT(GetRangeComponents(schema, range_cols, true));
  return docdb::DocKey(std::move(range_components)).Encode().ToStringBuffer();
}

template<class Req>
Status InitReadPartitionKey(
    const Schema& schema, const PartitionSchema& partition_schema,
    const std::string& last_partition, Req* request) {
  if (schema.num_hash_key_columns() > 0) {
    return InitHashPartitionKey(schema, partition_schema, request);
  }

  return InitRangePartitionKey(schema, last_partition, request);
}

template<class Req>
Status InitWritePartitionKey(
    const Schema& schema, const PartitionSchema& partition_schema, Req* request) {
  const auto& ybctid = request->ybctid_column_value().value();
  if (schema.num_hash_key_columns() > 0) {
    if (!IsNull(ybctid)) {
      const uint16 hash_code = VERIFY_RESULT(docdb::DocKey::DecodeHash(ybctid.binary_value()));
      request->set_hash_code(hash_code);
      SetPartitionKey(PartitionSchema::EncodeMultiColumnHashValue(hash_code), request);
      return Status::OK();
    }

    // Computing the partition_key.
    std::string temp;
    RETURN_NOT_OK(partition_schema.EncodePgsqlKey(request->partition_column_values(), &temp));
    SetPartitionKey(temp, request);
    return Status::OK();
  } else {
    // Range partitioned table
    if (!IsNull(ybctid)) {
      SetPartitionKey(ybctid.binary_value(), request);
      return Status::OK();
    }

    // Computing the range key.
    SetPartitionKey(
        VERIFY_RESULT(GetRangePartitionKey(schema, request->range_column_values())),
        request);
    return Status::OK();
  }
}

template <class Req>
Status DoGetRangePartitionBounds(const Schema& schema,
                                 const Req& request,
                                 vector<docdb::KeyEntryValue>* lower_bound,
                                 vector<docdb::KeyEntryValue>* upper_bound) {
  SCHECK(!schema.num_hash_key_columns(), IllegalState,
         "Cannot set range partition key for hash partitioned table");
  const auto& range_cols = request.range_column_values();
  const auto& condition_expr = request.condition_expr();
  if (condition_expr.has_condition() &&
      implicit_cast<size_t>(range_cols.size()) < schema.num_range_key_columns()) {
    auto prefixed_range_components = VERIFY_RESULT(docdb::InitKeyColumnPrimitiveValues(
        range_cols, schema, schema.num_hash_key_columns()));
    QLScanRange scan_range(schema, condition_expr.condition());
    *lower_bound = docdb::GetRangeKeyScanSpec(
        schema, &prefixed_range_components, &scan_range, nullptr, true /* lower_bound */);
    *upper_bound = docdb::GetRangeKeyScanSpec(
        schema, &prefixed_range_components, &scan_range, nullptr, false /* upper_bound */);
  } else if (!range_cols.empty()) {
    *lower_bound = VERIFY_RESULT(GetRangeComponents(schema, range_cols, true));
    *upper_bound = VERIFY_RESULT(GetRangeComponents(schema, range_cols, false));
  }
  return Status::OK();
}

} // namespace

//--------------------------------------------------------------------------------------------------
// YBOperation
//--------------------------------------------------------------------------------------------------

YBOperation::YBOperation(const shared_ptr<YBTable>& table)
  : table_(table) {
}

YBOperation::~YBOperation() {}

void YBOperation::SetTablet(const scoped_refptr<internal::RemoteTablet>& tablet) {
  tablet_ = tablet;
}

void YBOperation::ResetTablet() {
  tablet_.reset();
}

void YBOperation::ResetTable(std::shared_ptr<YBTable> new_table) {
  table_.reset();
  table_ = new_table;
  // tablet_ can no longer be valid.
  tablet_.reset();
}

bool YBOperation::IsYsqlCatalogOp() const {
  return table_->schema().table_properties().is_ysql_catalog_table();
}

void YBOperation::MarkTablePartitionListAsStale() {
  table_->MarkPartitionsAsStale();
}

//--------------------------------------------------------------------------------------------------
// YBRedisOp
//--------------------------------------------------------------------------------------------------

YBRedisOp::YBRedisOp(const shared_ptr<YBTable>& table)
    : YBOperation(table) {
}

RedisResponsePB* YBRedisOp::mutable_response() {
  if (!redis_response_) {
    redis_response_.reset(new RedisResponsePB());
  }
  return redis_response_.get();
}

const RedisResponsePB& YBRedisOp::response() const {
  return *DCHECK_NOTNULL(redis_response_.get());
}

OpGroup YBRedisReadOp::group() {
  return FLAGS_redis_allow_reads_from_followers ? OpGroup::kConsistentPrefixRead
                                                : OpGroup::kLeaderRead;
}

// YBRedisWriteOp -----------------------------------------------------------------

YBRedisWriteOp::YBRedisWriteOp(const shared_ptr<YBTable>& table)
    : YBRedisOp(table), redis_write_request_(new RedisWriteRequestPB()) {
}

size_t YBRedisWriteOp::space_used_by_request() const {
  return redis_write_request_->ByteSizeLong();
}

std::string YBRedisWriteOp::ToString() const {
  return "REDIS_WRITE " + redis_write_request_->key_value().key();
}

void YBRedisWriteOp::SetHashCode(uint16_t hash_code) {
  hash_code_ = hash_code;
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

size_t YBRedisReadOp::space_used_by_request() const {
  return redis_read_request_->SpaceUsedLong();
}

std::string YBRedisReadOp::ToString() const {
  return "REDIS_READ " + redis_read_request_->key_value().key();
}

void YBRedisReadOp::SetHashCode(uint16_t hash_code) {
  hash_code_ = hash_code;
  redis_read_request_->mutable_key_value()->set_hash_code(hash_code);
}

const std::string& YBRedisReadOp::GetKey() const {
  return redis_read_request_->key_value().key();
}

Status YBRedisReadOp::GetPartitionKey(std::string *partition_key) const {
  if (!redis_read_request_->key_value().has_key()) {
    *partition_key =
        PartitionSchema::EncodeMultiColumnHashValue(redis_read_request_->key_value().hash_code());
    return Status::OK();
  }
  const Slice& slice(redis_read_request_->key_value().key());
  return table_->partition_schema().EncodeRedisKey(slice, partition_key);
}

//--------------------------------------------------------------------------------------------------
// YBCql Operators
// - These ops should be prefixed with YBCql instead of YBql.
// - The prefixes "ql" or "QL" are used for common entities of all languages and not just CQL.
// - The name will be clean up later.
//--------------------------------------------------------------------------------------------------

YBqlOp::YBqlOp(const shared_ptr<YBTable>& table)
      : YBOperation(table) , ql_response_(new QLResponsePB()) {
}

YBqlOp::~YBqlOp() {
}

bool YBqlOp::succeeded() const {
  return response().status() == QLResponsePB::YQL_STATUS_OK;
}

// YBqlWriteOp -----------------------------------------------------------------

YBqlWriteOp::YBqlWriteOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table), ql_write_request_(new QLWriteRequestPB()) {
}

YBqlWriteOp::~YBqlWriteOp() {}

static std::unique_ptr<YBqlWriteOp> NewYBqlWriteOp(const shared_ptr<YBTable>& table,
                                                   QLWriteRequestPB::QLStmtType stmt_type) {
  auto op = std::unique_ptr<YBqlWriteOp>(new YBqlWriteOp(table));
  QLWriteRequestPB* req = op->mutable_request();
  req->set_type(stmt_type);
  req->set_client(YQL_CLIENT_CQL);
  // TODO: Request ID should be filled with CQL stream ID. Query ID should be replaced too.
  req->set_request_id(reinterpret_cast<uint64_t>(op.get()));
  req->set_query_id(op->GetQueryId());

  req->set_schema_version(table->schema().version());
  req->set_is_compatible_with_previous_version(
      table->schema().is_compatible_with_previous_version());

  return op;
}

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewInsert(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, QLWriteRequestPB::QL_STMT_INSERT);
}

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewUpdate(const std::shared_ptr<YBTable>& table) {
  return NewYBqlWriteOp(table, QLWriteRequestPB::QL_STMT_UPDATE);
}

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewDelete(const std::shared_ptr<YBTable>& table) {
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

uint16_t YBqlWriteOp::GetHashCode() const {
  return ql_write_request_->hash_code();
}

bool YBqlWriteOp::ReadsStaticRow() const {
  // A QL write op reads the static row if it reads a static column, or it writes to the static row
  // and has a user-defined timestamp (which DocDB requires a read-modify-write by the timestamp).
  return !ql_write_request_->column_refs().static_ids().empty() ||
         (writes_static_row_ && ql_write_request_->has_user_timestamp_usec());
}

bool YBqlWriteOp::ReadsPrimaryRow() const {
  // A QL write op reads the primary row reads a non-static column, it writes to the primary row
  // and has a user-defined timestamp (which DocDB requires a read-modify-write by the timestamp),
  // or if there is an IF clause.
  return !ql_write_request_->column_refs().ids().empty() ||
         (writes_primary_row_ && ql_write_request_->has_user_timestamp_usec()) ||
         ql_write_request_->has_if_expr();
}

bool YBqlWriteOp::WritesStaticRow() const {
  return writes_static_row_;
}

bool YBqlWriteOp::WritesPrimaryRow() const {
  return writes_primary_row_;
}

bool YBqlWriteOp::returns_sidecar() {
  return ql_write_request_->has_if_expr() || ql_write_request_->returns_status();
}

// YBqlWriteOp::HashHash/Equal ---------------------------------------------------------------
size_t YBqlWriteHashKeyComparator::operator()(const YBqlWriteOpPtr& op) const {
  size_t hash = 0;

  // Hash the table id.
  boost::hash_combine(hash, op->table()->id());

  // Hash the hash key.
  string key;
  for (const auto& value : op->request().hashed_column_values()) {
    AppendToKey(value.value(), &key);
  }
  boost::hash_combine(hash, key);

  return hash;
}

bool YBqlWriteHashKeyComparator::operator()(const YBqlWriteOpPtr& op1,
                                              const YBqlWriteOpPtr& op2) const {
  // Check if two write ops overlap that they apply to the same hash key in the same table.
  if (op1->table() != op2->table() && op1->table()->id() != op2->table()->id()) {
    return false;
  }
  const QLWriteRequestPB& req1 = op1->request();
  const QLWriteRequestPB& req2 = op2->request();
  if (req1.hashed_column_values_size() != req2.hashed_column_values_size()) {
    return false;
  }
  for (int i = 0; i < req1.hashed_column_values().size(); i++) {
    DCHECK(req1.hashed_column_values()[i].has_value());
    DCHECK(req2.hashed_column_values()[i].has_value());
    if (req1.hashed_column_values()[i].value() != req2.hashed_column_values()[i].value())
      return false;
  }
  return true;
}

// YBqlWriteOp::PrimaryHash/Equal ---------------------------------------------------------------
size_t YBqlWritePrimaryKeyComparator::operator()(const YBqlWriteOpPtr& op) const {
  size_t hash = YBqlWriteHashKeyComparator()(op);

  // Hash the range key also.
  string key;
  for (const auto& value : op->request().range_column_values()) {
    AppendToKey(value.value(), &key);
  }
  boost::hash_combine(hash, key);

  return hash;
}

bool YBqlWritePrimaryKeyComparator::operator()(const YBqlWriteOpPtr& op1,
                                                 const YBqlWriteOpPtr& op2) const {
  if (!YBqlWriteHashKeyComparator()(op1, op2)) {
    return false;
  }

  // Check if two write ops overlap that they apply to the range key also.
  const QLWriteRequestPB& req1 = op1->request();
  const QLWriteRequestPB& req2 = op2->request();
  if (req1.range_column_values_size() != req2.range_column_values_size()) {
    return false;
  }
  for (int i = 0; i < req1.range_column_values().size(); i++) {
    DCHECK(req1.range_column_values()[i].has_value());
    DCHECK(req2.range_column_values()[i].has_value());
    if (req1.range_column_values()[i].value() != req2.range_column_values()[i].value())
      return false;
  }
  return true;
}

// YBqlReadOp -----------------------------------------------------------------

YBqlReadOp::YBqlReadOp(const shared_ptr<YBTable>& table)
    : YBqlOp(table),
      ql_read_request_(new QLReadRequestPB()),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

YBqlReadOp::~YBqlReadOp() {}

OpGroup YBqlReadOp::group() {
  return yb_consistency_level_ == YBConsistencyLevel::CONSISTENT_PREFIX
      ? OpGroup::kConsistentPrefixRead : OpGroup::kLeaderRead;
}

std::unique_ptr<YBqlReadOp> YBqlReadOp::NewSelect(const shared_ptr<YBTable>& table) {
  std::unique_ptr<YBqlReadOp> op(new YBqlReadOp(table));
  QLReadRequestPB *req = op->mutable_request();
  req->set_client(YQL_CLIENT_CQL);
  // TODO: Request ID should be filled with CQL stream ID. Query ID should be replaced too.
  req->set_request_id(reinterpret_cast<uint64_t>(op.get()));
  req->set_query_id(op->GetQueryId());

  req->set_schema_version(table->schema().version());
  req->set_is_compatible_with_previous_version(
      table->schema().is_compatible_with_previous_version());

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

    // Check that the partition key we got from the paging state is within bounds.
    uint16 paging_state_hash_code = PartitionSchema::DecodeMultiColumnHashValue(*partition_key);
    if ((ql_read_request_->has_hash_code() &&
            paging_state_hash_code < ql_read_request_->hash_code()) ||
        (ql_read_request_->has_max_hash_code() &&
            paging_state_hash_code > ql_read_request_->max_hash_code())) {
      return STATUS_SUBSTITUTE(InternalError,
                               "Out of bounds partition key found in paging state:"
                               "Query's partition bounds: [$0, $1], paging state partition: $2",
                               ql_read_request_->hash_code(),
                               ql_read_request_->max_hash_code() ,
                               paging_state_hash_code);
    }

    ql_read_request_->set_hash_code(paging_state_hash_code);
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

//--------------------------------------------------------------------------------------------------
// YBPgsql Operators
//--------------------------------------------------------------------------------------------------

YBPgsqlOp::YBPgsqlOp(const shared_ptr<YBTable>& table, std::string* partition_key)
      : YBOperation(table), response_(new PgsqlResponsePB()),
        partition_key_(partition_key ? std::move(*partition_key) : std::string()) {
}

YBPgsqlOp::~YBPgsqlOp() {
}

bool YBPgsqlOp::succeeded() const {
  return response().status() == PgsqlResponsePB::PGSQL_STATUS_OK;
}

bool YBPgsqlOp::applied() {
  return succeeded() && !response_->skipped();
}

namespace {

std::string ResponseSuffix(const PgsqlResponsePB& response) {
  const auto str = response.ShortDebugString();
  return str.empty() ? std::string() : (", response: " + str);
}

} // namespace

//--------------------------------------------------------------------------------------------------
// YBPgsqlWriteOp

YBPgsqlWriteOp::YBPgsqlWriteOp(const shared_ptr<YBTable>& table, PgsqlWriteRequestPB* request)
    : YBPgsqlOp(table, request ? request->mutable_partition_key() : nullptr), request_(request) {
  if (!request) {
    request_holder_ = std::make_unique<PgsqlWriteRequestPB>();
    request_ = request_holder_.get();
  }
}

YBPgsqlWriteOp::~YBPgsqlWriteOp() {}

static std::unique_ptr<YBPgsqlWriteOp> NewYBPgsqlWriteOp(
    const shared_ptr<YBTable>& table,
    PgsqlWriteRequestPB::PgsqlStmtType stmt_type) {
  auto op = std::make_unique<YBPgsqlWriteOp>(table);
  PgsqlWriteRequestPB *req = op->mutable_request();
  req->set_stmt_type(stmt_type);
  req->set_client(YQL_CLIENT_PGSQL);
  req->set_table_id(table->id());
  req->set_schema_version(table->schema().version());
  req->set_stmt_id(op->GetQueryId());

  return op;
}

std::unique_ptr<YBPgsqlWriteOp> YBPgsqlWriteOp::NewInsert(const std::shared_ptr<YBTable>& table) {
  return NewYBPgsqlWriteOp(table, PgsqlWriteRequestPB::PGSQL_INSERT);
}

std::unique_ptr<YBPgsqlWriteOp> YBPgsqlWriteOp::NewUpdate(const std::shared_ptr<YBTable>& table) {
  return NewYBPgsqlWriteOp(table, PgsqlWriteRequestPB::PGSQL_UPDATE);
}

std::unique_ptr<YBPgsqlWriteOp> YBPgsqlWriteOp::NewDelete(const std::shared_ptr<YBTable>& table) {
  return NewYBPgsqlWriteOp(table, PgsqlWriteRequestPB::PGSQL_DELETE);
}

std::unique_ptr<YBPgsqlWriteOp> YBPgsqlWriteOp::NewTruncateColocated(
    const std::shared_ptr<YBTable>& table) {
  return NewYBPgsqlWriteOp(table, PgsqlWriteRequestPB::PGSQL_TRUNCATE_COLOCATED);
}

std::string YBPgsqlWriteOp::ToString() const {
  return Format(
      "PGSQL_WRITE $0$1$2", request_->ShortDebugString(),
      (write_time_ ? " write_time: " + write_time_.ToString() : ""), ResponseSuffix(response()));
}

void YBPgsqlWriteOp::SetHashCode(const uint16_t hash_code) {
  request_->set_hash_code(hash_code);
}

Status YBPgsqlWriteOp::GetPartitionKey(std::string* partition_key) const {
  if (!request_holder_) {
    return YBPgsqlOp::GetPartitionKey(partition_key);
  }
  RETURN_NOT_OK(InitWritePartitionKey(
      table_->InternalSchema(), table_->partition_schema(), request_));
  *partition_key = std::move(*request_->mutable_partition_key());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// YBPgsqlReadOp

YBPgsqlReadOp::YBPgsqlReadOp(const shared_ptr<YBTable>& table, PgsqlReadRequestPB* request)
    : YBPgsqlOp(table, request ? request->mutable_partition_key() : nullptr),
      request_(request),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
  if (!request) {
    request_holder_ = std::make_unique<PgsqlReadRequestPB>();
    request_ = request_holder_.get();
  }
}

std::unique_ptr<YBPgsqlReadOp> YBPgsqlReadOp::NewSelect(const shared_ptr<YBTable>& table) {
  std::unique_ptr<YBPgsqlReadOp> op(new YBPgsqlReadOp(table));
  PgsqlReadRequestPB *req = op->mutable_request();
  req->set_client(YQL_CLIENT_PGSQL);
  req->set_table_id(table->id());
  req->set_schema_version(table->schema().version());
  req->set_stmt_id(op->GetQueryId());

  return op;
}

std::unique_ptr<YBPgsqlReadOp> YBPgsqlReadOp::NewSample(const shared_ptr<YBTable>& table) {
  std::unique_ptr<YBPgsqlReadOp> op(new YBPgsqlReadOp(table));
  PgsqlReadRequestPB *req = op->mutable_request();
  req->set_client(YQL_CLIENT_PGSQL);
  req->set_table_id(table->id());
  req->set_schema_version(table->schema().version());
  req->set_stmt_id(op->GetQueryId());

  return op;
}

std::string YBPgsqlReadOp::ToString() const {
  return "PGSQL_READ " + request_->ShortDebugString() + ResponseSuffix(response());
}

void YBPgsqlReadOp::SetHashCode(const uint16_t hash_code) {
  request_->set_hash_code(hash_code);
}

std::vector<ColumnSchema> YBPgsqlReadOp::MakeColumnSchemasFromColDesc(
  const google::protobuf::RepeatedPtrField<PgsqlRSColDescPB>& rscol_descs) {
  std::vector<ColumnSchema> column_schemas;
  column_schemas.reserve(rscol_descs.size());
  for (const auto& rscol_desc : rscol_descs) {
    column_schemas.emplace_back(rscol_desc.name(), QLType::FromQLTypePB(rscol_desc.ql_type()));
  }
  return column_schemas;
}

std::vector<ColumnSchema> YBPgsqlReadOp::MakeColumnSchemasFromRequest() const {
  // Tests don't have access to the QL internal statement object, so they have to use rsrow
  // descriptor from the read request.
  return MakeColumnSchemasFromColDesc(request().rsrow_desc().rscol_descs());
}

OpGroup YBPgsqlReadOp::group() {
  return yb_consistency_level_ == YBConsistencyLevel::CONSISTENT_PREFIX
      ? OpGroup::kConsistentPrefixRead : OpGroup::kLeaderRead;
}

void YBPgsqlReadOp::SetUsedReadTime(const ReadHybridTime& used_time) {
  used_read_time_ = used_time;
}

Status YBPgsqlReadOp::GetPartitionKey(std::string* partition_key) const {
  if (!request_holder_) {
    return YBPgsqlOp::GetPartitionKey(partition_key);
  }
  RETURN_NOT_OK(InitReadPartitionKey(
      table_->InternalSchema(), table_->partition_schema(), table_->GetPartitionsShared()->back(),
      request_));
  *partition_key = std::move(*request_->mutable_partition_key());
  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBNoOp
////////////////////////////////////////////////////////////

YBNoOp::YBNoOp(const std::shared_ptr<YBTable>& table)
  : table_(table) {
}

Status YBNoOp::Execute(YBClient* client, const YBPartialRow& key) {
  string encoded_key;
  RETURN_NOT_OK(table_->partition_schema().EncodeKey(key, &encoded_key));
  CoarseTimePoint deadline = CoarseMonoClock::Now() + 5s;

  tserver::NoOpRequestPB noop_req;
  tserver::NoOpResponsePB noop_resp;

  for (int attempt = 1; attempt < 11; attempt++) {
    Synchronizer sync;
    auto remote_ = VERIFY_RESULT(client->data_->meta_cache_->LookupTabletByKeyFuture(
        table_, encoded_key, deadline).get());

    internal::RemoteTabletServer *ts = nullptr;
    std::vector<internal::RemoteTabletServer*> candidates;
    std::set<string> blacklist;  // TODO: empty set for now.
    Status lookup_status = client->data_->GetTabletServer(
       client,
       remote_,
       YBClient::ReplicaSelection::LEADER_ONLY,
       blacklist,
       &candidates,
       &ts);

    // If we get ServiceUnavailable, this indicates that the tablet doesn't
    // currently have any known leader. We should sleep and retry, since
    // it's likely that the tablet is undergoing a leader election and will
    // soon have one.
    if (lookup_status.IsServiceUnavailable() && CoarseMonoClock::Now() < deadline) {
      const int sleep_ms = attempt * 100;
      VLOG(1) << "Tablet " << remote_->tablet_id() << " current unavailable: "
              << lookup_status.ToString() << ". Sleeping for " << sleep_ms << "ms "
              << "and retrying...";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      continue;
    }
    RETURN_NOT_OK(lookup_status);

    auto now = CoarseMonoClock::Now();
    if (deadline < now) {
      return STATUS(TimedOut, "Op timed out, deadline expired");
    }

    // Recalculate the deadlines.
    // If we have other replicas beyond this one to try, then we'll use the default RPC timeout.
    // That gives us time to try other replicas later. Otherwise, use the full remaining deadline
    // for the user's call.
    CoarseTimePoint rpc_deadline;
    if (static_cast<int>(candidates.size()) - blacklist.size() > 1) {
      rpc_deadline = now + client->default_rpc_timeout();
      rpc_deadline = std::min(deadline, rpc_deadline);
    } else {
      rpc_deadline = deadline;
    }

    rpc::RpcController controller;
    controller.set_deadline(rpc_deadline);

    CHECK(ts->proxy());
    const Status rpc_status = ts->proxy()->NoOp(noop_req, &noop_resp, &controller);
    if (rpc_status.ok() && !noop_resp.has_error()) {
      break;
    }

    LOG(INFO) << rpc_status.CodeAsString();
    if (noop_resp.has_error()) {
      Status s = StatusFromPB(noop_resp.error().status());
      LOG(INFO) << rpc_status.CodeAsString();
    }
    /*
     * TODO: For now, we just try a few attempts and exit. Ideally, we should check for
     * errors that are retriable, and retry if so.
     * RETURN_NOT_OK(CanBeRetried(true, rpc_status, server_status, rpc_deadline, deadline,
     *                         candidates, blacklist));
     */
  }

  return Status::OK();
}

bool YBPgsqlReadOp::should_add_intents(IsolationLevel isolation_level) {
  return isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION ||
         IsValidRowMarkType(GetRowMarkTypeFromPB(*request_));
}

Status InitPartitionKey(
    const Schema& schema, const PartitionSchema& partition_schema,
    const std::string& last_partition, LWPgsqlReadRequestPB* request) {
  return InitReadPartitionKey(schema, partition_schema, last_partition, request);
}

Status InitPartitionKey(
    const Schema& schema, const PartitionSchema& partition_schema, LWPgsqlWriteRequestPB* request) {
  return InitWritePartitionKey(schema, partition_schema, request);
}

Status GetRangePartitionBounds(const Schema& schema,
                               const PgsqlReadRequestPB& request,
                               vector<docdb::KeyEntryValue>* lower_bound,
                               vector<docdb::KeyEntryValue>* upper_bound) {
  return DoGetRangePartitionBounds(schema, request, lower_bound, upper_bound);
}

Status GetRangePartitionBounds(const Schema& schema,
                               const LWPgsqlReadRequestPB& request,
                               vector<docdb::KeyEntryValue>* lower_bound,
                               vector<docdb::KeyEntryValue>* upper_bound) {
  return DoGetRangePartitionBounds(schema, request, lower_bound, upper_bound);
}

}  // namespace client
}  // namespace yb
