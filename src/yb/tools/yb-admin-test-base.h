// Copyright (c) YugaByte, Inc.
//
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

#pragma once

#include "yb/client/ql-dml-test-base.h"
#include "yb/tools/admin-test-base.h"

namespace yb {
namespace tools {

template <class... Args>
Result<std::string> RunAdminToolCommand(MiniCluster* cluster, Args&&... args) {
  return tools::RunAdminToolCommand(cluster->GetMasterAddresses(), std::forward<Args>(args)...);
}

class AdminCliTestBase : public client::KeyValueTableTest<MiniCluster> {
 protected:
  template <class... Args>
  Result<std::string> RunAdminToolCommand(Args&&... args) {
    return tools::RunAdminToolCommand(cluster_.get(), std::forward<Args>(args)...);
  }

  template <class... Args>
  Status RunAdminToolCommandAndGetErrorOutput(std::string* error_msg, Args&&... args) {
    auto command = ToStringVector(
        GetToolPath("yb-admin"), "-master_addresses", cluster_->GetMasterAddresses(),
        "--never_fsync=true", std::forward<Args>(args)...);
    LOG(INFO) << "Run tool: " << AsString(command);
    return Subprocess::Call(command, /* output */ nullptr, error_msg);
  }

  template <class... Args>
  Result<rapidjson::Document> RunAdminToolCommandJson(Args&&... args) {
    auto raw = VERIFY_RESULT(RunAdminToolCommand(std::forward<Args>(args)...));
    rapidjson::Document result;
    if (result.Parse(raw.c_str(), raw.length()).HasParseError()) {
      return STATUS_FORMAT(
          InvalidArgument, "Failed to parse json output $0: $1", result.GetParseError(), raw);
    }
    return result;
  }
};

Result<master::ListSnapshotsResponsePB> WaitForAllSnapshots(master::MasterBackupProxy* const proxy);

Result<std::string> GetCompletedSnapshot(
    master::MasterBackupProxy* const proxy, int num_snapshots = 1, int idx = 0);
}  // namespace tools
}  // namespace yb
