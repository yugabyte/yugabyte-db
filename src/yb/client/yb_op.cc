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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/row_mark.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/dockv/primitive_value_util.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/primitive_value.h"

#include "yb/dockv/value_type.h"
#include "yb/qlexpr/doc_scanspec_util.h"
#include "yb/qlexpr/ql_expr_util.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/qlexpr/ql_scanspec.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/yql/pggate/util/ybc_guc.h"

using namespace std::literals;

DEFINE_RUNTIME_bool(redis_allow_reads_from_followers, false,
    "If true, the read will be served from the closest replica in the same AZ, which can "
    "be a follower.");
TAG_FLAG(redis_allow_reads_from_followers, evolving);

namespace yb {
namespace client {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::string;

namespace {

void SetPartitionKey(const Slice& value, LWPgsqlReadRequestPB* request) {
  request->dup_partition_key(value);
}

void SetPartitionKey(const Slice& value, LWPgsqlWriteRequestPB* request) {
  request->dup_partition_key(value);
}

void SetHashCodePartitionKey(uint16_t hash_code, LWPgsqlReadRequestPB* request) {
  if (request->is_forward_scan()) {
    if (hash_code == 0) {
      request->clear_partition_key();
    } else {
      auto partition_key = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      SetPartitionKey(partition_key, request);
    }
  } else {
    if (hash_code == dockv::PartitionSchema::kMaxPartitionKey) {
      request->clear_partition_key();
    } else {
      auto partition_key = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code + 1);
      SetPartitionKey(partition_key, request);
    }
  }
}

template <typename Req>
void GetPartitionKey(const Req& request, std::string* partition_key) {
  if (request.has_partition_key()) {
    *partition_key = request.partition_key();
  } else {
    partition_key->clear();
  }
}

template<class Req>
auto* GetPagingState(const Req& request) {
  if (request.has_index_request()) {
    return request.index_request().has_paging_state()
        ? &request.index_request().paging_state() : nullptr;
  }
  return request.has_paging_state() ? &request.paging_state() : nullptr;
}

Result<uint16_t> GetHashCodeFromBound(const Slice& key) {
  uint16_t hash_code;
  if (dockv::PartitionSchema::IsValidHashPartitionKeyBound(key)) {
    DCHECK(!yb_allow_dockey_bounds) << "Invalid request bound: " << key.ToDebugHexString();
    hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(key);
  } else {
    DCHECK(yb_allow_dockey_bounds) << "Invalid request bound: " << key.ToDebugHexString();
    hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(key));
  }
  return hash_code;
}

template<class Req>
Status InitHashPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema, Req* request) {
  // Seek a specific partition_key from read_request, also set the hash_code/max_hash_code bounds.
  // 1. paging_state -- Use the partition key provided by the server. It is a follow-up request, so
  //    the hash code bounds are already set.
  // 2. hash column values -- Full set of hash values allow to calculate the hash code, which sets
  //    the partition key, and the both hash code bounds.
  // 3. lower and upper bound -- If set, provide the value for the hash_code and the max_hash_code
  //    respectively. Depending on the scan direction, one of them provides the partition key value.
  if (auto* paging_state = GetPagingState(*request); paging_state) {
    // TODO: DocDB used to set the next partition key as the hash code of the next row (see
    // PgsqlReadOperation::SetPagingState) regardless of the scan direction. That is inconsistent
    // with the current semantics of the partition key, which is exclusive in the backward scan.
    // While we have fixed it in DocDB, there is a backward compatibility problem, at the time of
    // upgrade there may be nodes still sending the inclusive hash code.
    // So for now we extract the hash code from the next row key, later on, when we don't have to
    // maintain backward compatibility with older versions, we can use the partition key from the
    // paging state unconditionally.
    if (paging_state->has_next_row_key()) {
      auto hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(paging_state->next_row_key()));
      SetHashCodePartitionKey(hash_code, request);
    } else {
      SetPartitionKey(paging_state->next_partition_key(), request);
    }
    return Status::OK();
  }
  if (!request->partition_column_values().empty()) {
    // If hashed columns are set, use them to compute the exact key and set the bounds
    auto hash_code = VERIFY_RESULT(partition_schema.PgsqlHashColumnCompoundValue(
        request->partition_column_values()));
    request->set_hash_code(hash_code);
    request->set_max_hash_code(hash_code);
    SetHashCodePartitionKey(hash_code, request);
    return Status::OK();
  }
  request->clear_partition_key();
  if (request->has_lower_bound()) {
    auto hash_code = VERIFY_RESULT(GetHashCodeFromBound(request->lower_bound().key()));
    request->set_hash_code(hash_code);
    if (request->is_forward_scan()) {
      SetHashCodePartitionKey(hash_code, request);
    }
  }
  if (request->has_upper_bound()) {
    auto max_hash_code = VERIFY_RESULT(GetHashCodeFromBound(request->upper_bound().key()));
    request->set_max_hash_code(max_hash_code);
    if (!request->is_forward_scan()) {
      SetHashCodePartitionKey(max_hash_code, request);
    }
  }
  return Status::OK();
}

