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

#pragma once

#include <rapidjson/document.h>

#include "yb/integration-tests/ts_itest-base.h"

#include "yb/util/result.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"

namespace yb {

class CassandraSession;
class CppCassandraDriver;

namespace tools {

class AdminTestBase : public tserver::TabletServerIntegrationTestBase {
 public:
  // Figure out where the admin tool is.
  std::string GetAdminToolPath() const;

  std::string GetMasterAddresses() const;

  template <class... Args>
  Result<std::string> CallAdmin(Args&&... args) {
    return CallAdminVec(ToStringVector(
        GetAdminToolPath(), "-master_addresses", GetMasterAddresses(),
        std::forward<Args>(args)...));
  }

  Result<std::string> CallAdminVec(const std::vector<std::string>& args);

  template <class... Args>
  Result<rapidjson::Document> CallJsonAdmin(Args&&... args) {
    return ParseJson(VERIFY_RESULT(CallAdmin(std::forward<Args>(args)...)));
  }

  Result<rapidjson::Document> ParseJson(const std::string& raw);

  Result<CassandraSession> CqlConnect(const std::string& db_name = std::string());

 private:
  std::unique_ptr<CppCassandraDriver> cql_driver_;
};

Result<const rapidjson::Value&> Get(const rapidjson::Value& value, const char* name);
Result<rapidjson::Value&> Get(rapidjson::Value* value, const char* name);

// Run a yb-admin command and return the output.
template <class... Args>
Result<std::string> RunAdminToolCommand(const std::string& master_addresses, Args&&... args) {
  auto command = ToStringVector(
      GetToolPath("yb-admin"), "-master_addresses", master_addresses,
      "--never_fsync=true",
      std::forward<Args>(args)...);
  std::string result;
  LOG(INFO) << "Run tool: " << AsString(command);
  RETURN_NOT_OK(Subprocess::Call(command, &result));
  return result;
}

}  // namespace tools
}  // namespace yb
