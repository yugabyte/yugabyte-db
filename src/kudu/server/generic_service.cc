// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/server/generic_service.h"

#include <gflags/gflags.h>
#include <string>
#include <unordered_set>

#include "kudu/gutil/map-util.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/server/clock.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/server/server_base.h"
#include "kudu/util/flag_tags.h"

DECLARE_bool(use_mock_wall_clock);
DECLARE_bool(use_hybrid_clock);

using std::string;
using std::unordered_set;

#ifdef COVERAGE_BUILD
extern "C" void __gcov_flush(void);
#endif


namespace kudu {
namespace server {

GenericServiceImpl::GenericServiceImpl(ServerBase* server)
  : GenericServiceIf(server->metric_entity()),
    server_(server) {
}

GenericServiceImpl::~GenericServiceImpl() {
}

void GenericServiceImpl::SetFlag(const SetFlagRequestPB* req,
                                 SetFlagResponsePB* resp,
                                 rpc::RpcContext* rpc) {

  // Validate that the flag exists and get the current value.
  string old_val;
  if (!google::GetCommandLineOption(req->flag().c_str(),
                                    &old_val)) {
    resp->set_result(SetFlagResponsePB::NO_SUCH_FLAG);
    rpc->RespondSuccess();
    return;
  }

  // Validate that the flag is runtime-changeable.
  unordered_set<string> tags;
  GetFlagTags(req->flag(), &tags);
  if (!ContainsKey(tags, "runtime")) {
    if (req->force()) {
      LOG(WARNING) << rpc->requestor_string() << " forcing change of "
                   << "non-runtime-safe flag " << req->flag();
    } else {
      resp->set_result(SetFlagResponsePB::NOT_SAFE);
      resp->set_msg("Flag is not safe to change at runtime");
      rpc->RespondSuccess();
      return;
    }
  }

  resp->set_old_value(old_val);

  // Try to set the new value.
  string ret = google::SetCommandLineOption(
      req->flag().c_str(),
      req->value().c_str());
  if (ret.empty()) {
    resp->set_result(SetFlagResponsePB::BAD_VALUE);
    resp->set_msg("Unable to set flag: bad value");
  } else {
    LOG(INFO) << rpc->requestor_string() << " changed flags via RPC: "
              << req->flag() << " from '" << old_val << "' to '"
              << req->value() << "'";
    resp->set_result(SetFlagResponsePB::SUCCESS);
    resp->set_msg(ret);
  }

  rpc->RespondSuccess();
}

void GenericServiceImpl::FlushCoverage(const FlushCoverageRequestPB* req,
                                       FlushCoverageResponsePB* resp,
                                       rpc::RpcContext* rpc) {
#ifdef COVERAGE_BUILD
  __gcov_flush();
  LOG(INFO) << "Flushed coverage info. (request from " << rpc->requestor_string() << ")";
  resp->set_success(true);
#else
  LOG(WARNING) << "Non-coverage build cannot flush coverage (request from "
               << rpc->requestor_string() << ")";
  resp->set_success(false);
#endif
  rpc->RespondSuccess();
}

void GenericServiceImpl::ServerClock(const ServerClockRequestPB* req,
                                     ServerClockResponsePB* resp,
                                     rpc::RpcContext* rpc) {
  resp->set_timestamp(server_->clock()->Now().ToUint64());
  rpc->RespondSuccess();
}

void GenericServiceImpl::SetServerWallClockForTests(const SetServerWallClockForTestsRequestPB *req,
                                                   SetServerWallClockForTestsResponsePB *resp,
                                                   rpc::RpcContext *context) {
  if (!FLAGS_use_hybrid_clock || !FLAGS_use_mock_wall_clock) {
    LOG(WARNING) << "Error setting wall clock for tests. Server is not using HybridClock"
        "or was not started with '--use_mock_wall_clock= true'";
    resp->set_success(false);
  }

  server::HybridClock* clock = down_cast<server::HybridClock*>(server_->clock());
  if (req->has_now_usec()) {
    clock->SetMockClockWallTimeForTests(req->now_usec());
  }
  if (req->has_max_error_usec()) {
    clock->SetMockMaxClockErrorForTests(req->max_error_usec());
  }
  resp->set_success(true);
  context->RespondSuccess();
}

void GenericServiceImpl::GetStatus(const GetStatusRequestPB* req,
                                   GetStatusResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  server_->GetStatusPB(resp->mutable_status());
  rpc->RespondSuccess();
}

} // namespace server
} // namespace kudu
