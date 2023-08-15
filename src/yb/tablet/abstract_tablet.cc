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

#include "yb/tablet/abstract_tablet.h"

#include "yb/common/ql_resultset.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/tablet/read_result.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/trace.h"

namespace yb::tablet {

Result<HybridTime> AbstractTablet::SafeTime(RequireLease require_lease,
                                            HybridTime min_allowed,
                                            CoarseTimePoint deadline) const {
  return DoGetSafeTime(require_lease, min_allowed, deadline);
}

Status AbstractTablet::HandleQLReadRequest(CoarseTimePoint deadline,
                                           const ReadHybridTime& read_time,
                                           const QLReadRequestPB& ql_read_request,
                                           const TransactionOperationContext& txn_op_context,
                                           QLReadRequestResult* result,
                                           WriteBuffer* rows_data) {

  // TODO(Robert): verify that all key column values are provided
  docdb::QLReadOperation doc_op(ql_read_request, txn_op_context);

  // Form a schema of columns that are referenced by this query.
  const auto doc_read_context = GetDocReadContext();
  Schema projection;
  const QLReferencedColumnsPB& column_pbs = ql_read_request.column_refs();
  std::vector<ColumnId> column_refs;
  for (int32_t id : column_pbs.static_ids()) {
    column_refs.emplace_back(id);
  }
  for (int32_t id : column_pbs.ids()) {
    column_refs.emplace_back(id);
  }
  RETURN_NOT_OK(doc_read_context->schema.CreateProjectionByIdsIgnoreMissing(
      column_refs, &projection));

  const QLRSRowDesc rsrow_desc(ql_read_request.rsrow_desc());
  QLResultSet resultset(&rsrow_desc, rows_data);

  TRACE("Start Execute");
  const Status s = doc_op.Execute(
      QLStorage(), deadline, read_time, *doc_read_context, projection, &resultset,
      &result->restart_read_ht);
  TRACE("Done Execute");
  if (!s.ok()) {
    if (s.IsQLError()) {
      result->response.set_status(QLResponsePB::YQL_STATUS_USAGE_ERROR);
    } else {
      result->response.set_status(QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
    }
    result->response.set_error_message(s.message().cdata(), s.message().size());
    return Status::OK();
  }
  result->response.Swap(&doc_op.response());

  RETURN_NOT_OK(CreatePagingStateForRead(
      ql_read_request, resultset.rsrow_count(), &result->response));

  result->response.set_status(QLResponsePB::YQL_STATUS_OK);
  return Status::OK();
}

Status AbstractTablet::ProcessPgsqlReadRequest(CoarseTimePoint deadline,
                                               const ReadHybridTime& read_time,
                                               bool is_explicit_request_read_time,
                                               const PgsqlReadRequestPB& pgsql_read_request,
                                               const std::shared_ptr<TableInfo>& table_info,
                                               const TransactionOperationContext& txn_op_context,
                                               const docdb::DocDBStatistics* statistics,
                                               PgsqlReadRequestResult* result) {
  docdb::PgsqlReadOperation doc_op(pgsql_read_request, txn_op_context);

  // Form a schema of columns that are referenced by this query.
  const auto doc_read_context = table_info->doc_read_context;
  const auto index_doc_read_context = pgsql_read_request.has_index_request()
    ? VERIFY_RESULT(GetDocReadContext(pgsql_read_request.index_request().table_id())) : nullptr;

  TRACE("Start Execute");
  auto fetched_rows = doc_op.Execute(
      QLStorage(), deadline, read_time, is_explicit_request_read_time, *doc_read_context,
      index_doc_read_context.get(), result->rows_data, &result->restart_read_ht, statistics);
  TRACE("Done Execute");
  if (!fetched_rows.ok()) {
    result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_RUNTIME_ERROR);
    const auto& s = fetched_rows.status();

    // TODO(14814, 18387): At the moment only one error status is supported.
    result->response.mutable_error_status()->Clear();
    StatusToPB(s, result->response.add_error_status());
    // For backward compatibility set also deprecated error message
    result->response.set_error_message(s.message().cdata(), s.message().size());
    return Status::OK();
  }
  result->response.Swap(&doc_op.response());

  result->num_rows_read = *fetched_rows;

  RETURN_NOT_OK(CreatePagingStateForRead(
      pgsql_read_request, *fetched_rows, &result->response));

  // TODO(neil) The clients' request should indicate what encoding method should be used. When
  // multi-shard is used to process more complicated queries, proxy-server might prefer a different
  // encoding. For now, we'll call PgsqlSerialize() without checking encoding method.
  result->response.set_status(PgsqlResponsePB::PGSQL_STATUS_OK);

  // Serializing data for PgGate API.
  CHECK(!pgsql_read_request.has_rsrow_desc()) << "Row description is not needed";
  TRACE("Done Handle");

  return Status::OK();
}

}  // namespace yb::tablet
