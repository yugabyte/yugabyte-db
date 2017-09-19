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

#include "yb/docdb/doc_operation.h"
#include "yb/tablet/abstract_tablet.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

CHECKED_STATUS AbstractTablet::HandleQLReadRequest(
    HybridTime timestamp, const QLReadRequestPB& ql_read_request, QLResponsePB* response,
    gscoped_ptr<faststring>* rows_data) {

  // TODO(Robert): verify that all key column values are provided
  docdb::QLReadOperation doc_op(ql_read_request);

  vector<ColumnId> column_ids;
  for (const auto column_id : ql_read_request.column_ids()) {
    column_ids.emplace_back(column_id);
  }
  QLRowBlock rowblock(SchemaRef(), column_ids);
  TRACE("Start Execute");
  const Status s = doc_op.Execute(QLStorage(), timestamp, SchemaRef(), &rowblock);
  TRACE("Done Execute");
  if (!s.ok()) {
    response->set_status(QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
    response->set_error_message(s.message().ToString());
    return Status::OK();
  }

  *response = std::move(doc_op.response());

  RETURN_NOT_OK(CreatePagingStateForRead(ql_read_request, rowblock, response));

  response->set_status(QLResponsePB::YQL_STATUS_OK);
  rows_data->reset(new faststring());
  TRACE("Start Serialize");
  rowblock.Serialize(ql_read_request.client(), rows_data->get());
  TRACE("Done Serialize");
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
