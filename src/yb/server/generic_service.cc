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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include "yb/server/generic_service.h"

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include <glog/logging.h>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/callback_forward.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/port.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/clock.h"
#include "yb/server/server_base.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_fwd.h"

using std::string;
using std::unordered_set;

DECLARE_string(flagfile);

#ifdef COVERAGE_BUILD
extern "C" void __gcov_flush(void);
#endif


namespace yb {
namespace server {

GenericServiceImpl::GenericServiceImpl(RpcServerBase* server)
  : GenericServiceIf(server->metric_entity()),
    server_(server) {
}

GenericServiceImpl::~GenericServiceImpl() {
}

void GenericServiceImpl::SetFlag(const SetFlagRequestPB* req,
                                 SetFlagResponsePB* resp,
                                 rpc::RpcContext rpc) {

  // Validate that the flag exists and get the current value.
  string old_val;
  if (!google::GetCommandLineOption(req->flag().c_str(),
                                    &old_val)) {
    resp->set_result(SetFlagResponsePB::NO_SUCH_FLAG);
    rpc.RespondSuccess();
    return;
  }

  // Validate that the flag is runtime-changeable.
  unordered_set<FlagTag> tags;
  GetFlagTags(req->flag(), &tags);
  if (!ContainsKey(tags, FlagTag::kRuntime)) {
    if (req->force()) {
      LOG(WARNING) << rpc.requestor_string() << " forcing change of "
                   << "non-runtime-safe flag " << req->flag();
    } else {
      resp->set_result(SetFlagResponsePB::NOT_SAFE);
      resp->set_msg("Flag is not safe to change at runtime");
      rpc.RespondSuccess();
      return;
    }
  }

  resp->set_old_value(old_val);

  // The gflags library sets new values of flags without synchronization.
  // TODO: patch gflags to use proper synchronization.
  ANNOTATE_IGNORE_WRITES_BEGIN();
  // Try to set the new value.
  string ret = google::SetCommandLineOption(
      req->flag().c_str(),
      req->value().c_str());
  ANNOTATE_IGNORE_WRITES_END();

  if (ret.empty()) {
    resp->set_result(SetFlagResponsePB::BAD_VALUE);
    resp->set_msg("Unable to set flag: bad value");
  } else {
    bool is_sensitive = ContainsKey(tags, FlagTag::kSensitive_info);
    LOG(INFO) << rpc.requestor_string() << " changed flags via RPC: "
              << req->flag() << " from '" << (is_sensitive ? "***" : old_val)
              << "' to '" << (is_sensitive ? "***" : req->value()) << "'";
    resp->set_result(SetFlagResponsePB::SUCCESS);
    resp->set_msg(ret);
  }

  rpc.RespondSuccess();
}

void GenericServiceImpl::RefreshFlags(const RefreshFlagsRequestPB* req,
                                      RefreshFlagsResponsePB* resp,
                                      rpc::RpcContext rpc) {
  if (yb::RefreshFlagsFile(FLAGS_flagfile)) {
    rpc.RespondSuccess();
  } else {
    rpc.RespondFailure(STATUS_SUBSTITUTE(InternalError,
                                         "Unable to refresh flagsfile: $0", FLAGS_flagfile));
  }
}

void GenericServiceImpl::GetFlag(const GetFlagRequestPB* req,
                                 GetFlagResponsePB* resp,
                                 rpc::RpcContext rpc) {
  // Validate that the flag exists and get the current value.
  string val;
  if (!google::GetCommandLineOption(req->flag().c_str(), &val)) {
    resp->set_valid(false);
    rpc.RespondSuccess();
    return;
  }
  resp->set_value(val);
  rpc.RespondSuccess();
}

void GenericServiceImpl::FlushCoverage(const FlushCoverageRequestPB* req,
                                       FlushCoverageResponsePB* resp,
                                       rpc::RpcContext rpc) {
#ifdef COVERAGE_BUILD
  __gcov_flush();
  LOG(INFO) << "Flushed coverage info. (request from " << rpc.requestor_string() << ")";
  resp->set_success(true);
#else
  LOG(WARNING) << "Non-coverage build cannot flush coverage (request from "
               << rpc.requestor_string() << ")";
  resp->set_success(false);
#endif
  rpc.RespondSuccess();
}

void GenericServiceImpl::ServerClock(const ServerClockRequestPB* req,
                                     ServerClockResponsePB* resp,
                                     rpc::RpcContext rpc) {
  resp->set_hybrid_time(server_->clock()->Now().ToUint64());
  rpc.RespondSuccess();
}

void GenericServiceImpl::GetStatus(const GetStatusRequestPB* req,
                                   GetStatusResponsePB* resp,
                                   rpc::RpcContext rpc) {
  server_->GetStatusPB(resp->mutable_status());
  rpc.RespondSuccess();
}

void GenericServiceImpl::Ping(
    const PingRequestPB* req, PingResponsePB* resp, rpc::RpcContext rpc) {
  rpc.RespondSuccess();
}

void GenericServiceImpl::ReloadCertificates(
    const ReloadCertificatesRequestPB* req, ReloadCertificatesResponsePB* resp,
    rpc::RpcContext rpc) {
  const auto status = server_->ReloadKeysAndCertificates();
  if (!status.ok()) {
    LOG(ERROR) << "Reloading certificates failed: " << status;
    rpc.RespondFailure(status);
    return;
  }

  LOG(INFO) << "Reloading certificates was successful";
  rpc.RespondSuccess();
}

void GenericServiceImpl::GetAutoFlagsConfigVersion(
    const GetAutoFlagsConfigVersionRequestPB* req, GetAutoFlagsConfigVersionResponsePB* resp,
    rpc::RpcContext rpc) {
  resp->set_config_version(server_->GetAutoFlagConfigVersion());

  rpc.RespondSuccess();
}

} // namespace server
} // namespace yb
