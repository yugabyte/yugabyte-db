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

#include "yb/tools/admin-test-base.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/subprocess.h"

namespace yb {
namespace tools {

namespace {

const char* const kAdminToolName = "yb-admin";

}

// Figure out where the admin tool is.
std::string AdminTestBase::GetAdminToolPath() const {
  return GetToolPath(kAdminToolName);
}

HostPort AdminTestBase::GetMasterAddresses() const {
  return cluster_->master()->bound_rpc_addr();
}

Result<std::string> AdminTestBase::CallAdminVec(const std::vector<std::string>& args) {
  std::string result;
  LOG(INFO) << "Execute: " << AsString(args);
  RETURN_NOT_OK(Subprocess::Call(args, &result));
  return result;
}

Result<rapidjson::Document> AdminTestBase::ParseJson(const std::string& raw) {
  rapidjson::Document result;
  if (result.Parse(raw.c_str(), raw.length()).HasParseError()) {
    return STATUS_FORMAT(
        InvalidArgument, "Failed to parse json output $0: $1", result.GetParseError(), raw);
  }
  return result;
}

}  // namespace tools
}  // namespace yb
