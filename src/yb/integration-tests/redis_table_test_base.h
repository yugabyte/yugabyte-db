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

#ifndef YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_
#define YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_

#include <string>

#include "yb/integration-tests/yb_table_test_base.h"

namespace yb {
namespace integration_tests {

class RedisTableTestBase : public YBTableTestBase {
 protected:
  client::YBTableName table_name() override;

  void CreateTable() override;
  void PutKeyValue(std::string key, std::string value) override;
  void PutKeyValueWithTtlNoFlush(std::string key, std::string value, int64_t ttl);
  void GetKeyValue(const std::string& key, const std::string& value, bool expect_not_found = false);

  void RedisSimpleSetCommands();
  void RedisSimpleGetCommands();

  void RedisTtlSetCommands();
  void RedisTtlGetCommands();

  bool enable_ysql() override { return false; }
};

}  // namespace integration_tests
}  // namespace yb

#endif  // YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_