template<class Req>
Status InitRangePartitionKey(const Schema& schema, Req* request) {
  // Seek a specific partition_key from read_request
  // 1. paging_state -- Use the partition key provided by the server.
  // 2. upper or lower bound -- If the scan direction is forward, the lower bound is the
  //    partition key. For the backward scan the partition key is implicitly exclusive, so if the
  //    upper bound is set and is inclusive, the partition key should be set strictly greater than
  //    the upper bound.
  if (auto *paging_state = GetPagingState(*request); paging_state) {
    SetPartitionKey(paging_state->next_partition_key(), request);
    return Status::OK();
  }
  request->clear_partition_key();
  if (request->has_lower_bound() && request->is_forward_scan()) {
    SetPartitionKey(request->lower_bound().key(), request);
  }
  if (request->has_upper_bound() && !request->is_forward_scan()) {
    if (request->upper_bound().is_inclusive()) {
      // Partition key is exclusive, so we add +inf to include the upper bound value itself.
      dockv::DocKey doc_key;
      VERIFY_RESULT(doc_key.DecodeFrom(
          request->upper_bound().key(),
          dockv::DocKeyPart::kWholeDocKey,
          dockv::AllowSpecial::kTrue));
      doc_key.AddRangeComponent(dockv::KeyEntryValue(dockv::KeyEntryType::kHighest));
      SetPartitionKey(doc_key.Encode(), request);
    } else {
      SetPartitionKey(request->upper_bound().key(), request);
    }
  }
  return Status::OK();
}

template <class Col>
Result<std::vector<dockv::KeyEntryValue>> GetRangeComponents(
    const Schema& schema, const Col& range_cols, const bool lower_bound) {
  size_t column_idx = 0;
  auto range_cols_it = range_cols.begin();
  const auto num_range_key_columns = schema.num_range_key_columns();
  dockv::KeyEntryValues result;
  for (const auto& col_id : schema.column_ids()) {
    if (!schema.is_range_column(col_id)) {
      continue;
    }

    const ColumnSchema& column_schema = VERIFY_RESULT(schema.column_by_id(col_id));

    if (schema.table_properties().partitioning_version() > 0) {
      if (column_idx < static_cast<size_t>(range_cols.size())) {
        result.push_back(dockv::KeyEntryValue::FromQLValuePBForKey(
            range_cols_it->value(), column_schema.sorting_type()));
      } else {
        result.emplace_back(
            lower_bound ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest);
      }
    } else {
      if (column_idx >= static_cast<size_t>(range_cols.size()) ||
          range_cols_it->value().value_case() == QLValuePB::VALUE_NOT_SET) {
        result.emplace_back(
            lower_bound ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest);
      } else {
        result.push_back(dockv::KeyEntryValue::FromQLValuePB(
            range_cols_it->value(), column_schema.sorting_type()));
      }
    }

    ++range_cols_it;
    if (++column_idx == num_range_key_columns) {
      break;
    }
  }

  if (!lower_bound) {
    result.emplace_back(dockv::KeyEntryType::kHighest);
  }
  return result;
}

