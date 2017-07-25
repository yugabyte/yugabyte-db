//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module is to define CQL processor. Each processor will be handling one and only one request
// at a time. As a result all processing code in this module and the modules that it is calling
// does not need to be thread safe.
//--------------------------------------------------------------------------------------------------

#ifndef YB_CQLSERVER_CQL_PROCESSOR_H_
#define YB_CQLSERVER_CQL_PROCESSOR_H_

#include "yb/client/client.h"

#include "yb/cqlserver/cql_message.h"
#include "yb/cqlserver/cql_rpc.h"
#include "yb/cqlserver/cql_statement.h"

#include "yb/sql/sql_processor.h"
#include "yb/sql/statement.h"

namespace yb {
namespace cqlserver {

class CQLServiceImpl;

class CQLMetrics : public sql::SqlMetrics {
 public:
  explicit CQLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);

  scoped_refptr<yb::Histogram> time_to_process_request_;
  scoped_refptr<yb::Histogram> time_to_get_cql_processor_;
  scoped_refptr<yb::Histogram> time_to_parse_cql_wrapper_;
  scoped_refptr<yb::Histogram> time_to_execute_cql_request_;

  scoped_refptr<yb::Histogram> time_to_queue_cql_response_;
  scoped_refptr<yb::Counter> num_errors_parsing_cql_;
  // Rpc level metrics
  yb::rpc::RpcMethodMetrics rpc_method_metrics_;
};


// A list of CQL processors and position in the list.
class CQLProcessor;
using CQLProcessorList = std::list<std::unique_ptr<CQLProcessor>>;
using CQLProcessorListPos = CQLProcessorList::iterator;

class CQLProcessor : public sql::SqlProcessor {
 public:
  // Constructor and destructor.
  explicit CQLProcessor(CQLServiceImpl* service_impl, const CQLProcessorListPos& pos);
  ~CQLProcessor();

  // Processing an inbound call.
  void ProcessCall(rpc::InboundCallPtr call);

 private:
  // Process a PREPARE, EXECUTE, QUERY or BATCH request.
  CQLResponse* ProcessPrepare(const PrepareRequest& req);
  CQLResponse* ProcessExecute(const ExecuteRequest& req);
  CQLResponse* ProcessQuery(const QueryRequest& req);
  CQLResponse* ProcessBatch(const BatchRequest& req);

  // Statement executed callback.
  void StatementExecuted(const Status& s, const sql::ExecutedResult::SharedPtr& result);

  // Process statement execution result.
  CQLResponse* ProcessResult(Status s, const sql::ExecutedResult::SharedPtr& result);

  // Send response back to client.
  void SendResponse(const CQLResponse& response);

  // Pointer to the containing CQL service implementation.
  CQLServiceImpl* const service_impl_;

  // CQL metrics.
  std::shared_ptr<CQLMetrics> cql_metrics_;

  // Position in the CQL processor list.
  const CQLProcessorListPos pos_;

  //----------------------------- StatementExecuted callback and state ---------------------------

  // Current call, request, statement, and batch index being processed.
  rpc::InboundCallPtr call_;
  std::unique_ptr<const CQLRequest> request_;
  std::shared_ptr<const CQLStatement> stmt_;
  int batch_index_ = 0;

  // Parse and execute begin times.
  MonoTime parse_begin_;
  MonoTime execute_begin_;

  // Statement executed callback.
  sql::StatementExecutedCallback statement_executed_cb_;

  //----------------------------------------------------------------------------------------------
};

}  // namespace cqlserver
}  // namespace yb

#endif  // YB_CQLSERVER_CQL_PROCESSOR_H_
