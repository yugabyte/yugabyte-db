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

#include "yb/cdc/cdc_util.h"

#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_rowblock.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/master/master_defaults.h"

#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb::cdc {

Result<std::optional<QLRow>> FetchOptionalCdcStreamInfo(
    client::TableHandle* table, client::YBSession* session, const TabletId& tablet_id,
    const CDCStreamId& stream_id, const std::vector<std::string>& columns) {
  const auto kCdcStreamIdColumnId = narrow_cast<ColumnIdRep>(
      Schema::first_column_id() + master::kCdcStreamIdIdx);

  const auto read_op = table->NewReadOp();
  auto* const req_read = read_op->mutable_request();
  QLAddStringHashValue(req_read, tablet_id);
  QLSetStringCondition(req_read->mutable_where_expr()->mutable_condition(),
                       kCdcStreamIdColumnId, QL_OP_EQUAL, stream_id);
  req_read->mutable_column_refs()->add_ids(kCdcStreamIdColumnId);
  table->AddColumns(columns, req_read);
  RETURN_NOT_OK(session->ReadSync(read_op));
  auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();
  if (row_block->row_count() != 1) {
    return std::nullopt;
  }
  return std::move(row_block->row(0));
}

Result<QLRow> FetchCdcStreamInfo(
    client::TableHandle* table, client::YBSession* session, const TabletId& tablet_id,
    const CDCStreamId& stream_id, const std::vector<std::string>& columns) {
  auto result = VERIFY_RESULT(FetchOptionalCdcStreamInfo(
      table, session, tablet_id, stream_id, columns));
  if (!result) {
    return STATUS_FORMAT(
        NotFound,
        "Did not find a row in the cdc_state table for the tablet $0 and stream $1",
        tablet_id, stream_id);
  }
  return std::move(*result);
}

}  // namespace yb::cdc
