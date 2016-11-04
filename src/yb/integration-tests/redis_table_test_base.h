// Copyright (c) YugaByte, Inc.

#ifndef YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_
#define YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H_

#include <string>

#include "yb/client/client.h"
#include "yb/client/redis_helpers.h"
#include "yb/integration-tests/yb_table_test_base.h"

namespace yb {
namespace integration_tests {

class RedisTableTestBase : public YBTableTestBase {
 protected:
  std::string table_name() override;

  void CreateTable() override;
  void PutKeyValue(client::YBSession* session, std::string key, std::string value) override;
  void GetKeyValue(std::string key, std::string value);

  void RedisSimpleSetCommands();
  void RedisSimpleGetCommands();
};

}  // namespace integration_tests
}  // namespace yb

#endif  // YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_BASE_H
