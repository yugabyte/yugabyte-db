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

  // Form a schema of columns that are referenced by this query.
  const Schema &schema = SchemaRef();
  Schema query_schema;
  const QLReferencedColumnsPB& column_pbs = ql_read_request.column_refs();
  vector<ColumnId> column_refs;
  for (int32_t id : column_pbs.static_ids()) {
    column_refs.emplace_back(id);
  }
  for (int32_t id : column_pbs.ids()) {
    column_refs.emplace_back(id);
  }
  RETURN_NOT_OK(schema.CreateProjectionByIdsIgnoreMissing(column_refs, &query_schema));

  QLRSRowDesc rsrow_desc(ql_read_request.rsrow_desc());
  QLResultSet resultset;
  TRACE("Start Execute");
  const Status s = doc_op.Execute(QLStorage(), timestamp, schema, query_schema, &resultset);
  TRACE("Done Execute");
  if (!s.ok()) {
    response->set_status(QLResponsePB::YQL_STATUS_RUNTIME_ERROR);
    response->set_error_message(s.message().ToString());
    return Status::OK();
  }
  *response = std::move(doc_op.response());

  RETURN_NOT_OK(CreatePagingStateForRead(ql_read_request, resultset.rsrow_count(), response));

  // TODO(neil) The clients' request should indicate what encoding method should be used. When
  // multi-shard is used to process more complicated queries, proxy-server might prefer a different
  // encoding. For now, we'll call CQLSerialize() without checking encoding method.
  response->set_status(QLResponsePB::YQL_STATUS_OK);
  rows_data->reset(new faststring());
  TRACE("Start Serialize");
  RETURN_NOT_OK(resultset.CQLSerialize(ql_read_request.client(),
                                       rsrow_desc,
                                       rows_data->get()));
  TRACE("Done Serialize");
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
