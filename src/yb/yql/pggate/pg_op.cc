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

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/primitive_value_util.h"

#include "yb/qlexpr/doc_scanspec_util.h"

#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/ybc_util.h"

namespace yb {
namespace pggate {

Result<bool> PrepareNextRequest(const PgTableDesc& table, PgsqlReadOp* read_op) {
  // Set up paging state for next request.
  auto& res = *read_op->response();
  if (!res.has_paging_state()) {
    return false;
  }

  // A query request can be nested, and paging state belong to the innermost query which is
  // the read operator that is operated first and feeds data to other queries.
  // Recursive Proto Message:
  //     PgsqlReadRequestPB { PgsqlReadRequestPB index_request; }
  auto& top_level_req = read_op->read_request();
  auto* req = &top_level_req;
  while (req->has_index_request()) {
    req = req->mutable_index_request();
  }

  // Backward scan for range partitioned tables has a special case on DocDB side: paging_state is
  // not reused, and upper_bound is configured instead to continue reading from the correct tablet.
  // This approach is not applicable for index read requests.
  const auto& paging_state = res.paging_state();
  if (&top_level_req == req &&
      !top_level_req.is_forward_scan() &&
      table.num_hash_key_columns() == 0 &&
      paging_state.has_next_partition_key() &&
      !paging_state.has_next_row_key()) {
    const auto& current_next_partition_key = paging_state.next_partition_key();

    // Need to check lower bound here because DocDB can check upper bound only.
    dockv::KeyEntryValues lower_bound, _;
    RETURN_NOT_OK(client::GetRangePartitionBounds(table.schema(), top_level_req, &lower_bound, &_));
    if (!lower_bound.empty()) {
      dockv::DocKey current_key(table.schema());
      VERIFY_RESULT(current_key.DecodeFrom(
          current_next_partition_key, dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
      if (current_key.CompareTo(dockv::DocKey(std::move(lower_bound))) < 0) {
        return false; // No need to continue, lower bound was reached.
      }
    }

    // Setting up upper bound for backward scan for the next request, returning false to indicate
    // paging state should not be used for the next request.
    top_level_req.clear_paging_state();
    top_level_req.mutable_upper_bound()->dup_key(current_next_partition_key);
    top_level_req.mutable_upper_bound()->set_is_inclusive(false);
  } else {
    *req->mutable_paging_state() = paging_state;
  }

  // Parse/Analysis/Rewrite catalog version has already been checked on the first request.
  // The docdb layer will check the target table's schema version is compatible.
  // This allows long-running queries to continue in the presence of other DDL statements
  // as long as they do not affect the table(s) being queried.
  req->clear_ysql_catalog_version();
  req->clear_backfill_spec();

  if (paging_state.has_read_time()) {
    auto paging_read_hybrid_time = ReadHybridTime::FromPB(paging_state.read_time());
    VLOG(4) << "Setting read time for next request: " << paging_read_hybrid_time;
    read_op->set_read_time(paging_read_hybrid_time);
  }

  // Setup backfill_spec for the next request.
  if (res.has_backfill_spec()) {
    req->dup_backfill_spec(res.backfill_spec());
  }

  // Limit is set lower than default if upper plan is estimated to consume no more than this
  // number of rows. Here the operation fetches next page, so the estimation is proven incorrect.
  // So resetting the limit to prevent excessive RPCs due to too small fetch size, if the estimation
  // is too far from reality.
  uint64_t prefetch_limit = yb_fetch_row_limit;
  if (req->limit() != prefetch_limit) {
    req->set_limit(prefetch_limit);
  }

  return true;
}

std::string PgsqlOp::ToString() const {
  return Format("{ $0 active: $1 read_time: $2 request: $3 }",
                is_read() ? "READ" : "WRITE", active_, read_time_, RequestToString());
}

PgsqlReadOp::PgsqlReadOp(ThreadSafeArena* arena, bool is_region_local)
    : PgsqlOp(arena, is_region_local), read_request_(arena) {
}

PgsqlReadOp::PgsqlReadOp(ThreadSafeArena* arena, const PgTableDesc& desc, bool is_region_local,
                         PgsqlMetricsCaptureType metrics_capture)
    : PgsqlReadOp(arena, is_region_local) {
  read_request_.set_client(YQL_CLIENT_PGSQL);
  read_request_.dup_table_id(desc.id().GetYbTableId());
  read_request_.set_schema_version(desc.schema_version());
  read_request_.set_stmt_id(reinterpret_cast<int64_t>(&read_request_));
  read_request_.set_metrics_capture(metrics_capture);
}

Status PgsqlReadOp::InitPartitionKey(const PgTableDesc& table) {
  return client::InitPartitionKey(
       table.schema(), table.partition_schema(), table.GetPartitionList(), &read_request_);
}

PgsqlOpPtr PgsqlReadOp::DeepCopy(const std::shared_ptr<void>& shared_ptr) const {
  auto result = ArenaMakeShared<PgsqlReadOp>(
      std::shared_ptr<ThreadSafeArena>(shared_ptr, &arena()), &arena(), is_region_local());
  result->set_read_time(read_time());
  result->read_request() = read_request();
  return result;
}

std::string PgsqlReadOp::RequestToString() const {
  return read_request_.ShortDebugString();
}

PgsqlWriteOp::PgsqlWriteOp(ThreadSafeArena* arena, bool need_transaction, bool is_region_local)
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
      std::shared_ptr<ThreadSafeArena>(shared_ptr, &arena()), &arena(), need_transaction_,
      is_region_local());
  result->write_request() = write_request();
  return result;
}

}  // namespace pggate
}  // namespace yb