template <class Col>
Result<std::string> GetRangePartitionKey(
    const Schema& schema, const Col& range_cols) {
  RSTATUS_DCHECK(!schema.num_hash_key_columns(), IllegalState,
      "Cannot get range partition key for hash partitioned table");

  auto range_components = VERIFY_RESULT(GetRangeComponents(schema, range_cols, true));
  return dockv::DocKey(std::move(range_components)).Encode().ToStringBuffer();
}

template<class Req>
Status InitReadPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema, Req* request) {
  if (schema.num_hash_key_columns() > 0) {
    return InitHashPartitionKey(schema, partition_schema, request);
  }

  return InitRangePartitionKey(schema, request);
}

template<class Req>
Status InitWritePartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema, Req* request) {
  const auto& ybctid = request->ybctid_column_value().value();
  if (schema.num_hash_key_columns() > 0) {
    if (!IsNull(ybctid)) {
      const uint16 hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(ybctid.binary_value()));
      request->set_hash_code(hash_code);
      SetPartitionKey(dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code), request);
      return Status::OK();
    }

    // Computing the partition_key.
    auto partition_key = VERIFY_RESULT(partition_schema.EncodePgsqlHash(
        request->partition_column_values()));
    request->set_hash_code(dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_key));
    SetPartitionKey(partition_key, request);
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
                                 vector<dockv::KeyEntryValue>* lower_bound,
                                 vector<dockv::KeyEntryValue>* upper_bound) {
  SCHECK(!schema.num_hash_key_columns(), IllegalState,
         "Cannot set range partition key for hash partitioned table");
  const auto& range_cols = request.range_column_values();
  const auto& condition_expr = request.condition_expr();
  if (condition_expr.has_condition() &&
      implicit_cast<size_t>(range_cols.size()) < schema.num_range_key_columns()) {
    auto prefixed_range_components = VERIFY_RESULT(qlexpr::InitKeyColumnValues(
        range_cols, schema, schema.num_hash_key_columns()));
    qlexpr::QLScanRange scan_range(schema, condition_expr.condition());
    *lower_bound = qlexpr::GetRangeKeyScanSpec(
        schema, &prefixed_range_components, &scan_range, nullptr, qlexpr::BoundType::kLower);
    *upper_bound = qlexpr::GetRangeKeyScanSpec(
        schema, &prefixed_range_components, &scan_range, nullptr, qlexpr::BoundType::kUpper);
  } else if (!range_cols.empty()) {
    *lower_bound = VERIFY_RESULT(GetRangeComponents(schema, range_cols, true));
    *upper_bound = VERIFY_RESULT(GetRangeComponents(schema, range_cols, false));
  }
  return Status::OK();
}

std::string ResponseSuffix(const LWPgsqlResponsePB& response) {
  const auto str = response.ShortDebugString();
  return str.empty() ? std::string() : (", response: " + str);
}

template <class Op, class... Args>
requires(std::derived_from<Op, YBPgsqlOp>)
auto NewYBPgsqlOp(Args&&... args) {
  auto op = std::make_shared<Op>(std::forward<Args>(args)...);
  op->mutable_request()->set_client(YQL_CLIENT_PGSQL);
  return op;
}

auto NewYBPgsqlWriteOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars,
    PgsqlWriteRequestPB::PgsqlStmtType stmt_type) {
  auto op = NewYBPgsqlOp<YBPgsqlWriteOp>(table, arena, *sidecars);
  auto& req = *op->mutable_request();
  req.set_stmt_type(stmt_type);
  req.ref_table_id(table->id());
  req.set_schema_version(table->schema().version());
  req.set_stmt_id(op->GetQueryId());

  return op;
}

