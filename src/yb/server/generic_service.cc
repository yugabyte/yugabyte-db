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

#include <ctype.h>
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
#include "yb/gutil/strings/split.h"
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
DECLARE_string(vmodule);

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

namespace {
// Validates that the requested updates to vmodule can be made, and calls
// google::SetVLOGLevel accordingly.
// modules not explicitly specified in the request are reset to 0.
// Returns the updated string to be used for --vmodule. or error status.
Result<string> ValidateAndSetVLOG(const SetFlagRequestPB* req) {
  vector<string> existing_settings = strings::Split(FLAGS_vmodule, ",");
  std::map<string, int> module_values;
  // Only modules which are already controlled through --vmodule may be updated.
  for (const auto& module_value : existing_settings) {
    vector<string> kv = strings::Split(module_value, "=");
    if (kv[0].empty()) {
      continue;
    }
    module_values.emplace(kv[0], 0);
  }
  auto requested_settings = strings::Split(req->value(), ",");
  for (const auto& module_value : requested_settings) {
    // Handle empty string.
    if (module_value == "") {
      continue;
    }
    vector<string> kv = strings::Split(module_value, "=");
    if (kv.size() != 2) {
      LOG(WARNING) << "Malformed request to set vmodule to " << req->value();
      return STATUS(InvalidArgument, "Unable to set flag: bad value");
    } else if (module_values.find(kv[0]) == module_values.end()) {
      LOG(WARNING) << "Cannot update vmodule setting for module " << kv[0];
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Cannot update vmodule for module $0 at runtime. Process was not started with vmodule "
          "set for $0. Now, it can only be controlled through --v. If you desire to enable "
          "vmodule for $0, it will require the process to be restarted with the correct vmodule.",
          kv[0]);
    } else if (kv[0].empty() || kv[1].empty()) {
      LOG(WARNING) << "Malformed value to set vmodule. " << module_value
                   << " contains an empty string";
      return STATUS(InvalidArgument, "Unable to set flag: bad value");
    }
    for (size_t i = 0; i < kv[1].size(); i++) {
      if (!isdigit(kv[1][i])) {
        LOG(WARNING) << "Cannot update vmodule setting for module " << kv[0];
        return STATUS_SUBSTITUTE(
            InvalidArgument,
            "$1 is not a valid number. Cannot update vmodule setting for module $0.", kv[1], kv[0]);
      }
    }
    module_values[kv[0]] = std::atoi(kv[1].c_str());
  }
  std::stringstream set_vmodules;
  bool is_first = true;
  for (auto iter = module_values.begin(); iter != module_values.end(); iter++) {
    google::SetVLOGLevel(iter->first.c_str(), iter->second);
    set_vmodules << (is_first ? "" : ",") << iter->first << "=" << iter->second;
    is_first = false;
  }
  return set_vmodules.str();
}
}  // namespace
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

  const string& flag_to_set = req->flag();
  string value_to_set = req->value();
  if (flag_to_set == "vmodule") {
    // Special handling for vmodule
    auto res = ValidateAndSetVLOG(req);
    if (!res) {
      resp->set_result(SetFlagResponsePB::BAD_VALUE);
      resp->set_msg(res.status().ToString());
      rpc.RespondSuccess();
      return;
    }
    value_to_set = *res;
  }
  // The gflags library sets new values of flags without synchronization.
  // TODO: patch gflags to use proper synchronization.
  ANNOTATE_IGNORE_WRITES_BEGIN();
  // Try to set the new value.
  string ret = google::SetCommandLineOption(flag_to_set.c_str(), value_to_set.c_str());
  ANNOTATE_IGNORE_WRITES_END();

  if (ret.empty()) {
    resp->set_result(SetFlagResponsePB::BAD_VALUE);
    resp->set_msg("Unable to set flag: bad value");
    rpc.RespondSuccess();
    return;
  }

  if (ContainsKey(tags, FlagTag::kPg)) {
    auto status = server_->ReloadPgConfig();
    if (!status.ok()) {
      resp->set_result(SetFlagResponsePB::PG_SET_FAILED);
      resp->set_msg(Format("Unable to set flag: $0", status.message()));
      rpc.RespondSuccess();
      return;
    }
  }

  bool is_sensitive = ContainsKey(tags, FlagTag::kSensitive_info);
  LOG(INFO) << rpc.requestor_string() << " changed flags via RPC: " << flag_to_set << " from '"
            << (is_sensitive ? "***" : old_val) << "' to '" << (is_sensitive ? "***" : value_to_set)
            << "'";
  resp->set_result(SetFlagResponsePB::SUCCESS);
  resp->set_msg(ret);

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
