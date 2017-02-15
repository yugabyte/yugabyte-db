//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/cqlserver/cql_processor.h"

METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_GetProcessor,
    "Time spent to get a processor for processing a CQL query request.",
    yb::MetricUnit::kMicroseconds,
    "Time spent to get a processor for processing a CQL query request.", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ProcessRequest,
    "Time spent processing a CQL query request. From parsing till executing",
    yb::MetricUnit::kMicroseconds,
    "Time spent processing a CQL query request. From parsing till executing", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ParseRequest,
    "Time spent parsing CQL query request", yb::MetricUnit::kMicroseconds,
    "Time spent parsing CQL query request", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_QueueResponse,
    "Time spent to queue the response for a CQL query request back on the network",
    yb::MetricUnit::kMicroseconds,
    "Time spent after computing the CQL response to queue it onto the connection.", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_CQLServerService_ExecuteRequest,
    "Time spent executing the CQL query request", yb::MetricUnit::kMicroseconds,
    "Time spent executing the CQL query request", 60000000LU, 2);
METRIC_DEFINE_counter(
    server, yb_cqlserver_CQLServerService_ParsingErrors, "Errors encountered when parsing ",
    yb::MetricUnit::kRequests, "Errors encountered when parsing ");

namespace yb {
namespace cqlserver {

using std::shared_ptr;
using std::unique_ptr;

using client::YBClient;
using client::YBSession;
using client::YBTableCache;
using sql::SqlProcessor;

CQLMetrics::CQLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity)
    : YbSqlMetrics(metric_entity) {
  time_to_process_request_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ProcessRequest.Instantiate(
          metric_entity);
  time_to_get_cql_processor_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_GetProcessor.Instantiate(metric_entity);
  time_to_parse_cql_wrapper_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ParseRequest.Instantiate(metric_entity);
  time_to_execute_cql_request_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_ExecuteRequest.Instantiate(
          metric_entity);
  time_to_queue_cql_response_ =
      METRIC_handler_latency_yb_cqlserver_CQLServerService_QueueResponse.Instantiate(metric_entity);
  num_errors_parsing_cql_ =
      METRIC_yb_cqlserver_CQLServerService_ParsingErrors.Instantiate(metric_entity);
}

CQLProcessor::CQLProcessor(
    shared_ptr<YBClient> client, shared_ptr<YBTableCache> cache, shared_ptr<CQLMetrics> cql_metrics)
    : SqlProcessor(client, cache), cql_metrics_(cql_metrics) {}

CQLProcessor::~CQLProcessor() {
}

void CQLProcessor::ProcessCall(const Slice& msg, unique_ptr<CQLResponse> *response) {
  unique_ptr<CQLRequest> request;

  // Parse the CQL request. If the parser failed, it sets the error message in response.
  MonoTime start = MonoTime::Now(MonoTime::FINE);
  if (CQLRequest::ParseRequest(msg, &request, response)) {
    MonoTime parsed = MonoTime::Now(MonoTime::FINE);
    cql_metrics_->time_to_parse_cql_wrapper_->Increment(
        parsed.GetDeltaSince(start).ToMicroseconds());
    // Execute the request.
    response->reset(request->Execute(this));
    MonoTime executed = MonoTime::Now(MonoTime::FINE);
    cql_metrics_->time_to_execute_cql_request_->Increment(
        executed.GetDeltaSince(parsed).ToMicroseconds());
  } else {
    cql_metrics_->num_errors_parsing_cql_->Increment();
  }
}

CQLResponse *CQLProcessor::ProcessQuery(const QueryRequest& req) {
  Status s = Run(req.query(), cql_metrics_.get());
  VLOG(1) << "RUN " << req.query();
  if (!s.ok()) {
    return new ErrorResponse(req, ErrorResponse::Code::SYNTAX_ERROR, s.ToString());
  }
  if (rows_result() == nullptr) {
    return new VoidResultResponse(req);
  }

  cql_metrics_->sql_response_size_bytes_->Increment(rows_result()->rows_data().size());
  return new RowsResultResponse(req, *rows_result());
}

}  // namespace cqlserver
}  // namespace yb
