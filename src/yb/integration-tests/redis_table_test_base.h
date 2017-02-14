// Copyright (c) YugaByte, Inc.

#ifndef YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_
#define YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_

#include <string>

#include "yb/client/client.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/integration-tests/yb_table_test_base.h"

namespace yb {
namespace integration_tests {

class RedisTableTestBase : public YBTableTestBase {
 protected:
  client::YBTableName table_name() override;

  void CreateTable() override;
  void PutKeyValue(std::string key, std::string value) override;
  void PutKeyValue(std::string key, std::string value, int64_t ttl);
  void GetKeyValue(const std::string& key, const std::string& value, bool expect_not_found = false);

  void RedisSimpleSetCommands();
  void RedisSimpleGetCommands();

  void RedisTtlSetCommands();
  void RedisTtlGetCommands();
};

}  // namespace integration_tests
}  // namespace yb

#endif  // YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_
