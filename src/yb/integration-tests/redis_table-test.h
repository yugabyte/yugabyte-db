// Copyright (c) YugaByte, Inc.
//
#ifndef YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_H_
#define YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_H_

#include "yb/client/client.h"
#include "yb/client/redis_helpers.h"
#include "yb/integration-tests/yb_table_test_base.h"

using std::string;
using std::vector;
using std::unique_ptr;

namespace yb {
namespace tablet {

using client::RedisConstants;
using client::YBSession;
using integration_tests::YBTableTestBase;

class RedisTableTest : public YBTableTestBase {
 protected:
  string table_name() override {
    return RedisConstants::kRedisTableName;
  }
  void CreateTable() override;
  void PutKeyValue(YBSession* session, string key, string value) override;

  void RedisSimpleSetCommands();
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_INTEGRATION_TESTS_REDIS_TABLE_TEST_H_
