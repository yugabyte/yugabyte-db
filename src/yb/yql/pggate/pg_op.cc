// Copyright (c) YugaByte, Inc.
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

#include "yb/yql/pggate/pg_op.h"

#include "yb/client/table.h"
#include "yb/client/yb_op.h"

#include "yb/common/partition.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_scanspec_util.h"
#include "yb/docdb/primitive_value_util.h"

#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"

#include "yb/util/scope_exit.h"

namespace yb {
namespace pggate {

Status ReviewResponsePagingState(const PgTableDesc& table, PgsqlReadOp* op) {
  auto* response = op->response();
  if (table.num_hash_key_columns() > 0 ||
      op->read_request().is_forward_scan() ||
      !response ||
      !response->has_paging_state() ||
      !response->paging_state().has_next_partition_key() ||
      response->paging_state().has_next_row_key()) {
    return Status::OK();
  }
  // Backward scan of range key only table. next_row_key is not specified in paging state.
  // In this case next_partition_key must be corrected as now it points to the partition start key
  // of already scanned tablet. Partition start key of the preceding tablet must be used instead.
  // Also lower bound is checked here because DocDB can check upper bound only.
  const auto& current_next_partition_key = response->paging_state().next_partition_key();
  std::vector<docdb::KeyEntryValue> lower_bound, upper_bound;
  RETURN_NOT_OK(client::GetRangePartitionBounds(
      table.schema(), op->read_request(), &lower_bound, &upper_bound));
  if (!lower_bound.empty()) {
    docdb::DocKey current_key(table.schema());
    VERIFY_RESULT(current_key.DecodeFrom(
        current_next_partition_key, docdb::DocKeyPart::kWholeDocKey, docdb::AllowSpecial::kTrue));
    if (current_key.CompareTo(docdb::DocKey(std::move(lower_bound))) < 0) {
      response->clear_paging_state();
      return Status::OK();
    }
  }
  const auto& partitions = table.GetPartitions();
  const auto idx = client::FindPartitionStartIndex(
      partitions, current_next_partition_key.ToBuffer());
  SCHECK_GT(
      idx, 0ULL,
      IllegalState, "Paging state for backward scan cannot point to first partition");
  SCHECK_EQ(
      partitions[idx], current_next_partition_key,
      IllegalState, "Paging state for backward scan must point to partition start key");
  const auto& next_partition_key = partitions[idx - 1];
  response->mutable_paging_state()->dup_next_partition_key(next_partition_key);
  return Status::OK();
}

bool PrepareNextRequest(PgsqlReadOp* read_op) {
  // Set up paging state for next request.
  // A query request can be nested, and paging state belong to the innermost query which is
  // the read operator that is operated first and feeds data to other queries.
  // Recursive Proto Message:
  //     PgsqlReadRequestPB { PgsqlReadRequestPB index_request; }
  auto& res = *read_op->response();
  if (!res.has_paging_state()) {
    return false;
  }
  auto* req = &read_op->read_request();
  while (req->has_index_request()) {
    req = req->mutable_index_request();
  }

  // Parse/Analysis/Rewrite catalog version has already been checked on the first request.
  // The docdb layer will check the target table's schema version is compatible.
  // This allows long-running queries to continue in the presence of other DDL statements
  // as long as they do not affect the table(s) being queried.
  req->clear_ysql_catalog_version();
  req->clear_paging_state();
  req->clear_backfill_spec();

  *req->mutable_paging_state() = std::move(*res.mutable_paging_state());
  const auto& paging_state = req->paging_state();
  if (paging_state.has_read_time()) {
    read_op->set_read_time(ReadHybridTime::FromPB(paging_state.read_time()));
  }

  // Setup backfill_spec for the next request.
  if (res.has_backfill_spec()) {
    *req->mutable_backfill_spec() = std::move(*res.mutable_backfill_spec());
  }

  // Limit is set lower than default if upper plan is estimated to consume no more than this
  // number of rows. Here the operation fetches next page, so the estimation is proven incorrect.
  // So resetting the limit to prevent excessive RPCs due to too small fetch size, if the estimation
  // is too far from reality.
  if (req->limit() < FLAGS_ysql_prefetch_limit) {
    req->set_limit(FLAGS_ysql_prefetch_limit);
  }

  return true;
}

std::string PgsqlOp::ToString() const {
  return Format("{ $0 active: $1 read_time: $2 request: $3 }",
                is_read() ? "READ" : "WRITE", active_, read_time_, RequestToString());
}

PgsqlReadOp::PgsqlReadOp(Arena* arena, bool is_region_local)
    : PgsqlOp(arena, is_region_local), read_request_(arena) {
}

PgsqlReadOp::PgsqlReadOp(Arena* arena, const PgTableDesc& desc, bool is_region_local)
    : PgsqlReadOp(arena, is_region_local) {
  read_request_.set_client(YQL_CLIENT_PGSQL);
  read_request_.dup_table_id(desc.id().GetYbTableId());
  read_request_.set_schema_version(desc.schema_version());
  read_request_.set_stmt_id(reinterpret_cast<int64_t>(&read_request_));
}

Status PgsqlReadOp::InitPartitionKey(const PgTableDesc& table) {
  return client::InitPartitionKey(
       table.schema(), table.partition_schema(), table.LastPartition(), &read_request_);
}

PgsqlOpPtr PgsqlReadOp::DeepCopy(const std::shared_ptr<void>& shared_ptr) const {
  auto result = ArenaMakeShared<PgsqlReadOp>(
      std::shared_ptr<Arena>(shared_ptr, &arena()), &arena(), is_region_local());
  result->read_request() = read_request();
  result->read_from_followers_ = read_from_followers_;
  return result;
}

std::string PgsqlReadOp::RequestToString() const {
  return read_request_.ShortDebugString();
}

PgsqlWriteOp::PgsqlWriteOp(Arena* arena, bool need_transaction, bool is_region_local)
    : PgsqlOp(arena, is_region_local), write_request_(arena),
      need_transaction_(need_transaction) {
}

Status PgsqlWriteOp::InitPartitionKey(const PgTableDesc& table) {
  return client::InitPartitionKey(table.schema(), table.partition_schema(), &write_request_);
}

std::string PgsqlWriteOp::RequestToString() const {
  return write_request_.ShortDebugString();
}

PgsqlOpPtr PgsqlWriteOp::DeepCopy(const std::shared_ptr<void>& shared_ptr) const {
  auto result = ArenaMakeShared<PgsqlWriteOp>(
      std::shared_ptr<Arena>(shared_ptr, &arena()), &arena(), need_transaction_,
      is_region_local());
  result->write_request() = write_request();
  return result;
}

}  // namespace pggate
}  // namespace yb
