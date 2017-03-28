// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_operation.h"
#include "yb/tablet/abstract_tablet.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

CHECKED_STATUS AbstractTablet::HandleYQLReadRequest(
    HybridTime timestamp, const YQLReadRequestPB& yql_read_request, YQLResponsePB* response,
    gscoped_ptr<faststring>* rows_data) {

  // TODO(Robert): verify that all key column values are provided
  docdb::YQLReadOperation doc_op(yql_read_request);

  vector<ColumnId> column_ids;
  for (const auto column_id : yql_read_request.column_ids()) {
    column_ids.emplace_back(column_id);
  }
  YQLRowBlock rowblock(SchemaRef(), column_ids);
  TRACE("Start Execute");
  const Status s = doc_op.Execute(YQLStorage(), timestamp, SchemaRef(), &rowblock);
  TRACE("Done Execute");
  if (!s.ok()) {
    response->set_status(YQLResponsePB::YQL_STATUS_RUNTIME_ERROR);
    response->set_error_message(s.message().ToString());
    return Status::OK();
  }

  *response = std::move(doc_op.response());

  RETURN_NOT_OK(CreatePagingStateForRead(yql_read_request, rowblock, response));

  response->set_status(YQLResponsePB::YQL_STATUS_OK);
  rows_data->reset(new faststring());
  TRACE("Start Serialize");
  rowblock.Serialize(yql_read_request.client(), rows_data->get());
  TRACE("Done Serialize");
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