auto NewYBPgsqlLockOp(const YBTablePtr& table, bool is_lock = true) {
  auto op = NewYBPgsqlOp<YBPgsqlLockOp>(table);
  auto& req = *op->mutable_request();
  req.set_is_lock(is_lock);
  return op;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// YBOperation
//--------------------------------------------------------------------------------------------------

YBOperation::YBOperation(const YBTablePtr& table, const ThreadSafeArenaPtr& arena)
    : table_(table), arena_(arena) {
}

YBOperation::~YBOperation() = default;

void YBOperation::SetTablet(const scoped_refptr<internal::RemoteTablet>& tablet) {
  tablet_ = tablet;
}

void YBOperation::ResetTablet() {
  tablet_.reset();
}

void YBOperation::ResetTable(const YBTablePtr& new_table) {
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

YBRedisOp::YBRedisOp(const YBTablePtr& table)
    : YBOperationBase(table, SharedThreadSafeArena()) {
}

OpGroup YBRedisReadOp::group() const {
  return FLAGS_redis_allow_reads_from_followers ? OpGroup::kConsistentPrefixRead
                                                : OpGroup::kLeaderRead;
}

// YBRedisWriteOp -----------------------------------------------------------------

YBRedisWriteOp::YBRedisWriteOp(const YBTablePtr& table)
    : YBRedisOp(table), redis_write_request_(arena_->ArenaObjectFactory()) {
}

size_t YBRedisWriteOp::space_used_by_request() const {
  return redis_write_request_->SpaceUsedLong();
}

std::string YBRedisWriteOp::ToString() const {
  return Format("REDIS_WRITE $0", redis_write_request_->key_value().key());
}

uint16_t YBRedisWriteOp::hash_code() const {
  return redis_write_request_->key_value().hash_code();
}

std::string_view YBRedisWriteOp::GetKey() const {
  return redis_write_request_->key_value().key();
}

Status YBRedisWriteOp::GetPartitionKey(std::string *partition_key) const {
  const Slice& slice(redis_write_request_->key_value().key());
  RETURN_NOT_OK(table_->partition_schema().EncodeRedisKey(slice, partition_key));
  if (!partition_key->empty()) {
    auto hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key);
    redis_write_request_->mutable_key_value()->set_hash_code(hash_code);
  }
  return Status::OK();
}

// YBRedisReadOp -----------------------------------------------------------------

YBRedisReadOp::YBRedisReadOp(const YBTablePtr& table)
    : YBRedisOp(table), redis_read_request_(arena_->ArenaObjectFactory()) {
}

size_t YBRedisReadOp::space_used_by_request() const {
  return redis_read_request_->SpaceUsedLong();
}

std::string YBRedisReadOp::ToString() const {
  return Format("REDIS_READ $0", redis_read_request_->key_value().key());
}

uint16_t YBRedisReadOp::hash_code() const {
  return redis_read_request_->key_value().hash_code();
}

std::string_view YBRedisReadOp::GetKey() const {
  return redis_read_request_->key_value().key();
}

Status YBRedisReadOp::GetPartitionKey(std::string *partition_key) const {
  if (!redis_read_request_->key_value().has_key()) {
    auto hash_code = redis_read_request_->key_value().hash_code();
    *partition_key = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
    return Status::OK();
  }
  const Slice& slice(redis_read_request_->key_value().key());
  RETURN_NOT_OK(table_->partition_schema().EncodeRedisKey(slice, partition_key));
  if (!partition_key->empty()) {
    auto hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key);
    redis_read_request_->mutable_key_value()->set_hash_code(hash_code);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// YBCql Operators
// - These ops should be prefixed with YBCql instead of YBql.
// - The prefixes "ql" or "QL" are used for common entities of all languages and not just CQL.
// - The name will be clean up later.
//--------------------------------------------------------------------------------------------------

YBqlOp::YBqlOp(const YBTablePtr& table, const ThreadSafeArenaPtr& arena)
    : YBOperationBase(table, arena) {
}

YBqlOp::~YBqlOp() {
}

bool YBqlOp::succeeded() const {
  return response().status() == QLResponsePB::YQL_STATUS_OK;
}

// YBqlWriteOp -----------------------------------------------------------------

YBqlWriteOp::YBqlWriteOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, LWQLWriteRequestPB* request)
    : YBqlOp(table, arena),
      ql_write_request_(request ? request : arena_->NewArenaObject<LWQLWriteRequestPB>()) {
}

YBqlWriteOp::~YBqlWriteOp() = default;

static std::unique_ptr<YBqlWriteOp> NewYBqlWriteOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena,
    QLWriteRequestPB::QLStmtType stmt_type) {
  auto op = std::unique_ptr<YBqlWriteOp>(
      new YBqlWriteOp(table, arena, /* request= */ nullptr));
  auto* req = op->mutable_request();
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

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewInsert(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena) {
  return NewYBqlWriteOp(table, arena, QLWriteRequestPB::QL_STMT_INSERT);
}

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewUpdate(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena) {
  return NewYBqlWriteOp(table, arena, QLWriteRequestPB::QL_STMT_UPDATE);
}

std::unique_ptr<YBqlWriteOp> YBqlWriteOp::NewDelete(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena) {
  return NewYBqlWriteOp(table, arena, QLWriteRequestPB::QL_STMT_DELETE);
}

std::string YBqlWriteOp::ToString() const {
  return Format("QL_WRITE $0", *ql_write_request_);
}

Status YBqlWriteOp::GetPartitionKey(string* partition_key) const {
  RETURN_NOT_OK(table_->partition_schema().EncodeKey(
      ql_write_request_->hashed_column_values(), partition_key));
  if (table_->partition_schema().IsHashPartitioning()) {
    ql_write_request_->set_hash_code(
        dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
  }
  return Status::OK();
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

namespace {

template <class Col>
bool ValuesEquals(const Col& lhs, const Col& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  auto it1 = lhs.begin();
  auto it2 = rhs.begin();
  for (; it1 != lhs.end(); ++it1, ++it2) {
    DCHECK(it1->has_value());
    DCHECK(it2->has_value());
    if (it1->value() != it2->value())
      return false;
  }
  return true;
}

} // namespace

bool YBqlWriteHashKeyComparator::operator()(
    const YBqlWriteOpPtr& op1, const YBqlWriteOpPtr& op2) const {
  // Check if two write ops overlap that they apply to the same hash key in the same table.
  if (op1->table() != op2->table() && op1->table()->id() != op2->table()->id()) {
    return false;
  }
  return ValuesEquals(op1->request().hashed_column_values(), op2->request().hashed_column_values());
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

bool YBqlWritePrimaryKeyComparator::operator()(
    const YBqlWriteOpPtr& op1, const YBqlWriteOpPtr& op2) const {
  if (!YBqlWriteHashKeyComparator()(op1, op2)) {
    return false;
  }

  // Check if two write ops overlap that they apply to the range key also.
  return ValuesEquals(op1->request().range_column_values(), op2->request().range_column_values());
}

// YBqlReadOp -----------------------------------------------------------------

YBqlReadOp::YBqlReadOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, LWQLReadRequestPB* request)
    : YBqlOp(table, arena),
      ql_read_request_(request ? request : arena_->NewArenaObject<LWQLReadRequestPB>()),
      yb_consistency_level_(YBConsistencyLevel::STRONG) {
}

YBqlReadOp::~YBqlReadOp() = default;

OpGroup YBqlReadOp::group() const {
  return yb_consistency_level_ == YBConsistencyLevel::CONSISTENT_PREFIX
      ? OpGroup::kConsistentPrefixRead : OpGroup::kLeaderRead;
}

std::unique_ptr<YBqlReadOp> YBqlReadOp::NewSelect(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena) {
  std::unique_ptr<YBqlReadOp> op(new YBqlReadOp(table, arena, /* request= */ nullptr));
  auto* req = op->mutable_request();
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
  return Format("QL_READ $0", *ql_read_request_);
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
      auto lower_bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key < lower_bound) *partition_key = std::move(lower_bound);
    }

    // Make sure given key is not bigger than upper bound (if any)
    if (ql_read_request_->has_max_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->max_hash_code());
      auto upper_bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
      if (*partition_key > upper_bound) *partition_key = std::move(upper_bound);
    }

    // Set both bounds to equal partition key now, because this is a point get
    ql_read_request_->set_hash_code(
          dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
    ql_read_request_->set_max_hash_code(
          dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
  } else {
    // Otherwise, set the partition key to the hash_code (lower bound of the token range).
    if (ql_read_request_->has_hash_code()) {
      uint16 hash_code = static_cast<uint16>(ql_read_request_->hash_code());
      *partition_key = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
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
    uint16 paging_state_hash_code = dockv::PartitionSchema::DecodeMultiColumnHashValue(
        *partition_key);
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

template <class Col>
std::vector<ColumnSchema> DoMakeColumnSchemasFromColDesc(const Col& rscol_descs) {
  std::vector<ColumnSchema> column_schemas;
  column_schemas.reserve(rscol_descs.size());
  for (const auto& rscol_desc : rscol_descs) {
    column_schemas.emplace_back(rscol_desc.name(), QLType::FromQLTypePB(rscol_desc.ql_type()));
  }
  return column_schemas;
}

std::vector<ColumnSchema> MakeColumnSchemasFromColDesc(
    const google::protobuf::RepeatedPtrField<QLRSColDescPB>& rscol_descs) {
  return DoMakeColumnSchemasFromColDesc(rscol_descs);
}

std::vector<ColumnSchema> MakeColumnSchemasFromColDesc(
    const ArenaList<LWQLRSColDescPB>& rscol_descs) {
  return DoMakeColumnSchemasFromColDesc(rscol_descs);
}

std::vector<ColumnSchema> YBqlReadOp::MakeColumnSchemasFromRequest() const {
  // Tests don't have access to the QL internal statement object, so they have to use rsrow
  // descriptor from the read request.
  return MakeColumnSchemasFromColDesc(request().rsrow_desc().rscol_descs());
}

Result<qlexpr::QLRowBlock> YBqlReadOp::MakeRowBlock() const {
  Schema schema(MakeColumnSchemasFromRequest());
  qlexpr::QLRowBlock result(schema);
  auto data = rows_data_.AsSlice();
  if (!data.empty()) {
    RETURN_NOT_OK(result.Deserialize(request().client(), &data));
  }
  return result;
}

//--------------------------------------------------------------------------------------------------
// YBPgsql Operators
//--------------------------------------------------------------------------------------------------

YBPgsqlOp::YBPgsqlOp(const YBTablePtr& table, const ThreadSafeArenaPtr& arena)
    : YBOperationBase(table, arena) {
}

bool YBPgsqlOp::succeeded() const {
  return response().status() == PgsqlResponsePB::PGSQL_STATUS_OK;
}

bool YBPgsqlOp::applied() {
  return succeeded() && !response().skipped();
}

YBPgsqlOpSidecarBase::YBPgsqlOpSidecarBase(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars& sidecars)
    : YBPgsqlOp(table, arena), sidecars_(sidecars) {
}

//--------------------------------------------------------------------------------------------------
// YBPgsqlWriteOp

YBPgsqlWriteOp::YBPgsqlWriteOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars& sidecars,
    LWPgsqlWriteRequestPB* request)
    : YBPgsqlOpSidecarBase(table, arena, sidecars),
      request_(request ? request : arena_->NewArenaObject<LWPgsqlWriteRequestPB>()),
      own_request_(!request) {
}

YBPgsqlWriteOpPtr YBPgsqlWriteOp::NewInsert(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars) {
  return NewYBPgsqlWriteOp(
      table, arena, sidecars, PgsqlWriteRequestPB::PGSQL_INSERT);
}

YBPgsqlWriteOpPtr YBPgsqlWriteOp::NewUpdate(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars) {
  return NewYBPgsqlWriteOp(
      table, arena, sidecars, PgsqlWriteRequestPB::PGSQL_UPDATE);
}

YBPgsqlWriteOpPtr YBPgsqlWriteOp::NewDelete(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars) {
  return NewYBPgsqlWriteOp(
      table, arena, sidecars, PgsqlWriteRequestPB::PGSQL_DELETE);
}

YBPgsqlWriteOpPtr YBPgsqlWriteOp::NewFetchSequence(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars) {
  return NewYBPgsqlWriteOp(
      table, arena, sidecars, PgsqlWriteRequestPB::PGSQL_FETCH_SEQUENCE);
}

std::string YBPgsqlWriteOp::ToString() const {
  return Format(
      "PGSQL_WRITE $0$1$2", request_->ShortDebugString(),
      (write_time_ ? " write_time: " + write_time_.ToString() : ""), ResponseSuffix(response()));
}

Status YBPgsqlWriteOp::GetPartitionKey(std::string* partition_key) const {
  if (!own_request_) {
    client::GetPartitionKey(*request_, partition_key);
    return Status::OK();
  }
  RETURN_NOT_OK(InitWritePartitionKey(
      table_->InternalSchema(), table_->partition_schema(), request_));
  *partition_key = std::move(*request_->mutable_partition_key());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// YBPgsqlReadOp

YBPgsqlReadOp::YBPgsqlReadOp(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars& sidecars,
    LWPgsqlReadRequestPB* request)
    : YBPgsqlOpSidecarBase(table, arena, sidecars),
      request_(request ? request : arena_->NewArenaObject<LWPgsqlReadRequestPB>()),
      own_request_(!request) {
}

YBPgsqlReadOpPtr YBPgsqlReadOp::NewSelect(
    const YBTablePtr& table, const ThreadSafeArenaPtr& arena, rpc::Sidecars* sidecars) {
  auto op = NewYBPgsqlOp<YBPgsqlReadOp>(
      table, arena, *sidecars);
  auto& req = *op->mutable_request();
  req.ref_table_id(op->table()->id());
  req.set_schema_version(op->table()->schema().version());
  req.set_stmt_id(op->GetQueryId());

  return op;
}

std::string YBPgsqlReadOp::ToString() const {
  return Format("PGSQL_READ $0$1", *request_, ResponseSuffix(response()));
}

std::vector<ColumnSchema> YBPgsqlReadOp::MakeColumnSchemasFromRequest() const {
  // Tests don't have access to the QL internal statement object, so they have to use rsrow
  // descriptor from the read request.
  return DoMakeColumnSchemasFromColDesc(request().rsrow_desc().rscol_descs());
}

OpGroup YBPgsqlReadOp::group() const {
  return yb_consistency_level_ == YBConsistencyLevel::CONSISTENT_PREFIX
      ? OpGroup::kConsistentPrefixRead : OpGroup::kLeaderRead;
}

void YBPgsqlReadOp::SetUsedReadTime(const ReadHybridTime& used_time, const TabletId& tablet) {
  used_read_time_ = used_time;
  used_tablet_ = tablet;
}

void GetPartitionKeyForBackwardScan(
    std::reference_wrapper<const TablePartitionList> partition_list, std::string* partition_key) {
  const auto& partitions = partition_list.get();
  if (partition_key->empty()) {
    *partition_key = partitions.back();
  } else {
    auto idx = FindPartitionStartIndex(partitions, *partition_key);
    if (partitions[idx] == *partition_key) {
      CHECK_GT(idx, 0) << "Invalid partition key: " << Slice(*partition_key).ToDebugHexString();
      --idx;
    }
    *partition_key = partitions[idx];
  }
}

Status YBPgsqlReadOp::GetPartitionKey(std::string* partition_key) const {
  if (!own_request_) {
    client::GetPartitionKey(*request_, partition_key);
    if (!request_->is_forward_scan()) {
      GetPartitionKeyForBackwardScan(*(table_->GetPartitionsShared()), partition_key);
    }
    return Status::OK();
  }

  RETURN_NOT_OK(InitReadPartitionKey(
      table_->InternalSchema(), table_->partition_schema(), request_));
  *partition_key = std::move(*request_->mutable_partition_key());
  if (!request_->is_forward_scan()) {
    GetPartitionKeyForBackwardScan(*(table_->GetPartitionsShared()), partition_key);
  }
  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBPgsqlLockOp
////////////////////////////////////////////////////////////

YBPgsqlLockOp::YBPgsqlLockOp(const YBTablePtr& table)
    : YBPgsqlOp(table, SharedThreadSafeArena()),
      request_(arena_->ArenaObjectFactory()) {
}

YBPgsqlLockOpPtr YBPgsqlLockOp::NewLock(const YBTablePtr& table) {
  return NewYBPgsqlLockOp(table);
}

YBPgsqlLockOpPtr YBPgsqlLockOp::NewUnlock(const YBTablePtr& table) {
  return NewYBPgsqlLockOp(table, /* is_lock = */ false);
}

OpGroup YBPgsqlLockOp::group() const {
  return request_.is_lock() ? OpGroup::kLock : OpGroup::kUnlock;
}

std::string YBPgsqlLockOp::ToString() const {
  return Format("ADVISORY LOCK $0", request_.ShortDebugString());
}

Status YBPgsqlLockOp::GetPartitionKey(std::string* partition_key) const {
  if (request_.lock_id().lock_partition_column_values().size()) {
    RETURN_NOT_OK(table_->partition_schema().EncodeKey(
        request_.lock_id().lock_partition_column_values(), partition_key));
  } else {
    RSTATUS_DCHECK(
      !request_.is_lock() && tablet() != nullptr,
      IllegalState, "");
    *partition_key = tablet()->partition().partition_key_start();
  }
  if (!partition_key->empty() && table_->partition_schema().IsHashPartitioning()) {
    request_.set_hash_code(dockv::PartitionSchema::DecodeMultiColumnHashValue(*partition_key));
  }
  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBNoOp
////////////////////////////////////////////////////////////

YBNoOp::YBNoOp(const YBTablePtr& table)
  : table_(table) {
}

Status YBNoOp::Execute(YBClient* client, const dockv::YBPartialRow& key) {
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
      // TODO: it is not clear why the overall rpc deadline is limited by default rpc timeout if
      // provided deadline is large enough. The similar limitation was considered as not relevant,
      // refer to https://github.com/yugabyte/yugabyte-db/issues/26722 for additional details.
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

bool YBPgsqlReadOp::should_apply_intents(IsolationLevel isolation_level) {
  return isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION ||
         IsValidRowMarkType(GetRowMarkTypeFromPB(*request_));
}

Status InitPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema,
    LWPgsqlReadRequestPB* request) {
  return InitReadPartitionKey(schema, partition_schema, request);
}

Status InitPartitionKey(
    const Schema& schema, const dockv::PartitionSchema& partition_schema,
    LWPgsqlWriteRequestPB* request) {
  return InitWritePartitionKey(schema, partition_schema, request);
}

Status GetRangePartitionBounds(const Schema& schema,
                               const PgsqlReadRequestPB& request,
                               vector<dockv::KeyEntryValue>* lower_bound,
                               vector<dockv::KeyEntryValue>* upper_bound) {
  return DoGetRangePartitionBounds(schema, request, lower_bound, upper_bound);
}

Status GetRangePartitionBounds(const Schema& schema,
                               const LWPgsqlReadRequestPB& request,
                               vector<dockv::KeyEntryValue>* lower_bound,
                               vector<dockv::KeyEntryValue>* upper_bound) {
  return DoGetRangePartitionBounds(schema, request, lower_bound, upper_bound);
}

bool IsTolerantToPartitionsChange(const YBOperation& op) {
  return (op.type() != YBOperation::PGSQL_READ) ||
      (down_cast<const YBPgsqlReadOp&>(op)).request().is_forward_scan();
}

}  // namespace client
}  // namespace yb
