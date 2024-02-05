//--------------------------------------------------------------------------------------------------
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
//
// This module is to define CQL processor. Each processor will be handling one and only one request
// at a time. As a result all processing code in this module and the modules that it is calling
// does not need to be thread safe.
// Notably, this does NOT apply to Reschedule implementation methods, which are called from
// different ExecContexts, so non-thread-safe fields should not be referenced there.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <memory>

#include "yb/rpc/service_if.h"

#include "yb/yql/cql/cqlserver/cqlserver_fwd.h"
#include "yb/yql/cql/cqlserver/cql_rpc.h"
#include "yb/yql/cql/cqlserver/cql_statement.h"

#include "yb/yql/cql/ql/ql_processor.h"
#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/util/cql_message.h"

namespace yb {
namespace cqlserver {

class CQLServiceImpl;

class CQLMetrics : public ql::QLMetrics {
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

  scoped_refptr<AtomicGauge<int64_t>> cql_processors_alive_;
  scoped_refptr<Counter> cql_processors_created_;

  scoped_refptr<AtomicGauge<int64_t>> parsers_alive_;
  scoped_refptr<Counter> parsers_created_;
};

// A list of CQL processors and position in the list.

class CQLProcessor : public ql::QLProcessor {
 public:
  // Constructor and destructor.
  explicit CQLProcessor(CQLServiceImpl* service_impl, const CQLProcessorListPos& pos);
  ~CQLProcessor();

  // Processing an inbound call.
  void ProcessCall(rpc::InboundCallPtr call);

  // Release the processor back to the CQLServiceImpl.
  void Release();

  void Shutdown();

 protected:
  bool NeedReschedule() override;
  void Reschedule(rpc::ThreadPoolTask* task) override;
  CoarseTimePoint GetDeadline() const override;

 private:
  bool CheckAuthentication(const ql::CQLRequest& req) const;

  // Process a CQL request.
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::CQLRequest& req);

  // Process specific CQL requests.
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::OptionsRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::StartupRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::PrepareRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::ExecuteRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::QueryRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::BatchRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::AuthResponseRequest& req);
  std::unique_ptr<ql::CQLResponse> ProcessRequest(const ql::RegisterRequest& req);

  // Get a prepared statement and adds it to the set of statements currently being executed.
  Result<std::shared_ptr<const CQLStatement>> GetPreparedStatement(
      const ql::CQLMessage::QueryId& id, SchemaVersion version);

  // Statement executed callback.
  void StatementExecuted(const Status& s, const ql::ExecutedResult::SharedPtr& result = nullptr);

  // Process statement execution result and error.
  std::unique_ptr<ql::CQLResponse> ProcessResult(const ql::ExecutedResult::SharedPtr& result);
  std::unique_ptr<ql::CQLResponse> ProcessAuthResult(const std::string& saved_hash, bool can_login);
  std::unique_ptr<ql::CQLResponse> ProcessError(
      const Status& s,
      boost::optional<ql::CQLMessage::QueryId> query_id = boost::none);

  // Send response back to client.
  void PrepareAndSendResponse(const std::unique_ptr<ql::CQLResponse>& response);
  void SendResponse(const ql::CQLResponse& response);

  void UpdateAshQueryId(const ql::CQLMessage::QueryId& query_id);

  ql::CQLMessage::QueryId GetPrepQueryId() const {
    return request_ && request_->opcode() == ql::CQLMessage::Opcode::EXECUTE
        ? static_cast<const ql::ExecuteRequest&>(*request_).query_id() : "";
  }

  ql::CQLMessage::QueryId GetUnprepQueryId() const {
    return request_ && request_->opcode() == ql::CQLMessage::Opcode::QUERY
        ? CQLStatement::GetQueryId(ql_env_.CurrentKeyspace(),
                                   static_cast<const ql::QueryRequest&>(*request_).query()) : "";
  }

  const std::unordered_map<std::string, std::vector<std::string>> kSupportedOptions = {
      {ql::CQLMessage::kCQLVersionOption,
          {"3.0.0" /* minimum */, "3.4.2" /* current */}},
      {ql::CQLMessage::kCompressionOption,
          {ql::CQLMessage::kLZ4Compression, ql::CQLMessage::kSnappyCompression}}
  };

  // Pointer to the containing CQL service implementation.
  CQLServiceImpl* const service_impl_;

  // CQL metrics.
  std::shared_ptr<CQLMetrics> cql_metrics_;

  // Position in the CQL processor list.
  const CQLProcessorListPos pos_;

  //----------------------------- StatementExecuted callback and state ---------------------------

  // Current call, request, prepared statements and parse trees being processed.
  CQLInboundCallPtr call_;
  std::shared_ptr<const ql::CQLRequest> request_;
  std::unordered_set<std::shared_ptr<const CQLStatement>> stmts_;
  std::unordered_set<ql::ParseTree::UniPtr> parse_trees_;

  // Current retry count.
  int retry_count_ = 0;

  // Parse and execute begin times.
  MonoTime parse_begin_;
  MonoTime execute_begin_;

  // Statement executed callback.
  ql::StatementExecutedCallback statement_executed_cb_;

  ScopedTrackedConsumption consumption_;

  //----------------------------------------------------------------------------------------------

  class ProcessRequestTask : public rpc::ThreadPoolTask {
   public:
    ProcessRequestTask& Bind(CQLProcessor* processor) {
      processor_ = processor;
      return *this;
    }

    virtual ~ProcessRequestTask() {}

   private:
    void Run() override {
      auto processor = processor_;
      processor_ = nullptr;
      std::unique_ptr<ql::CQLResponse> response(processor->ProcessRequest(*processor->request_));
      if (response != nullptr) {
        processor->SendResponse(*response);
      }
    }

    void Done(const Status& status) override {}

    CQLProcessor* processor_ = nullptr;
  };

  friend class ProcessRequestTask;

  ProcessRequestTask process_request_task_;
};

}  // namespace cqlserver
}  // namespace yb
