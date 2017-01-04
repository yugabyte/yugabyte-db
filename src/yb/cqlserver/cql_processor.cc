//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/cqlserver/cql_processor.h"

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::unique_ptr;

using client::YBClient;
using client::YBSession;
using sql::SqlProcessor;

CQLProcessor::CQLProcessor(shared_ptr<YBClient> client)
    : SqlProcessor(client) {
}

CQLProcessor::~CQLProcessor() {
}

void CQLProcessor::ProcessCall(const Slice& msg, unique_ptr<CQLResponse> *response) {
  unique_ptr<CQLRequest> request;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  if (CQLRequest::ParseRequest(msg, &request, response)) {
    // Execute the request.
    response->reset(request->Execute(this));
  }
}

CQLResponse *CQLProcessor::ProcessQuery(const QueryRequest& req) {
  Status s = Run(req.query());
  LOG(INFO) << "RUN " << req.query();
  if (!s.ok()) {
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }

  if (read_op() == nullptr) {
    return new VoidResultResponse(req);
  }

  return new RowsResultResponse(req, *read_op());
}

} // namespace cqlserver
} // namespace yb
