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

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/format.h"
#include "yb/util/status_format.h"
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

std::string AdminTestBase::GetMasterAddresses() const {
  std::string result;
  for (const auto* master : cluster_->master_daemons()) {
    if (!result.empty()) {
      result += ",";
    }
    result += AsString(master->bound_rpc_addr());
  }
  return result;
}

Result<std::string> AdminTestBase::CallAdminVec(const std::vector<std::string>& args) {
  std::string result;
  LOG(INFO) << "Execute: " << AsString(args);
  auto status = Subprocess::Call(args, &result, StdFdTypes{StdFdType::kOut, StdFdType::kErr});
  if (!status.ok()) {
    return status.CloneAndAppend(result);
  }
  return result;
}

Result<rapidjson::Document> AdminTestBase::ParseJson(const std::string& raw) {
  rapidjson::Document result;
  if (result.Parse(raw.c_str(), raw.length()).HasParseError()) {
    return STATUS_FORMAT(
        InvalidArgument, "Failed to parse json output (error code $0). Raw string: $1",
        result.GetParseError(), raw);
  }
  return result;
}

Result<CassandraSession> AdminTestBase::CqlConnect(const std::string& db_name) {
  if (!cql_driver_) {
    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    LOG(INFO) << "CQL hosts: " << AsString(hosts);
    cql_driver_ = std::make_unique<CppCassandraDriver>(
        hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kTrue);
  }
  auto result = VERIFY_RESULT(cql_driver_->CreateSession());
  if (!db_name.empty()) {
    RETURN_NOT_OK(result.ExecuteQuery(Format("USE $0", db_name)));
  }
  return result;
}

Result<const rapidjson::Value&> Get(const rapidjson::Value& value, const char* name) {
  auto it = value.FindMember(name);
  if (it == value.MemberEnd()) {
    return STATUS_FORMAT(InvalidArgument, "Missing $0 field", name);
  }
  return it->value;
}

Result<rapidjson::Value&> Get(rapidjson::Value* value, const char* name) {
  auto it = value->FindMember(name);
  if (it.operator==(value->MemberEnd())) {
    return STATUS_FORMAT(InvalidArgument, "Missing $0 field", name);
  }
  return it->value;
}

}  // namespace tools
}  // namespace yb
